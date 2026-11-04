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

"""End-to-end tests of termtest and the UART console's terminal reports."""

from harness import PROMPT


def test_a_uart_console_is_80_by_24(s):
    s.login()
    assert s.sh("termtest size") == "24 80"


def test_a_resize_report_arrives_in_one_read(s):
    s.login()
    s.child.sendline("termtest report")
    s.child.expect_exact("\x1b[?2048h", timeout=10)
    s.child.send("\x1b[48;30;100;0;0t")
    s.child.expect_exact("\x1b[?2048l", timeout=10)
    s.child.expect(r"\A\^\[\[48;30;100;0;0t\r*\n" + PROMPT, timeout=10)


def test_exiting_with_resize_reports_on_turns_them_off(s):
    s.login()
    s.child.sendline("termtest leave")
    s.child.expect_exact("\x1b[?2048h", timeout=10)
    s.child.expect_exact("\x1b[?2048l", timeout=10)
    s.child.expect(PROMPT, timeout=10)
    assert s.sh("echo after") == "after"


def test_termtest_erase_writes_at_the_position_it_is_given(s):
    s.login()
    s.child.sendline("termtest erase 60 100")
    s.child.expect_exact("\x1b[60;100Habc\x1b[K", timeout=10)
    s.child.sendline("")
    s.child.expect(PROMPT, timeout=10)


def test_termtest_inserts_and_deletes_rows_at_the_row_it_is_given(s):
    s.login()
    s.child.sendline("termtest insert 3 2")
    s.child.expect_exact("\x1b[999;1Hz\x1b[3;1H\x1b[2L", timeout=10)
    s.child.sendline("")
    s.child.expect(PROMPT, timeout=10)
    s.child.sendline("termtest delete")
    s.child.expect_exact("\x1b[999;1Hz\x1b[2;1H\x1b[1M", timeout=10)
    s.child.sendline("")
    s.child.expect(PROMPT, timeout=10)
