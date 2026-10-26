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
################################################################################


"""Check that the AgonLight2 boot path never reaches .ramtext.

Usage: AgonLight2PreloadCheck.py <objdump> <readelf> <elf>

Starting from _start, every interrupt handler, and every function named in the
HAL dispatch tables used before the load, this follows each call, jump, and
taken address through the flash .text section.  It stops at nanoOsStart, which
main() calls only after agonLight2LoadRamText() succeeds.  Any reference into
.ramtext, other than to its build-ID header, fails the check.
"""

import bisect
import collections
import re
import subprocess
import sys

HANDOFF = "_nanoOsStart"

PRELOAD_TABLES = re.compile(
    r"^_(agonLight2(Uart|Dio|Spi|Clock|Timer|Memory|BlockDevice)Functions"
    r"|halImpl(Uart|Dio|Spi|Clock|Timer|Memory|BlockDevice))$")

RAM_TEXT_HEADER_SIZE = 4


def run(*args):
    return subprocess.run(args, capture_output=True, text=True,
        check=True).stdout


def readSymbols(readelf, elf):
    symbols = []
    for line in run(readelf, "-sW", elf).splitlines():
        fields = line.split()
        if (len(fields) < 8) or (not fields[0].endswith(":")):
            continue
        try:
            value = int(fields[1], 16)
            size = int(fields[2], 0)
        except ValueError:
            continue
        symbols.append((value, size, fields[3], fields[7], fields[6]))
    return symbols


def readSections(readelf, elf):
    sections = {}
    for line in run(readelf, "-SW", elf).splitlines():
        match = re.match(
            r"\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+"
            r"([0-9a-f]+)", line)
        if match:
            sections[match.group(2)] = (int(match.group(3), 16),
                int(match.group(4), 16), match.group(1))
    return sections


def readBytes(objdump, elf, section):
    contents = {}
    for line in run(objdump, "-s", "-j", section, elf).splitlines():
        match = re.match(r"^ ([0-9a-f]+) ((?:[0-9a-f]+ ?){1,4})", line)
        if not match:
            continue
        address = int(match.group(1), 16)
        for byte in bytes.fromhex(match.group(2).replace(" ", "")):
            contents[address] = byte
            address += 1
    return contents


def main():
    objdump, readelf, elf = sys.argv[1:4]
    sections = readSections(readelf, elf)
    textStart, textSize, textIndex = sections[".text"]
    ramStart, ramSize, ramIndex = sections[".ramtext"]
    ramEnd = ramStart + ramSize

    symbols = readSymbols(readelf, elf)
    dataStart = dataEnd = None
    for value, size, kind, name, index in symbols:
        if name == "_agonLight2FlashDataStart":
            dataStart = value
        elif name == "__etext":
            dataEnd = value

    code = {}
    labels = set()
    for kind in ("FUNC", "NOTYPE"):
        for value, size, symbolKind, name, index in symbols:
            if (symbolKind == kind) and (index == textIndex) \
                    and (not (dataStart <= value < dataEnd)) \
                    and (value not in code):
                code[value] = name
                if kind == "NOTYPE":
                    labels.add(name)
    codeAddresses = sorted(code)
    ramNames = {}
    for value, size, kind, name, index in symbols:
        if (kind in ("FUNC", "NOTYPE")) and (index == ramIndex):
            ramNames.setdefault(value, name)
    ramAddresses = sorted(ramNames)

    def owner(address, addresses, names):
        index = bisect.bisect_right(addresses, address) - 1
        return names[addresses[index]] if index >= 0 else hex(address)

    edges = {}
    ramReferences = {}
    current = None
    for line in run(objdump, "-d", "-j", ".text", elf).splitlines():
        match = re.match(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)$", line)
        if not match:
            continue
        address = int(match.group(1), 16)
        if dataStart <= address < dataEnd:
            continue
        current = owner(address, codeAddresses, code)
        instruction = match.group(2)
        branch = instruction.startswith(("call", "jp", "jr", "djnz", "rst"))
        for target in re.findall(r"0x([0-9a-f]+)", instruction):
            target = int(target, 16)
            if ramStart + RAM_TEXT_HEADER_SIZE <= target < ramEnd:
                ramReferences.setdefault(current, set()).add(
                    owner(target, ramAddresses, ramNames))
            elif (target in code) and (branch or (code[target] not in labels)):
                edges.setdefault(current, set()).add(code[target])

    # Assembly labels carry no size, so execution can run on into the next one.
    for address, nextAddress in zip(codeAddresses, codeAddresses[1:]):
        if (code[address] in labels) and (code[nextAddress] in labels):
            edges.setdefault(code[address], set()).add(code[nextAddress])

    roots = {"_start"}
    ivtStart = ivtEnd = None
    for value, size, kind, name, index in symbols:
        if name == "__ivt_start":
            ivtStart = value
        elif name == "__ivt_end":
            ivtEnd = value
    for address, name in code.items():
        if ivtStart <= address < ivtEnd:
            roots.add(name)

    tableBytes = {}
    for section in (".data", ".text"):
        tableBytes.update(readBytes(objdump, elf, section))
    starts = sorted({(index, value) for value, size, kind, name, index
        in symbols if kind != "SECTION"})
    for value, size, kind, name, index in symbols:
        if not PRELOAD_TABLES.match(name):
            continue
        if size == 0:
            following = bisect.bisect_right(starts, (index, value))
            if (following < len(starts)) and (starts[following][0] == index):
                size = starts[following][1] - value
        for offset in range(0, size - 2):
            pointer = (tableBytes.get(value + offset, 0)
                | (tableBytes.get(value + offset + 1, 0) << 8)
                | (tableBytes.get(value + offset + 2, 0) << 16))
            if pointer in code:
                roots.add(code[pointer])
            elif pointer in ramNames:
                ramReferences.setdefault(name, set()).add(
                    owner(pointer, ramAddresses, ramNames))
                roots.add(name)

    parents = {root: None for root in roots}
    pending = collections.deque(sorted(roots, key=lambda root: root != "_start"))
    while pending:
        function = pending.popleft()
        if function == HANDOFF:
            continue
        for callee in edges.get(function, ()):
            if callee not in parents:
                parents[callee] = function
                pending.append(callee)

    failures = sorted(f for f in parents if f in ramReferences)
    if not failures:
        return 0

    sys.stderr.write("ERROR: code that runs before the .ramtext image is "
        "loaded reaches .ramtext.\n")
    for function in failures:
        chain = []
        link = function
        while link is not None:
            chain.append(link)
            link = parents[link]
        sys.stderr.write("  %s -> %s\n    via %s\n" % (function,
            ", ".join(sorted(ramReferences[function])),
            " <- ".join(chain)))
    sys.stderr.write("Add each .ramtext function named above to "
        "ld/AgonLight2Flash.ld.\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
