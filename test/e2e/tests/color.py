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

"""End-to-end tests of color."""

from harness import help_text


def test_color_sets_the_colors_and_clears_the_screen(s):
    s.login()
    assert s.sh("color 1e") == "\x1b[44;93m\x1b[2J\x1b[H"
    assert s.sh("color 1e") == "\x1b[44;93m\x1b[2J\x1b[H"
    assert s.sh("color 7") == "\x1b[40;37m\x1b[2J\x1b[H"
    assert s.sh("color F0") == "\x1b[107;30m\x1b[2J\x1b[H"
    assert s.sh("color c6") == "\x1b[101;33m\x1b[2J\x1b[H"


def test_color_without_a_valid_attribute_prints_its_usage(s):
    s.login()
    usage = help_text(s, "color --help")
    for command in ("color", "color 1g", "color 123", "color 1e 2"):
        assert s.sh(command) == usage, command
    assert s.sh("clear") == "\x1b[40;97m\x1b[2J\x1b[H"
