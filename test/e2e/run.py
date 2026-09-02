#!/usr/bin/env python3
"""Standalone end-to-end test runner for the NanoOs simulator.

Needs only `pexpect` (no pytest).  Emits TAP on stdout.

    test/e2e/run.py [-v] [name-substring-filter]

Builds (once) a FAT32 disk image via mkimage.sh, then for each simulator
binary spawns it on a fresh image copy and drives the console.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time

import pexpect

E2E_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(E2E_DIR))
CACHE_DIR = os.path.join(E2E_DIR, ".cache")
HOSTNAME = "nanoe2e"
BLOCK_FS = "contiguous"
SIM_BINARIES = ["nano-os-sim_stripped", "nano-os-sim"]
PROMPT = r"@%s mush[#$] " % HOSTNAME

VERBOSE = False


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
                   stdout=(None if VERBOSE else subprocess.DEVNULL))
    open(stamp, "w").close()
    return img


class Session:
    def __init__(self, sim_bin, image):
        self.tmp = tempfile.mkdtemp(prefix="nanoe2e-")
        img_copy = os.path.join(self.tmp, "disk.img")
        shutil.copyfile(image, img_copy)
        self.child = pexpect.spawn(sim_bin, [img_copy], encoding="utf-8", timeout=15)
        if VERBOSE:
            self.child.logfile_read = sys.stdout

    def wait_login_prompt(self):
        self.child.expect("login: ", timeout=15)

    def login(self, user="root", password="rootroot"):
        self.wait_login_prompt()
        self.child.sendline(user)
        self.child.expect("password: ", timeout=10)
        self.child.sendline(password)
        self.child.expect(PROMPT, timeout=10)

    def sh(self, command, timeout=10):
        self.child.sendline(command)
        self.child.expect_exact(command, timeout=timeout)
        self.child.expect(PROMPT, timeout=timeout)
        return self.child.before.replace("\r", "").strip("\n")

    def close(self):
        c = self.child
        try:
            if c.isalive():
                c.sendline("shutdown -h")
                c.expect(pexpect.EOF, timeout=8)
        except Exception:
            pass
        if c.isalive():
            c.terminate(force=True)
        shutil.rmtree(self.tmp, ignore_errors=True)


# --- tests: each takes a Session, asserts, raises on failure ------------

def test_login_root(s):
    s.login("root", "rootroot")


def test_login_rejects_bad_password(s):
    s.wait_login_prompt()
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("wrongpassword")
    idx = s.child.expect([PROMPT, "login: ", pexpect.TIMEOUT], timeout=10)
    assert idx != 0, "bad password produced a shell prompt"


def test_ps_lists_kernel_processes(s):
    s.login()
    out = s.sh("ps")
    assert "console" in out, out
    assert "memory manager" in out, out
    assert "ps" in out, out


def test_echo_roundtrips_argument(s):
    s.login()
    assert "hello-nanoos" in s.sh("echo hello-nanoos")


def test_hostname_file(s):
    s.login()
    assert "nanoe2e" in s.sh("cat /etc/hostname")


def test_pipe_between_commands(s):
    s.login()
    assert "needle-in-haystack" in s.sh("echo needle-in-haystack | grep needle")
    neg = s.sh("echo one-two-three | grep zzz")
    assert "one-two-three" not in neg, neg


def test_pipe_two_stage_is_the_working_max(s):
    # A single pipe (2 commands) is the deepest pipeline NanoOs currently
    # handles.  Confirm it works and the shell stays alive afterwards.
    s.login()
    assert "keep" in s.sh("echo keep-this-line | grep keep")
    assert "still-here" in s.sh("echo still-here")


def test_pipe_three_stage_does_not_crash_the_os(s):
    # Boundary: one command past the working max.  Expected behaviour is a
    # graceful error and a live shell (compare the background-job path,
    # which reports "Out of process slots" cleanly).  See BUGS.txt BUG-4:
    # the SIGSEGV is fixed, but this still hangs the shell (the -EBUSY spawn
    # is re-queued forever at Scheduler.c:3565).
    s.login()
    s.child.sendline("echo a | grep a | grep a")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=12)
    if idx == 1:
        sig = s.child.signalstatus
        raise AssertionError(
            f"simulator died on a 3-command pipeline "
            f"(signal {sig}{' = SIGSEGV' if sig == 11 else ''})")
    if idx == 2:
        raise AssertionError("3-command pipeline hung the shell")
    # Reached a prompt: make sure the shell is actually still usable.
    assert "alive" in s.sh("echo alive"), "shell unresponsive after the pipeline"


def test_background_jobs_fail_gracefully_past_the_slot_limit(s):
    # Contrast with the pipe path: launching more background jobs than there
    # are free process slots must report an error and leave the shell alive.
    s.login()
    saw_error = False
    for _ in range(6):
        s.child.sendline("looseLoop &")
        idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=8)
        assert idx == 0, f"background spawn killed/hung the shell (expect idx {idx})"
        if "out of process slots" in s.child.before.lower():
            saw_error = True
    assert saw_error, "never hit the process-slot limit"
    # Shell still works, and ps still works.
    assert "looseLoop" in s.sh("ps")


def test_unknown_command_errors(s):
    s.login()
    out = s.sh("no_such_command_here").lower()
    assert any(k in out for k in ("not found", "no such", "cannot", "error")), out


def test_shutdown_halts(s):
    s.login()
    s.child.sendline("shutdown -h")
    assert s.child.expect([pexpect.EOF, pexpect.TIMEOUT], timeout=10) == 0, \
        "shutdown -h did not terminate the simulator"


TESTS = [v for k, v in sorted(globals().items()) if k.startswith("test_")]

# Tests known to fail because of an open bug (see test/BUGS.txt).  A failure
# is reported as TAP "# TODO <reason>" and does not fail the run; if one
# starts passing the runner says so.
XFAIL = {
    "test_pipe_three_stage_does_not_crash_the_os":
        "BUG-4: 3+ command pipelines hang the shell (SIGSEGV fixed; the "
        "-EBUSY spawn is re-queued forever)",
}


def main(argv):
    global VERBOSE
    args = [a for a in argv[1:]]
    if "-v" in args:
        VERBOSE = True
        args.remove("-v")
    flt = args[0] if args else None

    image = build_image()

    cases = []
    for sim_name in SIM_BINARIES:
        sim_bin = os.path.join(REPO_ROOT, "sim", "bin", sim_name)
        for t in TESTS:
            label = f"{sim_name}/{t.__name__}"
            if flt and flt not in label:
                continue
            cases.append((label, sim_bin, t))

    print("TAP version 13")
    print(f"1..{len(cases)}")
    failures = 0
    for i, (label, sim_bin, t) in enumerate(cases, 1):
        if not os.path.exists(sim_bin):
            print(f"ok {i} - {label} # SKIP {os.path.basename(sim_bin)} not built")
            continue
        xfail = XFAIL.get(t.__name__)
        s = None
        try:
            s = Session(sim_bin, image)
            t(s)
            if xfail:
                print(f"ok {i} - {label} # TODO {xfail} -- PASSES NOW, promote it")
            else:
                print(f"ok {i} - {label}")
        except Exception as e:  # noqa: BLE001
            msg = str(e).splitlines()[0] if str(e) else type(e).__name__
            if xfail:
                print(f"not ok {i} - {label} # TODO {xfail}")
            else:
                failures += 1
                print(f"not ok {i} - {label}")
            print(f"#   {type(e).__name__}: {msg}")
            if s is not None and s.child.before:
                tail = s.child.before.replace("\r", "")[-300:]
                for line in tail.splitlines():
                    print(f"#   | {line}")
        finally:
            if s is not None:
                s.close()
            time.sleep(0.05)

    print(f"# {len(cases)} test(s), {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
