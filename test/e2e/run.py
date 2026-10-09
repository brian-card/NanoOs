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
# (label, binary, extra environment).  The poll-only pass makes the console
# UART report poll-only so the console's polling path stays covered.
SIM_CONFIGS = [
    ("nano-os-sim_stripped", "nano-os-sim_stripped", {}),
    ("nano-os-sim", "nano-os-sim", {}),
    ("nano-os-sim-poll", "nano-os-sim", {"NANO_OS_SIM_POLL_ONLY": "1"}),
]
PROMPT = r"mush [^\r\n]*@%s:[^\r\n]+[#$] " % HOSTNAME
# Same prompt, capturing the user and the working directory it reports.
PROMPT_PARTS = r"mush ([^\r\n@]+)@%s:([^\r\n]+)[#$] " % HOSTNAME

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


# Simulated memory size, in bytes, that leaves a heap of under 3 KB, as on the
# SAMD21 boards.
SMALL_MEMORY_SIZE = 71500


class Session:
    def __init__(self, sim_bin, image, env=None):
        self.sim_bin = sim_bin
        self.env = env
        self.tmp = tempfile.mkdtemp(prefix="nanoe2e-")
        img_copy = os.path.join(self.tmp, "disk.img")
        shutil.copyfile(image, img_copy)
        self.img_copy = img_copy
        self.spawn([img_copy])

    def spawn(self, args):
        # codec_errors="replace": a misbehaving simulator can emit bytes that
        # aren't valid UTF-8, and a UnicodeDecodeError out of expect() aborts
        # the test with a decoding traceback instead of reporting what the
        # simulator actually did.
        self.child = pexpect.spawn(self.sim_bin, args, encoding="utf-8",
                                   codec_errors="replace", timeout=15,
                                   env=dict(os.environ, **(self.env or {})))
        if VERBOSE:
            self.child.logfile_read = sys.stdout

    def restart_with_memory(self, memory_size):
        if self.child.isalive():
            self.child.terminate(force=True)
        self.spawn([self.img_copy, str(memory_size)])
        self.child.expect(r"Using (\d+) bytes of dynamic memory", timeout=15)
        return int(self.child.match.group(1))

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


def test_getty_shows_every_line_of_the_issue_file(s):
    s.wait_login_prompt()
    banner = s.child.before.replace("\r", "")
    assert re.search(r"\nNanoOs \S+ nanoe2e tty\d+\n\n$", banner), \
        repr(banner[-120:])


def test_getty_prompts_for_login_without_an_issue_file(s):
    s.login()
    assert s.sh("rm /etc/issue") == ""
    s.child.sendline("exit")
    s.child.expect("login: ", timeout=15)
    assert "ERROR" not in s.child.before, repr(s.child.before[-300:])
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("rootroot")
    s.child.expect(PROMPT, timeout=10)


def test_login_rejects_bad_password(s):
    s.wait_login_prompt()
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("wrongpassword")
    idx = s.child.expect([PROMPT, "login: ", pexpect.TIMEOUT], timeout=10)
    assert idx != 0, "bad password produced a shell prompt"


def promptParts(s):
    """Return (user, directory) from the next prompt the shell emits."""
    s.child.sendline("")
    s.child.expect(PROMPT_PARTS, timeout=10)
    return s.child.match.group(1), s.child.match.group(2)


def test_prompt_reports_the_user_and_working_directory(s):
    # A missing or empty PWD in the shell's environment has to fail here rather
    # than being absorbed by the prompt pattern.
    s.login()
    user, pwd = promptParts(s)
    assert user == "root", f"prompt user is {user!r}"
    assert pwd.startswith("/"), f"prompt directory is not absolute: {pwd!r}"
    assert "unknown" not in pwd, f"shell could not read PWD: {pwd!r}"
    assert pwd == s.sh("pwd").splitlines()[-1].strip(), \
        f"prompt directory {pwd!r} disagrees with pwd builtin"


def test_cd_changes_the_working_directory(s):
    # Each step checks the user as well as the directory: setenv reallocates the
    # environment block, so anything the shell cached a getenv pointer to shows
    # up here as a mangled prompt.
    s.login()
    home = promptParts(s)[1]

    for command, expected in (("cd /usr/bin", "/usr/bin"),
                              ("cd ..", "/usr"),
                              ("cd -", "/usr/bin"),
                              ("cd", home)):
        s.child.sendline(command)
        s.child.expect(PROMPT_PARTS, timeout=10)
        user, pwd = s.child.match.group(1), s.child.match.group(2)
        assert pwd == expected, f"{command!r} left the prompt at {pwd!r}"
        assert user == "root", f"{command!r} mangled the prompt user to {user!r}"

    assert "/usr/bin" not in s.sh("pwd"), \
        "bare cd did not return to the home directory"
    assert "alive" in s.sh("echo alive")


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


CATFILE_CONTENTS = "\n".join("line-%02d" % n for n in range(1, 41))


def test_cat_prints_a_file_longer_than_its_buffer(s):
    s.login()
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS


def ll_names(s, path):
    out = s.sh("ll " + path)
    names = [m.group("name") for m in (_LS_LINE_RE.match(line)
             for line in out.splitlines()) if m is not None]
    return sorted(n for n in names if n not in ("./", "../"))


def test_mv_renames_a_file_to_a_long_name(s):
    s.login()
    assert s.sh("mv /etc/catfile /etc/renamed-catfile.txt") == ""
    assert s.sh("cat /etc/renamed-catfile.txt") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == \
        ["hostname", "issue", "renamed-catfile.txt"]


def test_mv_moves_a_file_to_another_directory(s):
    s.login()
    assert s.sh("mv /etc/catfile /usr/catfile") == ""
    assert s.sh("cat /usr/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["hostname", "issue"]
    assert "catfile" in ll_names(s, "/usr")


def test_mv_replaces_an_existing_file(s):
    s.login()
    assert s.sh("mv /etc/catfile /etc/issue") == ""
    assert s.sh("cat /etc/issue") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["hostname", "issue"]


def test_mv_changes_only_the_case_of_a_name(s):
    s.login()
    assert s.sh("mv /etc/catfile /etc/CatFile") == ""
    assert s.sh("cat /etc/CatFile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["CatFile", "hostname", "issue"]


def test_mv_reports_a_missing_source(s):
    s.login()
    assert s.sh("mv /etc/missing /etc/other") == \
        'ERROR: Could not rename "/etc/missing" to "/etc/other": ' \
        'No such entry found'
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


def test_rm_removes_a_file(s):
    s.login()
    assert s.sh("rm /etc/catfile") == ""
    assert ll_names(s, "/etc") == ["hostname", "issue"]


def test_rm_reports_a_missing_file(s):
    s.login()
    assert s.sh("rm /etc/missing") == \
        'ERROR: Could not remove "/etc/missing": No such entry found'


def test_grep_reads_a_file_line_by_line(s):
    s.login()
    assert s.sh("grep line-05 /etc/catfile") == "line-05"
    assert s.sh("grep line-12 /etc/catfile") == "line-12"
    assert s.sh("grep 4 /etc/catfile") == \
        "line-04\nline-14\nline-24\nline-34\nline-40"


def test_small_memory_boots_to_a_working_shell(s):
    heap = s.restart_with_memory(SMALL_MEMORY_SIZE)
    assert heap < 3 * 1024, heap
    s.login()
    assert s.sh("echo small-memory") == "small-memory"
    assert s.sh("cat /etc/hostname") == "nanoe2e"


# fsck.vfat output that is expected on every image:  mformat's label mismatch,
# and the FSInfo free count, which the driver never updates.
_FSCK_KNOWN = re.compile(
    r"^(fsck\.fat \d|Volume label .* different\.|"
    r"\s+Auto-copying volume label|Free cluster summary wrong|"
    r"\s+Auto-correcting\.|Leaving filesystem unchanged\.|"
    r"\S+: \d+ files, \d+/\d+ clusters|$)")


def fsck_problems(s):
    s.child.sendline("shutdown -h")
    s.child.expect(pexpect.EOF, timeout=10)
    partition = os.path.join(s.tmp, "partition.img")
    subprocess.run(["dd", "if=" + s.img_copy, "of=" + partition, "bs=1M",
                    "skip=2", "status=none"], check=True)
    out = subprocess.run(["/sbin/fsck.vfat", "-n", partition],
                         capture_output=True, text=True)
    return [line for line in (out.stdout + out.stderr).splitlines()
            if not _FSCK_KNOWN.match(line)]


def test_mkdir_and_rmdir_manage_an_empty_directory(s):
    s.login()
    assert s.sh("mkdir /etc/newdir") == ""
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue", "newdir/"]
    assert ll_names(s, "/etc/newdir") == []
    assert ll_names(s, "/etc/newdir/..") == \
        ["catfile", "hostname", "issue", "newdir/"]
    assert s.sh("rmdir /etc/newdir") == ""
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
    assert fsck_problems(s) == []


def test_mkdir_nests_directories_and_holds_files(s):
    s.login()
    assert s.sh("mkdir /top") == ""
    assert s.sh("mkdir /top/inner") == ""
    assert ll_names(s, "/top/inner/..") == ["inner/"]
    assert s.sh("mv /etc/catfile /top/inner/catfile") == ""
    assert s.sh("cat /top/inner/catfile") == CATFILE_CONTENTS
    assert s.sh("rmdir /top/inner") == \
        'ERROR: Could not remove directory "/top/inner": Directory not empty'
    assert s.sh("rm /top/inner/catfile") == ""
    assert s.sh("rmdir /top/inner") == ""
    assert s.sh("rmdir /top") == ""
    assert "top/" not in ll_names(s, "/")
    assert fsck_problems(s) == []


def assert_created_now(s, command, listed_name):
    # The sim's clock is the host's, and its local time is UTC-8 with DST on.
    before = datetime.datetime.utcnow() - datetime.timedelta(hours=7)
    assert s.sh(command) == ""
    after = datetime.datetime.utcnow() - datetime.timedelta(hours=7)
    out = s.sh("ll /etc")
    m = re.search(r"(\w{3}) (\d+)-(\d+)-(\d+)\s+(\d+):(\d+):(\d+) "
                  + re.escape(listed_name) + "$", out, re.M)
    assert m, out
    shown = datetime.datetime(*(int(g) for g in m.groups()[1:]))
    assert before - datetime.timedelta(seconds=2) <= shown <= after, \
        (before, shown, after)
    assert m.group(1) == shown.strftime("%a"), out


def test_mkdir_stamps_the_directory_with_the_current_time(s):
    s.login()
    assert_created_now(s, "mkdir /etc/stamped", "stamped/")


def test_touch_creates_a_file_stamped_with_the_current_time(s):
    s.login()
    assert_created_now(s, "touch /etc/touched", "touched")
    assert s.sh("cat /etc/touched") == ""
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue", "touched"]
    assert fsck_problems(s) == []


def test_mkdir_and_rmdir_report_errors(s):
    s.login()
    assert s.sh("mkdir /etc/issue") == \
        'ERROR: Could not create directory "/etc/issue": File exists'
    assert s.sh("mkdir /missing/dir") == \
        'ERROR: Could not create directory "/missing/dir": No such entry found'
    assert s.sh("rmdir /etc/issue") == \
        'ERROR: Could not remove directory "/etc/issue": Not a directory'
    assert s.sh("rmdir /etc/missing") == \
        'ERROR: Could not remove directory "/etc/missing": No such entry found'
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


def ed_session(s, command, script):
    # The console echoes a line only when ed reads it, so a typed-ahead script
    # still comes back with each command followed by its own output.
    s.child.sendline(command)
    for line in script:
        s.child.sendline(line)
    s.child.expect(PROMPT, timeout=30)
    return s.child.before.replace("\r", "")


CATFILE_LINES = ["line-%02d" % n for n in range(1, 41)]


def test_ed_appends_and_writes_a_new_file(s):
    s.login()
    out = ed_session(s, "ed", ["a", "one", "two", "three", ".", ",p",
                               "w /etc/new.txt", "q"])
    assert out.endswith("ed\na\none\ntwo\nthree\n.\n,p\none\ntwo\nthree\n"
                        "w /etc/new.txt\n14\nq\n"), repr(out)
    assert s.sh("cat /etc/new.txt") == "one\ntwo\nthree"
    assert ll_names(s, "/etc") == \
        ["catfile", "hostname", "issue", "new.txt"]
    assert ll_names(s, "/tmp") == []


def test_ed_edits_an_existing_file(s):
    s.login()
    out = ed_session(s, "ed /etc/catfile", [
        "2,4p", "3d", ".=", "1i", "header", ".", "$a", "footer", ".",
        "2c", "changed", ".", "w", "q"])
    lines = CATFILE_LINES[:2] + CATFILE_LINES[3:]
    lines = ["header", "changed"] + lines[1:] + ["footer"]
    content = "\n".join(lines) + "\n"
    assert out.endswith(
        "ed /etc/catfile\n320\n2,4p\nline-02\nline-03\nline-04\n3d\n.=\n3\n"
        "1i\nheader\n.\n$a\nfooter\n.\n2c\nchanged\n.\nw\n%d\nq\n"
        % len(content)), repr(out)
    assert s.sh("cat /etc/catfile") == content.rstrip("\n")
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


def test_ed_resolves_addresses_and_marks(s):
    s.login()
    out = ed_session(s, "ed /etc/catfile", [
        "$=", "5ka", "'a,'a+2n", "-1p", "+2", "", ";+1p", ",=", "Q"])
    assert out.endswith(
        "ed /etc/catfile\n320\n$=\n40\n5ka\n'a,'a+2n\n5\tline-05\n"
        "6\tline-06\n7\tline-07\n-1p\nline-06\n+2\nline-08\n\nline-09\n"
        ";+1p\nline-09\nline-10\n,=\n40\nQ\n"), repr(out)


def ed_model_move(lines, a1, a2, dest):
    block = lines[a1 - 1:a2]
    rest = lines[:a1 - 1] + lines[a2:]
    at = dest if dest < a1 else dest - len(block)
    return rest[:at] + block + rest[at:]


def ed_model_copy(lines, a1, a2, dest):
    return lines[:dest] + lines[a1 - 1:a2] + lines[dest:]


def test_ed_moves_copies_and_joins_lines(s):
    s.login()
    lines = list(CATFILE_LINES)
    lines = ["".join(lines[0:3])] + lines[3:]
    lines = ed_model_move(lines, 2, 3, len(lines))
    lines = ed_model_move(lines, 10, 12, 4)
    lines = ed_model_copy(lines, 1, 1, 0)
    lines = ed_model_copy(lines, 2, 3, 5)
    lines = ed_model_copy(lines, 2, 3, 2)
    out = ed_session(s, "ed /etc/catfile", [
        "1,3j", "2,3m$", "10,12m4", "1t0", "2,3t5", "2,3t2", "1,3m2",
        ",p", "Q"])
    assert out.endswith(
        "1,3j\n2,3m$\n10,12m4\n1t0\n2,3t5\n2,3t2\n1,3m2\n?\n,p\n"
        + "\n".join(lines) + "\nQ\n"), repr(out)


def test_ed_reports_errors_and_guards_unsaved_changes(s):
    s.login()
    out = ed_session(s, "ed", [
        "x", "h", "H", "9p", "a", "text", ".", "q", "e /etc/hostname",
        "e /etc/hostname", ",p", "q"])
    assert out.endswith(
        "ed\nx\n?\nh\nunknown command\nH\nunknown command\n9p\n?\n"
        "invalid address\na\ntext\n.\nq\n?\nwarning: buffer modified\n"
        "e /etc/hostname\n?\nwarning: buffer modified\ne /etc/hostname\n7\n"
        ",p\nnanoe2e\nq\n"), repr(out)
    assert ll_names(s, "/tmp") == []


def test_ed_reads_and_appends_files(s):
    s.login()
    out = ed_session(s, "ed /etc/hostname", [
        "f", "$r /etc/issue", "w /etc/out.txt", "W /etc/out.txt", "q"])
    assert out.endswith(
        "ed /etc/hostname\n7\nf\n/etc/hostname\n$r /etc/issue\n13\n"
        "w /etc/out.txt\n21\nW /etc/out.txt\n21\nq\n"), repr(out)
    once = "nanoe2e\n\\s \\r \\n \\l\n\n"
    assert s.sh("cat /etc/out.txt") == (once + once).rstrip("\n")
    assert s.sh("cat /etc/hostname") == "nanoe2e"


def test_ed_reports_files_it_cannot_open(s):
    s.login()
    out = ed_session(s, "ed /etc/missing", [
        "h", "f", "a", "x", ".", "w /missing/out.txt", "h", "Q"])
    assert out.endswith(
        "ed /etc/missing\n?\nh\ncannot open input file\nf\n/etc/missing\n"
        "a\nx\n.\nw /missing/out.txt\n?\nh\ncannot open output file\nQ\n"), \
        repr(out)
    assert ll_names(s, "/tmp") == []
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


# Simulated memory size, in bytes, that leaves a command about 1 KB of heap,
# the budget for a command on the SAMD21 boards.
COMMAND_BUDGET_MEMORY_SIZE = 72500


def test_ed_loads_nothing_from_a_file_that_does_not_fit(s):
    s.restart_with_memory(COMMAND_BUDGET_MEMORY_SIZE)
    s.login()
    out = ed_session(s, "ed /etc/catfile", ["h", "$=", "f", "w", "Q"])
    assert out.endswith(
        "ed /etc/catfile\n?\nh\nout of memory\n$=\n0\nf\n?\nw\n?\nQ\n"), \
        repr(out)
    assert s.sh("cat /etc/catfile") == "\n".join(CATFILE_LINES)
    assert ll_names(s, "/tmp") == []


def test_ed_discards_typed_text_that_does_not_fit(s):
    # Each typed line is "d", which would delete a line if it were run as a
    # command after the buffer filled up.
    s.restart_with_memory(COMMAND_BUDGET_MEMORY_SIZE)
    s.login()
    out = ed_session(s, "ed", ["a"] + ["d"] * 200 + [".", "h", "$=", "Q"])
    m = re.search(r"\n\.\n\?\nh\nout of memory\n\$=\n(\d+)\nQ\n$", out)
    assert m, repr(out[-300:])
    assert 0 < int(m.group(1)) < 200, m.group(1)
    assert out.count("?") == 1, repr(out)
    assert s.sh("echo still-alive") == "still-alive"
    assert ll_names(s, "/tmp") == []


def test_ed_quits_when_too_little_memory_is_left_to_edit(s):
    s.restart_with_memory(SMALL_MEMORY_SIZE)
    s.login()
    ed_session(s, "ed /etc/catfile", ["Q"])
    assert s.sh("echo still-alive") == "still-alive"
    assert ll_names(s, "/tmp") == []


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
    # /etc always contains hostname and issue, both regular files (via
    # mcopy), written lowercase (see above).
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
    r'(?P<year>\d+)-(?P<month>\d+)-(?P<day>\d+)\s+\d+:\d+:\d+\s+(?P<name>\S+)$')


def test_ls_does_not_crash_on_populated_directory(s):
    # Regression test: ll's day-of-week column (weekdays[tm.tm_wday]) could
    # read an out-of-bounds index and dereference garbage as a string
    # pointer -- see test_ls_shows_correct_weekday_for_mtime for the root
    # cause. On the sim that's a SIGSEGV; on real hardware with no MMU it's
    # a hang instead. Checked separately from weekday correctness so a crash
    # here is reported distinctly from a wrong-but-harmless value.
    s.login()
    s.child.sendline("ll /usr/bin")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=12)
    if idx == 1:
        sig = s.child.signalstatus
        raise AssertionError(
            f"simulator died running ll on a populated directory "
            f"(signal {sig}{' = SIGSEGV' if sig == 11 else ''})")
    if idx == 2:
        raise AssertionError("ll hung listing a populated directory")
    out = s.child.before.replace("\r", "")
    matches = [m for m in (_LS_LINE_RE.match(line)
                           for line in out.splitlines()) if m is not None]
    assert matches, f"ll printed no listing lines for /usr/bin:\n{out}"
    # Every command lives in /usr/bin/<name>/main.overlay, so ll's own
    # directory has to appear -- a renamed or missing command shows up here
    # as a shell "not found" rather than as a silent pass.
    names = {m["name"].rstrip("/") for m in matches}
    assert "ll" in names, \
        f"ll did not list its own /usr/bin entry, got {sorted(names)}:\n{out}"
    assert "alive" in s.sh("echo alive"), "shell unresponsive after ll"


def test_ls_shows_correct_weekday_for_mtime(s):
    # Regression test for a bug where NanoOsTime.c's _yearStartDay lookup
    # table (used by nanoOsGmtime_r to compute tm_wday) lacked
    # KEEP_IN_FLASH. On the stripped binary (.rodata removed -- what
    # actually ships to AgonLight2/ItsyBitsy), the table held garbage
    # instead of {4,5,6,1,2,3,4,...}, so tm_wday came out wildly
    # out-of-range (e.g. -4 instead of 4) for perfectly ordinary dates.
    # Nothing had ever read tm_wday before ll started printing a weekday
    # column, so the bug was latent until then. Confirms every listed
    # file's printed weekday actually matches its printed date, rather
    # than just being one of the seven valid-looking abbreviations.
    s.login()
    out = s.sh("ll /etc")
    # Session.sh()'s PROMPT pattern doesn't include the "root" username
    # prefix, so .before always has a trailing "root" line bled in from the
    # start of the *next* prompt -- harmless for the substring checks every
    # other test does, but not a real ll line, so filter to lines that
    # actually look like one rather than asserting every line matches.
    lines = [line for line in out.splitlines() if _LS_LINE_RE.match(line)]
    assert lines, out
    checked = 0
    for line in lines:
        m = _LS_LINE_RE.match(line)
        expected = datetime.date(
            int(m["year"]), int(m["month"]), int(m["day"])).strftime("%a")
        assert m["wday"] == expected, (
            f"ll printed weekday {m['wday']!r} for "
            f"{m['year']}-{m['month']}-{m['day']}, expected {expected!r}: "
            f"{line!r}")
        checked += 1
    # /etc always contains exactly hostname and issue (see mkimage.sh) plus
    # . and .. -- four real entries, not zero.
    assert checked >= 4, f"expected at least 4 entries, got:\n{out}"


def test_typed_ahead_lines_both_run_in_order(s):
    # Both lines arrive in one read.  The second must neither be edited into
    # the console buffer while the shell is still reading the first out of it
    # nor be dropped because the shell wasn't waiting for it yet.  Whether both
    # lines land in one read depends on timing, so try a few.
    s.login()
    for first, second in (("abc", "def"), ("ghi", "jkl"), ("mno", "pqr")):
        s.child.send(f"echo {first}\recho {second}\r")
        s.child.expect(r"\n%s\r*\n" % first, timeout=10)
        # The typed-ahead line was echoed before the first one ran, so its
        # output follows the shell's next prompt on the same line.
        s.child.expect(r"%s\r*\n" % second, timeout=10)
        s.child.expect(PROMPT, timeout=10)
    assert s.sh("echo ok") == "ok"


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
    for config_name, sim_name, env in SIM_CONFIGS:
        sim_bin = os.path.join(REPO_ROOT, "sim", "bin", sim_name)
        for t in TESTS:
            label = f"{config_name}/{t.__name__}"
            if flt and flt not in label:
                continue
            cases.append((label, sim_bin, env, t))

    print("TAP version 13")
    print(f"1..{len(cases)}")
    failures = 0
    for i, (label, sim_bin, env, t) in enumerate(cases, 1):
        if not os.path.exists(sim_bin):
            print(f"ok {i} - {label} # SKIP {os.path.basename(sim_bin)} not built")
            continue
        xfail = XFAIL.get(t.__name__)
        s = None
        try:
            s = Session(sim_bin, image, env)
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
