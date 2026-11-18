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

"""Check that message payloads get their storage from src/kernel/MessageArgs.h.

    tools/check-message-args.py [file ...]

With no files, checks every .c and .h file under src/ and userspace/.  Fails
if a send passes the address of a local or global as its payload, if a
function's MESSAGE_ARGS_ALLOCATE has no MESSAGE_ARGS_FREE of the same name, or
if a function sets a pointer member that carries sender memory to anything
but a MESSAGE_BUFFER_SHARE it later unshares, storage from
MESSAGE_ARGS_CREATE it later destroys, NULL, or a pointer derived from the same
arguments.
"""

import glob
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Send wrappers and the index of their payload argument.
PAYLOAD_INDEX = {
    "initSendProcessMessageToPid": 2,
    "initSendProcessMessageToProcess": 2,
    "processMessageInit": 2,
    "processMessageInitInline": 2,
    "msg_init": 3,
    "msg_init_inline": 3,
    "schedulerInitSendMessageToPid": 2,
    "schedulerInitSendMessageToProcess": 2,
}

# Files that define the wrappers or the macros themselves.
EXCLUDED = {
    "src/kernel/Messages.c",
    "src/kernel/Messages.h",
    "src/kernel/MessageArgs.c",
    "src/kernel/MessageArgs.h",
}

# Pointer members that carry sender memory, by argument structure.
SHARED_MEMBERS = {
    "FilesystemFopenArgs": {"pathname", "mode"},
    "FilesystemRemoveArgs": {"pathname"},
    "FilesystemRenameArgs": {"oldpath", "newpath"},
    "FilesystemPathArgs": {"pathname"},
    "FilesystemOpendirArgs": {"pathname"},
    "FilesystemLstatArgs": {"pathname", "statbuf"},
    "FilesystemDirstatArgs": {"statbuf"},
    "FilesystemReaddirArgs": {"entry"},
    "Fat32FormatArgs": {"volumeLabel"},
    "FilesystemIoCommandArgs": {"buffer"},
    "GetFileBlockMetadataArgs": {"metadata"},
    "SchedulerGetProcessInfoArgs": {"processInfo"},
    "SchedulerReplaceOverlayArgs": {"overlay", "overlayId"},
    "SchedulerDirCapabilityArgs": {"name"},
    "SchedulerGetDirCapabilitiesArgs": {"buffer"},
    "SchedulerGetHostnameArgs": {"name"},
}

SEND_CALL = re.compile(r"\b(%s)\s*\(" % "|".join(PAYLOAD_INDEX))
ADDRESS_OF_STORAGE = re.compile(r"^&\s*\(?\s*[A-Za-z_]\w*(\s*\.\s*\w+)*\s*\)?$")
ALLOCATE = re.compile(r"\bMESSAGE_ARGS_ALLOCATE\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
# The parameter list just before a function body, and each pointer parameter
# in it, so that arguments a helper is handed are checked like its own.
PARAMETER_LIST = re.compile(r"\(([^()]*)\)\s*$")
POINTER_PARAMETER = re.compile(r"\b(\w+)\s*\*\s*(\w+)\s*(?=[,)]|$)")


def strip(text):
    """Blank out comments and string and character literals, keeping newlines.

    @param text The C source.

    @return The source with the same length and line breaks.
    """
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    return re.sub(
        r'//[^\n]*|/\*.*?\*/|"(\\.|[^"\\\n])*"|\'(\\.|[^\'\\\n])*\'',
        blank, text, flags=re.S)


def call_arguments(text, start):
    """Split the arguments of a call whose opening parenthesis ends at start.

    @param text The stripped source.
    @param start The index just after the call's opening parenthesis.

    @return The list of argument strings, stripped of surrounding space.
    """
    depth = 1
    arguments = []
    current = []
    i = start
    while i < len(text) and depth > 0:
        ch = text[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if depth == 1 and ch == ",":
            arguments.append("".join(current).strip())
            current = []
        elif depth > 0:
            current.append(ch)
        i += 1
    arguments.append("".join(current).strip())
    return arguments


def function_bodies(text):
    """Find the top-level brace blocks that are function bodies.

    @param text The stripped source.

    @return A list of (start, end) indexes of each body, braces included.
    """
    bodies = []
    depth = 0
    start = None
    for i, ch in enumerate(text):
        if ch == "{":
            if depth == 0:
                before = text[:i].rstrip()
                start = i if before.endswith(")") else None
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                bodies.append((start, i + 1))
                start = None
    return bodies


def line_of(text, index):
    """Return the 1-based line number of an index into text."""
    return text.count("\n", 0, index) + 1


def check(path, relative):
    """Check one file.

    @param path The path to the file.
    @param relative The path to report.

    @return The list of problems, one string each.
    """
    with open(path, errors="replace") as f:
        text = strip(f.read())
    problems = []

    for m in SEND_CALL.finditer(text):
        arguments = call_arguments(text, m.end())
        index = PAYLOAD_INDEX[m.group(1)]
        if index >= len(arguments):
            continue
        if ADDRESS_OF_STORAGE.match(arguments[index]):
            problems.append("%s:%d: %s payload %s is not message storage" % (
                relative, line_of(text, m.start()), m.group(1),
                arguments[index]))

    for start, end in function_bodies(text):
        body = text[start:end]
        for m in ALLOCATE.finditer(body):
            name = m.group(2)
            if not re.search(r"\bMESSAGE_ARGS_FREE\s*\(\s*%s\s*\)" % name,
                             body):
                problems.append("%s:%d: MESSAGE_ARGS_ALLOCATE of %s has no "
                                "MESSAGE_ARGS_FREE" % (
                                    relative, line_of(text, start + m.start()),
                                    name))
            for member in sorted(SHARED_MEMBERS.get(m.group(1), ())):
                problems.extend(check_member(
                    relative, text, start, body, name, member))
        parameters = PARAMETER_LIST.search(text, 0, start)
        if parameters is None:
            continue
        for m in POINTER_PARAMETER.finditer(parameters.group(1)):
            for member in sorted(SHARED_MEMBERS.get(m.group(1), ())):
                problems.extend(check_member(
                    relative, text, start, body, m.group(2), member))
    return problems


def check_member(relative, text, start, body, name, member):
    """Check every assignment to one pointer member of one argument variable.

    @param relative The path to report.
    @param text The stripped source.
    @param start The index of the function body in text.
    @param body The function body.
    @param name The argument variable.
    @param member The pointer member.

    @return The list of problems, one string each.
    """
    problems = []
    target = r"\b%s\s*->\s*%s" % (name, member)
    for m in re.finditer(target + r"\s*=(?!=)\s*([^;]*);", body):
        value = m.group(1)
        where = "%s:%d: %s->%s" % (
            relative, line_of(text, start + m.start()), name, member)
        if "MESSAGE_BUFFER_SHARE" in value:
            if not re.search(r"\bMESSAGE_BUFFER_UNSHARE\s*\(\s*(\(\s*\w+\s*\*\s*\)\s*)?"
                             + target, body):
                problems.append(where + " is shared but never unshared")
        elif "MESSAGE_ARGS_CREATE" in value:
            if not re.search(r"\bMESSAGE_ARGS_DESTROY\s*\(\s*" + target,
                             body):
                problems.append(where + " is created but never destroyed")
        elif value.strip() == "NULL" or re.search(r"\b%s\s*->" % name, value):
            continue
        else:
            problems.append(where + " carries sender memory without "
                            "MESSAGE_BUFFER_SHARE")
    return problems


def main(argv):
    if len(argv) > 1:
        paths = argv[1:]
    else:
        paths = sorted(
            glob.glob(os.path.join(REPO_ROOT, "src", "**", "*.[ch]"),
                      recursive=True)
            + glob.glob(os.path.join(REPO_ROOT, "userspace", "**", "*.[ch]"),
                        recursive=True))

    problems = []
    for path in paths:
        relative = os.path.relpath(os.path.abspath(path), REPO_ROOT)
        if relative in EXCLUDED:
            continue
        problems.extend(check(path, relative))

    for problem in problems:
        print(problem)
    print("%d problem(s) in %d file(s)" % (len(problems), len(paths)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
