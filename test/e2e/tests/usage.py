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

"""End-to-end tests of every command's --help and usage errors."""

from harness import help_text, ll_names


HELP_OPTION_LINE = "  --help  Show this help and exit."


def test_every_command_prints_its_usage_for_help_options(s):
    s.login()
    for entry in ll_names(s, "/usr/bin"):
        name = entry.rstrip("/")
        if entry.endswith("/"):
            out = help_text(s, name + " --help")
        else:
            # A file in /usr/bin is a script that runs another command.
            target = s.sh("cat /usr/bin/" + name).split()[0]
            out = s.sh(name + " --help")
            assert out == help_text(s, target + " --help"), out
            name = target
        last = out.splitlines()[-1]
        if name == "shutdown":
            assert last == "  --help        Show this help and exit.", out
        else:
            assert last == HELP_OPTION_LINE, out
    assert s.sh("echo still-here") == "still-here"


def test_usage_errors_print_the_same_summary_as_help(s):
    s.login()
    for command in ("cp", "mv", "grep", "kill", "mkdir", "rm", "rmdir",
                    "touch"):
        assert s.sh(command) == help_text(s, command + " --help"), command
    assert s.sh("shutdown") == help_text(s, "shutdown --help")
    assert s.sh("echo a --help") == "a --help"
    assert s.sh("echo -h") == "-h"
    assert s.sh("rm -h") == \
        'ERROR: Could not remove "-h": No such entry found'
