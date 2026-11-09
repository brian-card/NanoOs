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

"""End-to-end tests of mkdir and rmdir."""

from harness import (assert_created_now, CATFILE_CONTENTS, fsck_problems,
                     ll_names)


def test_mkdir_and_rmdir_manage_an_empty_directory(s):
    s.login()
    assert s.sh("mkdir /etc/newdir") == ""
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue", "newdir/"]
    assert ll_names(s, "/etc/newdir") == []
    assert ll_names(s, "/etc/newdir/..") == \
        ["catfile", "hostname", "issue", "newdir/"]
    assert s.sh("rmdir /etc/newdir") == ""
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
    assert fsck_problems(s) == []


def test_mkdir_nests_directories_and_holds_files(s):
    s.login()
    assert s.sh("mkdir /root/top") == ""
    assert s.sh("mkdir /root/top/inner") == ""
    assert ll_names(s, "/root/top/inner/..") == ["inner/"]
    assert s.sh("mv /etc/catfile /root/top/inner/catfile") == ""
    assert s.sh("cat /root/top/inner/catfile") == CATFILE_CONTENTS
    assert s.sh("rmdir /root/top/inner") == \
        'ERROR: Could not remove directory "/root/top/inner": Directory not empty'
    assert s.sh("rm /root/top/inner/catfile") == ""
    assert s.sh("rmdir /root/top/inner") == ""
    assert s.sh("rmdir /root/top") == ""
    assert "top/" not in ll_names(s, "/root")
    assert fsck_problems(s) == []


def test_mkdir_stamps_the_directory_with_the_current_time(s):
    s.login()
    assert_created_now(s, "mkdir /etc/stamped", "stamped/")


def test_mkdir_and_rmdir_report_errors(s):
    s.login()
    assert s.sh("mkdir /etc/issue") == \
        'ERROR: Could not create directory "/etc/issue": File exists'
    assert s.sh("mkdir /missing/dir") == \
        'ERROR: Could not create directory "/missing/dir": No such entry found'
    assert s.sh("rmdir /etc/issue") == \
        'ERROR: Could not remove directory "/etc/issue": Not a directory'
    assert s.sh("rmdir /etc/missing") == \
        'ERROR: Could not remove directory "/etc/missing": No such entry found'
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
