#!/usr/bin/env python3
"""Standalone end-to-end test runner for the NanoOs simulator.

Needs only `pexpect` (no pytest).  Emits TAP on stdout.

    test/e2e/run.py [-v] [name-substring-filter]

Builds (once) a FAT32 disk image via mkimage.sh, then for each simulator
binary spawns it on a fresh image copy and drives the console.
"""

import datetime
import os
import re
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
    # Regression test for a SIGSEGV/reboot on 3+-stage pipelines: the
    # process that is simultaneously a pipe reader (of an already-exited
    # upstream writer) and a pipe writer (to a still-live downstream
    # reader) could exit with a stale message still linked into its own
    # coroutine message queue.  closeProcessFileDescriptors() pushes a
    # stack-local ProcessMessage as a best-effort "you're unblocked" ping
    # to a waiting process and gives up after a bounded timeout; if the
    # receiver never drained it in time, the queue was left holding a
    # dangling pointer into the now-returned stack frame, and crashed
    # whatever later walked that queue - including the queue's own
    # destruction when the receiving process itself exited.  Fixed by
    # unlinking the message (msg_q_remove()/comessageQueueRemove()) before
    # giving up on it.  Confirmed via gdb backtraces on
    # sim/bin/nano-os-sim core dumps and printString-level tracing in the
    # real AgonLight2 emulator before the fix (msg_destroy() crashed on a
    # dangling msg->msg_sync while the AgonLight2 target instead
    # free-ran off the deep end into an eventual watchdog-style reboot).
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
    # Reached a prompt: the pipeline should have actually matched and
    # printed its output, and the shell must still be usable afterwards.
    assert "a" in s.child.before, s.child.before
    assert "alive" in s.sh("echo alive"), "shell unresponsive after the pipeline"


def test_pipe_four_stage_does_not_crash_the_os(s):
    # One stage deeper than the bug above ever needed to reproduce -
    # confirms the fix isn't specific to exactly 3 stages.
    s.login()
    s.child.sendline("echo a | grep a | grep a | grep a")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=12)
    if idx == 1:
        sig = s.child.signalstatus
        raise AssertionError(
            f"simulator died on a 4-command pipeline "
            f"(signal {sig}{' = SIGSEGV' if sig == 11 else ''})")
    if idx == 2:
        raise AssertionError("4-command pipeline hung the shell")
    assert "a" in s.child.before, s.child.before
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


def test_tightloop_is_killed_by_ctrl_c(s):
    # tightLoop is a pure `while (1);` -- it never yields cooperatively and
    # never calls back into the scheduler on its own.  It exists specifically
    # so e2e tests can confirm Ctrl-C still kills a foreground process in
    # that case: the console process detects ^C on the input stream and
    # sends SIGINT directly to the foreground process, and the scheduler's
    # preemptive multitasking (not the process yielding) is what lets that
    # signal actually be delivered and acted on.  See
    # docs/2026-06-06_Signals-Signatures-and-Stack-Overflows.md.
    s.login()
    s.child.sendline("tightLoop")
    # Give it a moment to actually become the foreground process before
    # interrupting it, so Ctrl-C exercises the kill path rather than racing
    # the shell's own dispatch of the command.
    time.sleep(0.5)
    s.child.sendcontrol("c")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=10)
    assert idx == 0, \
        "Ctrl-C did not return the shell to a prompt (tightLoop still " \
        "running or the shell died)"
    # The shell must be usable afterwards, and tightLoop must really be
    # gone -- not merely backgrounded.
    assert "alive" in s.sh("echo alive"), "shell unresponsive after killing tightLoop"
    assert "tightLoop" not in s.sh("ps"), "tightLoop still listed in ps after Ctrl-C"


def test_dirent_lists_directories_with_correct_type(s):
    # test/e2e/mkimage.sh always builds a root directory containing exactly
    # /etc and /usr (via mmd), both real subdirectories.  mtools writes
    # lowercase 8.3 names using the ntReserved case bits rather than an LFN,
    # so the case-preserving name comes back lowercase, not the legacy
    # all-uppercase short-name rendering.
    s.login()
    out = s.sh("dirtest")
    for name in ("usr", "etc"):
        assert f'd_name="{name}" d_type=4' in out, out


def test_dirent_lists_files_with_correct_type(s):
    # /etc always contains exactly hostname and issue, both regular files
    # (via mcopy), written lowercase (see above).
    s.login()
    out = s.sh("dirtest")
    for name in ("hostname", "issue"):
        assert f'd_name="{name}" d_type=8' in out, out


def test_dirent_includes_dot_and_dotdot_in_subdirectories(s):
    s.login()
    out = s.sh("dirtest")
    assert 'd_name="."' in out, out
    assert 'd_name=".."' in out, out


def test_dirent_opendir_rejects_missing_path_and_regular_file(s):
    # opendir must fail both for a path that doesn't exist and for a path
    # that names a regular file rather than a directory.
    s.login()
    out = s.sh("dirtest")
    assert "opendir(/nonexistent) -> NULL" in out, out
    assert "opendir(/etc/hostname) -> NULL" in out, out


def test_dirent_sets_errno_correctly(s):
    # ENOENT (8) for a path that doesn't exist, ENOTDIR (25) for a path that
    # names a regular file rather than a directory (see src/user/NanoOsErrno.h
    # for the numbering) -- distinct values, not the same generic failure.
    s.login()
    out = s.sh("dirtest")
    assert "opendir(/nonexistent) -> NULL, errno=8" in out, out
    assert 'opendir(/etc/hostname) -> NULL, errno=25, strerror="Not a directory"' in out, out
    # readdir must leave errno untouched when it returns NULL only because
    # the directory was exhausted -- that's not an error.
    assert "readdir past end of /etc -> NULL, errno=0" in out, out
    # closedir on a NULL DIR must fail (-1) and set errno (EBADF = 13).
    assert "closedir(NULL) -> -1, errno=13" in out, out


_LS_LINE_RE = re.compile(
    r'^[dl-][rwx-]{9}\s+\S+\s+\S+\s+\d+\s+(?P<wday>\w{3})\s+'
    r'(?P<year>\d+)-(?P<month>\d+)-(?P<day>\d+)\s+\d+:\d+:\d+\s+\S+$')


def test_ls_does_not_crash_on_populated_directory(s):
    # Regression test: ls's day-of-week column (weekdays[tm.tm_wday]) could
    # read an out-of-bounds index and dereference garbage as a string
    # pointer -- see test_ls_shows_correct_weekday_for_mtime for the root
    # cause. On the sim that's a SIGSEGV; on real hardware with no MMU it's
    # a hang instead. Checked separately from output correctness so a crash
    # here is reported distinctly from a wrong-but-harmless value.
    s.login()
    s.child.sendline("ls /usr/bin")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=12)
    if idx == 1:
        sig = s.child.signalstatus
        raise AssertionError(
            f"simulator died running ls on a populated directory "
            f"(signal {sig}{' = SIGSEGV' if sig == 11 else ''})")
    if idx == 2:
        raise AssertionError("ls hung listing a populated directory")
    assert "alive" in s.sh("echo alive"), "shell unresponsive after ls"


def test_ls_shows_correct_weekday_for_mtime(s):
    # Regression test for a bug where NanoOsTime.c's _yearStartDay lookup
    # table (used by nanoOsGmtime_r to compute tm_wday) lacked
    # KEEP_IN_FLASH. On the stripped binary (.rodata removed -- what
    # actually ships to AgonLight2/ItsyBitsy), the table held garbage
    # instead of {4,5,6,1,2,3,4,...}, so tm_wday came out wildly
    # out-of-range (e.g. -4 instead of 4) for perfectly ordinary dates.
    # Nothing had ever read tm_wday before ls started printing a weekday
    # column, so the bug was latent until then. Confirms every listed
    # file's printed weekday actually matches its printed date, rather
    # than just being one of the seven valid-looking abbreviations.
    s.login()
    out = s.sh("ls /etc")
    # Session.sh()'s PROMPT pattern doesn't include the "root" username
    # prefix, so .before always has a trailing "root" line bled in from the
    # start of the *next* prompt -- harmless for the substring checks every
    # other test does, but not a real ls line, so filter to lines that
    # actually look like one rather than asserting every line matches.
    lines = [line for line in out.splitlines() if _LS_LINE_RE.match(line)]
    assert lines, out
    checked = 0
    for line in lines:
        m = _LS_LINE_RE.match(line)
        expected = datetime.date(
            int(m["year"]), int(m["month"]), int(m["day"])).strftime("%a")
        assert m["wday"] == expected, (
            f"ls printed weekday {m['wday']!r} for "
            f"{m['year']}-{m['month']}-{m['day']}, expected {expected!r}: "
            f"{line!r}")
        checked += 1
    # /etc always contains exactly hostname and issue (see mkimage.sh) plus
    # . and .. -- four real entries, not zero.
    assert checked >= 4, f"expected at least 4 entries, got:\n{out}"


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
XFAIL = {}


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
