"""Heap figures for `ps | grep p | grep t` on the simulator's small-memory boot.

Builds (or reuses) the e2e image with the e2e runner's own builder and boots
the production simulator through the e2e harness.  Prints:

- the heap left at the prompt before and after each of five runs at the heap
  test_three_stage_pipeline_fits_a_tight_heap_after_login uses (3712 B), and
- the smallest heap at the prompt, to 16 B, at which three runs in a row
  still give the exact expected output.

Usage:  pipeline_heap.py [simulator-binary]
"""
import os, re, sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(0, os.path.join(REPO, 'test', 'e2e'))

import harness  # noqa: E402
import run  # noqa: E402

EXPECTED = re.compile(r"\d+  root ps\n\d+  root grep")


def boot(sim_bin, image, heap):
    """Boot a fresh simulator with `heap` bytes left at the prompt and log in."""
    s = harness.Session(sim_bin, image)
    size = harness.small_memory_size(s)
    s.restart_with_memory(size + heap - harness.SMALL_MEMORY_HEAP)
    s.login()
    return s


def runs_ok(sim_bin, image, heap, count):
    s = None
    try:
        s = boot(sim_bin, image, heap)
        for _ in range(count):
            if not EXPECTED.fullmatch(s.sh("ps | grep p | grep t")):
                return False
        return True
    except Exception:  # noqa: BLE001
        return False
    finally:
        run.close_session(s)


def main(argv):
    sim_bin = argv[1] if len(argv) > 1 else os.path.join(
        REPO, 'sim', 'bin', 'nano-os-sim')
    image = run.build_image()

    s = boot(sim_bin, image, 3712)
    try:
        before = harness.heap_at_prompt(s)
        after = []
        for _ in range(5):
            out = s.sh("ps | grep p | grep t")
            assert EXPECTED.fullmatch(out), out
            after.append(harness.heap_at_prompt(s))
    finally:
        run.close_session(s)
    print(f"heap at prompt before: {before}")
    print("heap at prompt after each run: " + " ".join(map(str, after)))

    low, high = 2688, 3712
    assert runs_ok(sim_bin, image, high, 3), "pipeline fails at 3712"
    while high - low > 16:
        mid = (low + high) // 2
        if runs_ok(sim_bin, image, mid, 3):
            high = mid
        else:
            low = mid
    print(f"smallest heap at prompt that runs it 3 times: {high} (fails at {low})")


if __name__ == '__main__':
    main(sys.argv)
