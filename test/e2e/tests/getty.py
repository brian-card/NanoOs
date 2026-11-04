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

"""End-to-end tests of getty."""

import re

from harness import PROMPT


def test_getty_shows_every_line_of_the_issue_file(s):
    s.wait_login_prompt()
    banner = s.child.before.replace("\r", "")
    assert re.search(r"(\n|\x1b\[H)NanoOs \S+ nanoe2e tty\d+\n\n$", banner), \
        repr(banner[-120:])


def test_getty_resets_the_colors_and_clears_before_the_banner(s):
    s.wait_login_prompt()
    banner = s.child.before.replace("\r", "")
    assert re.search(r"\x1b\[40;97m\x1b\[2J\x1b\[H\n*NanoOs \S+ nanoe2e tty\d+\n\n$",
                     banner), repr(banner[-120:])


def test_getty_prompts_for_login_without_an_issue_file(s):
    s.login()
    assert s.sh("rm /etc/issue") == ""
    s.child.sendline("exit")
    s.child.expect("login: ", timeout=15)
    assert "ERROR" not in s.child.before, repr(s.child.before[-300:])
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("rootroot")
    s.child.expect(PROMPT, timeout=10)
