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

"""End-to-end tests of ed."""

from harness import (ed_input, ed_session, ll_names, OVERSIZED_LINE_COUNT,
                     small_memory_size, write_oversized_file)


CATFILE_LINES = ["line-%02d" % n for n in range(1, 41)]


def test_ed_writes_a_line_that_starts_on_a_cluster_boundary(s):
    # Line 129 starts at offset 1024 in ed's scratch file, the first byte of
    # its second cluster.
    lines = ["cl-%04d" % n for n in range(1, 130)]
    s.login()
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(lines)
               + [(".", ""), ("w /etc/clusters", "1032\n"), ("q", "")])
    assert s.sh("cat /etc/clusters") == "\n".join(lines)


def test_ed_appends_and_writes_a_new_file(s):
    s.login()
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(["one", "two", "three"])
               + [(".", ""), (",p", "one\ntwo\nthree\n"),
                  ("w /etc/new.txt", "14\n"), ("q", "")])
    assert s.sh("cat /etc/new.txt") == "one\ntwo\nthree"
    assert ll_names(s, "/etc") == \
        ["catfile", "hostname", "issue", "new.txt"]
    assert ll_names(s, "/tmp") == []


def test_ed_edits_an_existing_file(s):
    s.login()
    lines = CATFILE_LINES[:2] + CATFILE_LINES[3:]
    lines = ["header", "changed"] + lines[1:] + ["footer"]
    content = "\n".join(lines) + "\n"
    ed_session(s, [
        ("ed /etc/catfile", "320\n"),
        ("2,4p", "line-02\nline-03\nline-04\n"), ("3d", ""), (".=", "3\n"),
        ("1i", ""), ("header", ""), (".", ""),
        ("$a", ""), ("footer", ""), (".", ""),
        ("2c", ""), ("changed", ""), (".", ""),
        ("w", "%d\n" % len(content)), ("q", "")])
    assert s.sh("cat /etc/catfile") == content.rstrip("\n")
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


def test_ed_resolves_addresses_and_marks(s):
    s.login()
    ed_session(s, [
        ("ed /etc/catfile", "320\n"), ("$=", "40\n"), ("5ka", ""),
        ("'a,'a+2n", "5\tline-05\n6\tline-06\n7\tline-07\n"),
        ("-1p", "line-06\n"), ("+2", "line-08\n"), ("", "line-09\n"),
        (";+1p", "line-09\nline-10\n"), (",=", "40\n"), ("Q", "")])


def ed_model_move(lines, a1, a2, dest):
    block = lines[a1 - 1:a2]
    rest = lines[:a1 - 1] + lines[a2:]
    at = dest if dest < a1 else dest - len(block)
    return rest[:at] + block + rest[at:]


def ed_model_copy(lines, a1, a2, dest):
    return lines[:dest] + lines[a1 - 1:a2] + lines[dest:]


def test_ed_refuses_marks_that_are_not_set(s):
    s.login()
    ed_session(s, [
        ("ed /etc/issue", "13\n"), ("2ka", ""), ("'bp", "?\n"),
        ("h", "invalid mark\n"), ("'a=", "2\n"),
        ("e /etc/hostname", "7\n"), ("'a=", "?\n"),
        ("h", "invalid mark\n"), ("q", "")])
    assert ll_names(s, "/tmp") == []


def test_ed_moves_copies_and_joins_lines(s):
    s.login()
    lines = list(CATFILE_LINES)
    lines = ["".join(lines[0:3])] + lines[3:]
    lines = ed_model_move(lines, 2, 3, len(lines))
    lines = ed_model_move(lines, 10, 12, 4)
    lines = ed_model_copy(lines, 1, 1, 0)
    lines = ed_model_copy(lines, 2, 3, 5)
    lines = ed_model_copy(lines, 2, 3, 2)
    ed_session(s, [
        ("ed /etc/catfile", "320\n"), ("1,3j", ""), ("2,3m$", ""),
        ("10,12m4", ""), ("1t0", ""), ("2,3t5", ""), ("2,3t2", ""),
        ("1,3m2", "?\n"), (",p", "\n".join(lines) + "\n"), ("Q", "")])


def test_ed_reports_errors_and_guards_unsaved_changes(s):
    s.login()
    ed_session(s, [
        ("ed", ""), ("x", "?\n"), ("h", "unknown command\n"),
        ("H", "unknown command\n"), ("9p", "?\ninvalid address\n"),
        ("a", ""), ("text", ""), (".", ""),
        ("q", "?\nwarning: buffer modified\n"),
        ("e /etc/hostname", "?\nwarning: buffer modified\n"),
        ("e /etc/hostname", "7\n"), (",p", "nanoe2e\n"), ("q", "")])
    assert ll_names(s, "/tmp") == []


def test_ed_counts_each_change_as_unsaved(s):
    s.login()
    for command, output in (("1t1", ""), ("1m2", ""), ("1,2j", ""),
                            ("r /etc/hostname", "7\n")):
        ed_session(s, [
            ("ed /etc/issue", "13\n"), (command, output),
            ("q", "?\n"), ("h", "warning: buffer modified\n"), ("Q", "")])
    assert ll_names(s, "/tmp") == []


def test_ed_undoes_and_redoes_each_change(s):
    s.login()
    ed_session(s, [
        ("ed", ""), ("u", "?\n"), ("h", "nothing to undo\n"),
        ("a", "")] + ed_input(["one", "two", "three", "four"]) + [
        (".", ""), ("u", ""), ("$=", "0\n"), ("u", ""), ("$=", "4\n"),
        ("2,3d", ""), (",p", "one\nfour\n"), ("u", ""),
        (",p", "one\ntwo\nthree\nfour\n"), ("u", ""), (",p", "one\nfour\n"),
        ("u", ""), ("1,2j", ""), (",p", "onetwo\nthree\nfour\n"), ("u", ""),
        (",p", "one\ntwo\nthree\nfour\n"), ("1,2m$", ""),
        (",p", "three\nfour\none\ntwo\n"), ("u", ""),
        (",p", "one\ntwo\nthree\nfour\n"), ("3,4t0", ""),
        (",p", "three\nfour\none\ntwo\nthree\nfour\n"), ("u", ""),
        (",p", "one\ntwo\nthree\nfour\n"),
        ("2r /etc/hostname", "7\n"), (",p", "one\ntwo\nnanoe2e\nthree\nfour\n"),
        ("u", ""), (",p", "one\ntwo\nthree\nfour\n"), ("Q", "")])
    assert ll_names(s, "/tmp") == []


def test_ed_reads_and_appends_files(s):
    s.login()
    ed_session(s, [
        ("ed /etc/hostname", "7\n"), ("f", "/etc/hostname\n"),
        ("$r /etc/issue", "13\n"), ("w /etc/out.txt", "21\n"),
        ("W /etc/out.txt", "21\n"), ("q", "")])
    once = "nanoe2e\n\\s \\r \\n \\l\n\n"
    assert s.sh("cat /etc/out.txt") == (once + once).rstrip("\n")
    assert s.sh("cat /etc/hostname") == "nanoe2e"


def test_ed_reports_files_it_cannot_open(s):
    s.login()
    ed_session(s, [
        ("ed /etc/missing", "?\n"), ("h", "cannot open input file\n"),
        ("f", "/etc/missing\n"), ("a", ""), ("x", ""), (".", ""),
        ("w /missing/out.txt", "?\n"), ("h", "cannot open output file\n"),
        ("Q", "")])
    assert ll_names(s, "/tmp") == []
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


def test_ed_edits_a_file_larger_than_the_heap(s):
    s.login()
    lines = write_oversized_file(s)
    s.restart_with_memory(small_memory_size(s))
    s.login()
    del lines[0:10]
    lines[0:0] = lines[99:110]
    del lines[110:121]
    ed_session(s, [
        ("ed /etc/bigfile", "%d\n" % (8 * OVERSIZED_LINE_COUNT)),
        ("$=", "%d\n" % OVERSIZED_LINE_COUNT), ("150p", "big-150\n"),
        ("1,10d", ""), ("$=", "%d\n" % (OVERSIZED_LINE_COUNT - 10)),
        ("100,110m0", ""), ("1p", lines[0] + "\n"), ("$p", lines[-1] + "\n"),
        ("w", "%d\n" % (8 * len(lines))), ("q", "")])
    assert s.sh("cat /etc/bigfile", timeout=30) == "\n".join(lines)
    assert ll_names(s, "/tmp") == []


def test_ed_keeps_typed_text_larger_than_the_heap(s):
    s.restart_with_memory(small_memory_size(s))
    s.login()
    typed = ["t%03d" % n for n in range(1, OVERSIZED_LINE_COUNT + 1)]
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(typed)
               + [(".", ""), ("$=", "%d\n" % OVERSIZED_LINE_COUNT),
                  ("150p", "t150\n"), ("Q", "")])
    assert s.sh("echo still-alive") == "still-alive"
    assert ll_names(s, "/tmp") == []


def test_ed_reads_a_file_larger_than_the_heap_into_the_buffer(s):
    s.login()
    write_oversized_file(s)
    s.restart_with_memory(small_memory_size(s))
    s.login()
    ed_session(s, [
        ("ed /etc/issue", "13\n"),
        ("r /etc/bigfile", "%d\n" % (8 * OVERSIZED_LINE_COUNT)),
        ("$=", "%d\n" % (OVERSIZED_LINE_COUNT + 2)), ("3p", "big-001\n"),
        ("$p", "big-%03d\n" % OVERSIZED_LINE_COUNT), ("Q", "")])
    assert ll_names(s, "/tmp") == []


def test_ed_quits_when_too_little_memory_is_left_to_edit(s):
    s.restart_with_memory(small_memory_size(s))
    s.login()
    ed_session(s, [("ed /etc/catfile", "320\n"), ("Q", "")])
    assert s.sh("echo still-alive") == "still-alive"
    assert ll_names(s, "/tmp") == []
