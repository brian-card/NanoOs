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

"""End-to-end tests of ver."""

import os
import re

from harness import help_text, PROMPT, REPO_ROOT


def test_ver_prints_the_version_and_copyright_between_blank_lines(s):
    with open(os.path.join(REPO_ROOT, "src", "kernel", "NanoOs.h")) as f:
        version = re.search(r'#define NANO_OS_VERSION\s+"([^"]+)"',
                            f.read()).group(1)
    s.login()
    s.child.sendline("ver")
    s.child.expect_exact("ver", timeout=10)
    s.child.expect(PROMPT, timeout=10)
    assert s.child.before.replace("\r", "") == \
        ("\n\nNanoOs Version %s\n"
         "Copyright (c) 2026 Brian Card\n"
         "Released under the MIT License.\n\n" % version), \
        repr(s.child.before)
    assert s.sh("ver extra") == help_text(s, "ver --help")
