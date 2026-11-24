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

"""End-to-end tests of ps."""

import re

from harness import help_text


def test_ps_lists_kernel_processes(s):
    s.login()
    out = s.sh("ps")
    assert "console" in out, out
    assert "memory manager" in out, out
    assert "ps" in out, out


def test_ps_shows_system_processes_owned_by_the_system(s):
    s.login()
    out = s.sh("ps")
    assert re.search(r"^\d+  system console$", out, re.M), out
    assert re.search(r"^\d+  system memory manager$", out, re.M), out
    assert re.search(r"^\d+  system filesystem$", out, re.M), out
    assert re.search(r"^\d+  root ps$", out, re.M), out


def levels(out):
    """Parse ps -l output into (user, level, name) tuples.

    @param out The output of ps -l.

    @return The tuples, in the order ps listed them.
    """
    return [m.groups() for m in re.finditer(
        r"^\d+  (\S+) (\S+) (.+)$", out, re.M)]


def test_ps_l_shows_each_processs_privilege_level(s):
    s.login()
    assert levels(s.sh("ps -l")) == [
        ("system", "kernel", "init"),
        ("system", "kernel", "console"),
        ("system", "kernel", "memory manager"),
        ("system", "executive", "filesystem"),
        ("system", "executive", "logger"),
        ("root", "supervisor", "ps"),
    ]


def test_ps_l_shows_a_background_job_at_user_level(s):
    s.login()
    s.sh("looseLoop &")
    assert ("root", "user", "looseLoop") in levels(s.sh("ps -l"))


def test_ps_with_an_unknown_option_prints_its_usage(s):
    s.login()
    out = s.sh("ps -x")
    assert s.sh("echo $?") == "1"
    assert out == help_text(s, "ps --help")
