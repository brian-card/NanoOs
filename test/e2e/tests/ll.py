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

"""End-to-end tests of ll."""

import datetime

import pexpect

from harness import LS_LINE_RE, CATFILE_CONTENTS, ll_names, PROMPT


def test_ll_lists_names_of_every_length(s):
    # readdir reuses one entry for the whole listing, so it has to grow for
    # the 40-character name and still hold the short names around it.
    s.login()
    longest = "a-name-long-enough-to-need-four-lfns.txt"
    assert s.sh("touch /etc/ab") == ""
    assert s.sh("mv /etc/catfile /etc/" + longest) == ""
    assert s.sh("touch /etc/ABC") == ""
    assert s.sh("touch /etc/medium-length-name") == ""
    assert ll_names(s, "/etc") == sorted(
        ["ABC", "ab", "hostname", "issue", longest, "medium-length-name"])
    assert s.sh("cat /etc/" + longest) == CATFILE_CONTENTS


def test_ls_does_not_crash_on_populated_directory(s):
    # Regression test: ll's day-of-week column (weekdays[tm.tm_wday]) could
    # read an out-of-bounds index and dereference garbage as a string
    # pointer -- see test_ls_shows_correct_weekday_for_mtime for the root
    # cause. On the sim that's a SIGSEGV; on real hardware with no MMU it's
    # a hang instead. Checked separately from weekday correctness so a crash
    # here is reported distinctly from a wrong-but-harmless value.
    s.login()
    s.child.sendline("ll /usr/bin")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=12)
    if idx == 1:
        sig = s.child.signalstatus
        raise AssertionError(
            f"simulator died running ll on a populated directory "
            f"(signal {sig}{' = SIGSEGV' if sig == 11 else ''})")
    if idx == 2:
        raise AssertionError("ll hung listing a populated directory")
    out = s.child.before.replace("\r", "")
    matches = [m for m in (LS_LINE_RE.match(line)
                           for line in out.splitlines()) if m is not None]
    assert matches, f"ll printed no listing lines for /usr/bin:\n{out}"
    # Every command lives in /usr/bin/<name>/main.overlay, so ll's own
    # directory has to appear -- a renamed or missing command shows up here
    # as a shell "not found" rather than as a silent pass.
    names = {m["name"].rstrip("/") for m in matches}
    assert "ll" in names, \
        f"ll did not list its own /usr/bin entry, got {sorted(names)}:\n{out}"
    assert "alive" in s.sh("echo alive"), "shell unresponsive after ll"


def test_ls_shows_correct_weekday_for_mtime(s):
    # Regression test for a bug where NanoOsTime.c's _yearStartDay lookup
    # table (used by nanoOsGmtime_r to compute tm_wday) lacked
    # KEEP_IN_FLASH. On the stripped binary (.rodata removed -- what
    # actually ships to AgonLight2/ItsyBitsy), the table held garbage
    # instead of {4,5,6,1,2,3,4,...}, so tm_wday came out wildly
    # out-of-range (e.g. -4 instead of 4) for perfectly ordinary dates.
    # Nothing had ever read tm_wday before ll started printing a weekday
    # column, so the bug was latent until then. Confirms every listed
    # file's printed weekday actually matches its printed date, rather
    # than just being one of the seven valid-looking abbreviations.
    s.login()
    out = s.sh("ll /etc")
    # Session.sh()'s PROMPT pattern doesn't include the "root" username
    # prefix, so .before always has a trailing "root" line bled in from the
    # start of the *next* prompt -- harmless for the substring checks every
    # other test does, but not a real ll line, so filter to lines that
    # actually look like one rather than asserting every line matches.
    lines = [line for line in out.splitlines() if LS_LINE_RE.match(line)]
    assert lines, out
    checked = 0
    for line in lines:
        m = LS_LINE_RE.match(line)
        expected = datetime.date(
            int(m["year"]), int(m["month"]), int(m["day"])).strftime("%a")
        assert m["wday"] == expected, (
            f"ll printed weekday {m['wday']!r} for "
            f"{m['year']}-{m['month']}-{m['day']}, expected {expected!r}: "
            f"{line!r}")
        checked += 1
    # /etc always contains exactly hostname and issue (see mkimage.sh) plus
    # . and .. -- four real entries, not zero.
    assert checked >= 4, f"expected at least 4 entries, got:\n{out}"
