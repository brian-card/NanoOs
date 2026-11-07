#!/usr/bin/env python3
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

"""Resolve folded stacks from the nanoos-profiler emulator and compare two
windows.  The repo must hold the Agon build that was profiled.

profan.py <repo> <a.stacks> [<b.stacks>] [--top N] [--incl]
Prints per-function self and inclusive sample counts for a (and a - b).
"""
import bisect, collections, glob, os, subprocess, sys, tempfile
NM = os.path.expanduser("~/agondev/bin/ez80-none-elf-nm")
OBJCOPY = os.path.expanduser("~/agondev/bin/ez80-none-elf-objcopy")

def symbols(elf):
    out = subprocess.run([NM, "-n", elf], capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "tTwW":
            syms.append((int(parts[0], 16), parts[2]))
    syms.sort()
    return [a for a, _ in syms], [n for _, n in syms]

def lookup(table, addr):
    addrs, names = table
    i = bisect.bisect_right(addrs, addr) - 1
    return names[i] if i >= 0 else "?%06x" % addr

def fingerprint(image):
    r24 = lambda o: (image[o] | (image[o + 1] << 8) | (image[o + 2] << 16)
                     if 0 <= o and o + 2 < len(image) else 0)
    exports = r24(0x26)
    return (exports ^ (r24(exports - 0xffe000 + 3) << 8)) & 0xffffffff

repo = sys.argv[1]
kernel = symbols(os.path.join(repo, "build/agon/NanoOs.elf"))
fs = symbols(os.path.join(repo, "usr/obj/contiguous-filesystem/overlay.elf"))
overlays = {}
for elf in glob.glob(os.path.join(repo, "usr/obj/**/overlay.elf"), recursive=True):
    rel = os.path.relpath(elf, os.path.join(repo, "usr/obj")).replace("/overlay.elf", "")
    if "contiguous-filesystem" in rel:
        continue
    with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
        subprocess.run([OBJCOPY, "-O", "binary", elf, tmp.name], check=True)
        fp = fingerprint(tmp.read())
    if fp in overlays:
        print("fingerprint clash:", rel, overlays[fp][0], file=sys.stderr)
    overlays[fp] = (rel, symbols(elf))

def name(frame):
    if "#" in frame:
        a, fp = frame.split("#")
        rel, table = overlays.get(int(fp, 16), ("ovl?" + fp, ([], [])))
        return rel + ":" + lookup(table, int(a, 16)) if table[0] else rel
    a = int(frame, 16)
    if 0x40000 <= a < 0x50000:
        return "fs:" + lookup(fs, a)
    return lookup(kernel, a)

def load(path):
    selfc, incl, total = collections.Counter(), collections.Counter(), 0
    for line in open(path):
        stack, n = line.rsplit(" ", 1)
        n = int(n)
        names = [name(f) for f in stack.split(";")]
        total += n
        selfc[names[-1]] += n
        for nm in set(names):
            incl[nm] += n
    return selfc, incl, total

args = [a for a in sys.argv[2:] if not a.startswith("--")]
top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 30
a = load(args[0])
b = load(args[1]) if len(args) > 1 and not args[1].isdigit() else None
which = 1 if "--incl" in sys.argv else 0
print("total samples", a[2], "" if b is None else "minus %d = %d" % (b[2], a[2] - b[2]))
rows = []
for nm in a[which]:
    d = a[which][nm] - (b[which][nm] if b else 0)
    rows.append((d, a[which][nm], nm))
for d, n, nm in sorted(rows, reverse=True)[:top]:
    print("%8d %8d  %s" % (d, n, nm))
