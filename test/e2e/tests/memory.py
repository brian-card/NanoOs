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

"""End-to-end tests of booting with a SAMD21-sized heap."""

import re

from harness import (heap_at_prompt, SkipTest, SMALL_MEMORY_HEAP,
                     SMALL_MEMORY_HEAP_RANGE, small_memory_size)


def test_small_memory_boots_to_a_working_shell(s):
    s.restart_with_memory(small_memory_size(s))
    s.login()
    heap = heap_at_prompt(s)
    assert SMALL_MEMORY_HEAP_RANGE[0] <= heap < SMALL_MEMORY_HEAP_RANGE[1], heap
    assert s.sh("echo small-memory") == "small-memory"
    assert s.sh("cat /etc/hostname") == "nanoe2e"


def skip_with_file_cache(s):
    """Skip a test of a SAMD21-sized heap in a configuration that uses the file cache.

    @param s The test's Session.
    """
    if (s.env or {}).get("NANO_OS_SIM_FILE_CACHE"):
        raise SkipTest("the file cache is only used on targets with ample RAM")


def assert_pipeline_runs(s):
    """Run a three-stage pipeline and check its exact output.

    @param s The test's Session.
    """
    out = s.sh("ps | grep p | grep t")
    assert re.fullmatch(r"\d+  root ps\n\d+  root grep", out), out


def test_three_stage_pipeline_fits_a_tight_heap_after_login(s):
    # Fits only if checking an overlay's header doesn't take a block buffer.
    skip_with_file_cache(s)
    s.restart_with_memory(small_memory_size(s) + 3712 - SMALL_MEMORY_HEAP)
    s.login()
    for _ in range(3):
        assert_pipeline_runs(s)
