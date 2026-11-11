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

"""End-to-end tests of mv."""

from harness import CATFILE_CONTENTS, fsck_problems, ll_names


def test_mv_renames_a_file_to_a_long_name(s):
    s.login()
    assert s.sh("mv /etc/catfile /etc/renamed-catfile.txt") == ""
    assert s.sh("cat /etc/renamed-catfile.txt") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == \
        ["hostname", "issue", "renamed-catfile.txt"]


def test_mv_moves_a_file_to_another_directory(s):
    s.login()
    assert s.sh("mv /etc/catfile /usr/catfile") == ""
    assert s.sh("cat /usr/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["hostname", "issue"]
    assert "catfile" in ll_names(s, "/usr")


def test_mv_replaces_an_existing_file(s):
    s.login()
    assert s.sh("mv /etc/catfile /etc/issue") == ""
    assert s.sh("cat /etc/issue") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["hostname", "issue"]


def test_mv_moves_files_into_a_directory(s):
    s.login()
    assert s.sh("mkdir scratch") == ""
    assert s.sh("cp /usr/bin/helloworld/main.overlay scratch/main.overlay") \
        == ""
    assert s.sh("mkdir /usr/bin/hello2") == ""
    assert s.sh("mv scratch/main.overlay /usr/bin/hello2") == ""
    assert ll_names(s, "/root/scratch") == []
    assert ll_names(s, "/usr/bin/hello2") == ["main.overlay"]
    assert s.sh("hello2") == "Hello, world!"
    assert s.sh("mv /usr/bin/hello2/main.overlay /root/scratch/") == ""
    assert ll_names(s, "/root/scratch") == ["main.overlay"]
    assert s.sh("mv /etc/catfile /etc/hostname /root/scratch") == ""
    assert ll_names(s, "/root/scratch") == \
        ["catfile", "hostname", "main.overlay"]
    assert s.sh("cat /root/scratch/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["issue"]
    assert fsck_problems(s) == []


def test_mv_reports_a_target_that_is_not_a_directory(s):
    s.login()
    assert s.sh("mv /etc/catfile /etc/hostname /etc/issue") == \
        'ERROR: "/etc/issue" is not a directory'
    assert s.sh("mv /etc/missing /usr") == \
        'ERROR: Could not rename "/etc/missing" to "/usr/missing": ' \
        'No such entry found'
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


def test_mv_changes_only_the_case_of_a_name(s):
    s.login()
    assert s.sh("mv /etc/catfile /etc/CatFile") == ""
    assert s.sh("cat /etc/CatFile") == CATFILE_CONTENTS
    assert s.sh("cat /etc/catfile") == \
        'ERROR: Could not open file "/etc/catfile": No such entry found'
    assert ll_names(s, "/etc") == ["CatFile", "hostname", "issue"]


def test_mv_reports_a_missing_source(s):
    s.login()
    assert s.sh("mv /etc/missing /etc/other") == \
        'ERROR: Could not rename "/etc/missing" to "/etc/other": ' \
        'No such entry found'
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
