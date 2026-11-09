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

"""End-to-end tests of cat."""

from harness import assert_ctrl_c_kills, CATFILE_CONTENTS


def test_hostname_file(s):
    s.login()
    assert "nanoe2e" in s.sh("cat /etc/hostname")


def test_cat_prints_a_file_longer_than_its_buffer(s):
    s.login()
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS


def test_cat_reports_a_missing_file(s):
    s.login()
    assert s.sh("cat /etc/missing") == \
        'ERROR: Could not open file "/etc/missing": No such entry found'


def test_cat_reports_a_directory(s):
    s.login()
    assert s.sh("cat /usr/bin") == \
        'ERROR: Could not open file "/usr/bin": Invalid argument'


def test_cat_reading_the_console_is_killed_by_ctrl_c(s):
    assert_ctrl_c_kills(s, "cat")
