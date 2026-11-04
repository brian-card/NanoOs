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

from vtscreen import VtScreen
from fakevdp import (FakeVdp, KEYCODE_CAPSLOCK, KEYCODE_DELETE, KEYCODE_DOWN,
                     KEYCODE_END, KEYCODE_HOME, KEYCODE_LEFT, KEYCODE_NUMLOCK,
                     KEYCODE_RIGHT, KEYCODE_UP, KEYSTATE, LED_CAPS_LOCK,
                     LED_NUM_LOCK)

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

    def write_file(self, path, text):
        """Stop the simulator and write a file into this session's image."""
        if self.child.isalive():
            self.child.terminate(force=True)
        subprocess.run(["mcopy", "-o", "-i", self.img_copy + "@@2097152", "-",
                        "::" + path], input=text.encode(),
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)

    def restart_with_file(self, path, text):
        """Write a file into this session's image and boot again."""
        self.write_file(path, text)
        self.spawn([self.img_copy])

    def restart_with_config_sys(self, text):
        """Write /etc/config.sys into this session's image and boot again."""
        self.restart_with_file("/etc/config.sys", text)

    def restart_with_vdp(self, config_sys=VDP_CONFIG_SYS, **kwargs):
        """Boot again with vdp.drv serving a console on a FakeVdp.

        @param config_sys The /etc/config.sys to boot with.
        @param kwargs Arguments for FakeVdp.

        @return The FakeVdp.
        """
        self.vdp = FakeVdp(os.path.join(self.tmp, "vdp.sock"), **kwargs)
        self.env = dict(self.env or {}, NANO_OS_SIM_UART0=self.vdp.path)
        self.restart_with_config_sys(config_sys)
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
    assert re.search(r"(\n|\x1b\[H)NanoOs \S+ nanoe2e tty\d+\n\n$", banner), \
        repr(banner[-120:])


def test_getty_resets_the_colors_and_clears_before_the_banner(s):
    s.wait_login_prompt()
    banner = s.child.before.replace("\r", "")
    assert re.search(r"\x1b\[40;97m\x1b\[2J\x1b\[H\n*NanoOs \S+ nanoe2e tty\d+\n\n$",
                     banner), repr(banner[-120:])


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
    kinds = [kind for _t, kind, _d in vdp.events]
    first_mode = kinds.index("mode")
    resets = [detail for _t, _k, detail in vdp.events[:first_mode]]
    assert resets == VDP_RESET, \
        f"the VDP wasn't reset before the mode change: {resets}"
    setup = [(kind, detail)
             for _t, kind, detail in vdp.events[first_mode:first_mode + 14]]
    assert setup == [("mode", 0), ("behaviour", (1, 0xFE)), ("layout", 1),
                     ("cls", None), ("cursor", 1), ("mouse", "reset"),
                     ("mouse", "disable"), ("pointer", VDP_POINTER_ID),
                     ("font", VDP_FONT_ID),
                     ("colour", CGA_WHITE), ("colour", 128 + CGA_BLACK),
                     ("cls", None), ("tab", (0, 0)),
                     ("text", setup[13][1])], \
        f"the screen wasn't set up first: {setup}"
    assert vdp.palette == CGA_PALETTE, \
        f"the palette isn't CGA's: {vdp.palette}"
    delay = vdp.events[first_mode][0] - vdp.answered
    assert delay < 1.0, f"the screen was set up {delay:.1f} s after the answer"


CGA_PALETTE = dict(enumerate([
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00),
    (0x00, 0xAA, 0xAA), (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA),
    (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA), (0x55, 0x55, 0x55),
    (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55),
    (0xFF, 0xFF, 0xFF),
]))
CGA_BLACK = 0
CGA_WHITE = 15
# What vdp.drv sends to undo the state an eZ80 reset leaves on the VDP, as
# FakeVdp records it: (VDU 23,0 or VDU 23 command, detail).
VDP_RESET = [
    (0xC1, 0), (0x98, 1), (0xFE, 0), (27, None), (0xA0, 0xFFFF), (0x91, None),
    (0xC4, 4), (0x85, ("rate", 255, 16384)),
] + [(0x85, ("reset", channel)) for channel in range(32)] + [
    (0x88, (500, 100, 0)),
]
VDP_POINTER_ID = 0xF000
VDP_SETUP_POLL_COOKIE = 0x4f
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


VDP_FONT_ID = 0xF100
VDP_GLYPHS = {
    "A": ["..###.", ".#...#", ".#...#", ".#####", ".#...#", ".#...#",
          ".#...#", "......"],
    "g": ["......", "......", "..####", ".#...#", ".#...#", "..####",
          ".....#", "..###."],
    "~": ["......", "......", "..#...", ".#.#.#", "....#.", "......",
          "......", "......"],
    " ": ["......"] * 8,
    "\xff": ["......"] * 8,
    # Code page 437's smiley, e acute, box drawing, full block, and theta.
    "\x01": ["..###.", ".#...#", ".##.##", ".#...#", ".##.##", ".#.#.#",
             "..###.", "......"],
    "\x82": ["....#.", "...#..", "..###.", ".#...#", ".#####", ".#....",
             "..###.", "......"],
    "\xc4": ["......"] * 3 + ["######"] + ["......"] * 4,
    "\xc9": ["......", "......", "..####", "..#...", "..#.##", "..#.#.",
             "..#.#.", "..#.#."],
    "\xdb": ["######"] * 8,
    "\xe9": ["..###.", ".#...#", ".#...#", ".#####", ".#...#", ".#...#",
             "..###.", "......"],
}


def assert_vdp_font(vdp):
    """Check that a FakeVdp draws text in the driver's 6x8 font.

    @param vdp The FakeVdp, after the driver has set up its screen.
    """
    assert vdp.font == VDP_FONT_ID, f"the selected font is {vdp.font}"
    width, height, _data = vdp.fonts[VDP_FONT_ID]
    assert (width, height) == (6, 8), f"the font is {width}x{height}"
    for ch, rows in VDP_GLYPHS.items():
        assert vdp.glyph(ch) == rows, \
            f"the glyph for {ch!r} is:\n" + "\n".join(vdp.glyph(ch))
    # A monitor can cut off the screen's first pixel column, so only glyphs
    # that fill the cell (inverse bullets, shades, box drawing, blocks) use it.
    fills_cell = {0x08, 0x0A} | set(range(0xB0, 0xE0))
    for code in set(range(256)) - fills_cell:
        rows = vdp.glyph(chr(code))
        assert all(row[0] == "." for row in rows), \
            f"the glyph for {code:#04x} uses the left column:\n" \
            + "\n".join(rows)


def assert_vdp_pointer(vdp):
    """Check that a FakeVdp shows the driver's mouse pointer.

    @param vdp The FakeVdp, after the driver has set up its screen.
    """
    assert vdp.mouse_enabled, "the mouse wasn't enabled"
    assert vdp.mouse_visible, "the pointer isn't shown"
    assert vdp.mouse_cursor == VDP_POINTER_ID, \
        f"the pointer is cursor {vdp.mouse_cursor}"
    image = vdp.pointer_image()
    assert image == VDP_POINTER, \
        "the pointer isn't the driver's cursor:\n" + "\n".join(image or [])
    _bitmap, hot_x, hot_y = vdp.cursors[VDP_POINTER_ID]
    assert (hot_x, hot_y) == (0, 0), f"the hotspot is at {hot_x},{hot_y}"


def test_uart_console_left_out_of_config_sys_is_owned_by_no_one(s):
    # The memory manager owns every console port until it has printed its boot
    # message, so a port no shell takes must be released back to no one.
    vdp = s.restart_with_vdp(config_sys=(
        "DEVICE=/usr/lib/vdp.drv\n"
        "HARDWARE=/usr/lib/vdp.drv UART0\n"
        "CONSOLE=/usr/lib/vdp.drv /usr/bin/getty\n"))
    vdp.expect("login: ")
    s.child.sendcontrol("c")
    i = s.child.expect([r"Killing process \d+|Could not send message|"
                        r"Could not communicate", pexpect.TIMEOUT], timeout=3)
    assert i == 1, "Ctrl-C on the unused UART signalled " + s.child.after


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
    assert_vdp_font(vdp)
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


def vdp_screen_events(vdp, start):
    """Return the color, clear and cursor move events the VDP got after an
    index.

    @param vdp The FakeVdp.
    @param start The index in vdp.events to start from.

    @return A list of (kind, detail) tuples.
    """
    with vdp.lock:
        return [(kind, detail) for _, kind, detail in vdp.events[start:]
                if kind in ("colour", "cls", "tab")]


def test_vdp_color_and_clear_set_the_text_colors_and_clear(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    expected = [("colour", 15), ("colour", 128 + 2), ("cls", None),
                ("tab", (0, 0))]
    for command in ("color 2f", "clear"):
        vdp.clear()
        with vdp.lock:
            start = len(vdp.events)
        vdp.type(command + "\r")
        vdp.expect(PROMPT)
        assert vdp_screen_events(vdp, start) == expected, command
        assert re.fullmatch(PROMPT, vdp.screen), repr(vdp.screen)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vdp_draws_control_characters_as_glyphs(s):
    # 0x0c would clear the screen and 0x16 change the mode if sent bare.
    s.child.terminate(force=True)
    subprocess.run(["mcopy", "-o", "-i", s.img_copy + "@@2097152", "-",
                    "::/glyphs.txt"],
                   input=b"glyphs:\x01\x0c\x16\x1f\x7f:end\n",
                   env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.clear()
    with vdp.lock:
        start = len(vdp.events)
    vdp.type("cat /glyphs.txt\r")
    vdp.expect(r"\r\nglyphs:\x01\x0c\x16\x1f\x7f:end\r\n" + PROMPT)
    with vdp.lock:
        kinds = {kind for _t, kind, _d in vdp.events[start:]}
    assert kinds == {"text"}, f"control characters ran as commands: {kinds}"
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


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
    first_time, _kind, first_detail = vdp.events[0]
    assert first_detail == VDP_RESET[0], \
        f"output was sent before the driver gave up: {vdp.events[:3]}"
    waited = first_time - vdp.first_poll
    assert 14.5 < waited < 16.5, f"gave up after {waited:.1f} s, not 15 s"
    late = [cookie for t, cookie in zip(vdp.polls, vdp.poll_cookies)
            if t > first_time]
    assert late and all(cookie == VDP_SETUP_POLL_COOKIE for cookie in late), \
        f"handshake polls continued after giving up: {late}"


def test_vdp_driver_holds_output_until_the_mouse_reset_is_done(s):
    vdp = s.restart_with_vdp(mouse_reset_seconds=2.0)
    vdp.expect("login: ", timeout=20)
    reset_at = next(t for t, kind, _d in vdp.events if kind == "mouse")
    text_at = next(t for t, kind, _d in vdp.events if kind == "text")
    assert any(t > reset_at for t in vdp.polls), \
        "the driver didn't poll after resetting the mouse"
    answered = vdp.mouse_reset_answered
    assert (answered is not None) and (text_at >= answered), \
        "text was sent before the VDP finished resetting the mouse"
    assert answered - reset_at >= 1.9
    assert_vdp_font(vdp)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vdp_pointer_appears_once_a_mouse_is_plugged_in(s):
    vdp = s.restart_with_vdp(mouse=False)
    vdp.expect("login: ")
    assert not vdp.mouse_enabled, "the mouse was enabled with none plugged in"
    assert not vdp.mouse_visible, "a pointer was shown with no mouse"
    vdp.mouse = True
    vdp.disconnect()
    vdp.wait_connections(2)
    deadline = time.time() + 10
    while not vdp.mouse_visible:
        assert time.time() < deadline, \
            "the restarted driver never showed the pointer"
        time.sleep(0.1)
    assert_vdp_pointer(vdp)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vdp_pointer_goes_away_once_the_mouse_is_unplugged(s):
    vdp = s.restart_with_vdp()
    vdp.expect("login: ")
    assert_vdp_pointer(vdp)
    vdp.mouse = False
    vdp.disconnect()
    vdp.wait_connections(2)
    deadline = time.time() + 10
    while sum(kind == "cls" for _t, kind, _d in vdp.events) < 2:
        assert time.time() < deadline, \
            "the restarted driver never set up the screen"
        time.sleep(0.1)
    vdp.type("root\r")
    vdp.expect("password: ")
    assert not vdp.mouse_enabled, "the mouse is still enabled with none plugged in"
    assert not vdp.mouse_visible, "the pointer is still shown with no mouse"
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


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
    fonts = sum(kind == "font" for _t, kind, _d in vdp.events)
    assert fonts == 2, f"the font was selected {fonts} times, not 2"
    assert_vdp_font(vdp)
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


def wait_vdp_screen_setups(vdp, count):
    """Wait until a FakeVdp's screen has been set up a number of times.

    @param vdp The FakeVdp.
    @param count The number of setups to wait for.
    """
    deadline = time.time() + 10
    while sum(kind == "cls" for _t, kind, _d in vdp.events) < count:
        assert time.time() < deadline, \
            "the restarted driver never set up the screen"
        time.sleep(0.1)


def test_vdp_numlock_option_turns_num_lock_on(s):
    vdp = s.restart_with_vdp(VDP_CONFIG_SYS.replace(
        "DEVICE=/usr/lib/vdp.drv\n", "DEVICE=/usr/lib/vdp.drv --numlock=on\n"))
    vdp_login(vdp)
    keystates = [detail[1] for _t, kind, detail in vdp.events
                 if kind == "reset" and detail[0] == KEYSTATE]
    assert keystates == [(500, 100, LED_NUM_LOCK)], \
        f"the keyboard was set to {keystates}"
    # The VDP's own Num Lock is still off, so it reports navigation keys.
    vdp.clear()
    vdp.type("echo ")
    for ch in "12.":
        vdp.keypad(ch)
    vdp.type("\r")
    vdp.expect(r"\r\n12\.\r\n" + PROMPT)

    vdp.lock_key(KEYCODE_NUMLOCK)
    vdp.wait_leds(0)
    vdp.clear()
    vdp.type("echo ab")
    vdp.keypad("4")
    vdp.type("x\r")
    vdp.expect(r"\r\naxb\r\n" + PROMPT)

    vdp.lock_key(KEYCODE_NUMLOCK)
    vdp.wait_leds(LED_NUM_LOCK)
    vdp.lock_key(KEYCODE_CAPSLOCK)
    vdp.wait_leds(LED_NUM_LOCK | LED_CAPS_LOCK)
    vdp.lock_key(KEYCODE_CAPSLOCK)
    vdp.wait_leds(LED_NUM_LOCK)

    # A restarted driver starts from /etc/config.sys again.
    vdp.lock_key(KEYCODE_NUMLOCK)
    vdp.wait_leds(0)
    vdp.disconnect()
    vdp.wait_connections(2)
    wait_vdp_screen_setups(vdp, 2)
    vdp.wait_leds(LED_NUM_LOCK)
    vdp.clear()
    vdp.type("echo ")
    vdp.keypad("7")
    vdp.type("\r")
    vdp.expect(r"\r\n7\r\n" + PROMPT)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vi_draws_on_the_vdp_console(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    rows, columns = vdp.grid_rows, vdp.grid_columns

    def shows(lines, status=None):
        def check(grid):
            return (grid[:rows - 1] == lines + ["~"] * (rows - 1 - len(lines))
                    and (status is None or grid[rows - 1] == status))
        return check

    def type_shown(text, shown):
        # Wait for vi to catch up before typing more, since keys typed while
        # it falls far behind are dropped.
        for start in range(0, len(text), 8):
            vdp.type(text[start:start + 8])
            vdp.wait_grid(lambda grid: shown(text[:start + 8], grid))

    vdp.type("vi /etc/vdp.txt\r")
    vdp.wait_grid(shows([""], '"/etc/vdp.txt" [New File]'))
    long_line = "".join(chr(ord("a") + n % 26) for n in range(columns + 3))
    vdp.type("i")
    type_shown(long_line, lambda typed, grid: (
        grid[0] + (grid[1] if len(typed) > columns else "")) == typed)
    vdp.type("\rx\tyz\x1b")
    vdp.wait_grid(shows([long_line[:columns], long_line[columns:],
                         "x       yz"], ""))
    # Erasing to the end of a wrapped line's last row.
    vdp.press(KEYCODE_UP)
    vdp.type("$x")
    vdp.wait_grid(shows([long_line[:columns], long_line[columns:-1],
                         "x       yz"]))
    count = rows + 10
    vdp.type("Go")
    for n in range(1, count + 1):
        line = "l%02d" % n
        vdp.type(line + ("\r" if n < count else "\x1b"))
        vdp.wait_grid(lambda grid: line in grid)
    screen_rows = [long_line[:columns], long_line[columns:-1],
                   "x       yz"] + ["l%02d" % n for n in range(1, count + 1)]
    vdp.wait_grid(lambda grid: grid[:rows - 1] == screen_rows[-(rows - 1):]
                  and grid[rows - 1] == "")
    vdp.type("gg")
    vdp.wait_grid(lambda grid: grid[:rows - 1] == screen_rows[:rows - 1])
    vdp.type(":wq\r")
    vdp.expect(r"%dL, \d+C written" % (count + 2))
    vdp.expect(PROMPT)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vdp_keypad_navigates_without_the_numlock_option(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.clear()
    vdp.type("echo ab")
    vdp.keypad("4")
    vdp.type("x\r")
    vdp.expect(r"\r\naxb\r\n" + PROMPT)
    vdp.lock_key(KEYCODE_NUMLOCK)
    vdp.wait_leds(LED_NUM_LOCK)
    vdp.clear()
    vdp.type("echo ")
    vdp.keypad("3")
    vdp.type("\r")
    vdp.expect(r"\r\n3\r\n" + PROMPT)


def test_vdp_driver_reports_and_ignores_unrecognized_arguments(s):
    vdp = s.restart_with_vdp(VDP_CONFIG_SYS.replace(
        "DEVICE=/usr/lib/vdp.drv\n",
        "DEVICE=/usr/lib/vdp.drv --bogus --numlock=yes stray --numlock=on\n"))
    vdp.expect("login: ")
    # getty's clear wipes the warnings, so look for them in what was drawn.
    with vdp.lock:
        drawn = "".join("\f" if kind == "cls" else chr(detail)
                        for _t, kind, detail in vdp.events
                        if kind in ("cls", "text"))
    assert re.search(
        r"\fvdp\.drv: Ignoring unrecognized argument \"--bogus\"\r\n"
        r"vdp\.drv: Ignoring unrecognized argument \"--numlock=yes\"\r\n"
        r"vdp\.drv: Ignoring unrecognized argument \"stray\"\r\n"
        r"\fNanoOs ", drawn), repr(drawn[:300])
    vdp_login(vdp)
    assert vdp.leds == LED_NUM_LOCK, f"the LEDs are {vdp.leds:#x}"
    vdp.clear()
    vdp.type("echo ")
    vdp.keypad("9")
    vdp.type("\r")
    vdp.expect(r"\r\n9\r\n" + PROMPT)
    s.login()
    assert s.sh("echo uart1-too") == "uart1-too"


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
    edit(s, "echo older\r", "older")
    edit(s, "echo kept\r", "kept")
    for keys, shown in (("echo dropped" + UP, "echo kept"),
                        ("echo typed", "echo typed")):
        s.child.send(keys)
        s.child.expect_exact(shown, timeout=10)
        s.child.send("\x03")
        s.child.expect_exact("^C", timeout=10)
        s.child.expect(PROMPT, timeout=10)
        assert s.child.before.replace("\r", "") == "\n", repr(s.child.before)
    edit(s, UP + "\r", "kept")
    edit(s, UP + UP + UP + "\r", "older")
    edit(s, UP * 4 + DOWN + "\r", "kept")
    assert s.sh("cat .mush_history") == "\n".join([
        "echo older", "echo kept", "echo older", "echo kept",
        "cat .mush_history"])


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


def test_a_uart_console_is_80_by_24(s):
    s.login()
    assert s.sh("termtest size") == "24 80"


def test_a_resize_report_arrives_in_one_read(s):
    s.login()
    s.child.sendline("termtest report")
    s.child.expect_exact("\x1b[?2048h", timeout=10)
    s.child.send("\x1b[48;30;100;0;0t")
    s.child.expect_exact("\x1b[?2048l", timeout=10)
    s.child.expect(r"\A\^\[\[48;30;100;0;0t\r*\n" + PROMPT, timeout=10)


def test_exiting_with_resize_reports_on_turns_them_off(s):
    s.login()
    s.child.sendline("termtest leave")
    s.child.expect_exact("\x1b[?2048h", timeout=10)
    s.child.expect_exact("\x1b[?2048l", timeout=10)
    s.child.expect(PROMPT, timeout=10)
    assert s.sh("echo after") == "after"


def test_termtest_erase_writes_at_the_position_it_is_given(s):
    s.login()
    s.child.sendline("termtest erase 60 100")
    s.child.expect_exact("\x1b[60;100Habc\x1b[K", timeout=10)
    s.child.sendline("")
    s.child.expect(PROMPT, timeout=10)


def test_termtest_inserts_and_deletes_rows_at_the_row_it_is_given(s):
    s.login()
    s.child.sendline("termtest insert 3 2")
    s.child.expect_exact("\x1b[999;1Hz\x1b[3;1H\x1b[2L", timeout=10)
    s.child.sendline("")
    s.child.expect(PROMPT, timeout=10)
    s.child.sendline("termtest delete")
    s.child.expect_exact("\x1b[999;1Hz\x1b[2;1H\x1b[1M", timeout=10)
    s.child.sendline("")
    s.child.expect(PROMPT, timeout=10)


def test_vdp_console_inserts_and_deletes_rows(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    rows = vdp.grid_rows

    def shows(top, bottom):
        def check(grid):
            return (grid[:len(top)] == top
                    and grid[len(top):rows - len(bottom)]
                    == [""] * (rows - len(top) - len(bottom))
                    and grid[rows - len(bottom):] == bottom)
        return check

    # termtest waits for input, so the insertion is the last thing it writes.
    vdp.type("termtest insert 2 2\r")
    vdp.wait_grid(shows(["a", "", "", "b", "c", "d", "e"], []))
    assert (vdp.grid_row, vdp.grid_column) == (1, 0)
    vdp.type("\r")
    vdp.expect(PROMPT + r"$")
    vdp.type("termtest delete 2 2\r")
    vdp.wait_grid(shows(["a", "d", "e"], ["z", "", ""]))
    vdp.type("\r")
    vdp.expect(PROMPT + r"$")
    vdp.type("termtest delete 1 99\r")
    vdp.wait_grid(shows([], []))
    vdp.type("\r")
    vdp.expect(PROMPT + r"$")
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vdp_console_reports_its_size(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.type("termtest size\r")
    vdp.expect(r"\r\n60 106\r\n" + PROMPT)
    vdp.type("termtest report\r")
    vdp.expect(r"\r\n\^\[\[48;60;106;480;640t\r\n" + PROMPT)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vdp_console_moves_the_cursor_and_erases_to_the_end_of_the_line(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    assert vdp.cursor_behaviour & 1, "scroll protect is off"
    start = len(vdp.events)
    vdp.type("termtest erase\r")
    # termtest waits for input, so nothing drawn after the erase can finish it.
    deadline = time.time() + 10
    with vdp.lock:
        while (("tab", (7, 2)) not in
               [(kind, detail) for _, kind, detail in vdp.events[start:]]):
            left = deadline - time.time()
            assert left > 0, "the erase never moved the cursor back"
            vdp.lock.wait(left)
        drawn = [(kind, detail) for _, kind, detail in vdp.events[start:]
                 if kind in ("tab", "text")]
    at = drawn.index(("tab", (4, 2)))
    expected = ([("text", ord(c)) for c in "abc"]
                + [("text", ord(" "))] * 99 + [("tab", (7, 2))])
    assert drawn[at + 1:at + 1 + len(expected)] == expected, drawn[at:]
    vdp.type("\r")
    vdp.expect(PROMPT + r"$")
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


class ViTerminal:
    """Run vi in a Session and keep a VtScreen of what it draws."""

    def __init__(self, s, command, rows=24, columns=80):
        """Start a command that draws on the screen.

        @param s The Session, logged in.
        @param command The command line to run.
        @param rows The number of rows of the screen model.
        @param columns The number of columns of the screen model.
        """
        self.s = s
        self.screen = VtScreen(rows, columns)
        self.output = ""
        s.child.delaybeforesend = None
        s.child.sendline(command)

    def _pump(self, timeout):
        try:
            data = self.s.child.read_nonblocking(4096, timeout)
        except pexpect.TIMEOUT:
            return
        self.output += data
        self.screen.feed(data)

    def wait(self, check, timeout=15):
        """Read output until the screen passes a check.

        @param check A function that takes the VtScreen and returns whether
          it shows what is wanted.
        @param timeout Seconds to wait.
        """
        deadline = time.time() + timeout
        while not check(self.screen):
            if time.time() > deadline:
                raise AssertionError("screen never matched:\n%s" % self.screen)
            self._pump(0.1)

    def wait_lines(self, expected, timeout=15):
        """Read output until the screen's top rows are exactly some lines.

        @param expected The text of the rows, from the top.
        @param timeout Seconds to wait.
        """
        self.wait(lambda screen: screen.lines()[:len(expected)] == expected,
                  timeout)

    def wait_status(self, text, timeout=15):
        """Read output until the status line is exactly some text.

        @param text The text.
        @param timeout Seconds to wait.
        """
        self.wait(lambda screen: screen.line(screen.rows - 1) == text, timeout)

    def keys(self, text):
        """Type keys.

        @param text The keys.
        """
        self.s.child.send(text)

    def settle(self, quiet=0.5):
        """Read output until none arrives for a while.

        @param quiet Seconds without output that count as settled.
        """
        length = -1
        while length != len(self.output):
            length = len(self.output)
            self._pump(quiet)

    def wait_exit(self, timeout=15):
        """Read output until the shell's prompt follows vi's output."""
        deadline = time.time() + timeout
        while not re.search(PROMPT + "$", self.output):
            if time.time() > deadline:
                raise AssertionError("vi never exited:\n%s" % self.screen)
            self._pump(0.1)


def vi_lines(texts, rows=24):
    """Return the rows vi shows for some lines on a screen with no wrapping.

    @param texts The lines.
    @param rows The number of rows on the screen.
    """
    return list(texts) + ["~"] * (rows - 1 - len(texts))


def vi_rows(lines, rows, columns):
    """Return the text rows vi shows for lines from the top of the screen.

    @param lines The lines, from the one at the top.
    @param rows The number of rows on the screen, with the status line.
    @param columns The number of columns on the screen.
    """
    shown = []
    for line in lines:
        line = line.expandtabs(8)
        shown += [line[at:at + columns].rstrip()
                  for at in range(0, len(line), columns)] or [""]
    return (shown + ["~"] * rows)[:rows - 1]


def vi_row_lines(rows, columns):
    """Return a file's lines for vi_replaces_rows:  enough to fill the screen
    and more, with every fourth line wrapping onto two rows and a | in its
    middle, and line 9 one column longer than a row.  The wrapped lines are
    mostly tabs, so that they are short enough to edit and to join.

    @param rows The number of rows on the screen.
    @param columns The number of columns on the screen.
    """
    lines = []
    for n in range(1, rows + 12):
        if n == 9:
            lines.append(("w%02d" % n).ljust(columns + 1, "w"))
        elif n % 4 == 3:
            tabs = (columns * 3) // 32
            lines.append("L%02d" % n + "\t" * tabs + "|"
                         + "+" * (columns - tabs * 8 + 16))
        else:
            lines.append("line%02d" % n)
    return lines


def vi_replaces_rows(show, lines):
    """Check the screen after each command that adds, removes, or changes
    lines in the middle of it, with wrapped lines above and below.

    @param show A function that types keys at vi, then waits until its text
      rows show a list of lines from the top.
    @param lines The file's lines, from vi_row_lines, which vi is showing
      from the first with the cursor on it.

    @return The lines after the last command, which leaves the cursor on the
    last of them.
    """
    lines = list(lines)

    def find(prefix):
        return next(n for n, line in enumerate(lines)
                    if line.startswith(prefix))

    def undo_and_redo(before):
        show("u", before)
        show("\x12", lines)

    before = list(lines)
    lines[5:5] = ["new"]
    show("5Gonew\x1b", lines)
    undo_and_redo(before)
    lines[4:4] = ["above"]
    show("5GOabove\x1b", lines)
    lines[1:2] = ["li", "ne02"]
    show("2G0lli\r\x1b", lines)
    at = find("L03")
    middle = lines[at].index("|")
    before = list(lines)
    lines[at:at + 1] = [lines[at][:middle], lines[at][middle:]]
    show("%dG0/|\ri\r\x1b" % (at + 1), lines)
    undo_and_redo(before)
    at = find("L07")
    before = list(lines)
    lines[at + 1:at + 1] = [lines[at]]
    show("%dGyyp" % (at + 1), lines)
    undo_and_redo(before)
    lines[3:3] = [lines[3]] * 2
    show("4Gyy2P", lines)
    at = find("L07")
    del lines[at + 1]
    show("%dGdd" % (at + 2), lines)
    before = list(lines)
    del lines[at - 1:at + 2]
    show("%dG3dd" % at, lines)
    undo_and_redo(before)
    at = find("L11")
    before = list(lines)
    lines[at - 1:at + 1] = [lines[at - 1] + " " + lines[at]]
    show("%dGJ" % at, lines)
    undo_and_redo(before)
    at = find("w09")
    lines[at] = lines[at][:-1]
    show("%dG$x" % (at + 1), lines)
    lines[at] += "w"
    show("p", lines)
    removed = lines[1:4]
    del lines[1:4]
    show(":2,4d\r", lines)
    lines[1:1] = removed
    show("u", lines)
    del lines[12:]
    show("13GdG", lines)
    del lines[11:]
    show("dd", lines)
    del lines[8:]
    show(":9,$d\r", lines)
    return lines


def vi_written(name, lines):
    """Return the status vi shows after writing lines to a file.

    @param name The file's name.
    @param lines The lines.
    """
    return '"%s" %dL, %dC written' % (
        name, len(lines), sum(len(line) + 1 for line in lines))


def vi_line_rows(line, columns):
    """Return the number of screen rows vi shows a line on.

    @param line The line.
    @param columns The number of columns on the screen.
    """
    return max(1, -(-len(line.expandtabs(8)) // columns))


def vi_top_for_bottom(lines, line, rows, columns):
    """Return the number of the top line when vi scrolls a line to the
    bottom of the screen.

    @param lines The lines.
    @param line The number of the line at the bottom.
    @param rows The number of rows on the screen.
    @param columns The number of columns on the screen.
    """
    used = vi_line_rows(lines[line - 1], columns)
    top = line
    while top > 1 and used + vi_line_rows(lines[top - 2], columns) <= rows - 1:
        used += vi_line_rows(lines[top - 2], columns)
        top -= 1
    return top


def vi_last_shown(lines, top, rows, columns):
    """Return the number of the last line that fits on the screen whole.

    @param lines The lines.
    @param top The number of the top line.
    @param rows The number of rows on the screen.
    @param columns The number of columns on the screen.
    """
    used = 0
    line = top
    while used + vi_line_rows(lines[line - 1], columns) <= rows - 1:
        used += vi_line_rows(lines[line - 1], columns)
        line += 1
    return line - 1


def vi_scrolls_rows(show, lines, rows, columns):
    """Check the screen as vi scrolls down and up a line at a time, and as a
    line typed in insert mode grows onto another row, with wrapped lines
    scrolling off and onto the screen.

    @param show A function that types keys at vi, then waits until its text
      rows show a list of lines from the top.
    @param lines The file's lines, from vi_row_lines, which vi is showing
      from the first with the cursor on it.
    """
    lines = list(lines)
    top = 1
    last = vi_last_shown(lines, top, rows, columns)
    show("%dG" % last, lines)
    for line in range(last + 1, last + 5):
        top = vi_top_for_bottom(lines, line, rows, columns)
        show("j", lines[top - 1:])
    show("H", lines[top - 1:])
    assert top > 2
    for _ in range(min(4, top - 1)):
        top -= 1
        show("k", lines[top - 1:])
    last = vi_last_shown(lines, top, rows, columns)
    show("%dG" % last, lines[top - 1:])
    for line in (last + 3, last + 6):
        top = vi_top_for_bottom(lines, line, rows, columns)
        show("3j", lines[top - 1:])
    assert top > 4
    show("H", lines[top - 1:])
    top -= 3
    show("3k", lines[top - 1:])
    last = vi_last_shown(lines, top, rows, columns)
    lines[last:last] = ["new"]
    top = vi_top_for_bottom(lines, last + 1, rows, columns)
    show("%dGA\rnew\x1b" % last, lines[top - 1:])
    lines[last + 1:last + 1] = ["open"]
    top = vi_top_for_bottom(lines, last + 2, rows, columns)
    show("oopen\x1b", lines[top - 1:])
    grow = "g" * (columns - len(lines[top + 2]))
    lines[top + 2] += grow
    show("%dGA%s" % (top + 3, grow), lines[top - 1:])
    lines[top + 2] += "rows"
    show("rows\x1b", lines[top - 1:])
    last = vi_last_shown(lines, top, rows, columns)
    grow = "b" * (columns - len(lines[last - 1]))
    lines[last - 1] += grow + "z"
    top = vi_top_for_bottom(lines, last, rows, columns)
    show("%dGA%sz\x1b" % (last, grow), lines[top - 1:])
    show("G", lines[vi_top_for_bottom(lines, len(lines), rows, columns) - 1:])
    show("1G", lines)
    return lines


def test_vi_scrolls_by_moving_the_rows_on_the_screen(s):
    lines = vi_row_lines(24, 80)
    s.restart_with_file("/etc/scroll.txt", "".join(l + "\n" for l in lines))
    s.login()
    vi = ViTerminal(s, "vi /etc/scroll.txt")
    vi.wait_lines(vi_rows(lines, 24, 80))

    def show(keys, expected):
        vi.keys(keys)
        vi.wait_lines(vi_rows(expected, 24, 80))

    vi_scrolls_rows(show, lines, 24, 80)
    vi.keys(":q!\r")
    vi.wait_exit()


def test_vi_scrolls_by_moving_the_rows_on_the_vdp(s):
    lines = vi_row_lines(60, 106)
    s.write_file("/etc/scroll.txt", "".join(l + "\n" for l in lines))
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    rows, columns = vdp.grid_rows, vdp.grid_columns

    def show(keys, expected):
        vdp.type(keys)
        vdp.wait_grid(lambda grid:
                      grid[:rows - 1] == vi_rows(expected, rows, columns))

    show("vi /etc/scroll.txt\r", lines)
    vi_scrolls_rows(show, lines, rows, columns)
    vdp.type(":q!\r")
    vdp.expect(PROMPT)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vi_replaces_only_the_rows_that_change(s):
    lines = vi_row_lines(24, 80)
    s.restart_with_file("/etc/rows.txt", "".join(l + "\n" for l in lines))
    s.login()
    vi = ViTerminal(s, "vi /etc/rows.txt")
    vi.wait_lines(vi_rows(lines, 24, 80))

    def show(keys, expected):
        vi.keys(keys)
        vi.wait_lines(vi_rows(expected, 24, 80))

    lines = vi_replaces_rows(show, lines)
    written = vi_written("/etc/rows.txt", lines)
    vi.keys(":w\r")
    vi.wait_status(written)
    # Moving the rows below a change never moves the status line.
    vi.keys("kdd")
    del lines[-2]
    vi.wait_lines(vi_rows(lines, 24, 80))
    assert vi.screen.line(23) == written, str(vi.screen)
    vi.keys(":q!\r")
    vi.wait_exit()


def test_vi_replaces_only_the_rows_that_change_on_the_vdp(s):
    lines = vi_row_lines(60, 106)
    s.write_file("/etc/rows.txt", "".join(l + "\n" for l in lines))
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    rows, columns = vdp.grid_rows, vdp.grid_columns
    assert (rows, columns) == (60, 106)

    def show(keys, expected):
        vdp.type(keys)
        vdp.wait_grid(lambda grid:
                      grid[:rows - 1] == vi_rows(expected, rows, columns))

    show("vi /etc/rows.txt\r", lines)
    vi_replaces_rows(show, lines)
    vdp.type(":q!\r")
    vdp.expect(PROMPT)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_vi_writes_only_the_rows_that_change_or_come_into_view(s):
    lines = ["line%02d" % n for n in range(1, 41)]
    s.restart_with_file("/etc/ret.txt", "".join(l + "\n" for l in lines))
    s.login()
    vi = ViTerminal(s, "vi /etc/ret.txt")
    vi.wait_lines(vi_lines(lines[:23]))

    def written(keys, expected):
        vi.settle()
        start = len(vi.output)
        vi.keys(keys)
        vi.wait_lines(expected)
        vi.settle()
        return vi.output[start:]

    vi.keys("10G")
    redraw = written("\x0c", vi_lines(lines[:23]))
    vi.keys("A")
    vi.wait_status("-- INSERT --")
    lines[10:10] = [""]
    split = written("\r", vi_lines(lines[:23]))
    assert re.findall(r"line\d\d", split) == ["line10"], repr(split)
    assert len(split) * 3 < len(redraw), (len(split), len(redraw))
    vi.keys("\x1b")
    vi.wait_status("")
    del lines[10]
    undo = written("u", vi_lines(lines[:23]))
    assert re.findall(r"line\d\d", undo) == ["line10", "line23"], repr(undo)
    assert len(undo) * 3 < len(redraw), (len(undo), len(redraw))
    vi.keys("23G")
    scroll = written("j", vi_lines(lines[1:24]))
    assert re.findall(r"line\d\d", scroll) == ["line24"], repr(scroll)
    vi.keys("A")
    vi.wait_status("-- INSERT --")
    lines[24:24] = [""]
    split = written("\r", vi_lines(lines[2:25]))
    assert re.findall(r"line\d\d", split) == ["line24"], repr(split)
    assert len(split) * 3 < len(redraw), (len(split), len(redraw))
    vi.keys("\x1b:q!\r")
    vi.wait_exit()


def test_vi_creates_a_file(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/new.txt")
    vi.wait_status('"/etc/new.txt" [New File]')
    vi.wait_lines(vi_lines([""]))
    vi.keys("ihello")
    vi.wait_lines(["hello"])
    assert vi.screen.line(23) == "-- INSERT --"
    vi.keys("\x1b")
    vi.wait_status("")
    vi.keys(":wq\r")
    vi.wait_exit()
    assert s.sh("cat /etc/new.txt") == "hello"


def test_vi_deletes_yanks_and_puts_lines(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/lines.txt")
    vi.wait_status('"/etc/lines.txt" [New File]')
    vi.keys("ione\rtwo\rthree\x1b")
    vi.wait_lines(vi_lines(["one", "two", "three"]))
    vi.keys("kdd")
    vi.wait_lines(vi_lines(["one", "three"]))
    vi.keys("p")
    vi.wait_lines(vi_lines(["one", "three", "two"]))
    vi.keys("kkyyjP")
    vi.wait_lines(vi_lines(["one", "one", "three", "two"]))
    vi.keys("jjx")
    vi.wait_lines(vi_lines(["one", "one", "three", "wo"]))
    vi.keys("$xx")
    vi.wait_lines(vi_lines(["one", "one", "three", ""]))
    vi.keys("dd")
    vi.wait_lines(vi_lines(["one", "one", "three"]))
    vi.keys(":w\r")
    vi.wait_status('"/etc/lines.txt" 3L, 14C written')
    vi.keys(":q\r")
    vi.wait_exit()
    assert s.sh("cat /etc/lines.txt") == "one\none\nthree"


def test_vi_splits_and_edits_lines_in_insert_mode(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/split.txt")
    vi.wait_status('"/etc/split.txt" [New File]')
    vi.keys("iabcdef\x1b")
    vi.wait_lines(["abcdef"])
    vi.keys("hhi\r\x1b")
    vi.wait_lines(vi_lines(["abc", "def"]))
    vi.keys("A\x7f\x7fX\x1bkIY\x1bOtop\x1bjoend\x1b")
    vi.wait_lines(vi_lines(["top", "Yabc", "end", "dX"]))
    vi.keys(":wq\r")
    vi.wait_exit()
    assert s.sh("cat /etc/split.txt") == "top\nYabc\nend\ndX"


def test_vi_guards_unsaved_changes(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/unsaved.txt")
    vi.wait_status('"/etc/unsaved.txt" [New File]')
    vi.keys("ix\x1b:q\r")
    vi.wait_status("No write since last change (add ! to override)")
    vi.keys(":qq\r")
    vi.wait_status("Trailing characters")
    vi.keys(":z\r")
    vi.wait_status("Not an editor command")
    vi.keys(":e\r")
    vi.wait_status("No write since last change (add ! to override)")
    vi.keys(":q!\r")
    vi.wait_exit()
    assert "unsaved.txt" not in ll_names(s, "/etc")


def test_vi_wraps_long_lines_and_expands_tabs(s):
    s.login()
    long_line = "".join(chr(ord("a") + n % 26) for n in range(100))
    vi = ViTerminal(s, "vi /etc/wrap.txt")
    vi.wait_status('"/etc/wrap.txt" [New File]')
    vi.keys("i" + long_line + "\rx\tyz\x1b")
    vi.wait_lines(vi_lines([long_line[:80], long_line[80:], "x       yz"]))
    vi.keys("k$x")
    vi.wait_lines(vi_lines([long_line[:80], long_line[80:99],
                            "x       yz"]))
    vi.keys(":q!\r")
    vi.wait_exit()


def vi_long_line(length):
    """Return a line whose text shows where each part of it is:  the index of
    every fifth character, then a |.

    @param length The length of the line, a multiple of 5.
    """
    return "".join("%04d|" % n for n in range(0, length, 5))


def test_vi_edits_a_line_longer_than_its_buffer(s):
    long_line = vi_long_line(300)
    short = "s" * 100
    lines = ["first", long_line, short, "last"]
    s.restart_with_file("/etc/long.txt", "".join(l + "\n" for l in lines))
    s.login()
    vi = ViTerminal(s, "vi /etc/long.txt")
    vi.wait_lines(vi_rows(lines, 24, 80))

    def show(keys, line, at=1, index=None):
        before = list(lines)
        lines[at] = line
        vi.keys(keys)
        expected = vi_rows(lines, 24, 80)
        cursor = (None if index is None
                  else (sum(vi_line_rows(l, 80) for l in lines[:at])
                        + index // 80, index % 80))
        vi.wait(lambda screen: (screen.lines()[:len(expected)] == expected)
                and ((cursor is None)
                     or ((screen.row, screen.column) == cursor)))
        return before

    show("jylp", long_line[0] + long_line)
    show("$x", lines[1][:-1])
    show("p", lines[1] + "|")
    show("0200lrX", lines[1][:200] + "X" + lines[1][201:], index=200)
    vi.keys("a")
    vi.wait_status("-- INSERT --")
    vi.settle()
    start = len(vi.output)
    before = show("Q", lines[1][:201] + "Q" + lines[1][201:], index=202)
    vi.settle()
    # Only the window onto the line and what follows it are drawn again.
    assert "0000|" not in vi.output[start:], repr(vi.output[start:])
    show("\x1bu", before[1])
    typed = "abcdefghij" * 15
    inserted = lines[1][:100] + typed + lines[1][100:]
    before = show("0100li%s\x1b" % typed, inserted)
    show("u", before[1])
    show("\x12", inserted)
    before = show("0250li" + "\b" * 80 + "\x1b",
                  lines[1][:170] + lines[1][250:])
    show("u", before[1])
    show("Aend\x1b", lines[1] + "end", index=len(lines[1]) + 2)
    before = show("Istart\x1b", "start" + lines[1])
    show("u", before[1])
    show("jA" + "t" * 20 + "\x1b", short + "t" * 20, 2)
    show("0x", short[1:] + "t" * 20, 2)
    whole = lines[1]
    lines[1:2] = [whole[:150], whole[150:]]
    vi.keys("k0150li\r\x1b")
    vi.wait_lines(vi_rows(lines, 24, 80))
    lines[1:3] = [whole]
    vi.keys("u")
    vi.wait_lines(vi_rows(lines, 24, 80))
    lines[1:1] = [""]
    show("$Oabove\x1b", "above")
    vi.keys(":w\r")
    vi.wait_status(vi_written("/etc/long.txt", lines))
    vi.keys(":q\r")
    vi.wait_exit()
    assert s.sh("cat /etc/long.txt", timeout=30) == "\n".join(lines)


def test_vi_repeats_and_replaces_on_a_line_longer_than_its_buffer(s):
    lines = ["first", vi_long_line(300), "last", "s" * 95]
    s.restart_with_file("/etc/long.txt", "".join(l + "\n" for l in lines))
    s.login()
    vi = ViTerminal(s, "vi /etc/long.txt")
    vi.wait_lines(vi_rows(lines, 24, 80))

    def show(keys, line, index, at=1):
        lines[at] = line
        vi.keys(keys)
        expected = vi_rows(lines, 24, 80)
        cursor = (sum(vi_line_rows(l, 80) for l in lines[:at]) + index // 80,
                  index % 80)
        vi.wait(lambda screen: (screen.lines()[:len(expected)] == expected)
                and ((screen.row, screen.column) == cursor))

    line = lines[1]
    show("j0200lix\b\x1b", line, 199)
    show("0200lhhhhhiab\x1b", line[:195] + "ab" + line[195:], 196)
    line = lines[1]
    show(".", line[:196] + "ab" + line[196:], 197)
    line = lines[1]
    end = len(line) - 1
    show("$i0123456789\x1b", line[:end] + "0123456789" + line[end:], end + 9)
    line = lines[1]
    show(".", line[:end + 9] + "0123456789" + line[end + 9:], end + 18)
    line = lines[1]
    typed = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij"
    show("0200lR%s\x1b" % typed, line[:200] + typed + line[236:], 235)
    line = lines[1]
    show("$Rxyz\x1b", line[:-1] + "xyz", len(line) + 1)
    # . puts text that doesn't fit in the line buffer in the line from the
    # text file.
    line = lines[1]
    typed = "".join(chr(ord("a") + n % 26) for n in range(60))
    show("0100li%s\x1b" % typed, line[:100] + typed + line[100:], 159)
    line = lines[1]
    show(".", line[:159] + typed + line[159:], 218)
    line = lines[1]
    show("0150lR%s\x1b" % ("Z" * 30), line[:150] + "Z" * 30 + line[180:], 179)
    line = lines[1]
    show("0250l.", line[:250] + "Z" * 30 + line[280:], 279)
    show(":3\r0iABCDEFGHIJ\x1b", "ABCDEFGHIJlast", 9, 2)
    show(":4\r0.", "ABCDEFGHIJ" + lines[3], 9, 3)
    vi.keys(":w\r")
    vi.wait_status(vi_written("/etc/long.txt", lines))
    vi.keys(":q\r")
    vi.wait_exit()
    assert s.sh("cat /etc/long.txt", timeout=30) == "\n".join(lines)


def vi_word_line(letter, length):
    """Return a line of words, punctuation, and blanks whose words are each
    unique:  the letter and the word's index, a comma, then a space.

    @param letter The letter each word starts with.
    @param length The length of the line.
    """
    return "".join("%s%04d, " % (letter, n)
                   for n in range(0, length, 7))[:length]


def vi_class(c):
    """Return the class vi's word motions give a character:  0 for a blank,
    1 for a letter, digit, or underscore, and 2 for anything else.

    @param c The character.
    """
    return 0 if c in " \t" else (1 if (c.isalnum() or c == "_") else 2)


def vi_word_motion(line, at, motion):
    """Return the index a word motion reaches within a line.

    @param line The line.
    @param at The index to move from.
    @param motion w, e, or b.
    """
    if motion == "b":
        at -= 1
        while vi_class(line[at]) == 0:
            at -= 1
        while at > 0 and vi_class(line[at - 1]) == vi_class(line[at]):
            at -= 1
        return at
    if motion == "e":
        at += 1
        while vi_class(line[at]) == 0:
            at += 1
        while (at + 1 < len(line)
               and vi_class(line[at + 1]) == vi_class(line[at])):
            at += 1
        return at
    word = vi_class(line[at])
    while at < len(line) and word != 0 and vi_class(line[at]) == word:
        at += 1
    while at < len(line) and vi_class(line[at]) == 0:
        at += 1
    return at


def test_vi_operates_across_the_window_onto_a_long_line(s):
    lines = ["first", vi_word_line("a", 112), vi_word_line("b", 200),
             "  joined", "last"]
    s.restart_with_file("/etc/words.txt", "".join(l + "\n" for l in lines))
    s.login()
    vi = ViTerminal(s, "vi /etc/words.txt")
    vi.wait_lines(vi_rows(lines, 24, 80))

    def show(keys, at, line, index):
        lines[at] = line
        vi.keys(keys)
        expected = vi_rows(lines, 24, 80)
        cursor = None if index is None else (
            sum(vi_line_rows(l, 80) for l in lines[:at]) + index // 80,
            index % 80)
        vi.wait(lambda screen: (screen.lines()[:len(expected)] == expected)
                and ((cursor is None)
                     or ((screen.row, screen.column) == cursor)))

    for at in (1, 2):
        line = lines[at]
        for p in (3, len(line) // 2, len(line) - 9):
            go = ":%d\r0%dl" % (at + 1, p)
            for motion in "web":
                show(go + motion, at, line,
                     vi_word_motion(line, p, motion))
            show(go + "$", at, line, len(line) - 1)
            # Moving to p passes through index 0, so get there first.
            show(go, at, line, p)
            show("y0", at, line, 0)
            w = vi_word_motion(line, p, "w")
            e = vi_word_motion(line, p, "e")
            show(go + "dw", at, line[:p] + line[w:], p)
            show("u", at, line, None)
            show(go + "cwXY\x1b", at, line[:p] + "XY" + line[e + 1:], p + 1)
            show("u", at, line, None)
            show(go + "y$0P", at, line[p:] + line, len(line) - p - 1)
            show("u", at, line, None)
            show(go + "D", at, line[:p], p - 1)
            show("u", at, line, None)
            show(go + "d0", at, line[p:], 0)
            show("u", at, line, None)
            show(go + "ylp", at, line[:p + 1] + line[p:], p + 1)
            show("u", at, line, None)
            show(go + "x.", at, line[:p] + line[p + 2:], p)
            show("uu", at, line, None)
        late = len(line) - 20
        late -= late % 7
        show("%s/%s\r" % (go, line[late:late + 5]), at, line, late)
        # The search reads the line in pieces, the first ending in this word.
        show("%s?%s\r" % (go, line[84:89]), at, line, 84)
        following = lines.pop(at + 1)
        show(":%d\rJ" % (at + 1), at, line + " " + following.lstrip(),
             len(line))
        lines.insert(at + 1, following)
        show("u", at, line, None)
    show(":3\r0b", 1, lines[1], 110)
    show(":5\r0ld9l", 4, "l", 0)
    show("u", 4, "last", None)
    show(":1\r/b0175\r", 2, lines[2], 175)
    vi.keys(":q!\r")
    vi.wait_exit()


def test_vi_operates_on_a_line_thousands_of_characters_long(s):
    line = vi_word_line("c", 3000)
    lines = ["first", line, "  joined", "last"]
    s.restart_with_file("/etc/many.txt", "".join(l + "\n" for l in lines))
    s.login()
    vi = ViTerminal(s, "vi /etc/many.txt")
    vi.wait_status('"/etc/many.txt" 4L, %dC' % (len(line) + 21))
    for p in (2990, 1500, 3):
        w = vi_word_motion(line, p, "w")
        vi.keys(":2\r0%dldw" % p)
        line = line[:p] + line[w:]
        e = vi_word_motion(line, p, "e")
        vi.keys("0%dlcwXY\x1b" % p)
        line = line[:p] + "XY" + line[e + 1:]
        vi.keys("0%dly$0P" % p)
        line = line[p:] + line
    found = line.index("c1400")
    vi.keys("0/c1400\rx")
    line = line[:found] + line[found + 1:]
    vi.keys("0%dlD" % (len(line) - 50))
    line = line[:len(line) - 50]
    vi.keys("J")
    lines[1:3] = [line + " joined"]
    vi.keys(":w\r")
    vi.wait_status(vi_written("/etc/many.txt", lines), timeout=60)
    vi.keys(":q\r")
    vi.wait_exit(timeout=60)
    assert s.sh("cat /etc/many.txt", timeout=30) == "\n".join(lines)


def test_vi_edits_a_line_thousands_of_characters_long(s):
    long_line = vi_long_line(3000)
    lines = ["first", long_line, "last"]
    s.restart_with_file("/etc/huge.txt", "".join(l + "\n" for l in lines))
    s.login()
    vi = ViTerminal(s, "vi /etc/huge.txt")
    vi.wait_status('"/etc/huge.txt" 3L, %dC' % (len(long_line) + 12))
    vi.keys("j$x")
    line = long_line[:-1]
    vi.keys("0x")
    line = line[1:]
    vi.keys("01500lrQ")
    line = line[:1500] + "Q" + line[1501:]
    vi.keys("02000li" + "w" * 120 + "\x1b")
    line = line[:2000] + "w" * 120 + line[2000:]
    vi.keys("A" + "z" * 60 + "\x1b")
    line += "z" * 60
    vi.keys("I<\x1b")
    line = "<" + line
    lines[1] = line
    vi.keys(":w\r")
    vi.wait_status(vi_written("/etc/huge.txt", lines), timeout=60)
    vi.keys("uuuuuu:q!\r")
    vi.wait_exit(timeout=60)
    assert s.sh("cat /etc/huge.txt", timeout=30) == "\n".join(lines)


def test_vi_scrolls_to_keep_the_cursor_on_the_screen(s):
    s.login()
    lines = ["line%02d" % n for n in range(1, 31)]
    vi = ViTerminal(s, "vi /etc/scroll.txt")
    vi.wait_status('"/etc/scroll.txt" [New File]')
    vi.keys("i" + "\r".join(lines) + "\x1b")
    vi.wait_lines(lines[7:30])
    vi.keys(":w\r")
    vi.wait_status('"/etc/scroll.txt" 30L, 210C written')
    vi.keys("k" * 29)
    vi.wait_lines(lines[0:23])
    vi.keys("j" * 23)
    vi.wait_lines(lines[1:24])
    vi.keys(":q\r")
    vi.wait_exit()


def test_vi_edits_a_file_larger_than_the_heap(s):
    s.login()
    lines = write_oversized_file(s)
    s.restart_with_memory(small_memory_size(s))
    s.login()
    vi = ViTerminal(s, "vi /etc/bigfile")
    vi.wait_status('"/etc/bigfile" %dL, %dC' % (len(lines), 8 * len(lines)))
    vi.wait_lines(lines[:23])
    vi.keys("j" * 29 + "dd")
    del lines[29]
    vi.wait_lines(lines[7:30])
    vi.keys("x:wq\r")
    lines[29] = lines[29][1:]
    vi.wait_exit()
    assert s.sh("cat /etc/bigfile", timeout=30) == "\n".join(lines)
    assert ll_names(s, "/tmp") == []


def test_vi_undoes_and_redoes_whole_changes(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/undo.txt")
    vi.wait_status('"/etc/undo.txt" [New File]')
    vi.keys("u")
    vi.wait_status("Already at oldest change")
    vi.keys("ione\rtwo\rthree\x1b")
    vi.wait_lines(vi_lines(["one", "two", "three"]))
    vi.wait_status("")
    vi.keys("u")
    vi.wait_lines(vi_lines([""]))
    vi.keys("\x12")
    vi.wait_lines(vi_lines(["one", "two", "three"]))
    vi.keys("\x12")
    vi.wait_status("Already at newest change")
    # Redo leaves the cursor where the change started, on line 1.
    vi.keys("jdd")
    vi.wait_lines(vi_lines(["one", "three"]))
    vi.keys("u")
    vi.wait_lines(vi_lines(["one", "two", "three"]))
    vi.keys("yyp")
    vi.wait_lines(vi_lines(["one", "two", "two", "three"]))
    vi.keys("u")
    vi.wait_lines(vi_lines(["one", "two", "three"]))
    vi.keys("0x")
    vi.wait_lines(vi_lines(["one", "wo", "three"]))
    vi.keys("u")
    vi.wait_lines(vi_lines(["one", "two", "three"]))
    vi.keys("\x12")
    vi.wait_lines(vi_lines(["one", "wo", "three"]))
    vi.keys(":wq\r")
    vi.wait_exit()
    assert s.sh("cat /etc/undo.txt") == "one\nwo\nthree"


def vi_file(s, path, lines):
    """Write a file for vi to edit.

    @param s The Session, logged in.
    @param path The file's path.
    @param lines The file's lines.
    """
    size = sum(len(line) + 1 for line in lines)
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(lines)
               + [(".", ""), ("w " + path, "%d\n" % size), ("q", "")])


def test_vi_moves_by_words_lines_and_counts(s):
    s.login()
    vi_file(s, "/etc/words.txt", ["one two three four", "five six", "seven"])
    vi = ViTerminal(s, "vi /etc/words.txt")
    vi.wait_status('"/etc/words.txt" 3L, 34C')
    vi.keys("wx")
    vi.wait_lines(vi_lines(["one wo three four", "five six", "seven"]))
    vi.keys("2wx")
    vi.wait_lines(vi_lines(["one wo three our", "five six", "seven"]))
    vi.keys("bx")
    vi.wait_lines(vi_lines(["one wo hree our", "five six", "seven"]))
    vi.keys("ex")
    vi.wait_lines(vi_lines(["one wo hre our", "five six", "seven"]))
    vi.keys("0x$x")
    vi.wait_lines(vi_lines(["ne wo hre ou", "five six", "seven"]))
    vi.keys("wx")
    vi.wait_lines(vi_lines(["ne wo hre ou", "ive six", "seven"]))
    vi.keys("3x")
    vi.wait_lines(vi_lines(["ne wo hre ou", " six", "seven"]))
    vi.keys("^x")
    vi.wait_lines(vi_lines(["ne wo hre ou", " ix", "seven"]))
    vi.keys("Gx")
    vi.wait_lines(vi_lines(["ne wo hre ou", " ix", "even"]))
    vi.keys("ggx")
    vi.wait_lines(vi_lines(["e wo hre ou", " ix", "even"]))
    vi.keys("2Gx")
    vi.wait_lines(vi_lines(["e wo hre ou", " x", "even"]))
    vi.keys("Lx")
    vi.wait_lines(vi_lines(["e wo hre ou", " x", "ven"]))
    vi.keys("Hx")
    vi.wait_lines(vi_lines([" wo hre ou", " x", "ven"]))
    vi.keys("\rx-X")
    vi.wait_lines(vi_lines(["wo hre ou", "", "ven"]))
    vi.keys(":q!\r")
    vi.wait_exit()


def test_vi_runs_operators_over_motions(s):
    s.login()
    vi_file(s, "/etc/ops.txt",
            ["alpha beta gamma", "delta epsilon", "zeta eta", "theta"])
    vi = ViTerminal(s, "vi /etc/ops.txt")
    vi.wait_status('"/etc/ops.txt" 4L, 46C')
    vi.keys("dw")
    vi.wait_lines(vi_lines(["beta gamma", "delta epsilon", "zeta eta",
                            "theta"]))
    # cw changes only the word, not the blank after it.
    vi.keys("cwBETA\x1b")
    vi.wait_lines(vi_lines(["BETA gamma", "delta epsilon", "zeta eta",
                            "theta"]))
    vi.keys("jD")
    vi.wait_lines(vi_lines(["BETA gamma", "del", "zeta eta", "theta"]))
    vi.keys("0ye$p")
    vi.wait_lines(vi_lines(["BETA gamma", "deldel", "zeta eta", "theta"]))
    vi.keys("jdj")
    vi.wait_lines(vi_lines(["BETA gamma", "deldel"]))
    vi.keys("kyjGp")
    vi.wait_lines(vi_lines(["BETA gamma", "deldel", "BETA gamma",
                            "deldel"]))
    vi.keys("3GccNEW\x1b")
    vi.wait_lines(vi_lines(["BETA gamma", "deldel", "NEW", "deldel"]))
    vi.keys("0C new\x1b")
    vi.wait_lines(vi_lines(["BETA gamma", "deldel", " new", "deldel"]))
    vi.keys("ggdG")
    vi.wait_lines(vi_lines([""]))
    vi.keys("u")
    vi.wait_lines(vi_lines(["BETA gamma", "deldel", " new", "deldel"]))
    vi.keys("3G2dd")
    vi.wait_lines(vi_lines(["BETA gamma", "deldel"]))
    vi.keys(":wq\r")
    vi.wait_exit()
    assert s.sh("cat /etc/ops.txt") == "BETA gamma\ndeldel"


def test_vi_repeats_puts_and_operators_with_counts(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/counts.txt")
    vi.wait_status('"/etc/counts.txt" [New File]')
    vi.keys("iab\x1b")
    vi.wait_lines(vi_lines(["ab"]))
    vi.keys("yl3p")
    vi.wait_lines(vi_lines(["abbbb"]))
    vi.keys("yy2p")
    vi.wait_lines(vi_lines(["abbbb", "abbbb", "abbbb"]))
    vi.keys("2yyP")
    vi.wait_lines(vi_lines(["abbbb", "abbbb", "abbbb", "abbbb", "abbbb"]))
    vi.keys("ggd2j")
    vi.wait_lines(vi_lines(["abbbb", "abbbb"]))
    vi.keys("9dd")
    vi.wait_lines(vi_lines([""]))
    vi.keys("ixyz\x1b0y$2P")
    vi.wait_lines(vi_lines(["xyzxyzxyz"]))
    vi.keys(":q!\r")
    vi.wait_exit()


def test_vi_searches_marks_replaces_and_joins(s):
    s.login()
    vi_file(s, "/etc/find.txt", ["apple", "banana", "cherry", "banana split"])
    vi = ViTerminal(s, "vi /etc/find.txt")
    vi.wait_status('"/etc/find.txt" 4L, 33C')
    vi.keys("n")
    vi.wait_status("No previous search pattern")
    vi.keys("/ana\r")
    vi.keys("x")
    vi.wait_lines(vi_lines(["apple", "bnana", "cherry", "banana split"]))
    vi.keys("nrX")
    vi.wait_lines(vi_lines(["apple", "bnXna", "cherry", "banana split"]))
    vi.keys("nx")
    vi.wait_lines(vi_lines(["apple", "bnXna", "cherry", "bnana split"]))
    vi.keys("?err\rx")
    vi.wait_lines(vi_lines(["apple", "bnXna", "chrry", "bnana split"]))
    vi.keys("/zzz\r")
    vi.wait_status("Pattern not found")
    vi.keys("ma'b")
    vi.wait_status("Mark not set")
    vi.keys("gg'aJ")
    vi.wait_lines(vi_lines(["apple", "bnXna", "chrry bnana split"]))
    vi.keys("gg2J")
    vi.wait_lines(vi_lines(["apple bnXna", "chrry bnana split"]))
    # ? searches backward, and N the other way.
    vi.keys("G?a\rx")
    vi.wait_lines(vi_lines(["apple bnXn", "chrry bnana split"]))
    vi.keys("Nx")
    vi.wait_lines(vi_lines(["apple bnXn", "chrry bnna split"]))
    vi.keys("ZZ")
    vi.wait_exit()
    assert s.sh("cat /etc/find.txt") == "apple bnXn\nchrry bnna split"


def test_vi_runs_ex_commands(s):
    s.login()
    vi_file(s, "/etc/exa.txt", ["one", "two", "three"])
    vi_file(s, "/etc/exb.txt", ["four", "five"])
    vi = ViTerminal(s, "vi /etc/exa.txt")
    vi.wait_status('"/etc/exa.txt" 3L, 14C')
    vi.keys(":2\rx")
    vi.wait_lines(vi_lines(["one", "wo", "three"]))
    vi.keys(":1t$\r")
    vi.wait_lines(vi_lines(["one", "wo", "three", "one"]))
    vi.keys(":1,2d\r")
    vi.wait_lines(vi_lines(["three", "one"]))
    vi.keys(":r /etc/exb.txt\r")
    vi.wait_lines(vi_lines(["three", "four", "five", "one"]))
    vi.keys("x")
    vi.wait_lines(vi_lines(["three", "four", "ive", "one"]))
    vi.keys(":1,2j\r")
    vi.wait_lines(vi_lines(["threefour", "ive", "one"]))
    vi.keys(":x\r")
    vi.wait_exit()
    assert s.sh("cat /etc/exa.txt") == "threefour\nive\none"
    vi = ViTerminal(s, "vi /etc/exa.txt")
    vi.wait_status('"/etc/exa.txt" 3L, 18C')
    vi.keys("x:e /etc/exb.txt\r")
    vi.wait_status("No write since last change (add ! to override)")
    vi.keys(":e! /etc/exb.txt\r")
    vi.wait_status('"/etc/exb.txt" 2L, 10C')
    vi.wait_lines(vi_lines(["four", "five"]))
    vi.keys("p")
    vi.keys("Gx:x\r")
    vi.wait_exit()
    assert s.sh("cat /etc/exb.txt") == "four\nive"
    assert s.sh("cat /etc/exa.txt") == "threefour\nive\none"


def test_vi_repeats_the_last_change_with_dot(s):
    s.login()
    vi_file(s, "/etc/dot.txt", ["one two three", "four five", "six", "seven"])
    vi = ViTerminal(s, "vi /etc/dot.txt")
    vi.wait_status('"/etc/dot.txt" 4L, 34C')
    vi.keys("dw.")
    vi.wait_lines(vi_lines(["three", "four five", "six", "seven"]))
    vi.keys("j2x.")
    vi.wait_lines(vi_lines(["three", " five", "six", "seven"]))
    # A count given to . replaces the change's count for later repeats too.
    vi.keys("1..")
    vi.wait_lines(vi_lines(["three", "ive", "six", "seven"]))
    vi.keys("jA!\x1bj.")
    vi.wait_lines(vi_lines(["three", "ive", "six!", "seven!"]))
    vi.keys("ggcwONE\x1bj0.")
    vi.wait_lines(vi_lines(["ONE", "ONE", "six!", "seven!"]))
    vi.keys("Gonew\x1b.")
    vi.wait_lines(vi_lines(["ONE", "ONE", "six!", "seven!", "new", "new"]))
    # Text inserted across a Return can't be repeated.
    vi.keys("ia\rb\x1b.")
    vi.wait_lines(vi_lines(["ONE", "ONE", "six!", "seven!", "new", "nea",
                            "bw"]))
    vi.keys(":wq\r")
    vi.wait_exit()
    assert s.sh("cat /etc/dot.txt") == \
        "ONE\nONE\nsix!\nseven!\nnew\nnea\nbw"


def test_vi_keeps_keys_typed_before_it_reads_them(s):
    s.login()
    alphabet = "abcdefghijklmnopqrstuvwxyz"
    # Everything is typed before vi has started, so it arrives in one batch.
    s.child.delaybeforesend = None
    s.child.send("vi /etc/ahead.txt\ri" + alphabet * 2 + "\x1b0x:wq\r")
    s.child.expect(PROMPT, timeout=30)
    assert s.sh("cat /etc/ahead.txt") == (alphabet * 2)[1:]


def test_vi_keeps_editing_after_an_ex_command_empties_the_buffer(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/empty.txt")
    vi.wait_status('"/etc/empty.txt" [New File]')
    vi.keys("ia\rb\rc\x1b")
    vi.wait_lines(vi_lines(["a", "b", "c"]))
    vi.keys(":1,$d\r")
    vi.wait_lines(vi_lines([""]))
    vi.keys("u")
    vi.wait_lines(vi_lines(["a", "b", "c"]))
    vi.keys(":1,3d\r")
    vi.wait_lines(vi_lines([""]))
    vi.keys("ix\ry\x1b")
    vi.wait_lines(vi_lines(["x", "y"]))
    vi.keys(":wq\r")
    vi.wait_exit()
    assert s.sh("cat /etc/empty.txt") == "x\ny"


def test_vi_replaces_text_with_R(s):
    s.login()
    vi_file(s, "/etc/replace.txt", ["abcdef", "ghijkl"])
    vi = ViTerminal(s, "vi /etc/replace.txt")
    vi.wait_status('"/etc/replace.txt" 2L, 14C')
    vi.keys("Rx")
    vi.wait_status("-- REPLACE --")
    vi.keys("y\x1b")
    vi.wait_lines(vi_lines(["xycdef", "ghijkl"]))
    vi.keys("j0.")
    vi.wait_lines(vi_lines(["xycdef", "xyijkl"]))
    # Past the end of the line, R appends.
    vi.keys("k$Rpq\x1b")
    vi.wait_lines(vi_lines(["xycdepq", "xyijkl"]))
    # Backspace only moves back, and makes the change one . can't repeat.
    vi.keys("0lRm\bn\x1b")
    vi.wait_lines(vi_lines(["xncdepq", "xyijkl"]))
    vi.keys("j$.0x")
    vi.wait_lines(vi_lines(["xncdepq", "yijkl"]))
    # Return splits the line, and replacing carries on on the new one.
    vi.keys("0Ra\rb\x1b")
    vi.wait_lines(vi_lines(["xncdepq", "a", "bjkl"]))
    vi.keys(":wq\r")
    vi.wait_exit()
    assert s.sh("cat /etc/replace.txt") == "xncdepq\na\nbjkl"


def test_vi_inserts_a_control_character_typed_after_ctrl_v(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/ctrlv.txt")
    vi.wait_status('"/etc/ctrlv.txt" [New File]')
    vi.keys("ia\x16\x01b\x16\x1bc\x1b")
    vi.wait_lines(vi_lines(["a^Ab^[c"]))
    vi.keys(":wq\r")
    vi.wait_exit()
    assert s.sh("cat /etc/ctrlv.txt") == "a\x01b\x1bc"


def test_vi_pages_and_moves_within_the_screen(s):
    s.login()
    lines = ["line%02d" % n for n in range(1, 61)]
    vi = ViTerminal(s, "vi /etc/pages.txt")
    vi.wait_status('"/etc/pages.txt" [New File]')
    vi.keys("i" + "\r".join(lines) + "\x1b")
    vi.wait_lines(lines[37:60])
    vi.keys("gg")
    vi.wait_lines(lines[0:23])
    vi.keys("\x06")
    vi.wait_lines(lines[22:45])
    vi.keys("Mx")
    vi.wait_lines(lines[22:33] + ["ine34"] + lines[34:45])
    vi.keys("\x02")
    vi.wait_lines(lines[0:23])
    vi.keys("Lx")
    vi.wait_lines(lines[0:22] + ["ine23"])
    vi.keys(":q!\r")
    vi.wait_exit()


def test_vi_redraws_for_a_resize_report(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/resize.txt")
    vi.wait_status('"/etc/resize.txt" [New File]')
    vi.keys("iabcdefghij\x1b")
    vi.wait_lines(["abcdefghij"])
    # A report sent within the escape delay would join the Escape key's input.
    vi.wait_status("")
    vi.keys("\x1b[48;6;4;0;0t")
    vi.wait_lines(["abcd", "efgh", "ij", "~", "~"])
    vi.keys("A")
    vi.wait(lambda screen: screen.line(5) == "-- INSERT --")
    vi.keys("k")
    vi.wait_lines(["abcd", "efgh", "ijk"])
    vi.keys("\x1b[48;24;80;0;0t")
    vi.wait(lambda screen: screen.line(23) == "-- INSERT --")
    vi.wait_lines(vi_lines(["abcdefghijk"]))
    vi.keys("l")
    vi.wait_lines(vi_lines(["abcdefghijkl"]))
    vi.keys("\x1b:q!\r")
    vi.wait_exit()


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


def passwdChecksum(user, password):
    """Return the password field login accepts for user and password."""
    return str(sum(map(ord, user + password)))


PASSWD_FILE = (
    "bad:1:x:0:Bad UID:/root:/usr/bin/mush\n"
    "root:%s:0:0:Root User:/root:/usr/bin/mush\n"
    "big:1:7:7:%s:/root:/usr/bin/mush\n"
    "alice:%s:5:5:Alice:/root:/usr/bin/mush\n"
) % (passwdChecksum("root", "toor"), "g" * 100,
     passwdChecksum("alice", "wonderland"))


def test_login_uses_the_passwd_file_when_there_is_one(s):
    s.restart_with_file("/etc/passwd", PASSWD_FILE)
    s.login("alice", "wonderland")
    user, _ = promptParts(s)
    assert user == "alice", f"prompt user is {user!r}"
    out = s.sh("ps")
    assert re.search(r"^\d+  alice ps$", out, re.M), out
    assert s.sh("echo back") == "back"
    user, _ = promptParts(s)
    assert user == "alice", f"prompt user after a command is {user!r}"


def test_passwd_line_with_a_bad_uid_matches_no_user(s):
    s.restart_with_file("/etc/passwd", PASSWD_FILE)
    s.restart_with_config_sys("CONSOLE=UART1 /usr/bin/ps\n")
    s.child.expect(r"\d+  (\S+) ps\r*\n", timeout=15)
    assert s.child.match.group(1) == "unknown", s.child.match.group(1)


def test_passwd_file_replaces_the_built_in_users(s):
    s.restart_with_file("/etc/passwd", PASSWD_FILE)
    for user, password in (("root", "rootroot"), ("user1", "user1user1")):
        s.wait_login_prompt()
        s.child.sendline(user)
        s.child.expect("password: ", timeout=10)
        s.child.sendline(password)
        s.child.expect("Login failed!", timeout=10)
    s.login("root", "toor")


def test_shipped_passwd_file_matches_the_built_in_users(s):
    with open(os.path.join(REPO_ROOT, "etc", "passwd")) as passwd:
        s.restart_with_file("/etc/passwd", passwd.read())
    for user in ("root", "user1", "user2"):
        s.login(user, user * 2)
        assert promptParts(s)[0] == user
        s.child.sendline("exit")


def test_passwd_entry_too_long_for_the_buffer_is_an_error(s):
    s.restart_with_file("/etc/passwd", PASSWD_FILE)
    s.wait_login_prompt()
    s.child.sendline("big")
    s.child.expect("getpwnam_r returned status (\\d+)", timeout=10)
    assert s.child.match.group(1) == "16", s.child.match.group(1)


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


PROFILE = "/root/.mush_profile"


def test_login_runs_the_profile_once(s):
    # Every foreground command restarts the shell, so a profile that ran again
    # would undo the cd made after it.  The export after echo shows the profile
    # resumed after the command replaced the shell.
    s.restart_with_file(PROFILE, "# Set up.\n\n  cd /usr\necho from-profile\n"
                        "export OLDPWD=/tmp\n")
    s.login()
    out = s.child.before.replace("\r", "")
    assert out.split("Login successful!\n", 1)[1].strip("\n") \
        == "from-profile", out
    assert promptParts(s) == ("root", "/usr")
    assert s.sh("echo restarted") == "restarted"
    assert s.sh("cd -") == ""
    assert promptParts(s) == ("root", "/tmp")
    assert s.sh("cd /etc") == ""
    assert s.sh("echo restarted") == "restarted"
    assert help_text(s, "mush --help").splitlines()[0] == "Usage: mush [script]"
    assert promptParts(s) == ("root", "/etc")


def test_profile_skips_a_line_that_is_too_long(s):
    long_line = "export LONG=" + "x" * 100
    s.restart_with_file(PROFILE, "\n".join([
        "echo one", long_line, "export NAME", "cd /usr", ""]))
    s.login()
    out = s.child.before.replace("\r", "")
    assert out.split("Login successful!\n", 1)[1].strip("\n").splitlines() == [
        "one",
        PROFILE + ": line 2: too long",
        "Usage: export NAME=value"], out
    assert promptParts(s) == ("root", "/usr")


def write_files(s, files):
    """Write files into the session's image, boot again, and log in as root.

    @param s The Session.
    @param files A dict of absolute path to file contents.
    """
    if s.child.isalive():
        s.child.terminate(force=True)
    for path, text in files.items():
        subprocess.run(["mmd", "-i", s.img_copy + "@@2097152",
                        "::" + os.path.dirname(path)],
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"),
                       capture_output=True)
        subprocess.run(["mcopy", "-o", "-i", s.img_copy + "@@2097152", "-",
                        "::" + path], input=text.encode(),
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)
    s.spawn([s.img_copy])
    s.login()


GREET = "echo hello\ncd /etc\nexport OLDPWD=/tmp\necho world\npwd\n"


def test_script_runs_by_name_path_and_mush(s):
    # Each echo replaces the shell, so every later line runs in a restarted
    # shell that has to find its place in the script again.
    write_files(s, {"/root/greet": GREET})
    home = promptParts(s)[1]
    assert s.sh("cd /usr") == ""
    for command in ("/root/greet", "../root/greet", "mush /root/greet"):
        assert s.sh(command) == "hello\nworld\n/etc", command
        assert promptParts(s) == ("root", "/usr"), command
    assert s.sh("cd") == ""
    assert s.sh("./greet") == "hello\nworld\n/etc"
    assert s.sh("export PATH=/usr/bin:" + home) == ""
    assert s.sh("greet") == "hello\nworld\n/etc"
    assert promptParts(s) == ("root", home)
    assert s.sh("cd -") == ""
    assert promptParts(s) == ("root", "/usr"), "the script's export leaked"


def test_script_runs_scripts_inside_it(s):
    write_files(s, {
        "/root/outer": "echo outer-start\n./inner\nmush inner\necho outer-end\n",
        "/root/inner": "echo inner\nexit\necho not-reached\n",
        "/root/again": "echo level\n./again\necho unwound\n",
    })
    assert s.sh("./outer") == "outer-start\ninner\ninner\nouter-end"
    assert s.sh("./again") == "\n".join(
        ["level"] * 9
        + ["/root/again: Too many scripts running inside each other"]
        + ["unwound"] * 9)
    assert s.sh("echo alive") == "alive"


def test_script_refuses_the_background_and_missing_files(s):
    write_files(s, {"/root/greet": GREET})
    for command in ("./greet &", "echo a | ./greet"):
        assert s.sh(command) == \
            "/root/greet: A script can't run in the background or in a pipeline"
    assert s.sh("mush missing") == "missing: No such entry found"
    assert s.sh("./missing") == "./missing: command not found"
    assert promptParts(s)[1] == "/root"
    assert s.sh("echo alive") == "alive"


def test_profile_adds_a_directory_to_the_path(s):
    write_files(s, {PROFILE: "export PATH=${PATH}:${HOME}/bin\n",
                    "/root/bin/hello": "echo hello from bin\n"})
    assert s.sh("echo $PATH") == "/usr/bin:/root/bin"
    assert s.sh("hello") == "hello from bin"
    assert s.sh("echo $?") == "0"


def test_variables_expand_in_command_lines(s):
    s.login()
    home = promptParts(s)[1]
    assert s.sh("echo $HOME ${USER} x${USER}y $UNSET- \\$HOME ${HOME $") \
        == home + " root xrooty - $HOME ${HOME $"
    assert s.sh("export GREETING=hi") == ""
    assert s.sh("echo ${GREETING}there") == "hithere"
    assert s.sh("$UNSET") == ""
    assert s.sh("cd ${HOME}/../etc") == ""
    assert promptParts(s) == ("root", "/etc")


def test_exit_status_reports_the_last_command(s):
    s.login()
    for command, status in (("echo ok", "0"), ("cat /missing", "1"),
                            ("nosuch", "127"), ("export", "1"),
                            ("pwd", "0")):
        s.sh(command)
        assert s.sh("echo $?") == status, command
    help_text(s, "mush --help")
    assert s.sh("echo ${?}") == "0"
    s.child.sendline("tightLoop")
    time.sleep(0.5)
    s.child.sendcontrol("c")
    s.child.expect(PROMPT, timeout=10)
    assert s.sh("echo $?") == "130"


def test_script_sees_the_exit_status_of_its_commands(s):
    write_files(s, {"/root/status": "cat /missing\necho status=$?\n"
                                    "nosuch\necho status=$?\n"})
    assert s.sh("./status").splitlines()[1:] == [
        "status=1", "nosuch: command not found", "status=127"]


def test_export_sets_a_variable_later_commands_inherit(s):
    s.login()
    for command in ("export", "export =x", "export HOME =/tmp", "export HOME"):
        assert s.sh(command) == "Usage: export NAME=value", command
    assert s.sh("export HOME=/tmp  ") == ""
    assert s.sh("echo restarted") == "restarted"
    assert s.sh("cd") == ""
    assert promptParts(s) == ("root", "/tmp")


def test_ps_lists_kernel_processes(s):
    s.login()
    out = s.sh("ps")
    assert "console" in out, out
    assert "memory manager" in out, out
    assert "ps" in out, out


def test_echo_roundtrips_argument(s):
    s.login()
    assert "hello-nanoos" in s.sh("echo hello-nanoos")


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


def test_color_sets_the_colors_and_clears_the_screen(s):
    s.login()
    assert s.sh("color 1e") == "\x1b[44;93m\x1b[2J\x1b[H"
    assert s.sh("color 1e") == "\x1b[44;93m\x1b[2J\x1b[H"
    assert s.sh("color 7") == "\x1b[40;37m\x1b[2J\x1b[H"
    assert s.sh("color F0") == "\x1b[107;30m\x1b[2J\x1b[H"
    assert s.sh("color c6") == "\x1b[101;33m\x1b[2J\x1b[H"


def test_color_without_a_valid_attribute_prints_its_usage(s):
    s.login()
    usage = help_text(s, "color --help")
    for command in ("color", "color 1g", "color 123", "color 1e 2"):
        assert s.sh(command) == usage, command
    assert s.sh("clear") == "\x1b[40;97m\x1b[2J\x1b[H"


def test_clear_uses_the_colors_set_until_getty_resets_them(s):
    s.login()
    assert s.sh("clear") == "\x1b[40;97m\x1b[2J\x1b[H"
    s.sh("color 2f")
    assert s.sh("clear") == "\x1b[42;97m\x1b[2J\x1b[H"
    s.child.sendline("exit")
    s.wait_login_prompt()
    assert "\x1b[40;97m\x1b[2J\x1b[H" in s.child.before, \
        repr(s.child.before[-120:])
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("rootroot")
    s.child.expect(PROMPT, timeout=10)
    assert s.sh("clear") == "\x1b[40;97m\x1b[2J\x1b[H"
    assert s.sh("clear extra") == help_text(s, "clear --help")


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
    # Typing with no delay makes every run read input that ed hasn't asked for.
    s.child.delaybeforesend = None
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


def test_ed_refuses_marks_that_are_not_set(s):
    s.login()
    ed_session(s, [
        ("ed /etc/issue", "13\n"), ("2ka", ""), ("'bp", "?\n"),
        ("h", "invalid mark\n"), ("'a=", "2\n"),
        ("e /etc/hostname", "7\n"), ("'a=", "?\n"),
        ("h", "invalid mark\n"), ("q", "")])
    assert ll_names(s, "/tmp") == []


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


def test_ed_counts_each_change_as_unsaved(s):
    s.login()
    for command, output in (("1t1", ""), ("1m2", ""), ("1,2j", ""),
                            ("r /etc/hostname", "7\n")):
        ed_session(s, [
            ("ed /etc/issue", "13\n"), (command, output),
            ("q", "?\n"), ("h", "warning: buffer modified\n"), ("Q", "")])
    assert ll_names(s, "/tmp") == []


def test_ed_undoes_and_redoes_each_change(s):
    s.login()
    ed_session(s, [
        ("ed", ""), ("u", "?\n"), ("h", "nothing to undo\n"),
        ("a", "")] + ed_input(["one", "two", "three", "four"]) + [
        (".", ""), ("u", ""), ("$=", "0\n"), ("u", ""), ("$=", "4\n"),
        ("2,3d", ""), (",p", "one\nfour\n"), ("u", ""),
        (",p", "one\ntwo\nthree\nfour\n"), ("u", ""), (",p", "one\nfour\n"),
        ("u", ""), ("1,2j", ""), (",p", "onetwo\nthree\nfour\n"), ("u", ""),
        (",p", "one\ntwo\nthree\nfour\n"), ("1,2m$", ""),
        (",p", "three\nfour\none\ntwo\n"), ("u", ""),
        (",p", "one\ntwo\nthree\nfour\n"), ("3,4t0", ""),
        (",p", "three\nfour\none\ntwo\nthree\nfour\n"), ("u", ""),
        (",p", "one\ntwo\nthree\nfour\n"),
        ("2r /etc/hostname", "7\n"), (",p", "one\ntwo\nnanoe2e\nthree\nfour\n"),
        ("u", ""), (",p", "one\ntwo\nthree\nfour\n"), ("Q", "")])
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


# More lines than a table of them in memory could hold at SMALL_MEMORY_HEAP.
OVERSIZED_LINE_COUNT = 300


def write_oversized_file(s):
    """Write /etc/bigfile with OVERSIZED_LINE_COUNT lines, using ed.

    @param s The Session, logged in.

    @return The file's lines.
    """
    big_lines = ["big-%03d" % n for n in range(1, OVERSIZED_LINE_COUNT + 1)]
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(big_lines)
               + [(".", ""), ("w /etc/bigfile", "%d\n" % (8 * len(big_lines))),
                  ("q", "")])
    return big_lines


def test_ed_edits_a_file_larger_than_the_heap(s):
    s.login()
    lines = write_oversized_file(s)
    s.restart_with_memory(small_memory_size(s))
    s.login()
    del lines[0:10]
    lines[0:0] = lines[99:110]
    del lines[110:121]
    ed_session(s, [
        ("ed /etc/bigfile", "%d\n" % (8 * OVERSIZED_LINE_COUNT)),
        ("$=", "%d\n" % OVERSIZED_LINE_COUNT), ("150p", "big-150\n"),
        ("1,10d", ""), ("$=", "%d\n" % (OVERSIZED_LINE_COUNT - 10)),
        ("100,110m0", ""), ("1p", lines[0] + "\n"), ("$p", lines[-1] + "\n"),
        ("w", "%d\n" % (8 * len(lines))), ("q", "")])
    assert s.sh("cat /etc/bigfile", timeout=30) == "\n".join(lines)
    assert ll_names(s, "/tmp") == []


def test_ed_keeps_typed_text_larger_than_the_heap(s):
    s.restart_with_memory(small_memory_size(s))
    s.login()
    typed = ["t%03d" % n for n in range(1, OVERSIZED_LINE_COUNT + 1)]
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(typed)
               + [(".", ""), ("$=", "%d\n" % OVERSIZED_LINE_COUNT),
                  ("150p", "t150\n"), ("Q", "")])
    assert s.sh("echo still-alive") == "still-alive"
    assert ll_names(s, "/tmp") == []


def test_ed_reads_a_file_larger_than_the_heap_into_the_buffer(s):
    s.login()
    write_oversized_file(s)
    s.restart_with_memory(small_memory_size(s))
    s.login()
    ed_session(s, [
        ("ed /etc/issue", "13\n"),
        ("r /etc/bigfile", "%d\n" % (8 * OVERSIZED_LINE_COUNT)),
        ("$=", "%d\n" % (OVERSIZED_LINE_COUNT + 2)), ("3p", "big-001\n"),
        ("$p", "big-%03d\n" % OVERSIZED_LINE_COUNT), ("Q", "")])
    assert ll_names(s, "/tmp") == []


def test_ed_quits_when_too_little_memory_is_left_to_edit(s):
    s.restart_with_memory(small_memory_size(s))
    s.login()
    ed_session(s, [("ed /etc/catfile", "320\n"), ("Q", "")])
    assert s.sh("echo still-alive") == "still-alive"
    assert ll_names(s, "/tmp") == []


def test_pipe_into_a_missing_command_leaves_the_shell_reading_the_console(s):
    s.login()
    assert s.sh("echo a | nosuch") == "nosuch: command not found"
    assert s.sh("echo alive") == "alive"


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


def test_background_output_reaches_the_uart_console(s):
    # The typed line is echoed, so only the expanded output reads bg-output.
    s.login()
    assert s.sh("export X=put") == ""
    s.child.sendline("echo bg-out$X &")
    s.child.expect_exact("echo bg-out$X &", timeout=10)
    s.child.expect(r"bg-output\r*\n", timeout=10)
    assert "non-owning" not in s.child.before
    assert s.sh("echo alive") == "alive"


def test_logout_silences_background_jobs(s):
    s.login()
    s.child.sendline("looseLoop tick &")
    s.child.expect_exact("looseLoop tick &", timeout=10)
    s.child.expect(r"\btick\r*\n", timeout=10)
    s.child.sendline("exit")
    s.wait_login_prompt()
    i = s.child.expect([r"\btick\r*\n|non-owning", pexpect.TIMEOUT], timeout=4)
    assert i == 1, "a background job printed after its user logged out"
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("rootroot")
    s.child.expect(PROMPT, timeout=10)
    assert "looseLoop" in s.sh("ps")


def test_kill_terminates_a_background_process(s):
    s.login()
    s.sh("looseLoop &")
    m = re.search(r"^(\d+)  root looseLoop$", s.sh("ps"), re.M)
    assert m, "looseLoop is not running"
    pid = int(m.group(1))
    # The scheduler's asynchronous "Killing process" log line can land either
    # side of the prompt.
    assert s.sh("kill %d" % pid).splitlines()[0] == \
        "Termination of process %d successful." % pid
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
        assert help_text(s, "mush --help").splitlines()[0] == "Usage: mush [script]"
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
