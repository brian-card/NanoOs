#!/usr/bin/env python3
"""Standalone end-to-end test runner for the NanoOs simulator.

Needs only `pexpect` (no pytest).  Emits TAP on stdout.

    test/e2e/run.py [-v] [-j JOBS] [name-substring-filter]

Builds (once) a FAT32 disk image via mkimage.sh, then for each simulator
binary spawns it on a fresh image copy and drives the console.  Tests run in
JOBS worker processes, one per available processor by default (one with -v).
The tests are the test_* functions of the modules in tests/, one module per
command tested.
"""

import concurrent.futures
import glob
import importlib
import multiprocessing
import os
import subprocess
import sys

import harness
from harness import (E2E_DIR, HOSTNAME, REPO_ROOT, Session, small_memory_key,
                     small_memory_size)

CACHE_DIR = os.path.join(E2E_DIR, ".cache")
BLOCK_FS = "contiguous"

# (label, binary, extra environment).  The poll-only pass makes the console
# UART report poll-only so the console's polling path stays covered.  The
# cache passes call file overlays through the overlay file cache.
SIM_CONFIGS = [
    ("nano-os-sim_stripped", "nano-os-sim_stripped", {}),
    ("nano-os-sim", "nano-os-sim", {}),
    ("nano-os-sim-poll", "nano-os-sim", {"NANO_OS_SIM_POLL_ONLY": "1"}),
    ("nano-os-sim_stripped-cache", "nano-os-sim_stripped",
     {"NANO_OS_SIM_FILE_CACHE": "1"}),
    ("nano-os-sim-cache", "nano-os-sim", {"NANO_OS_SIM_FILE_CACHE": "1"}),
]


def build_image():
    os.makedirs(CACHE_DIR, exist_ok=True)
    img = os.path.join(CACHE_DIR, f"disk-{BLOCK_FS}.img")
    stamp = img + ".stamp"

    newest = 0.0
    for base in ("src", "sim", "usr/src"):
        for root, _d, files in os.walk(os.path.join(REPO_ROOT, base)):
            if "/obj" in root or "/bin" in root:
                continue
            for f in files:
                if f.endswith((".c", ".h", ".cpp", ".ld")) or f == "makefile":
                    newest = max(newest, os.path.getmtime(os.path.join(root, f)))

    if os.path.exists(img) and os.path.exists(stamp) \
            and os.path.getmtime(stamp) >= newest:
        return img

    print(f"# building {img} ...")
    subprocess.run([os.path.join(E2E_DIR, "mkimage.sh"), img, HOSTNAME, BLOCK_FS],
                   cwd=REPO_ROOT, check=True,
                   stdout=(None if harness.VERBOSE else subprocess.DEVNULL))
    open(stamp, "w").close()
    return img


def load_tests():
    """Import every module in tests/ and collect its test functions.

    @return The test functions, sorted by name.
    """
    tests = {}
    for path in sorted(glob.glob(os.path.join(E2E_DIR, "tests", "*.py"))):
        name = os.path.splitext(os.path.basename(path))[0]
        if name == "__init__":
            continue
        module = importlib.import_module("tests." + name)
        for k, v in vars(module).items():
            if k.startswith("test_") and callable(v):
                assert k not in tests, \
                    f"{k} is in both {tests[k].__module__} and {module.__name__}"
                tests[k] = v
    return [tests[k] for k in sorted(tests)]


TESTS = load_tests()


# Tests known to fail because of an open bug (see test/BUGS.txt).  A failure
# is reported as TAP "# TODO <reason>" and does not fail the run; if one
# starts passing the runner says so.
XFAIL = {}


def uses_small_memory(t):
    """Report whether a test boots the simulator at the small-memory size.

    @param t The test function.

    @return True if the test calls small_memory_size, False otherwise.
    """
    return "small_memory_size" in t.__code__.co_names


def init_worker(verbose):
    harness.VERBOSE = verbose


def probe_small_memory_size(sim_bin, env, image):
    """Boot a simulator once and return the memory size that leaves SMALL_MEMORY_HEAP.

    @param sim_bin The simulator binary to boot.
    @param env Extra environment for the simulator.
    @param image The disk image to boot a copy of.

    @return The simulated memory size, in bytes.
    """
    s = Session(sim_bin, image, env)
    try:
        return small_memory_size(s)
    finally:
        s.close()


def run_case(i, label, sim_bin, env, test_module, test_name, image,
             small_size):
    """Run one test against a fresh simulator and return its TAP lines.

    @param i The test's TAP number.
    @param label The test's TAP description.
    @param sim_bin The simulator binary to run.
    @param env Extra environment for the simulator.
    @param test_module The name of the module that holds the test.
    @param test_name The name of the test function.
    @param image The disk image to boot a copy of.
    @param small_size The small-memory size for this configuration, or None.

    @return A tuple of the TAP lines and whether the test counts as a failure.
    """
    if small_size is not None:
        harness._small_memory_sizes[small_memory_key(sim_bin, env)] = small_size
    xfail = XFAIL.get(test_name)
    lines = []
    failed = False
    s = None
    try:
        s = Session(sim_bin, image, env)
        getattr(importlib.import_module(test_module), test_name)(s)
        if xfail:
            lines.append(f"ok {i} - {label} # TODO {xfail} -- PASSES NOW, promote it")
        else:
            lines.append(f"ok {i} - {label}")
    except Exception as e:  # noqa: BLE001
        msg = str(e).splitlines()[0] if str(e) else type(e).__name__
        if xfail:
            lines.append(f"not ok {i} - {label} # TODO {xfail}")
        else:
            failed = True
            lines.append(f"not ok {i} - {label}")
        lines.append(f"#   {type(e).__name__}: {msg}")
        if s is not None and s.child.before:
            tail = s.child.before.replace("\r", "")[-300:]
            for line in tail.splitlines():
                lines.append(f"#   | {line}")
    finally:
        if s is not None:
            s.close()
    return lines, failed


def main(argv):
    args = [a for a in argv[1:]]
    if "-v" in args:
        harness.VERBOSE = True
        args.remove("-v")
    jobs = 1 if harness.VERBOSE else len(os.sched_getaffinity(0))
    if "-j" in args:
        at = args.index("-j")
        jobs = int(args[at + 1])
        del args[at:at + 2]
    flt = args[0] if args else None

    image = build_image()

    cases = []
    for config_name, sim_name, env in SIM_CONFIGS:
        sim_bin = os.path.join(REPO_ROOT, "sim", "bin", sim_name)
        for t in TESTS:
            label = f"{config_name}/{t.__name__}"
            if flt and flt not in label:
                continue
            cases.append((label, sim_bin, env, t))

    print("TAP version 13")
    print(f"1..{len(cases)}", flush=True)
    failures = 0
    context = multiprocessing.get_context("forkserver")
    with concurrent.futures.ProcessPoolExecutor(
            max_workers=jobs, mp_context=context,
            initializer=init_worker, initargs=(harness.VERBOSE,)) as pool:
        probes = {}
        for _label, sim_bin, env, t in cases:
            key = small_memory_key(sim_bin, env)
            if os.path.exists(sim_bin) and key not in probes \
                    and uses_small_memory(t):
                probes[key] = pool.submit(
                    probe_small_memory_size, sim_bin, env, image)

        runs = [None] * len(cases)
        waiting = []
        for i, (label, sim_bin, env, t) in enumerate(cases, 1):
            if not os.path.exists(sim_bin):
                continue
            probe = probes.get(small_memory_key(sim_bin, env))
            if probe is None or not uses_small_memory(t):
                runs[i - 1] = pool.submit(run_case, i, label, sim_bin, env,
                                          t.__module__, t.__name__, image,
                                          None)
            else:
                waiting.append((i, label, sim_bin, env, t, probe))
        for i, label, sim_bin, env, t, probe in waiting:
            size = None if probe.exception() else probe.result()
            runs[i - 1] = pool.submit(run_case, i, label, sim_bin, env,
                                      t.__module__, t.__name__, image, size)

        for i, ((label, sim_bin, _env, _t), run) in enumerate(
                zip(cases, runs), 1):
            if run is None:
                print(f"ok {i} - {label} # SKIP "
                      f"{os.path.basename(sim_bin)} not built", flush=True)
                continue
            try:
                lines, failed = run.result()
            except Exception as e:  # noqa: BLE001
                lines = [f"not ok {i} - {label}",
                         f"#   {type(e).__name__}: {e}"]
                failed = True
            failures += failed
            print("\n".join(lines), flush=True)

    print(f"# {len(cases)} test(s), {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
