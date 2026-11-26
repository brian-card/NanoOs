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
import shutil
import signal
import subprocess
import sys
import tempfile
import traceback

import harness
from harness import (E2E_DIR, HOSTNAME, REPO_ROOT, Session, small_memory_key,
                     small_memory_size)

CACHE_DIR = os.path.join(E2E_DIR, ".cache")
BLOCK_FS = "contiguous"

# Every expect has its own timeout, so a test still running after this many
# seconds is blocked somewhere a timeout can't reach.
TEST_TIME_LIMIT = 300
CLOSE_TIME_LIMIT = 30

# (label, binary, extra environment).  The poll-only pass makes the console
# UART report poll-only so the console's polling path stays covered.  The
# cache passes call file overlays through the overlay file cache.  The checked
# pass runs the simulator built with the message-argument checker; any report
# it prints fails the test.  The bridge pass runs BRIDGE_MODULES' tests with
# each login's shell in a console session that sessiontest bridges to UART1,
# and the term pass runs them with each login's shell in a term in Nook,
# typed into on the FakeVdp's keyboard.
SIM_CONFIGS = [
    ("nano-os-sim_stripped", "nano-os-sim_stripped", {}),
    ("nano-os-sim", "nano-os-sim", {}),
    ("nano-os-sim-poll", "nano-os-sim", {"NANO_OS_SIM_POLL_ONLY": "1"}),
    ("nano-os-sim_stripped-cache", "nano-os-sim_stripped",
     {"NANO_OS_SIM_FILE_CACHE": "1"}),
    ("nano-os-sim-cache", "nano-os-sim", {"NANO_OS_SIM_FILE_CACHE": "1"}),
    ("nano-os-sim-checked", "nano-os-sim-checked", {}),
    ("nano-os-sim-bridge", "nano-os-sim", {harness.BRIDGE_VARIABLE: "1"}),
    ("nano-os-sim-term", "nano-os-sim", {harness.TERM_VARIABLE: "1"}),
]

# The modules whose tests the bridge pass runs:  the ones that drive a console
# the way a terminal window's session will be driven.
BRIDGE_MODULES = ("tests.ed", "tests.help", "tests.kill", "tests.looseLoop",
                  "tests.mush", "tests.passwd", "tests.termtest",
                  "tests.tightLoop", "tests.usage", "tests.vi")

# Tests in BRIDGE_MODULES that can't run over the bridge, and why.
BRIDGE_CANNOT = {
    "test_line_editor_history_survives_logging_out":
        "logs out, which ends a session rather than starting getty",
    "test_logout_silences_background_jobs":
        "logs out, which ends a session rather than starting getty",
    "test_failed_pipelines_leave_no_processes_input_or_memory_behind":
        "lists every process, and the bridge's are among them",
    "test_pipelines_return_their_memory":
        "greps every process, and the bridge's are among them",
}


# Tests in BRIDGE_MODULES that can't run in term, and why.
TERM_CANNOT = dict(BRIDGE_CANNOT, **{
    "test_failed_pipelines_leave_no_processes_input_or_memory_behind":
        "lists every process, and Nook's and term's are among them",
    "test_pipelines_return_their_memory":
        "greps every process, and Nook's and term's are among them",
    "test_a_resize_report_arrives_in_one_read":
        "types a resize report as one burst, which keys can't be",
    "test_vi_redraws_for_a_resize_report":
        "types a resize report as one burst, which keys can't be",
    "test_vi_shows_the_cursor_position_on_the_status_line":
        "types a resize report as one burst, which keys can't be",
    "test_typed_ahead_lines_both_run_in_order":
        "types two lines as one burst, which keys can't be",
    "test_line_editor_keeps_lines_typed_ahead":
        "types two lines as one burst, which keys can't be",
    "test_mush_help_leaves_the_user_logged_in":
        "compares the heap, which sysnook and term use meanwhile",
    "test_failed_background_launches_release_their_slots":
        "compares the heap, which sysnook and term use meanwhile",
})


def build_image():
    os.makedirs(CACHE_DIR, exist_ok=True)
    img = os.path.join(CACHE_DIR, f"disk-{BLOCK_FS}.img")
    stamp = img + ".stamp"

    newest = 0.0
    for base in ("src", "sim", "userspace/usr/src"):
        for root, _d, files in os.walk(os.path.join(REPO_ROOT, base)):
            if "/obj" in root or "/bin" in root:
                continue
            for f in files:
                if f.endswith((".c", ".h", ".cpp", ".ld")) or f == "makefile":
                    newest = max(newest, os.path.getmtime(os.path.join(root, f)))

    binaries = [os.path.join(REPO_ROOT, "sim", "bin", sim_name)
                for _config, sim_name, _env in SIM_CONFIGS]
    if os.path.exists(img) and os.path.exists(stamp) \
            and os.path.getmtime(stamp) >= newest \
            and all(os.path.exists(b) for b in binaries):
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


class TimeLimitExceeded(Exception):
    """Raised in a worker when a test runs past TEST_TIME_LIMIT."""


def time_limit_exceeded(_signum, frame):
    """Abort the running test from wherever it is blocked.

    @param _signum The signal number.
    @param frame The frame that was running.
    """
    stack = traceback.extract_stack(frame)
    ours = [f for f in stack if f.filename.startswith(E2E_DIR)] or stack
    places = [ours[-1]] if ours[-1] is stack[-1] else [ours[-1], stack[-1]]
    raise TimeLimitExceeded(
        "still running after %d s, at %s" % (TEST_TIME_LIMIT, ", then ".join(
            "%s:%d in %s" % (os.path.basename(f.filename), f.lineno, f.name)
            for f in places)))


def close_session(s):
    """Close a test's Session, if it has one, under a time limit of its own.

    @param s The Session, or None.
    """
    signal.alarm(CLOSE_TIME_LIMIT)
    try:
        if s is not None:
            s.close()
    finally:
        signal.alarm(0)


def stop_worker(_signum, _frame):
    """Kill this worker's simulators and exit at once.

    @param _signum The signal number.
    @param _frame The frame that was running.
    """
    harness.kill_open_sessions()
    os._exit(1)


def init_worker(verbose):
    harness.VERBOSE = verbose
    signal.signal(signal.SIGALRM, time_limit_exceeded)
    # A Ctrl-C landing in a worker can strand it holding a pool lock, so only
    # the main process takes it and stops the workers with SIGTERM.
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, stop_worker)


def stop_pool(pool):
    """Cancel an interrupted run's queued tests and stop its workers.

    @param pool The ProcessPoolExecutor running the tests.
    """
    workers = list(pool._processes.values())
    pool.shutdown(wait=False, cancel_futures=True)
    for w in workers:
        w.terminate()
    for w in workers:
        w.join(CLOSE_TIME_LIMIT)


def remove_session_dirs():
    """Remove every Session temporary directory, including any a run leaked."""
    pattern = os.path.join(tempfile.gettempdir(),
                           harness.SESSION_DIR_PREFIX + "*")
    for d in glob.glob(pattern):
        shutil.rmtree(d, ignore_errors=True)


def probe_small_memory_size(sim_bin, env, image):
    """Boot a simulator once and return the memory size that leaves SMALL_MEMORY_HEAP.

    @param sim_bin The simulator binary to boot.
    @param env Extra environment for the simulator.
    @param image The disk image to boot a copy of.

    @return The simulated memory size, in bytes.
    """
    signal.alarm(TEST_TIME_LIMIT)
    s = None
    try:
        s = Session(sim_bin, image, env)
        return small_memory_size(s)
    finally:
        close_session(s)


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
    if (harness.BRIDGE_VARIABLE in (env or {})) and (test_name in BRIDGE_CANNOT):
        return [f"ok {i} - {label} # SKIP {BRIDGE_CANNOT[test_name]}"], False
    if (harness.TERM_VARIABLE in (env or {})) and (test_name in TERM_CANNOT):
        return [f"ok {i} - {label} # SKIP {TERM_CANNOT[test_name]}"], False
    try:
        signal.alarm(TEST_TIME_LIMIT)
        s = Session(sim_bin, image, env)
        getattr(importlib.import_module(test_module), test_name)(s)
        reports = harness.message_args_reports(s)
        assert not reports, f"{len(reports)} checker report(s): {reports[0]}"
        if xfail:
            lines.append(f"ok {i} - {label} # TODO {xfail} -- PASSES NOW, promote it")
        else:
            lines.append(f"ok {i} - {label}")
    except harness.SkipTest as e:
        lines.append(f"ok {i} - {label} # SKIP {e}")
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
        close_session(s)
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
            if ((harness.BRIDGE_VARIABLE in env)
                    or (harness.TERM_VARIABLE in env)) \
                    and (t.__module__ not in BRIDGE_MODULES):
                continue
            label = f"{config_name}/{t.__name__}"
            if flt and flt not in label:
                continue
            cases.append((label, sim_bin, env, t))

    print("TAP version 13")
    print(f"1..{len(cases)}", flush=True)
    failures = 0
    context = multiprocessing.get_context("forkserver")
    pool = concurrent.futures.ProcessPoolExecutor(
        max_workers=jobs, mp_context=context,
        initializer=init_worker, initargs=(harness.VERBOSE,))
    try:
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
    except KeyboardInterrupt:
        stop_pool(pool)
        remove_session_dirs()
        print("# interrupted", flush=True)
        # Dying of SIGINT, not exiting, is what stops a calling script too.
        signal.signal(signal.SIGINT, signal.SIG_DFL)
        os.kill(os.getpid(), signal.SIGINT)
    pool.shutdown()
    remove_session_dirs()

    print(f"# {len(cases)} test(s), {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
