#!/usr/bin/env python3
###############################################################################
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
###############################################################################
"""End-to-end tests of the namespace that processes see."""

import io
import os
import re
import subprocess

RODATA = "/system/data/NanoOs_rodata.bin"
NOT_FOUND = "No such entry found"


def image_names(s, path):
    """List a directory on the session's image, from the real root.

    @param s The Session.
    @param path The directory's path from the real root.

    @return The sorted names in the directory.
    """
    out = subprocess.run(
        ["mdir", "-b", "-i", s.img_copy + "@@2097152", "::" + path],
        env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), capture_output=True,
        check=True).stdout.decode()
    return sorted(os.path.basename(line.rstrip("/")) for line in out.split())


def test_a_login_shell_cannot_read_the_system_tree(s):
    s.login()
    assert NOT_FOUND in s.sh("cat " + RODATA)
    assert NOT_FOUND in s.sh("cat /secure/shadow")


def test_dots_do_not_climb_out_of_userspace(s):
    s.login()
    assert NOT_FOUND in s.sh("cat /etc/../.." + RODATA)
    assert s.sh("cd /etc") == ""
    assert NOT_FOUND in s.sh("cat ../.." + RODATA)
    assert NOT_FOUND in s.sh("cat ../../userspace/etc/issue")


def test_a_shell_cannot_run_a_system_program(s):
    s.login()
    assert s.sh("/system/bin/login") == "/system/bin/login: command not found"
    assert s.sh("/usr/../system/bin/login") == \
        "/usr/../system/bin/login: command not found"


def test_the_real_tree_is_three_directories(s):
    s.login()
    s.child.terminate(force=True)
    assert image_names(s, "/") == ["secure", "system", "userspace"]


def test_the_system_programs_are_only_in_system(s):
    s.login()
    out = s.sh("ll /usr/bin")
    for name in ("getty/", "login/", "logger/"):
        assert name not in out.split(), out
    assert NOT_FOUND in s.sh("cat /usr/lib/vdp.drv/main.overlay")
    s.child.terminate(force=True)
    assert image_names(s, "/system") == ["bin", "data", "drivers"]
    assert image_names(s, "/system/bin") == ["getty", "logger", "login"]
    assert "NanoOs_rodata.bin" in image_names(s, "/system/data")


def test_the_logger_runs_from_the_system_tree(s):
    s.child.terminate(force=True)
    s.spawn([s.img_copy])
    boot_output = io.StringIO()
    s.child.logfile_read = boot_output
    s.login()
    assert re.search(r"^\d+  root logger$", s.sh("ps"), re.M)
    assert "Could not open binary file" not in boot_output.getvalue()
