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

"""End-to-end tests of vdp.drv and the console it serves on a FakeVdp."""

import os
import re
import subprocess
import time

from fakevdp import (KEYCODE_CAPSLOCK, KEYCODE_NUMLOCK, KEYSTATE,
                     LED_CAPS_LOCK, LED_NUM_LOCK)
from harness import CATFILE_CONTENTS, PROMPT, VDP_CONFIG_SYS, vdp_login


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
                    "::/userspace/etc/glyphs.txt"],
                   input=b"glyphs:\x01\x0c\x16\x1f\x7f:end\n",
                   env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.clear()
    with vdp.lock:
        start = len(vdp.events)
    vdp.type("cat /etc/glyphs.txt\r")
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
        "DEVICE=/system/drivers/vdp.drv\n", "DEVICE=/system/drivers/vdp.drv --numlock=on\n"))
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

    # A restarted driver starts from /system/data/config.sys again.
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
        "DEVICE=/system/drivers/vdp.drv\n",
        "DEVICE=/system/drivers/vdp.drv --bogus --numlock=yes stray --numlock=on\n"))
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
