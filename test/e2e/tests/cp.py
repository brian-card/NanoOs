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

"""End-to-end tests of cp."""

import os
import subprocess

from harness import CATFILE_CONTENTS, fsck_problems, ll_names


def test_cp_copies_a_file_to_a_new_name(s):
    s.login()
    assert s.sh("cp /etc/catfile /etc/copied-catfile.txt") == ""
    assert s.sh("cat /etc/copied-catfile.txt") == CATFILE_CONTENTS
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == \
        ["catfile", "copied-catfile.txt", "hostname", "issue"]
    assert fsck_problems(s) == []


def test_cp_truncates_a_longer_existing_file(s):
    s.login()
    issue = s.sh("cat /etc/issue")
    assert len(issue) < len(CATFILE_CONTENTS)
    assert s.sh("cp /etc/issue /etc/catfile") == ""
    assert s.sh("cat /etc/catfile") == issue
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
    assert fsck_problems(s) == []


def test_cp_copies_files_into_a_directory(s):
    s.login()
    assert s.sh("mkdir /root/top") == ""
    assert s.sh("cp /etc/catfile /etc/issue /root/top") == ""
    assert ll_names(s, "/root/top") == ["catfile", "issue"]
    assert s.sh("cat /root/top/catfile") == CATFILE_CONTENTS
    assert s.sh("cd /etc") == ""
    assert s.sh("cp hostname ../root/top/") == ""
    assert ll_names(s, "/root/top") == ["catfile", "hostname", "issue"]
    assert s.sh("cat /root/top/hostname") == s.sh("cat hostname")
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
    assert fsck_problems(s) == []


def image_file(s, path):
    """Read a file out of the session's image after the simulator has halted.

    @param s The Session.
    @param path The file's absolute path on the image.

    @return The file's contents, as bytes.
    """
    return subprocess.run(
        ["mtype", "-i", s.img_copy + "@@2097152", "::" + path],
        env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), capture_output=True,
        check=True).stdout


def test_cp_copies_a_binary_file_byte_for_byte(s):
    # An overlay starts with a NULL pointer, so a copy that treats its buffer
    # as a string writes nothing.
    s.login()
    source = "/usr/bin/cp/main.overlay"
    assert s.sh("cp %s /etc/copied.overlay" % source) == ""
    original = s.sh("ll /usr/bin/cp").splitlines()[-1].split()
    copied = s.sh("ll /etc").splitlines()[-1].split()
    assert copied[-1] == "copied.overlay" and copied[3] == original[3], \
        (original, copied)
    assert fsck_problems(s) == []
    expected = image_file(s, source)
    assert b"\0" in expected
    assert image_file(s, "/etc/copied.overlay") == expected


def test_copied_command_runs(s):
    s.login()
    assert s.sh("mkdir /usr/bin/hello2") == ""
    assert s.sh("cp /usr/bin/helloworld/main.overlay /usr/bin/hello2") == ""
    assert s.sh("hello2") == "Hello, world!"


def test_cp_reports_errors(s):
    s.login()
    assert s.sh("cp /etc/missing /etc/other") == \
        'ERROR: Could not open file "/etc/missing": No such entry found'
    assert s.sh("cp /etc /top") == 'ERROR: "/etc" is a directory'
    assert s.sh("cp /etc/catfile /etc/../etc/catfile") == \
        'ERROR: "/etc/catfile" and "/etc/../etc/catfile" are the same file'
    assert s.sh("cp /etc/catfile /etc/issue /etc/hostname") == \
        'ERROR: "/etc/hostname" is not a directory'
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
    assert "top/" not in ll_names(s, "/")
