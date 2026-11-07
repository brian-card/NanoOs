#!/bin/bash
################################################################################
#
#                       Copyright (c) 2026 Brian Card
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
#                                 Brian Card
#                       https://github.com/brian-card
#

# Profile vi on fab-agon-emulator's VDP console.  Opens /etc/ops.txt (59
# lines), then profiles two inserts:  "ret" types 10 lines ending in Return,
# and "chars" types the same 30 characters with no Return.  Each window runs
# from the first key until vi is back in normal mode, and leaves
# $WORK/<name>.txt (instruction count), <name>.stacks (folded IX-chain
# stacks), and <name>.callees in $WORK.  Resolve the stacks with profan.py.
#
# EMULATOR must hold a fab-agon-emulator built from the nanoos-profiler
# branch, plus firmware/.  CALLEES is an optional comma-separated list of hex
# addresses to count arrivals at.
#
# The emulator opens a window on DISPLAY:  every X call has a timeout, the
# window is the one whose _NET_WM_PID is the emulator started here, and the
# script waits for the window to go away before exiting.  A hung `import` on
# a stale window once froze the display.
#
# Usage:  WORK=<dir> [REPO=<repo>] [EMULATOR=<dir>] [CALLEES=<addrs>] agon-prof.sh
set -e
WORK=${WORK:?set WORK to a scratch directory}
REPO=${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}
EMULATOR=${EMULATOR:-$HOME/src/fab-agon-emulator}
TOOLS=$(cd "$(dirname "$0")" && pwd)
export DISPLAY=${DISPLAY:-:1}
E=$WORK/emu
rm -rf "$E"; mkdir -p "$E/firmware"
cp -L "$EMULATOR/fab-agon-emulator" "$E/"
cp "$EMULATOR/firmware/mos_platform.bin" "$EMULATOR/firmware/vdp_platform.so" "$E/firmware/"
(cd "$REPO" && rm -rf usr/obj && AGDEV_HOME=${AGDEV_HOME:-$HOME/agondev} ./builddev \
  AgonLight2 nanoemu "$E/NanoOs.img" contiguous "" "$E/firmware/mos_platform.bin" \
  > "$WORK/agon-build.log" 2>&1)
python3 -c 'print("\n".join("u%02d" % n for n in range(1, 60)))' > "$WORK/ops.txt"
mcopy -o -i "$E/NanoOs.img@@2097152" "$WORK/ops.txt" ::/etc/ops.txt

cd "$E"
NANOOS_PROFILE=$WORK/prof NANOOS_PROFILE_CALLEES=${CALLEES:-} SDL_AUDIODRIVER=dummy setsid ./fab-agon-emulator --sdcard-img NanoOs.img > "$E/emu.log" 2>&1 &
P=$!
W=""
for i in $(seq 1 60); do
  for w in $(timeout 5 xwininfo -root -tree 2>/dev/null | grep "Fab Agon" | awk '{print $1}'); do
    [ "$(timeout 5 xprop -id "$w" _NET_WM_PID 2>/dev/null | awk '{print $3}')" = "$P" ] && W=$w
  done
  [ -n "$W" ] && break; sleep 1
done
cleanup() {
  kill "$P" 2>/dev/null || true
  for i in $(seq 1 20); do kill -0 "$P" 2>/dev/null || break; sleep 0.5; done
  kill -9 "$P" 2>/dev/null || true
  [ -n "$W" ] && for i in $(seq 1 20); do timeout 5 xwininfo -id "$W" >/dev/null 2>&1 || break; sleep 0.5; done
}
trap cleanup EXIT
[ -z "$W" ] && { echo "no window for pid $P"; exit 1; }
snap() { timeout 5 import -window "$W" "$@"; }
keys() { timeout 120 python3 "$TOOLS/xkeys.py" "$W" "$@"; }
status() { snap -crop 100x8+0+472 "$WORK/status.png"; m=$(identify -format "%[mean]" "$WORK/status.png"); echo "${m%.*}"; }
wait_insert() { for i in $(seq 1 1500); do [ "$(status)" -gt 1000 ] && break; sleep 0.2; done; date +%s.%N; }
wait_normal() { for i in $(seq 1 1500); do [ "$(status)" -lt 200 ] && break; sleep 0.2; done; date +%s.%N; }
for i in $(seq 1 90); do
  snap -crop 60x10+0+16 "$WORK/probe.png" 2>/dev/null || true
  m=$(identify -format "%[mean]" "$WORK/probe.png" 2>/dev/null || echo 0)
  [ "${m%.*}" -gt 1000 ] 2>/dev/null && break; sleep 1
done
keys 'root\r'; sleep 2; keys 'rootroot\r'; sleep 4
keys 'vi /etc/ops.txt\r'; sleep 15; keys 'G'; sleep 5
window() {  # name keys -- profile the time from sending keys until vi is back in normal mode
  rm -f $WORK/prof $WORK/prof.stacks $WORK/prof.callees; touch $WORK/prof.on; sleep 0.3
  start=$(date +%s.%N); XKEYS_DELAY=0.01 keys "$2"
  blank=0
  for i in $(seq 1 1500); do
    if [ "$(status)" -lt 200 ]; then [ $blank -eq 0 ] && last=$(date +%s.%N); blank=$((blank + 1)); else blank=0; fi
    [ $blank -ge 3 ] && break; sleep 0.5
  done
  rm -f $WORK/prof.on; sleep 2
  cp $WORK/prof $WORK/$1.txt; cp $WORK/prof.stacks $WORK/$1.stacks; cp $WORK/prof.callees $WORK/$1.callees
  echo "$1: $(echo "$last - $start" | bc) s, $(cat $WORK/prof)"
}
T10=$(python3 -c 'print("\\r".join("t%02d" % n for n in range(1, 11)))')
C10=$(python3 -c 'print("".join("t%02d" % n for n in range(1, 11)))')
window ret "o${T10}\x1b"
sleep 3
window chars "o${C10}\x1b"
snap "$WORK/final.png"
