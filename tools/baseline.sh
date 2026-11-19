#!/bin/bash
# Record the measurements that the message-argument-storage plan compares
# before and after:  image sizes and symbol tables for the simulator,
# ItsyBitsy M0, and Agon Light 2; Agon flash headroom; filesystem overlay
# sizes against their windows; SAMD21 stack depth of every sender; the
# small-memory pipeline heap figures; and the pass counts of every test
# layer.  Builds through builddev, the sim makefile, and the test runners.
#
# Usage:  OUT=<dir> baseline.sh [--no-tests]
set -e
OUT=${OUT:?set OUT to the directory to write the measurements to}
REPO=$(cd "$(dirname "$0")/.." && pwd)
TOOLS=$(cd "$(dirname "$0")" && pwd)
A=${ARM_BIN:-$HOME/.arduino15/packages/adafruit/tools/arm-none-eabi-gcc/9-2019q4/bin}
export AGDEV_HOME=${AGDEV_HOME:-$HOME/agondev}
EZ=$AGDEV_HOME/bin
RUN_TESTS=1
[ "${1}" = "--no-tests" ] && RUN_TESTS=0

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$REPO"

symbols() {
	"$1" -S --defined-only "$2" \
		| awk 'NF == 4 { print $4, $3, $2 } NF == 3 { print $3, $2, "-" }' | sort
}

echo "baseline: simulator"
make -C sim clean > /dev/null
make -j "$(nproc)" -C sim > "$work/sim.log" 2>&1
size sim/bin/nano-os-sim > "$OUT/sim.size"
symbols nm sim/bin/nano-os-sim > "$OUT/sim.nm"

echo "baseline: ItsyBitsy M0"
PATH=$A:$PATH ./builddev itsybitsy_m0 baseline "$work/samd.img" overlay \
	> "$work/samd.log" 2>&1
"$A/arm-none-eabi-size" build/itsybitsy_m0/NanoOs.elf > "$OUT/itsybitsy.size"
symbols "$A/arm-none-eabi-nm" build/itsybitsy_m0/NanoOs.elf > "$OUT/itsybitsy.nm"
{
	echo "# overlay-filesystem slot, window 4096 bytes (NanoOsArduinoSamd21.ld)"
	for elf in $(ls -d userspace/usr/obj/overlay-filesystem/*/overlay.elf | sort -V); do
		"$A/arm-none-eabi-size" "$elf" | tail -1 \
			| awk -v e="$elf" '{ print e, $1 + $2 + $3 }'
	done
} > "$OUT/itsybitsy-fs.size"

echo "baseline: Agon Light 2"
./builddev AgonLight2 baseline "$work/agon.img" contiguous > "$work/agon.log" 2>&1
"$EZ/ez80-none-elf-size" build/agon/NanoOs.elf > "$OUT/agon.size"
symbols "$EZ/ez80-none-elf-nm" build/agon/NanoOs.elf > "$OUT/agon.nm"
stripped=$(stat -c %s build/agon/NanoOs_stripped.bin)
echo "flash free: $((131072 - stripped)) (131072 - NanoOs_stripped.bin $stripped)" \
	> "$OUT/agon-flash.txt"
{
	echo "# contiguous filesystem, window 65536 bytes (NanoOsAgonLight2Contiguous.ld)"
	"$EZ/ez80-none-elf-size" userspace/usr/obj/contiguous-filesystem/overlay.elf \
		| tail -1 | awk '{ print "contiguous-filesystem", $1 + $2 + $3 }'
} > "$OUT/agon-fs.size"

echo "baseline: SAMD21 stack depth"
rm -rf build/su
make -f makefiles/ArduinoSamd21Makefile BOARD=itsybitsy_m0 BUILD_DIR=build/su \
	CC="$A/arm-none-eabi-gcc -fstack-usage" CXX="$A/arm-none-eabi-g++ -fstack-usage" \
	-j "$(nproc)" > "$work/su.log" 2>&1
"$A/arm-none-eabi-objdump" -d build/su/NanoOs.elf > "$work/kernel.dis"
python3 "$TOOLS/stackdepth.py" build/su "$work/kernel.dis" src > "$OUT/itsybitsy.stack"
rm -rf build/su

echo "baseline: pipeline heap"
python3 "$TOOLS/pipeline_heap.py" 2> "$work/pipeline.log" \
	| grep "^heap\|^smallest" > "$OUT/pipeline-heap.txt"

if [ "$RUN_TESTS" = "1" ]; then
	tally() {
		grep -E "^(not )?ok " "$1" | awk '
			/# TODO/ { todo++; next }
			/# SKIP/ { skip++; next }
			/^ok/ { pass++; next }
			{ fail++ }
			END { printf "pass %d, fail %d, skip %d, todo %d\n",
				pass, fail, skip, todo }'
	}
	echo "baseline: harness"
	make -C test clean > /dev/null
	make -C test run > "$work/harness.tap" 2>&1 || true
	echo "harness: $(tally "$work/harness.tap")" > "$OUT/tests.txt"
	echo "baseline: e2e"
	python3 test/e2e/run.py > "$work/e2e.tap" 2>&1 || true
	echo "e2e: $(tally "$work/e2e.tap")" >> "$OUT/tests.txt"
	grep "^not ok" "$work/e2e.tap" | grep -v "# TODO" >> "$OUT/tests.txt" || true
	echo "baseline: emulator"
	python3 test/emulator/run.py > "$work/emulator.tap" 2>&1 || true
	echo "emulator: $(tally "$work/emulator.tap")" >> "$OUT/tests.txt"
	grep "^not ok\|^1\.\.0" "$work/emulator.tap" >> "$OUT/tests.txt" || true
fi
echo "baseline: written to $OUT"
