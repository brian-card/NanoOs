#!/usr/bin/env python3
"""Standalone end-to-end test runner for the NanoOs simulator.

Needs only `pexpect` (no pytest).  Emits TAP on stdout.

    test/e2e/run.py [-v] [-j JOBS] [name-substring-filter]

Builds (once) a FAT32 disk image via mkimage.sh, then for each simulator
binary spawns it on a fresh image copy and drives the console.  Tests run in
JOBS worker processes, one per available processor by default (one with -v).
"""

import concurrent.futures
import datetime
import multiprocessing
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import pexpect

from fakevdp import (FakeVdp, KEYCODE_DELETE, KEYCODE_DOWN, KEYCODE_END,
                     KEYCODE_HOME, KEYCODE_LEFT, KEYCODE_RIGHT, KEYCODE_UP)

E2E_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(E2E_DIR))
CACHE_DIR = os.path.join(E2E_DIR, ".cache")
HOSTNAME = "nanoe2e"
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
PROMPT = r"mush [^\r\n]*@%s:[^\r\n]+[#$] " % HOSTNAME
# Same prompt, capturing the user and the working directory it reports.
PROMPT_PARTS = r"mush ([^\r\n@]+)@%s:([^\r\n]+)[#$] " % HOSTNAME
# A getty on UART1 and another on a VDP console served by vdp.drv, which owns
# UART0.
VDP_CONFIG_SYS = (
    "DEVICE=/usr/lib/vdp.drv\n"
    "HARDWARE=/usr/lib/vdp.drv UART0\n"
    "CONSOLE=UART1 /usr/bin/getty\n"
    "CONSOLE=/usr/lib/vdp.drv /usr/bin/getty\n")

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


# Heap, in bytes, left at the shell prompt in the small-memory tests:  a little
# over 2.5 KB, as on the SAMD21 boards.
SMALL_MEMORY_HEAP_RANGE = (2560, 2816)
SMALL_MEMORY_HEAP = 2688
# Simulated memory size, in bytes, booted once to find the size that leaves
# SMALL_MEMORY_HEAP.  The heap left at a given size depends on the host
# compiler's stack frame sizes, so it has to be measured on each host.
SMALL_MEMORY_PROBE_SIZE = 75000
_small_memory_sizes = {}


class Session:
    def __init__(self, sim_bin, image, env=None):
        self.sim_bin = sim_bin
        self.env = env
        self.tmp = tempfile.mkdtemp(prefix="nanoe2e-")
        img_copy = os.path.join(self.tmp, "disk.img")
        subprocess.run(["cp", "--sparse=always", image, img_copy], check=True)
        self.img_copy = img_copy
        self.vdp = None
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

    def restart_with_config_sys(self, text):
        """Write /etc/config.sys into this session's image and boot again."""
        if self.child.isalive():
            self.child.terminate(force=True)
        subprocess.run(["mcopy", "-o", "-i", self.img_copy + "@@2097152", "-",
                        "::/etc/config.sys"], input=text.encode(),
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)
        self.spawn([self.img_copy])

    def restart_with_vdp(self, **kwargs):
        """Boot again with vdp.drv serving a console on a FakeVdp.

        @param kwargs Arguments for FakeVdp.

        @return The FakeVdp.
        """
        self.vdp = FakeVdp(os.path.join(self.tmp, "vdp.sock"), **kwargs)
        self.env = dict(self.env or {}, NANO_OS_SIM_UART0=self.vdp.path)
        self.restart_with_config_sys(VDP_CONFIG_SYS)
        return self.vdp

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
        if self.vdp is not None:
            self.vdp.close()
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


def test_config_sys_console_runs_its_program_instead_of_getty(s):
    s.restart_with_config_sys("CONSOLE=UART1 /usr/bin/echo from-config-sys\n")
    i = s.child.expect(["from-config-sys", "login: "], timeout=15)
    assert i == 0, "getty ran instead of the CONSOLE line's program"


def test_config_sys_console_port_name_ignores_case(s):
    s.restart_with_config_sys("console=uart1 /usr/bin/echo lower-case-port\n")
    i = s.child.expect(["lower-case-port", "login: "], timeout=15)
    assert i == 0, "getty ran instead of the CONSOLE line's program"


def test_config_sys_console_on_unknown_port_falls_back_to_getty(s):
    s.restart_with_config_sys("CONSOLE=UART7 /usr/bin/echo wrong-port\n")
    i = s.child.expect(["wrong-port", "login: "], timeout=15)
    assert i == 1, "the program for a port that doesn't exist ran"
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("rootroot")
    s.child.expect(PROMPT, timeout=10)


def test_config_sys_console_on_a_device_forwards_its_output(s):
    s.restart_with_config_sys(
        "DEVICE=/usr/lib/loopback.drv 1\n"
        "CONSOLE=/usr/lib/loopback.drv /usr/bin/echo via-forwarding-port\n")
    i = s.child.expect(["via-forwarding-port", "login: "], timeout=15)
    assert i == 0, "getty ran instead of the forwarded console's program"


def test_config_sys_uart_and_device_consoles_each_get_a_getty(s):
    s.restart_with_config_sys(
        "DEVICE=/usr/lib/loopback.drv 1\n"
        "CONSOLE=UART1 /usr/bin/getty\n"
        "CONSOLE=/usr/lib/loopback.drv /usr/bin/getty\n")
    seen = set()
    while seen != {"tty0", "tty1"}:
        s.child.expect(r"(tty\d)", timeout=15)
        seen.add(s.child.match.group(1))
    s.login()


def test_config_sys_hardware_grant_keeps_a_drivers_uart(s):
    s.restart_with_config_sys(
        "DEVICE=/usr/lib/loopback.drv 1\n"
        "HARDWARE=/usr/lib/loopback.drv UART1\n"
        "CONSOLE=/usr/lib/loopback.drv /usr/bin/echo granted-uart1\n")
    i = s.child.expect(["granted-uart1", "login: "], timeout=15)
    assert i == 0, "the driver's output didn't reach UART1"


def test_config_sys_hardware_grant_limits_a_driver_to_its_uart(s):
    s.restart_with_config_sys(
        "DEVICE=/usr/lib/loopback.drv 1\n"
        "HARDWARE=/usr/lib/loopback.drv UART0\n"
        "CONSOLE=UART1 /usr/bin/getty\n"
        "CONSOLE=/usr/lib/loopback.drv /usr/bin/echo granted-uart0\n")
    i = s.child.expect(["granted-uart0", "login: "], timeout=15)
    assert i == 1, "a driver granted only UART0 wrote to UART1"
    i = s.child.expect(["granted-uart0", pexpect.TIMEOUT], timeout=3)
    assert i == 1, "a driver granted only UART0 wrote to UART1"


def assert_vdp_set_up_on_answer(vdp):
    """Check that the driver set up a FakeVdp's screen once it answered.

    @param vdp The FakeVdp, after it has drawn the login prompt.
    """
    setup = [(kind, detail) for _t, kind, detail in vdp.events[:6]]
    assert setup == [("mode", 0), ("layout", 1), ("cls", None),
                     ("cursor", 1), ("pointer", VDP_POINTER_ID),
                     ("text", setup[5][1])], \
        f"the screen wasn't set up first: {vdp.events[:6]}"
    delay = vdp.events[0][0] - vdp.answered
    assert delay < 1.0, f"the screen was set up {delay:.1f} s after the answer"


VDP_POINTER_ID = 0xF000
VDP_POINTER = [
    "B...........",
    "BB..........",
    "BWB.........",
    "BWWB........",
    "BWWWB.......",
    "BWWWWB......",
    "BWWWWWB.....",
    "BWWWWWWB....",
    "BWWWWWWWB...",
    "BWWWWWWWWB..",
    "BWWWWWBBBB..",
    "BWWBWWB.....",
    "BWB.BWWB....",
    "BB..BWWB....",
    "B....BWWB...",
    ".....BWWB...",
    "......BWB...",
    ".......B....",
]


def assert_vdp_pointer(vdp):
    """Check that a FakeVdp shows the driver's mouse pointer.

    @param vdp The FakeVdp, after the driver has set up its screen.
    """
    assert vdp.mouse_enabled, "the mouse wasn't enabled"
    assert vdp.mouse_cursor == VDP_POINTER_ID, \
        f"the pointer is cursor {vdp.mouse_cursor}"
    image = vdp.pointer_image()
    assert image == VDP_POINTER, \
        "the pointer isn't the driver's cursor:\n" + "\n".join(image or [])
    _bitmap, hot_x, hot_y = vdp.cursors[VDP_POINTER_ID]
    assert (hot_x, hot_y) == (0, 0), f"the hotspot is at {hot_x},{hot_y}"


def vdp_login(vdp):
    """Log in as root on a FakeVdp's console.

    @param vdp The FakeVdp.
    """
    vdp.expect("login: ")
    vdp.type("root\r")
    vdp.expect("password: ")
    vdp.type("rootroot\r")
    vdp.expect(PROMPT)


def test_vdp_console_logs_in_and_runs_a_command(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    assert_vdp_set_up_on_answer(vdp)
    assert_vdp_pointer(vdp)
    vdp.clear()
    vdp.type("echo vdp-oops")
    vdp.type("\b" * 4)
    vdp.type("ok\r")
    vdp.expect(r"\r\nvdp-ok\r\n" + PROMPT)
    assert "oops" not in vdp.screen, \
        f"Backspace didn't erase: {vdp.screen!r}"
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"
    s.login()
    assert s.sh("echo uart1-too") == "uart1-too"


def test_vdp_keyboard_types_a_pipeline(s):
    # The VDP starts in the UK layout, where a US keyboard's | key types ~.
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.clear()
    vdp.type("echo pipe-ok | grep pipe\r")
    vdp.expect(r"echo pipe-ok \| grep pipe\r\npipe-ok\r\n" + PROMPT)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vdp_driver_replays_output_from_before_the_vdp_answered(s):
    vdp = s.restart_with_vdp(answer_after=2.0)
    vdp.expect(r"NanoOs [^\r\n]*\r\n(.*\r\n)*login: ", timeout=20)
    assert vdp.dropped == 0, \
        f"{vdp.dropped} bytes were sent before the VDP answered"
    early = [t for t in vdp.polls if t < vdp.answered]
    assert len(early) >= 3, f"only {len(early)} polls before the answer"
    gaps = [b - a for a, b in zip(early, early[1:] + [vdp.answered])]
    assert all(0.3 < gap < 0.9 for gap in gaps), \
        f"polls weren't resent every 500 ms: {gaps}"
    assert_vdp_set_up_on_answer(vdp)
    vdp_login(vdp)


def test_vdp_driver_writes_anyway_when_the_vdp_never_answers(s):
    vdp = s.restart_with_vdp(answer=False)
    vdp.expect("login: ", timeout=25)
    first_time, first_kind, _ = vdp.events[0]
    assert first_kind == "mode", \
        f"output was sent before the driver gave up: {vdp.events[:3]}"
    waited = first_time - vdp.first_poll
    assert 14.5 < waited < 16.5, f"gave up after {waited:.1f} s, not 15 s"
    assert vdp.polls[-1] <= first_time, "polls continued after giving up"


def test_vdp_driver_is_restarted_after_losing_the_vdp(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.disconnect()
    vdp.wait_connections(2)
    deadline = time.time() + 10
    while sum(kind == "cls" for _t, kind, _d in vdp.events) < 2:
        assert time.time() < deadline, "the restarted driver never set up the screen"
        time.sleep(0.1)
    vdp.type("echo after-restart\r")
    vdp.expect(r"\r\nafter-restart\r\n" + PROMPT)
    pointers = sum(kind == "pointer" for _t, kind, _d in vdp.events)
    assert pointers == 2, f"the pointer was set {pointers} times, not 2"
    assert_vdp_pointer(vdp)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"
    s.login()
    assert s.sh("echo uart1-too") == "uart1-too"


def test_vdp_draws_background_output_and_the_uart_console_still_works(s):
    # Processes on the VDP console write straight to vdp.drv, and the
    # background job inherits that from the shell.
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.clear()
    vdp.type("helloworld &\r")
    vdp.expect(r"\r\nHello, world!\r\n")
    vdp.type("echo vdp-ok\r")
    vdp.expect(r"\r\nvdp-ok\r\n" + PROMPT)
    s.login()
    for n in range(3):
        assert s.sh("echo uart1-%d" % n) == "uart1-%d" % n
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS
    vdp.clear()
    vdp.type("cat /etc/catfile\r")
    vdp.expect(r"\r\nline-40\r\n" + PROMPT)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vdp_driver_restarts_after_losing_the_vdp_mid_output(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.type("cat /etc/catfile\r")
    vdp.disconnect()
    vdp.wait_connections(2)
    deadline = time.time() + 10
    while sum(kind == "cls" for _t, kind, _d in vdp.events) < 2:
        assert time.time() < deadline, \
            "the restarted driver never set up the screen"
        time.sleep(0.1)
    for n in range(6):
        vdp.type("echo round-%d\r" % n)
        vdp.expect(r"\r\nround-%d\r\n" % n + PROMPT)
    s.login()
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS


UP, DOWN, RIGHT, LEFT = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"
HOME, END, DELETE = "\x1b[H", "\x1b[F", "\x1b[3~"


def edit(s, keys, output):
    """Type keys at the shell prompt and wait for the command's output.

    @param s The Session, at a prompt.
    @param keys The keys to type, ending with Enter.
    @param output The output the edited command line must print.
    """
    s.child.send(keys)
    s.child.expect(r"\r\n" + re.escape(output) + r"\r*\n" + PROMPT, timeout=10)


def test_line_editor_left_and_right_move_the_cursor(s):
    s.login()
    edit(s, "echo ac" + LEFT + "b\r", "abc")
    edit(s, "echo ad" + LEFT + LEFT + RIGHT + "bc\r", "abcd")
    edit(s, "echo a" + RIGHT + RIGHT + "b\r", "ab")


def test_line_editor_home_and_end_jump_to_the_ends(s):
    s.login()
    edit(s, "cho a" + HOME + "e\r", "a")
    edit(s, "echo b" + HOME + END + "c\r", "bc")
    for home, end in (("\x1bOH", "\x1bOF"), ("\x1b[1~", "\x1b[4~"),
                      ("\x1b[7~", "\x1b[8~")):
        edit(s, "cho d" + home + "e" + end + "e\r", "de")


def test_line_editor_delete_and_backspace_remove_mid_line(s):
    s.login()
    edit(s, "echo abxc" + LEFT + LEFT + DELETE + "\r", "abc")
    edit(s, "echo abxc" + LEFT + "\x7f\r", "abc")
    edit(s, "echo abxc" + LEFT + "\b\r", "abc")
    edit(s, "echo ab" + DELETE + "c\r", "abc")
    edit(s, "xecho abc" + HOME + "\x7f" + DELETE + "\r", "abc")


def test_line_editor_up_and_down_walk_the_history(s):
    s.login()
    edit(s, UP + DOWN + "echo first\r", "first")
    edit(s, UP + "\r", "first")
    edit(s, "echo second\r", "second")
    edit(s, "pwd\r", "/root")
    edit(s, UP + UP + UP + "\r", "first")
    edit(s, UP + UP + UP + DOWN + "\r", "/root")
    edit(s, UP + DOWN + "\r" + "echo after-empty\r", "after-empty")
    edit(s, "echo typed" + UP + "\x7f" * 6 + "-edited\r", "after-edited")
    edit(s, UP * 8 + "\r", "first")
    assert s.sh("cat .mush_history") == "\n".join([
        "echo first", "echo second", "pwd", "echo first", "pwd",
        "echo after-empty", "echo after-edited", "echo first",
        "cat .mush_history"])


def test_line_editor_history_survives_logging_out(s):
    s.login()
    edit(s, "echo remembered\r", "remembered")
    s.child.sendline("exit")
    s.login()
    edit(s, UP + UP + "\r", "remembered")


def test_line_editor_ctrl_c_abandons_the_line(s):
    s.login()
    edit(s, "echo kept\r", "kept")
    s.child.send("echo dropped" + UP + "\x03")
    s.child.expect_exact("^C", timeout=10)
    s.child.expect(PROMPT, timeout=10)
    assert s.child.before.replace("\r", "") == "\n", repr(s.child.before)
    edit(s, UP + "\r", "kept")


def test_line_editor_takes_an_arrow_key_arriving_in_pieces(s):
    s.login()
    s.child.delaybeforesend = None
    s.child.send("echo ac\x1b")
    time.sleep(0.05)
    s.child.send("[")
    time.sleep(0.05)
    s.child.send("D")
    edit(s, "b\r", "abc")


def test_line_editor_drops_a_lone_escape(s):
    s.login()
    s.child.send("\x1b")
    time.sleep(0.1)
    edit(s, "echo after-escape\r", "after-escape")


def test_line_editor_keeps_lines_typed_ahead(s):
    s.login()
    s.child.delaybeforesend = None
    s.child.send("echo one\recho two\r")
    s.child.expect(r"\none\r*\n" + PROMPT, timeout=10)
    s.child.expect(r"\ntwo\r*\n" + PROMPT, timeout=10)
    assert s.sh("echo after") == "after"


def test_line_editor_leaves_other_programs_input_cooked(s):
    s.login()
    s.child.sendline("cat")
    s.child.expect(r"cat\r*\n", timeout=10)
    s.child.send("abx\x7fc\r")
    s.child.expect(r"\x08c\r*\nabc\r*\n", timeout=10)
    s.child.send("\x03")
    s.child.expect(PROMPT, timeout=10)
    edit(s, "echo ab" + LEFT + "x\r", "axb")


def test_vdp_line_editor_redraws_the_line(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.type("a-much-longer-line\r")
    vdp.expect(r"\r\na-much-longer-line: command not found\r\n" + PROMPT)
    vdp.clear()
    vdp.type("echo ac")
    vdp.press(KEYCODE_LEFT)
    vdp.type("b")
    vdp.press(KEYCODE_HOME)
    vdp.press(KEYCODE_DELETE)
    vdp.type("X")
    vdp.press(KEYCODE_END)
    vdp.type("d")
    vdp.expect(r"^Xcho abcd$")
    vdp.press(KEYCODE_UP)
    vdp.expect(r"^a-much-longer-line$")
    vdp.press(KEYCODE_DOWN)
    vdp.expect(r"^ {18}$")
    vdp.type("Xcho abcd")
    vdp.press(KEYCODE_LEFT)
    vdp.press(KEYCODE_LEFT)
    vdp.type("\b")
    vdp.press(KEYCODE_RIGHT)
    vdp.type("\r")
    vdp.expect(r"^Xcho acd {10}\r\nXcho: command not found\r\n" + PROMPT)
    vdp.clear()
    vdp.press(KEYCODE_UP)
    vdp.press(KEYCODE_HOME)
    vdp.press(KEYCODE_DELETE)
    vdp.type("e\r")
    vdp.expect(r"^echo acd\r\nacd\r\n" + PROMPT)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


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


def test_long_names_match_on_every_fragment(s):
    # The two 27-character names share their last two 13-character LFN
    # fragments, and the 13-character name fills one fragment with no
    # terminator.
    s.login()
    missing = 'ERROR: Could not open file "%s": No such entry found'
    assert s.sh("mv /etc/catfile /etc/aaaaaaaaaaaaa-same-tail.txt") == ""
    assert s.sh("touch /etc/abcdefghijklm") == ""
    assert s.sh("cat /etc/bbbbbbbbbbbbb-same-tail.txt") == \
        missing % "/etc/bbbbbbbbbbbbb-same-tail.txt"
    assert s.sh("cat /etc/AAAAAAAAAAAAA-Same-Tail.TXT") == CATFILE_CONTENTS
    assert s.sh("cat /etc/abcdefghijklmn") == missing % "/etc/abcdefghijklmn"
    assert s.sh("cat /etc/abcdefghijkl") == missing % "/etc/abcdefghijkl"
    assert s.sh("cat /etc/abcdefghijklm") == ""
    assert ll_names(s, "/etc") == \
        ["aaaaaaaaaaaaa-same-tail.txt", "abcdefghijklm", "hostname", "issue"]


def test_ll_lists_names_of_every_length(s):
    # readdir reuses one entry for the whole listing, so it has to grow for
    # the 40-character name and still hold the short names around it.
    s.login()
    longest = "a-name-long-enough-to-need-four-lfns.txt"
    assert s.sh("touch /etc/ab") == ""
    assert s.sh("mv /etc/catfile /etc/" + longest) == ""
    assert s.sh("touch /etc/ABC") == ""
    assert s.sh("touch /etc/medium-length-name") == ""
    assert ll_names(s, "/etc") == sorted(
        ["ABC", "ab", "hostname", "issue", longest, "medium-length-name"])
    assert s.sh("cat /etc/" + longest) == CATFILE_CONTENTS


def test_path_components_match_whole_names(s):
    # Each directory in a path is looked up as a slice of the path, so a
    # component that is only a prefix of a directory's name must not match it.
    s.login()
    missing = 'ERROR: Could not open file "%s": No such entry found'
    assert s.sh("mkdir /etc/abcdefgh-long-dir") == ""
    assert s.sh("touch /etc/abcdefgh-long-dir/f") == ""
    assert s.sh("cat /etc/abcdefgh-long-dir/f") == ""
    assert s.sh("cat /etc/ABCDEFGH-Long-Dir/f") == ""
    assert s.sh("cat /etc/abcdefgh-long/f") == missing % "/etc/abcdefgh-long/f"
    assert s.sh("cat /etc/abcdefgh-long-dirx/f") == \
        missing % "/etc/abcdefgh-long-dirx/f"


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


def test_mv_moves_files_into_a_directory(s):
    s.login()
    assert s.sh("mkdir tmp") == ""
    assert s.sh("cp /usr/bin/helloworld/main.overlay tmp/main.overlay") == ""
    assert s.sh("mkdir /usr/bin/hello2") == ""
    assert s.sh("mv tmp/main.overlay /usr/bin/hello2") == ""
    assert ll_names(s, "/root/tmp") == []
    assert ll_names(s, "/usr/bin/hello2") == ["main.overlay"]
    assert s.sh("hello2") == "Hello, world!"
    assert s.sh("mv /usr/bin/hello2/main.overlay /root/tmp/") == ""
    assert ll_names(s, "/root/tmp") == ["main.overlay"]
    assert s.sh("mv /etc/catfile /etc/hostname /root/tmp") == ""
    assert ll_names(s, "/root/tmp") == ["catfile", "hostname", "main.overlay"]
    assert s.sh("cat /root/tmp/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["issue"]
    assert fsck_problems(s) == []


def test_mv_reports_a_target_that_is_not_a_directory(s):
    s.login()
    assert s.sh("mv /etc/catfile /etc/hostname /etc/issue") == \
        'ERROR: "/etc/issue" is not a directory'
    assert s.sh("mv /etc/missing /usr") == \
        'ERROR: Could not rename "/etc/missing" to "/usr/missing": ' \
        'No such entry found'
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


def test_mv_changes_only_the_case_of_a_name(s):
    s.login()
    assert s.sh("mv /etc/catfile /etc/CatFile") == ""
    assert s.sh("cat /etc/CatFile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["CatFile", "hostname", "issue"]


def test_cp_copies_a_file_to_a_new_name(s):
    s.login()
    assert s.sh("cp /etc/catfile /etc/copied-catfile.txt") == ""
    assert s.sh("cat /etc/copied-catfile.txt") == CATFILE_CONTENTS
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == \
        ["catfile", "copied-catfile.txt", "hostname", "issue"]
    assert fsck_problems(s) == []


def test_cp_truncates_a_longer_existing_file(s):
    s.login()
    issue = s.sh("cat /etc/issue")
    assert len(issue) < len(CATFILE_CONTENTS)
    assert s.sh("cp /etc/issue /etc/catfile") == ""
    assert s.sh("cat /etc/catfile") == issue
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
    assert fsck_problems(s) == []


def test_cp_copies_files_into_a_directory(s):
    s.login()
    assert s.sh("mkdir /top") == ""
    assert s.sh("cp /etc/catfile /etc/issue /top") == ""
    assert ll_names(s, "/top") == ["catfile", "issue"]
    assert s.sh("cat /top/catfile") == CATFILE_CONTENTS
    assert s.sh("cd /etc") == ""
    assert s.sh("cp hostname ../top/") == ""
    assert ll_names(s, "/top") == ["catfile", "hostname", "issue"]
    assert s.sh("cat /top/hostname") == s.sh("cat hostname")
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
    assert fsck_problems(s) == []


def image_file(s, path):
    """Read a file out of the session's image after the simulator has halted.

    @param s The Session.
    @param path The file's absolute path on the image.

    @return The file's contents, as bytes.
    """
    return subprocess.run(
        ["mtype", "-i", s.img_copy + "@@2097152", "::" + path],
        env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), capture_output=True,
        check=True).stdout


def test_cp_copies_a_binary_file_byte_for_byte(s):
    # An overlay starts with a NULL pointer, so a copy that treats its buffer
    # as a string writes nothing.
    s.login()
    source = "/usr/bin/cp/main.overlay"
    assert s.sh("cp %s /etc/copied.overlay" % source) == ""
    original = s.sh("ll /usr/bin/cp").splitlines()[-1].split()
    copied = s.sh("ll /etc").splitlines()[-1].split()
    assert copied[-1] == "copied.overlay" and copied[3] == original[3], \
        (original, copied)
    assert fsck_problems(s) == []
    expected = image_file(s, source)
    assert b"\0" in expected
    assert image_file(s, "/etc/copied.overlay") == expected


def test_copied_command_runs(s):
    s.login()
    assert s.sh("mkdir /usr/bin/hello2") == ""
    assert s.sh("cp /usr/bin/helloworld/main.overlay /usr/bin/hello2") == ""
    assert s.sh("hello2") == "Hello, world!"


def test_cp_reports_errors(s):
    s.login()
    assert s.sh("cp /etc/missing /etc/other") == \
        'ERROR: Could not open file "/etc/missing": No such entry found'
    assert s.sh("cp /etc /top") == 'ERROR: "/etc" is a directory'
    assert s.sh("cp /etc/catfile /etc/CATFILE") == \
        'ERROR: "/etc/catfile" and "/etc/CATFILE" are the same file'
    assert s.sh("cp /etc/catfile /etc/issue /etc/hostname") == \
        'ERROR: "/etc/hostname" is not a directory'
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
    assert "top/" not in ll_names(s, "/")


def test_cat_reports_a_missing_file(s):
    s.login()
    assert s.sh("cat /etc/missing") == \
        'ERROR: Could not open file "/etc/missing": No such entry found'


def test_cat_reports_a_directory(s):
    s.login()
    assert s.sh("cat /etc") == \
        'ERROR: Could not open file "/etc": Invalid argument'


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


def heap_at_prompt(s):
    m = re.match(r"- Dynamic memory left: (\d+)$", s.sh("ps").splitlines()[0])
    assert m, s.child.before
    return int(m.group(1))


def small_memory_key(sim_bin, env):
    return (sim_bin, tuple(sorted((env or {}).items())))


def small_memory_size(s):
    # The heap grows byte for byte with the memory size, so one probe boot per
    # simulator and configuration finds the size.
    key = small_memory_key(s.sim_bin, s.env)
    if key not in _small_memory_sizes:
        s.restart_with_memory(SMALL_MEMORY_PROBE_SIZE)
        s.login()
        _small_memory_sizes[key] = \
            SMALL_MEMORY_PROBE_SIZE + SMALL_MEMORY_HEAP - heap_at_prompt(s)
    return _small_memory_sizes[key]


def test_small_memory_boots_to_a_working_shell(s):
    s.restart_with_memory(small_memory_size(s))
    s.login()
    heap = heap_at_prompt(s)
    assert SMALL_MEMORY_HEAP_RANGE[0] <= heap < SMALL_MEMORY_HEAP_RANGE[1], heap
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
    subprocess.run(["dd", "if=" + s.img_copy, "of=" + partition, "bs=64K",
                    "skip=32", "conv=sparse", "status=none"], check=True)
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
    before = datetime.datetime.now(datetime.UTC) - datetime.timedelta(hours=7)
    assert s.sh(command) == ""
    after = datetime.datetime.now(datetime.UTC) - datetime.timedelta(hours=7)
    out = s.sh("ll /etc")
    m = re.search(r"(\w{3}) (\d+)-(\d+)-(\d+)\s+(\d+):(\d+):(\d+) "
                  + re.escape(listed_name) + "$", out, re.M)
    assert m, out
    shown = datetime.datetime(*(int(g) for g in m.groups()[1:]),
                               tzinfo=datetime.UTC)
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


def test_relative_paths_resolve_against_the_working_directory(s):
    s.login()
    assert s.sh("cd /etc") == ""
    assert s.sh("cat catfile") == CATFILE_CONTENTS
    assert ll_names(s, ".") == ["catfile", "hostname", "issue"]
    assert s.sh("mkdir newdir") == ""
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue", "newdir/"]
    assert s.sh("mv catfile newdir/moved") == ""
    assert ll_names(s, "/etc/newdir") == ["moved"]
    assert s.sh("cd newdir") == ""
    assert s.sh("cat ../newdir/./moved") == CATFILE_CONTENTS
    assert s.sh("rm moved") == ""
    assert s.sh("cd ..") == ""
    assert s.sh("rmdir newdir") == ""
    assert ll_names(s, "/etc") == ["hostname", "issue"]
    assert fsck_problems(s) == []


def test_directories_grow_past_their_first_cluster(s):
    # A 1 KiB cluster holds 32 entries, and each of these names takes three.
    s.login()
    names = ["file-with-long-name-%02d" % n for n in range(40)]
    assert s.sh("mkdir /etc/many") == ""
    for name in names:
        assert s.sh("touch /etc/many/" + name) == ""
    assert s.sh("cp /etc/catfile /etc/many/catfile") == ""
    assert ll_names(s, "/etc/many") == sorted(names + ["catfile"])
    assert s.sh("cat /etc/many/catfile") == CATFILE_CONTENTS
    assert s.sh("rm /etc/many/" + names[0]) == ""
    assert ll_names(s, "/etc/many") == sorted(names[1:] + ["catfile"])
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


def console_pattern(pattern):
    """Let a pattern's newlines match the carriage returns the console adds.

    @param pattern A regular expression whose newlines are literal or escaped.

    @return The pattern with each newline allowing carriage returns before it.
    """
    return pattern.replace("\\\n", "\n").replace("\n", "\r*\n")


def ed_session(s, steps):
    """Type ed's input one line at a time and match the whole transcript.

    @param s The Session to type into.
    @param steps (line, output) pairs, starting with the command that runs ed.
      An output is the exact text the line produces, or a compiled pattern
      for it.

    @return The match of the transcript against every step's line and output.
    """
    transcript = ""
    expected = ""
    for line, output in steps:
        if isinstance(output, str):
            output = re.escape(output)
        else:
            output = output.pattern
        step = re.escape(line + "\n") + output
        expected += step
        s.child.sendline(line)
        # Like a terminal, the console echoes the next typed-ahead line as soon
        # as ed takes this one, which can be before ed prints its output.
        if output:
            s.child.expect(console_pattern(step), timeout=30)
            transcript += s.child.before + s.child.after
    s.child.expect(PROMPT, timeout=30)
    transcript = (transcript + s.child.before).replace("\r", "")
    m = re.fullmatch(expected, transcript)
    assert m, repr(transcript[-300:])
    return m


def ed_input(lines):
    """Make ed steps for lines of text typed in input mode.

    @param lines The lines of text.

    @return A (line, "") step for each line, since input mode prints nothing.
    """
    return [(line, "") for line in lines]


CATFILE_LINES = ["line-%02d" % n for n in range(1, 41)]


def test_ed_writes_a_line_that_starts_on_a_cluster_boundary(s):
    # Line 129 starts at offset 1024 in ed's scratch file, the first byte of
    # its second cluster.
    lines = ["cl-%04d" % n for n in range(1, 130)]
    s.login()
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(lines)
               + [(".", ""), ("w /etc/clusters", "1032\n"), ("q", "")])
    assert s.sh("cat /etc/clusters") == "\n".join(lines)


def test_ed_appends_and_writes_a_new_file(s):
    s.login()
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(["one", "two", "three"])
               + [(".", ""), (",p", "one\ntwo\nthree\n"),
                  ("w /etc/new.txt", "14\n"), ("q", "")])
    assert s.sh("cat /etc/new.txt") == "one\ntwo\nthree"
    assert ll_names(s, "/etc") == \
        ["catfile", "hostname", "issue", "new.txt"]
    assert ll_names(s, "/tmp") == []


def test_ed_edits_an_existing_file(s):
    s.login()
    lines = CATFILE_LINES[:2] + CATFILE_LINES[3:]
    lines = ["header", "changed"] + lines[1:] + ["footer"]
    content = "\n".join(lines) + "\n"
    ed_session(s, [
        ("ed /etc/catfile", "320\n"),
        ("2,4p", "line-02\nline-03\nline-04\n"), ("3d", ""), (".=", "3\n"),
        ("1i", ""), ("header", ""), (".", ""),
        ("$a", ""), ("footer", ""), (".", ""),
        ("2c", ""), ("changed", ""), (".", ""),
        ("w", "%d\n" % len(content)), ("q", "")])
    assert s.sh("cat /etc/catfile") == content.rstrip("\n")
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


def test_ed_resolves_addresses_and_marks(s):
    s.login()
    ed_session(s, [
        ("ed /etc/catfile", "320\n"), ("$=", "40\n"), ("5ka", ""),
        ("'a,'a+2n", "5\tline-05\n6\tline-06\n7\tline-07\n"),
        ("-1p", "line-06\n"), ("+2", "line-08\n"), ("", "line-09\n"),
        (";+1p", "line-09\nline-10\n"), (",=", "40\n"), ("Q", "")])


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
    ed_session(s, [
        ("ed /etc/catfile", "320\n"), ("1,3j", ""), ("2,3m$", ""),
        ("10,12m4", ""), ("1t0", ""), ("2,3t5", ""), ("2,3t2", ""),
        ("1,3m2", "?\n"), (",p", "\n".join(lines) + "\n"), ("Q", "")])


def test_ed_reports_errors_and_guards_unsaved_changes(s):
    s.login()
    ed_session(s, [
        ("ed", ""), ("x", "?\n"), ("h", "unknown command\n"),
        ("H", "unknown command\n"), ("9p", "?\ninvalid address\n"),
        ("a", ""), ("text", ""), (".", ""),
        ("q", "?\nwarning: buffer modified\n"),
        ("e /etc/hostname", "?\nwarning: buffer modified\n"),
        ("e /etc/hostname", "7\n"), (",p", "nanoe2e\n"), ("q", "")])
    assert ll_names(s, "/tmp") == []


def test_ed_reads_and_appends_files(s):
    s.login()
    ed_session(s, [
        ("ed /etc/hostname", "7\n"), ("f", "/etc/hostname\n"),
        ("$r /etc/issue", "13\n"), ("w /etc/out.txt", "21\n"),
        ("W /etc/out.txt", "21\n"), ("q", "")])
    once = "nanoe2e\n\\s \\r \\n \\l\n\n"
    assert s.sh("cat /etc/out.txt") == (once + once).rstrip("\n")
    assert s.sh("cat /etc/hostname") == "nanoe2e"


def test_ed_reports_files_it_cannot_open(s):
    s.login()
    ed_session(s, [
        ("ed /etc/missing", "?\n"), ("h", "cannot open input file\n"),
        ("f", "/etc/missing\n"), ("a", ""), ("x", ""), (".", ""),
        ("w /missing/out.txt", "?\n"), ("h", "cannot open output file\n"),
        ("Q", "")])
    assert ll_names(s, "/tmp") == []
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]


# More lines than ed's line table can hold at SMALL_MEMORY_HEAP.
OVERSIZED_LINE_COUNT = 300


def test_ed_loads_nothing_from_a_file_that_does_not_fit(s):
    big_lines = ["big-%03d" % n for n in range(1, OVERSIZED_LINE_COUNT + 1)]
    s.login()
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(big_lines)
               + [(".", ""), ("w /etc/bigfile", "%d\n" % (8 * len(big_lines))),
                  ("q", "")])
    s.restart_with_memory(small_memory_size(s))
    s.login()
    ed_session(s, [
        ("ed /etc/bigfile", "?\n"), ("h", "out of memory\n"), ("$=", "0\n"),
        ("f", "?\n"), ("w", "?\n"), ("Q", "")])
    assert s.sh("cat /etc/bigfile", timeout=30) == "\n".join(big_lines)
    assert ll_names(s, "/tmp") == []


def test_ed_discards_typed_text_that_does_not_fit(s):
    # Each typed line is "d", which would delete a line if it were run as a
    # command after the buffer filled up.
    s.restart_with_memory(small_memory_size(s))
    s.login()
    m = ed_session(s, [("ed", ""), ("a", "")]
                   + ed_input(["d"] * OVERSIZED_LINE_COUNT)
                   + [(".", "?\n"), ("h", "out of memory\n"),
                      ("$=", re.compile("(\\d+)\n")), ("Q", "")])
    assert 0 < int(m.group(1)) < OVERSIZED_LINE_COUNT, m.group(1)
    assert s.sh("echo still-alive") == "still-alive"
    assert ll_names(s, "/tmp") == []


def test_ed_quits_when_too_little_memory_is_left_to_edit(s):
    s.restart_with_memory(small_memory_size(s))
    s.login()
    ed_session(s, [("ed /etc/catfile", "320\n"), ("Q", "")])
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
    # tightLoop never yields, so Ctrl-C only gets through because the
    # preemption timer forces it off the processor; the kill then runs on top
    # of the preempted context.
    assert_ctrl_c_kills(s, "tightLoop")


def test_kill_terminates_a_background_process(s):
    s.login()
    s.sh("looseLoop &")
    m = re.search(r"^(\d+)  root looseLoop$", s.sh("ps"), re.M)
    assert m, "looseLoop is not running"
    pid = int(m.group(1))
    assert s.sh("kill %d" % pid) == "Termination of process %d successful." % pid
    assert "looseLoop" not in s.sh("ps")
    assert s.sh("kill 99") == \
        'Process termination returned status "Invalid argument".'
    assert s.sh("echo alive") == "alive"


def test_dirent_lists_directories_with_correct_type(s):
    # test/e2e/mkimage.sh always builds a root directory containing /etc and
    # /usr (via mmd), both real subdirectories.  mtools writes
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


def test_helloworld_prints_its_greeting(s):
    s.login()
    assert s.sh("helloworld") == "Hello, world!"


def test_help_describes_every_command(s):
    s.login()
    lines = s.sh("help").splitlines()
    assert lines[-2:] == ["", "Run a command with --help to see its usage."]
    described = [line.split(":")[0] for line in lines[:-2]]
    installed = [name.rstrip("/") for name in ll_names(s, "/usr/bin")]
    assert described == sorted(installed + ["cd", "exit", "pwd"],
                               key=str.lower), described


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


def ps_entries(s):
    """Run ps and parse its process lines.

    @param s The Session.

    @return A list of (pid, user, name) tuples.
    """
    lines = s.sh("ps").splitlines()
    assert lines[0].startswith("- Dynamic memory left: "), lines
    assert lines[-1].startswith("- Dynamic memory left: "), lines
    entries = []
    for line in lines[1:-1]:
        m = re.fullmatch(r"(\d+)  (\S+) (.+)", line)
        assert m, line
        entries.append((int(m.group(1)), m.group(2), m.group(3)))
    return entries


def assert_ctrl_c_kills(s, command):
    """Run a foreground command, interrupt it, and check nothing is left over.

    @param s The Session.
    @param command The command line to run and interrupt.
    """
    s.login()
    before = ps_entries(s)
    s.child.sendline(command)
    time.sleep(0.5)
    s.child.sendcontrol("c")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=10)
    assert idx == 0, "Ctrl-C did not return the shell to a prompt"
    assert "overflowed" not in s.child.before, s.child.before
    assert ps_entries(s) == before
    for _ in range(3):
        assert s.sh("looseLoop &") == ""
    assert [name for _pid, _user, name in ps_entries(s)].count("looseLoop") \
        == 3
    assert s.sh("echo alive") == "alive"


def test_looseloop_is_killed_by_ctrl_c(s):
    assert_ctrl_c_kills(s, "looseLoop")


def test_cat_reading_the_console_is_killed_by_ctrl_c(s):
    assert_ctrl_c_kills(s, "cat")


def test_commands_that_cannot_load_leave_the_shell_running(s):
    s.login()
    before = ps_entries(s)
    assert s.sh("mkdir /usr/bin/corrupt") == ""
    assert s.sh("cp /etc/catfile /usr/bin/corrupt/main.overlay") == ""
    assert s.sh("mkdir /usr/bin/empty") == ""
    assert s.sh("touch /usr/bin/empty/main.overlay") == ""
    for command in ("corrupt", "empty", "corrupt &", "empty &"):
        name = command.split()[0]
        assert s.sh(command) == name + ": cannot execute"
        assert ps_entries(s) == before, command
    for _ in range(3):
        assert s.sh("looseLoop &") == ""
    assert [name for _pid, _user, name in ps_entries(s)].count("looseLoop") \
        == 3


def test_failed_background_launches_release_their_slots(s):
    s.login()
    heaps = []
    for _ in range(8):
        assert s.sh("no_such_command_here &") == \
            "no_such_command_here: command not found"
        heaps.append(heap_at_prompt(s))
    # The file cache makes the heap alternate, but it must not trend down.
    assert min(heaps[4:]) >= min(heaps[:4]), heaps
    for _ in range(3):
        assert s.sh("looseLoop &") == ""
    assert [name for _pid, _user, name in ps_entries(s)].count("looseLoop") \
        == 3


HELP_OPTION_LINE = "  --help  Show this help and exit."


def help_text(s, command):
    """Run a command and return its output, which must be a usage summary.

    @param s The Session.
    @param command The command line, which asks for help.

    @return The output.
    """
    name = command.split()[0]
    out = s.sh(command)
    lines = out.splitlines()
    assert re.fullmatch(r"Usage: %s( .*)?" % re.escape(name), lines[0]), out
    assert "Options:" in lines, out
    return out


def test_every_command_prints_its_usage_for_help_options(s):
    s.login()
    for name in [n.rstrip("/") for n in ll_names(s, "/usr/bin")]:
        out = help_text(s, name + " --help")
        last = out.splitlines()[-1]
        if name == "shutdown":
            assert last == "  --help        Show this help and exit.", out
        else:
            assert last == HELP_OPTION_LINE, out
    assert s.sh("echo still-here") == "still-here"


def test_mush_help_leaves_the_user_logged_in(s):
    s.login()
    assert s.sh("cd /etc") == ""
    before = ps_entries(s)
    user, pwd = promptParts(s)
    heaps = []
    for _ in range(8):
        assert help_text(s, "mush --help").splitlines()[0] == "Usage: mush"
        assert promptParts(s) == (user, pwd)
        assert s.sh("pwd") == "/etc"
        assert ps_entries(s) == before
        heaps.append(heap_at_prompt(s))
    # The file cache makes the heap alternate, but it must not trend down.
    assert min(heaps[4:]) >= min(heaps[:4]), heaps


def test_usage_errors_print_the_same_summary_as_help(s):
    s.login()
    for command in ("cp", "mv", "grep", "kill", "mkdir", "rm", "rmdir",
                    "touch", "login"):
        assert s.sh(command) == help_text(s, command + " --help"), command
    assert s.sh("shutdown") == help_text(s, "shutdown --help")
    assert s.sh("echo a --help") == "a --help"
    assert s.sh("echo -h") == "-h"
    assert s.sh("rm -h") == \
        'ERROR: Could not remove "-h": No such entry found'


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
    assert s.sh("no_such_command_here arg") == \
        "no_such_command_here: command not found"
    assert s.sh("echo still-here") == "still-here"


def test_unknown_background_command_errors(s):
    s.login()
    assert s.sh("no_such_command_here &") == \
        "no_such_command_here: command not found"
    assert s.sh("echo still-here") == "still-here"


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


def uses_small_memory(t):
    """Report whether a test boots the simulator at the small-memory size.

    @param t The test function.

    @return True if the test calls small_memory_size, False otherwise.
    """
    return "small_memory_size" in t.__code__.co_names


def init_worker(verbose):
    global VERBOSE
    VERBOSE = verbose


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


def run_case(i, label, sim_bin, env, test_name, image, small_size):
    """Run one test against a fresh simulator and return its TAP lines.

    @param i The test's TAP number.
    @param label The test's TAP description.
    @param sim_bin The simulator binary to run.
    @param env Extra environment for the simulator.
    @param test_name The name of the test function.
    @param image The disk image to boot a copy of.
    @param small_size The small-memory size for this configuration, or None.

    @return A tuple of the TAP lines and whether the test counts as a failure.
    """
    if small_size is not None:
        _small_memory_sizes[small_memory_key(sim_bin, env)] = small_size
    xfail = XFAIL.get(test_name)
    lines = []
    failed = False
    s = None
    try:
        s = Session(sim_bin, image, env)
        globals()[test_name](s)
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
    global VERBOSE
    args = [a for a in argv[1:]]
    if "-v" in args:
        VERBOSE = True
        args.remove("-v")
    jobs = 1 if VERBOSE else len(os.sched_getaffinity(0))
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
            initializer=init_worker, initargs=(VERBOSE,)) as pool:
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
                                          t.__name__, image, None)
            else:
                waiting.append((i, label, sim_bin, env, t, probe))
        for i, label, sim_bin, env, t, probe in waiting:
            size = None if probe.exception() else probe.result()
            runs[i - 1] = pool.submit(run_case, i, label, sim_bin, env,
                                      t.__name__, image, size)

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
