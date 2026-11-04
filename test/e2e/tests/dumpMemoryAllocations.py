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

"""End-to-end tests of dumpMemoryAllocations."""

import re

from harness import ps_entries


def test_dump_memory_allocations_accounts_for_the_heap(s):
    s.login()
    out = s.sh("dumpMemoryAllocations").splitlines()
    assert out[0] == "Outstanding allocations:", out
    split = out.index("Available memory blocks:")
    owned = [re.fullmatch(r"  0x[0-9a-f]+: (\d+) bytes owned by (\d+)", line)
             for line in out[1:split]]
    free = [re.fullmatch(r"  0x[0-9a-f]+: (\d+) bytes available", line)
            for line in out[split + 1:]]
    assert owned and all(owned), out
    assert free and all(free), out
    pids = {int(m.group(2)) for m in owned}
    assert pids <= {pid for pid, _user, _name in ps_entries(s)}, (pids, out)
