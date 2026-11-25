#!/bin/bash
# Print the sizes of the console's and the scheduler's state, and of the
# structures they're made of, on the simulator, the ItsyBitsy M0 and the Agon
# Light 2.  The simulator and ItsyBitsy figures come from the debug
# information of the images the last builds left (sim/bin/nano-os-sim,
# build/itsybitsy_m0/NanoOs.elf); the Agon's image has none, so Scheduler.c is
# compiled again with the Agon's flags and the sizes of arrays of each size are
# read from the assembly, since the Agon's objects record no symbol sizes.
#
# Usage:  structsizes.sh
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
A=${ARM_BIN:-$HOME/.arduino15/packages/adafruit/tools/arm-none-eabi-gcc/9-2019q4/bin}
ARM_GDB=${ARM_GDB:-arm-none-eabi-gdb}
export AGDEV_HOME=${AGDEV_HOME:-$HOME/agondev}
EZ=$AGDEV_HOME/bin
cd "$REPO"

TYPES="ConsoleState ConsolePort ConsoleBuffer struct@SchedulerState ProcessDescriptor"

gdbSizes() {
	local gdb=$1 elf=$2 label=$3 args=()
	for type in $TYPES; do
		args+=(-ex "print sizeof(${type/@/ })")
	done
	"$gdb" -batch "${args[@]}" "$elf" 2>/dev/null \
		| sed -n 's/^\$[0-9]* = //p' \
		| paste -d' ' <(printf '%s\n' $TYPES | tr @ ' ') - \
		| sed "s/^/$label /"
}

gdbSizes gdb sim/bin/nano-os-sim sim
gdbSizes "$ARM_GDB" build/itsybitsy_m0/NanoOs.elf itsybitsy

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
{
	echo "#include \"$REPO/src/kernel/Scheduler.c\""
	for type in $TYPES; do
		name=$(echo "$type" | sed 's/.*@//')
		echo "char structSize_$name[sizeof(${type/@/ })];"
	done
} > "$work/probe.c"
flags=$(make -s -pq -f makefiles/AgonLight2Makefile 2>/dev/null \
	| sed -n 's/^CFLAGS := //p' | head -1)
eval "$EZ/ez80-none-elf-clang $flags -w -S $work/probe.c -o $work/probe.s"
awk '/^_structSize_/ { name = substr($1, 13); sub(/:$/, "", name); next }
	name != "" && $1 == ".skip" { print name, $2; name = "" }' "$work/probe.s" \
	| while read -r name size; do
		for type in $TYPES; do
			if [ "${type#*@}" = "$name" ]; then
				echo "agon ${type/@/ } $size"
			fi
		done
	done
