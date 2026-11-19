#!/bin/bash
# Build the SAMD21 image and overlays, recompile the vi and ex overlay
# sources with -fstack-usage, and disassemble the linked overlays, for
# vistack.py.  Also builds the kernel with -fstack-usage into build/su.
#
# Usage:  WORK=<dir> [REPO=<repo>] measure-ovl.sh
set -e
WORK=${WORK:?set WORK to a scratch directory}
REPO=${REPO:-$(cd "$(dirname "$0")/.." && pwd)}
A=${ARM_BIN:-$HOME/.arduino15/packages/adafruit/tools/arm-none-eabi-gcc/9-2019q4/bin}
mkdir -p "$WORK"
cd "$REPO"
rm -rf build/su
make -f makefiles/ArduinoSamd21Makefile BOARD=itsybitsy_m0 BUILD_DIR=build/su \
  CC="$A/arm-none-eabi-gcc -fstack-usage" CXX="$A/arm-none-eabi-g++ -fstack-usage" \
  -j8 > "$WORK/su-build.log" 2>&1
"$A/arm-none-eabi-objdump" -d build/su/NanoOs.elf > "$WORK/kernel.dis"
rm -rf userspace/usr/obj
PATH=$A:$PATH ./builddev itsybitsy_m0 benchhost "$WORK/samd.img" overlay \
  > "$WORK/samd.log" 2>&1 || { grep -n "error\|overflowed" "$WORK/samd.log" | head; exit 1; }
FL="-Wall -Wextra -std=gnu17 -mcpu=cortex-m0 -Os -nostdlib -ffreestanding -fno-pic -fno-pie -static -ffunction-sections -fdata-sections -fno-jump-tables -fno-stack-protector -fstack-usage"
INC="-I$REPO/userspace/usr/src/lib/ex/include -I$REPO/userspace/usr/src/lib/textstore/include -I$REPO/userspace/usr/src/lib/screen/include -I$REPO/userspace/usr/src/commands/vi/include -I$REPO/userspace/usr/include"
rm -rf "$WORK/su"; mkdir -p "$WORK/su"
for d in userspace/usr/src/commands/vi/*/ userspace/usr/src/lib/ex/*/; do
  for f in "$d"*.c; do
    [ -f "$f" ] || continue
    b=$(basename "$f" .c); [ "$b" = OverlayMap ] && continue
    (cd "$d" && "$A/arm-none-eabi-gcc" $FL $INC -I../include -c "$b.c" -o "$WORK/su/$(basename "$d")_$b.o")
  done
done
(cd userspace/usr/src && "$A/arm-none-eabi-gcc" -mcpu=cortex-m0 -Os -ffreestanding -fno-pic -fstack-usage -I../include -c start.c -o "$WORK/start.o")
echo "ready:  WORK=$WORK REPO=$REPO python3 $(dirname "$0")/vistack.py"
