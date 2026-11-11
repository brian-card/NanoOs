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


"""End-to-end tests of mush's cd built-in."""

from tests.capabilities import DENIED, HOMES, boot_with, repo_file


def test_cd_to_a_missing_directory_fails_and_stays_put(s):
    s.login()
    assert s.sh("cd /etc/nothere") == \
        "cd: /etc/nothere: No such entry found"
    assert s.sh("echo $?") == "1"
    assert s.sh("pwd") == "/root"


def test_cd_to_a_file_fails(s):
    s.login()
    assert s.sh("cd /etc/hostname") == "cd: /etc/hostname: Not a directory"
    assert s.sh("pwd") == "/root"


def test_a_failed_cd_leaves_oldpwd_alone(s):
    s.login()
    assert s.sh("cd /etc") == ""
    assert s.sh("echo $?") == "0"
    assert s.sh("cd nothere") == "cd: nothere: No such entry found"
    assert s.sh("cd -") == ""
    assert s.sh("pwd") == "/root"


def test_cd_needs_stat_on_the_directory(s):
    boot_with(s, directories=HOMES)
    s.login("user1", "user1user1")
    assert s.sh("cd ..") == "cd: ..: " + DENIED
    assert s.sh("cd /") == "cd: /: " + DENIED
    assert s.sh("pwd") == "/home/user1"


def test_stat_alone_is_enough_for_cd(s):
    boot_with(s, {"/etc/capabilities": repo_file("etc", "capabilities")
                  + "* s /drop/\n"}, HOMES + ("/drop",))
    s.login("user1", "user1user1")
    assert s.sh("cd /drop") == ""
    assert s.sh("pwd") == "/drop"
    assert DENIED in s.sh("ll")


def test_cd_refuses_a_path_too_long_to_hold(s):
    # A typed line can't be this long, so the name comes from a variable.
    s.login()
    assert s.sh("export X=" + "a" * 50) == ""
    name = "a" * 100
    assert s.sh("cd /$X$X") == "cd: /%s: Name too long" % name
    assert s.sh("cd $X$X") == "cd: %s: Name too long" % name
    assert s.sh("pwd") == "/root"
