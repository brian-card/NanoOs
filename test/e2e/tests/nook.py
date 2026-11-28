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

"""End-to-end tests of the window manager, /system/commands/sysnook, which
nook runs:  taking and giving back the display, painting its windows,
moving and resizing them live and by outline, and what that costs."""

import os
import re
import subprocess
import time

import pexpect

import gfxrecords as gr
from fakevdp import KEYMOD_ALT_LEFT, KEYMOD_ALT_RIGHT
from harness import (PROMPT, VDP_CONFIG_SYS, heap_at_prompt,
                     message_args_reports, ps_entries, settled)
from tests.graphics import (ACQUIRE, RELEASE, assert_pixels, glyph_pixels,
                            wait_set_up)
from tests.vdp_drv import VDP_FONT_ID


NOOK_LINE = "GRAPHICS=/system/drivers/vdp.drv /system/commands/sysnook\n"
HOMES = ("/home", "/home/user1", "/home/user2")

# The layout in WmTheme.h, Wm.h and sysnook's Start.c:  a 1-pixel border, a
# 14-pixel title bar inside it, and grab zones 2 pixels either side of the
# frame's edge.
BORDER = 1
TITLE_HEIGHT = 14
FRAME_TOP = BORDER + TITLE_HEIGHT
INSET = 8
KEEP_TITLE = 32
MIN_WIDTH = 84
MIN_HEIGHT = FRAME_TOP + 16 + BORDER
CELL = 20
FRAMES = [(24, 24, 200, 150), (260, 40, 180, 130), (300, 200, 322, 256)]
BODIES = [gr.LIGHT_GRAY, gr.YELLOW, gr.WHITE]
DESKTOP = gr.CYAN
TITLE_BAR = gr.BLUE
INACTIVE_BAR = gr.LIGHT_GRAY
INACTIVE_BORDER = gr.DARK_GRAY
CELL_FILLS = [gr.GREEN, gr.RED, gr.MAGENTA, gr.BROWN]
CELL_EDGE = gr.BLACK
ROW_TEXT = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcd"


def boot_nook(session, **vdp_args):
    """Make home directories for user1 and user2, boot with vdp.drv and the
    window manager's GRAPHICS line, and log in on UART1 as root.

    @param session The session.
    @param vdp_args Arguments for FakeVdp.
    """
    if session.child.isalive():
        session.child.terminate(force=True)
    session.write_file(DEMO_SCRIPT, DEMO_COMMANDS)
    for directory in HOMES:
        subprocess.run(["mmd", "-D", "s", "-i",
                        session.img_copy + "@@2097152",
                        "::/userspace" + directory],
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
    session.restart_with_vdp(config_sys=VDP_CONFIG_SYS + NOOK_LINE, **vdp_args)
    session.login()
    wait_set_up(session.vdp)


def vdp_login_as(vdp, user, password):
    """Log in on a FakeVdp's console.

    @param vdp The FakeVdp.
    @param user The user name.
    @param password The password.
    """
    vdp.expect("login: ")
    vdp.type(user + "\r")
    vdp.expect("password: ")
    vdp.type(password + "\r")
    vdp.expect(PROMPT)


def wait_pixels(vdp, expected, timeout=30):
    """Wait until pixels of a FakeVdp's canvas have colors.

    @param vdp The FakeVdp.
    @param expected A dictionary of (x, y) to palette index.
    @param timeout Seconds to wait.
    """
    deadline = time.time() + timeout
    while True:
        actual = {point: vdp.pixel(*point) for point in expected}
        if actual == expected:
            return
        if time.time() > deadline:
            raise AssertionError({point: (actual[point], expected[point])
                                  for point in expected
                                  if actual[point] != expected[point]})
        with vdp.lock:
            vdp.lock.wait(0.1)


def corner_point(frame):
    """Return the bottom-right pixel of a frame, its border's corner and the
    middle of its bottom-right grab zone."""
    left, top, width, height = frame
    return (left + width - 1, top + height - 1)


# The three windows graphics 6's --demo drew, made by a wintest that stays
# until nook ends:  two plain ones, and window 2, whose body is a canvas of
# 192 cells.  The script is a file, since it's longer than a command line.
DEMO_SCRIPT = "/etc/nookdemo"
DEMO_COMMANDS = ("window 24,24,200,150 Window_0 face 1 0xAAAAAAFF "
                 "window 260,40,180,130 Window_1 face 2 0xFFFF55FF "
                 "canvas 1 0,0,320,240 window 300,200,322,256 Window_2 "
                 "cells 3 1 192 wait 3600000")


def run_nook(vdp, options="", demo=True, session=None):
    """Run nook on the VDP console and wait until the window manager has
    painted the screen:  the three windows a wintest on UART1 makes, or the
    desktop alone.

    @param vdp The FakeVdp.
    @param options nook's options.
    @param demo Whether to make the three windows, which needs the session.
    @param session The session.

    @return The number of context commands FakeVdp had received before.
    """
    base = len(vdp.context_events())
    line = ("nook --finish " + options).strip()
    vdp.type(line + "\r")
    assert vdp.wait_context_events(base + 1)[base:base + 1] == ACQUIRE
    wait_pixels(vdp, {(0, 0): DESKTOP, (639, 479): DESKTOP,
                      (320, 240): DESKTOP})
    if demo:
        run_client(session, "@" + DEMO_SCRIPT)
        expect_line(session, "wintest: cells 3: ok", timeout=60)
        left, top = FRAMES[2][0] + BORDER, FRAMES[2][1] + FRAME_TOP
        wait_pixels(vdp, {corner_point(FRAMES[2]): TITLE_BAR,
                          (left + 311, top + 231): cell_fill(11, 15)})
    settle(vdp)
    return base


def start_nook(session, options="", user=None, demo=True):
    """Log in on the VDP console, run nook there, and wait until the
    window manager has painted the screen.

    @param session The session.
    @param options nook's options.
    @param user (name, password) to log in as, or None for root.
    @param demo Whether to pass --demo.

    @return The number of context commands FakeVdp had received before.
    """
    vdp = session.vdp
    vdp_login_as(vdp, *(user or ("root", "rootroot")))
    return run_nook(vdp, options, demo, session)


def quit_nook(session, base):
    """Take the display back with Ctrl+Alt+Backspace, which is how sysnook is
    ended, and wait until it has been given back and the VDP console shows its
    prompt again.

    @param session The session.
    @param base The number of context commands before nook.
    """
    vdp = session.vdp
    vdp.release_display()
    events = vdp.wait_context_events(base + 3)
    assert events[base:] == ACQUIRE + RELEASE, events[base:]


def vdp_status(vdp):
    """Ask the VDP console's shell for the last command's exit status.

    @param vdp The FakeVdp.

    @return The status.
    """
    vdp.type("echo status $?\r")
    return int(vdp.expect(r"\r\nstatus (\d+)\r\n" + PROMPT).group(1))


def sysnook_pid(session):
    """Get sysnook's process ID from ps on UART1.

    @param session The session.

    @return The process ID.
    """
    match = re.search(r"^(\d+) +\S+ sysnook$", session.sh("ps"), re.M)
    assert match, "sysnook isn't in ps"
    return int(match.group(1))


def check_clean(session):
    """Check that neither FakeVdp nor the checked build reported a problem.

    @param session The session.
    """
    assert session.vdp.errors == [], session.vdp.errors
    reports = message_args_reports(session)
    assert reports == [], reports


# Step 1:  nook, the display, and the console.

def test_nook_the_revoke_key_gives_the_console_back_cleared_to_its_prompt(
        session):
    boot_nook(session)
    vdp = session.vdp
    vdp_login_as(vdp, "root", "rootroot")
    vdp.type("echo before\r")
    vdp.expect(r"\r\nbefore\r\n" + PROMPT)
    prompt = [line for line in vdp.grid_lines() if line][-1]
    base = run_nook(vdp, session=session)
    wait_pixels(vdp, {(0, 0): DESKTOP, (639, 479): DESKTOP})
    quit_nook(session, base)
    # The driver clears the screen when it takes the display back, and
    # replays only what was printed while it was owned:  nothing here.
    vdp.wait_grid(lambda lines: [line for line in lines if line] == [prompt])
    assert vdp_status(vdp) == 0
    check_clean(session)


def test_nook_leaves_the_console_drawing_text_with_its_background(session):
    boot_nook(session)
    vdp = session.vdp
    quit_nook(session, start_nook(session))
    vdp.expect(PROMPT + "$")
    vdp.type("echo abx\bc\r")
    vdp.expect(r"\r\nabc\r\n" + PROMPT)
    check_clean(session)


def test_nook_a_second_nook_is_told_the_display_is_busy(session):
    boot_nook(session)
    base = start_nook(session)
    assert session.sh("nook") == "sysnook: display busy"
    assert session.sh("echo status $?") == "status 1"
    quit_nook(session, base)
    check_clean(session)


def kill_sysnook_from_uart(session, user):
    """Start sysnook as user1 on the VDP console, log in on UART1 as another
    user, and kill sysnook from there.

    @param session The session.
    @param user (name, password) to kill it as.

    @return kill's output, and the number of context commands before
      nook.
    """
    session.child.sendline("exit")
    session.login(*user)
    base = start_nook(session, user=("user1", "user1user1"), demo=False)
    return session.sh("kill %d" % sysnook_pid(session)), base


def test_nook_killed_by_the_user_who_started_it_gives_the_console_back(session):
    boot_nook(session)
    output, base = kill_sysnook_from_uart(session,
                                          ("user1", "user1user1"))
    assert output.startswith("Termination of process"), output
    events = session.vdp.wait_context_events(base + 3)
    assert events[base:] == ACQUIRE + RELEASE, events[base:]
    session.vdp.expect(PROMPT + "$")
    check_clean(session)


def test_nook_killed_by_root_gives_the_console_back(session):
    boot_nook(session)
    output, base = kill_sysnook_from_uart(session, ("root", "rootroot"))
    assert output.startswith("Termination of process"), output
    events = session.vdp.wait_context_events(base + 3)
    assert events[base:] == ACQUIRE + RELEASE, events[base:]
    session.vdp.expect(PROMPT + "$")
    check_clean(session)


def test_nook_another_users_kill_is_refused(session):
    boot_nook(session)
    output, base = kill_sysnook_from_uart(session, ("user2", "user2user2"))
    assert output == \
        'Process termination returned status "Permission denied".', output
    status = session.sh("echo status $?")
    assert status == "status 255", status
    time.sleep(1)
    assert session.vdp.context_events()[base:] == ACQUIRE
    quit_nook(session, base)
    check_clean(session)


# Step 2:  the windows and their painting.

def glyph_on(vdp, code, color, background):
    """Return a glyph of the driver's font as rows of palette indices.

    @param vdp The FakeVdp.
    @param code The character code.
    @param color The palette index of its set pixels.
    @param background The palette index under its clear pixels.

    @return The rows.
    """
    return [[background if pixel == gr.BLACK else pixel for pixel in row]
            for row in glyph_pixels(vdp, VDP_FONT_ID, code, color)]


def cell_fill(row, column):
    """Return the fill color of a cell of window 2."""
    return CELL_FILLS[(row + column) & 3]


def window_points(frame, body, active=True):
    """Return sample points of a plain window:  its title bar, border and
    body.

    @param frame The window's (left, top, width, height).
    @param body The palette index of its body, or None to leave it out.
    @param active Whether it's the active window.

    @return A dictionary of (x, y) to palette index.
    """
    left, top, width, height = frame
    right, bottom = left + width, top + height
    inner_right, inner_bottom = right - BORDER, bottom - BORDER
    bar = TITLE_BAR if active else INACTIVE_BAR
    border = TITLE_BAR if active else INACTIVE_BORDER
    points = {(right - 12, top + 1): bar, (left, top): border,
              (right - 1, bottom - 1): border}
    if body is not None:
        points[(left + 5, bottom - 5)] = body
        points[(inner_right - INSET - 1, inner_bottom - INSET - 1)] = body
    return points


def assert_windows(vdp, frames, active=2):
    """Check sample pixels of the desktop and the three windows, each at a
    frame, window 2 full of cells, and each window's title.

    @param vdp The FakeVdp.
    @param frames The windows' frames.
    @param active The number of the active window.
    """
    expected = {}
    for number, frame in enumerate(frames):
        body = BODIES[number] if number < 2 else None
        expected.update(window_points(frame, body, number == active))
    left, top = frames[2][0] + BORDER, frames[2][1] + FRAME_TOP
    expected.update({
        # Row 1, column 0:  fill and outline corners.
        (left + 10, top + 30): cell_fill(1, 0),
        (left, top + CELL): CELL_EDGE, (left + 19, top + 39): CELL_EDGE,
        # Row 11, column 15, near the body's bottom-right corner.
        (left + 311, top + 231): cell_fill(11, 15)})
    for point in ((0, 0), (639, 479), (250, 300)):
        expected[point] = DESKTOP
    assert_pixels(vdp, expected)
    for number, frame in enumerate(frames):
        ink, bar = (gr.WHITE, TITLE_BAR) if number == active else (
            INACTIVE_BORDER, INACTIVE_BAR)
        assert vdp.pixels(frame[0] + 5, frame[1] + 4, 6, 8) == glyph_on(
            vdp, ord("W"), ink, bar), number
    # The row of text across row 0:  its third character, over cell 0.
    assert vdp.pixels(left + 12, top + 6, 6, 8) == glyph_on(
        vdp, ROW_TEXT[2], gr.WHITE, cell_fill(0, 0))


def test_nook_paints_its_windows_exactly(session):
    boot_nook(session)
    base = start_nook(session)
    assert_windows(session.vdp, FRAMES)
    quit_nook(session, base)
    check_clean(session)


def cells_body(vdp):
    """Return window 2's body as graphics 6's demonstration window drew it:
    rows of 16 cells, each filled and outlined, with text across every third
    row.

    @param vdp The FakeVdp, whose font the text is drawn in.

    @return Rows of palette indices, 320 by 240.
    """
    rows = [[0] * (16 * CELL) for _ in range(12 * CELL)]
    for row in range(12):
        for column in range(16):
            left, top = column * CELL, row * CELL
            for y in range(top, top + CELL):
                for x in range(left, left + CELL):
                    edge = y in (top, top + CELL - 1) or \
                        x in (left, left + CELL - 1)
                    rows[y][x] = CELL_EDGE if edge else cell_fill(row, column)
    width, height, data = vdp.fonts[VDP_FONT_ID]
    row_bytes = (width + 7) // 8
    for row in range(0, 12, 3):
        for index, code in enumerate(ROW_TEXT):
            for y in range(height):
                start = (code * height + y) * row_bytes
                bits = int.from_bytes(data[start:start + row_bytes], "big")
                for x in range(width):
                    if bits & (1 << (row_bytes * 8 - 1 - x)):
                        rows[row * CELL + 6 + y][index * width + x] = gr.WHITE
    return rows


def test_nook_window_2s_canvas_draws_the_cells_exactly(session):
    boot_nook(session)
    vdp = session.vdp
    calls = vdp.count_events("call")
    base = start_nook(session)
    assert_windows(vdp, FRAMES)
    left, top = FRAMES[2][0] + BORDER, FRAMES[2][1] + FRAME_TOP
    assert vdp.pixels(left, top, 16 * CELL, 12 * CELL) == cells_body(vdp)
    # The canvas's 2.6 KB of records are 6 preuploads of at most 512 bytes,
    # called once each for the first frame of the window.
    assert vdp.count_events("call") - calls == 6
    quit_nook(session, base)
    check_clean(session)


# Step 3:  live moves.

def drag(vdp, start, end, steps=10, button=1):
    """Press a mouse button at a point, move the pointer to another in
    steps, and let the button go there.

    @param vdp The FakeVdp.
    @param start The (x, y) of the press.
    @param end The (x, y) of the release.
    @param steps The number of motions between them.
    @param button The button bit to hold.
    """
    vdp.mouse(*start)
    vdp.mouse(*start, buttons=button)
    for step in range(1, steps + 1):
        vdp.mouse(start[0] + (end[0] - start[0]) * step // steps,
                  start[1] + (end[1] - start[1]) * step // steps,
                  buttons=button)
        time.sleep(0.02)
    vdp.mouse(*end)


def moved(frame, delta_x, delta_y):
    """Return a frame moved by a distance."""
    return (frame[0] + delta_x, frame[1] + delta_y, frame[2], frame[3])


def wait_canvas(vdp, expected, timeout=30):
    """Wait until a FakeVdp's whole canvas equals a snapshot.

    @param vdp The FakeVdp.
    @param expected The snapshot, as bytes.
    @param timeout Seconds to wait.
    """
    deadline = time.time() + timeout
    while bytes(vdp.canvas) != expected:
        if time.time() > deadline:
            actual = bytes(vdp.canvas)
            wrong = [(index % 640, index // 640)
                     for index in range(len(expected))
                     if actual[index] != expected[index]]
            raise AssertionError(f"{len(wrong)} pixels differ, first "
                                 f"{wrong[:8]}")
        with vdp.lock:
            vdp.lock.wait(0.1)


def test_nook_a_window_moved_live_lands_exactly_and_uncovers_the_desktop(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, "--move=live")
    plots = vdp.count_events("plot")
    drag(vdp, (300, 45), (100, 295))
    frame = moved(FRAMES[1], -200, 250)
    expected = window_points(frame, BODIES[1])
    left, top, width, height = FRAMES[1]
    for point in ((left, top), (left + width - 1, top + height - 1),
                  (left + 100, top + 60)):
        expected[point] = DESKTOP
    wait_pixels(vdp, expected)
    assert vdp.pixels(frame[0] + 5, frame[1] + 4, 6, 8) == glyph_on(
        vdp, ord("W"), gr.WHITE, TITLE_BAR)
    assert vdp.count_events("plot") > plots
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_window_moved_across_another_and_back_restores_it_exactly(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, "--move=live")
    initial = bytes(vdp.canvas)
    drag(vdp, (300, 45), (390, 255))
    frame = moved(FRAMES[1], 90, 210)
    expected = window_points(frame, BODIES[1])
    left, top = FRAMES[2][0] + BORDER, FRAMES[2][1] + FRAME_TOP
    # Window 2's cells beside and below window 1, and where it was.
    expected[(left + 10, top + 30)] = cell_fill(1, 0)
    expected[(left + 310, top + 230)] = cell_fill(11, 15)
    expected[(FRAMES[1][0] + 5, FRAMES[1][1] + 5)] = DESKTOP
    wait_pixels(vdp, expected)
    drag(vdp, (390, 255), (300, 45))
    # Moving window 1 made it active; a click on window 2 makes the stack
    # and the colors what they were.
    click(vdp, (400, 300))
    wait_canvas(vdp, initial)
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_move_ended_with_the_next_press_in_one_message_is_painted(
        session):
    # The last motion, the release and a press that starts another grab
    # arrive in one input message, sent while the window server paints the
    # motion before:  the move must still be painted where it ended.  A VDP
    # reading 5 KB a second keeps that frame going while they arrive.
    boot_nook(session, read_rate=5000)
    vdp = session.vdp
    base = start_nook(session, "--move=live")
    vdp.mouse(300, 45)
    vdp.mouse(300, 45, buttons=1)
    for step in range(1, 10):
        vdp.mouse(300 + 9 * step, 45 + 21 * step, buttons=1)
        time.sleep(0.02)
    vdp.mice_at_once([(390, 255, 1), (390, 255, 0), (100, 30, 0),
                      (100, 30, 1), (100, 30, 0)])
    expected = window_points(moved(FRAMES[1], 90, 210), BODIES[1],
                             active=False)
    expected[(FRAMES[1][0] + 5, FRAMES[1][1] + 5)] = DESKTOP
    wait_pixels(vdp, expected)
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_window_moved_off_the_screen_and_back_is_repainted_exactly(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, "--move=live")
    initial = bytes(vdp.canvas)
    drag(vdp, (400, 205), (630, 205))
    wait_pixels(vdp, {(530, 205): TITLE_BAR, (529, 300): DESKTOP,
                      (535, 300): cell_fill(4, 0)})
    drag(vdp, (630, 205), (400, 205))
    wait_canvas(vdp, initial)
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_moved_window_keeps_32_pixels_of_its_title_bar_on_the_screen(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session)
    # The caption buttons take the right 54 pixels of a title bar, so a
    # window can't be dragged far enough left for the limit to hold it.
    # Grabbed 5 pixels down, window 1 would go to y -5:  it stays at 0.
    drag(vdp, (300, 45), (300, 0))
    wait_pixels(vdp, {(300, 0): TITLE_BAR, (300, FRAME_TOP): BODIES[1],
                      (259, 0): DESKTOP})
    # Window 0, grabbed 4 pixels in, would go to x 635 and y 473.
    drag(vdp, (FRAMES[0][0] + 4, FRAMES[0][1] + 6), (639, 479))
    wait_pixels(vdp, {(640 - KEEP_TITLE, 479): TITLE_BAR,
                      (639 - KEEP_TITLE, 479): DESKTOP,
                      (639, 480 - FRAME_TOP): TITLE_BAR,
                      (639, 479 - FRAME_TOP): DESKTOP})
    quit_nook(session, base)
    check_clean(session)


# Step 4:  live resizes.

def test_nook_a_window_resized_live_grows_and_shrinks_exactly(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, "--resize=live")
    anchored = {(310, 242): cell_fill(1, 0)}
    # Grabbed on its corner pixel, window 2 grows to 340x278.
    drag(vdp, corner_point(FRAMES[2]), (639, 477))
    grown = (300, 200, 340, 278)
    expected = window_points(grown, None)
    expected.update(anchored)
    expected.update({
        (639, 201): TITLE_BAR, (630, 300): gr.WHITE, (625, 460): gr.WHITE,
        # Where the old border was is the body's face, beside and below the
        # cells.
        (621, 300): gr.WHITE, (615, 455): gr.WHITE})
    wait_pixels(vdp, expected)
    # Grabbed on its corner pixel, it shrinks to 155x139.
    drag(vdp, corner_point(grown), (454, 338))
    shrunk = (300, 200, 155, 139)
    expected = window_points(shrunk, None)
    expected.update(anchored)
    expected.update({
        (454, 201): TITLE_BAR, (455, 201): DESKTOP,
        (446, 326): cell_fill(5, 7),
        # What the shrink left shows what is under it.
        (630, 300): DESKTOP, (625, 460): DESKTOP, (500, 400): DESKTOP,
        (615, 448): DESKTOP, (639, 473): DESKTOP})
    wait_pixels(vdp, expected)
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_window_is_never_resized_below_its_minimum(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session)
    drag(vdp, corner_point(FRAMES[2]), (0, 0))
    smallest = (300, 200, MIN_WIDTH, MIN_HEIGHT)
    expected = window_points(smallest, None)
    expected.update({
        (383, 201): TITLE_BAR, (384, 201): DESKTOP,
        (305, 226): cell_fill(0, 0), (305, 232): DESKTOP,
        (299, 226): DESKTOP, (384, 226): DESKTOP})
    wait_pixels(vdp, expected)
    quit_nook(session, base)
    check_clean(session)


# Step 5:  outline mode.

def hold_drag(vdp, start, end, steps=10, button=1):
    """Press a mouse button at a point and move the pointer to another in
    steps, keeping the button down.

    @param vdp The FakeVdp.
    @param start The (x, y) of the press.
    @param end The (x, y) to stop at.
    @param steps The number of motions.
    @param button The button bit to hold.
    """
    vdp.mouse(*start)
    vdp.mouse(*start, buttons=button)
    for step in range(1, steps + 1):
        vdp.mouse(start[0] + (end[0] - start[0]) * step // steps,
                  start[1] + (end[1] - start[1]) * step // steps,
                  buttons=button)
        time.sleep(0.02)


def with_outline(canvas, frame):
    """Return a canvas with a 1-pixel outline of a frame drawn on it in
    white by XOR.

    @param canvas The canvas, as bytes.
    @param frame The outline's (left, top, width, height).

    @return The new canvas, as bytes.
    """
    left, top, width, height = frame
    right, bottom = left + width - 1, top + height - 1
    points = set()
    for column in range(left, right + 1):
        points.add((column, top))
        points.add((column, bottom))
    for row in range(top, bottom + 1):
        points.add((left, row))
        points.add((right, row))
    result = bytearray(canvas)
    for column, row in points:
        if 0 <= column < 640 and 0 <= row < 480:
            result[row * 640 + column] ^= gr.WHITE
    return bytes(result)


def restart_nook(session, base, options):
    """Quit the window manager and start it again with options.

    @param session The session.
    @param base The number of context commands before the last nook.
    @param options nook's options.

    @return The number of context commands before the new nook.
    """
    vdp = session.vdp
    quit_nook(session, base)
    vdp.expect(PROMPT + "$")
    return run_nook(vdp, options, session=session)


def outline_then_live(session, options, start, end, grabbed, outline):
    """Drag in outline mode, checking that after the press only the outline
    changes until the release, then check that the canvas after the release
    equals live mode's after the same drag.

    @param session The session.
    @param options The nook options for outline mode.
    @param start The (x, y) of the press.
    @param end The (x, y) of the release.
    @param grabbed The frame of the window pressed on.
    @param outline The frame the outline should have at the end.
    """
    vdp = session.vdp
    base = start_nook(session, options)
    initial = bytes(vdp.canvas)
    # The press activates the window and draws the outline on its frame.
    vdp.mouse(*start)
    vdp.mouse(*start, buttons=1)
    settle(vdp)
    pressed = with_outline(bytes(vdp.canvas), grabbed)
    hold_drag(vdp, start, end)
    wait_canvas(vdp, with_outline(pressed, outline))
    vdp.mouse(*end)
    wait_pixels(vdp, {corner_point(outline): TITLE_BAR})
    settle(vdp)
    after = bytes(vdp.canvas)
    base = restart_nook(session, base, "--move=live --resize=live")
    assert bytes(vdp.canvas) == initial
    drag(vdp, start, end)
    wait_canvas(vdp, after)
    quit_nook(session, base)
    check_clean(session)


def test_nook_an_outline_move_changes_nothing_but_the_outline_until_release(
        session):
    boot_nook(session)
    outline_then_live(session, "--move=outline", (300, 45), (390, 255),
                      FRAMES[1], moved(FRAMES[1], 90, 210))


def test_nook_an_outline_resize_changes_nothing_but_the_outline_until_release(
        session):
    boot_nook(session)
    outline_then_live(session, "--resize=outline", corner_point(FRAMES[2]),
                      (454, 338), FRAMES[2], (300, 200, 155, 139))


# Step 6:  revocation.

def revoke(session, base):
    """Take the display back with the hot key and check that sysnook exited 0
    and the VDP console has its prompt again.

    @param session The session.
    @param base The number of context commands before nook.
    """
    vdp = session.vdp
    vdp.release_display()
    events = vdp.wait_context_events(base + 3)
    assert events[base:] == ACQUIRE + RELEASE, events[base:]
    vdp.expect(PROMPT + "$")
    assert vdp_status(vdp) == 0


def test_nook_exits_0_when_the_display_is_taken_back(session):
    boot_nook(session)
    revoke(session, start_nook(session))
    check_clean(session)


def test_nook_exits_0_when_the_display_is_taken_back_during_a_live_drag(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, "--move=live")
    hold_drag(vdp, (300, 45), (390, 255))
    wait_pixels(vdp, window_points(moved(FRAMES[1], 90, 210), BODIES[1]))
    revoke(session, base)
    check_clean(session)


def test_nook_exits_0_when_the_display_is_taken_back_during_an_outline_drag(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, "--move=outline --resize=outline")
    initial = bytes(vdp.canvas)
    hold_drag(vdp, corner_point(FRAMES[2]), (454, 338))
    wait_canvas(vdp, with_outline(initial, (300, 200, 155, 139)))
    revoke(session, base)
    check_clean(session)


# Step 10:  leaks.

def test_nook_gives_back_all_the_memory_it_used(session):
    boot_nook(session)
    vdp = session.vdp
    vdp_login_as(vdp, "root", "rootroot")
    heaps = []
    for _run in range(24):
        base = run_nook(vdp, "--move=live", session=session)
        drag(vdp, (300, 45), (390, 255))
        wait_pixels(vdp, window_points(moved(FRAMES[1], 90, 210), BODIES[1]))
        quit_nook(session, base)
        vdp.expect(PROMPT + "$")
        heaps.append(settled(lambda: heap_at_prompt(session)))
    # The first ten or so runs fill the kernel's caches, fragment the heap, and
    # give long-lived processes a capability entry for each PID the free queue
    # hands out in turn; free memory then holds, or cycles by up to 80 bytes
    # with the shells' own state, and dips for a run when an allocation takes
    # a whole free block.  A leak lowers every run after it, the best of a
    # window too:  16 bytes a run loses 128 over the eight runs between these.
    assert max(heaps[18:24]) >= max(heaps[10:16]) - 48, heaps
    check_clean(session)


# Nook plan 1, step 5:  the window protocol's first messages.

HELLO = "wintest: screen 640x480 work 0,0,640,480 cell 6x8 role none"


def start_client(session):
    """Run wintest wait on UART1 and wait until it has its greeting.

    @param session The session.
    """
    session.child.sendline("wintest wait")
    session.child.expect_exact(HELLO, timeout=10)


def test_nook_a_client_gets_the_screen_and_its_role(session):
    boot_nook(session)
    base = start_nook(session)
    assert session.sh("wintest hello") == HELLO
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_client_with_no_window_server_is_told_so(session):
    boot_nook(session)
    assert session.sh("wintest hello") == "wintest: no window server"
    assert session.sh("echo $?") == "1"


def test_nook_runs_sysnook_at_executive_level_and_its_clients_at_user(session):
    boot_nook(session)
    base = start_nook(session)
    session.child.sendline("wintest hold &")
    for _ in range(50):
        out = session.sh("ps -l")
        if " wintest\n" in out + "\n":
            break
    assert re.search(r"^\d+  root user wintest$", out, re.M), out
    assert re.search(r"^\d+  root executive sysnook$", out, re.M), out
    quit_nook(session, base)
    check_clean(session)


def test_nook_another_users_client_cant_connect(session):
    boot_nook(session)
    base = start_nook(session, user=("user1", "user1user1"), demo=False)
    assert session.sh("wintest hello") == "wintest: error %d" % 2
    assert session.sh("echo $?") == "1"
    quit_nook(session, base)
    check_clean(session)


def test_nook_taking_the_display_back_disconnects_its_clients(session):
    boot_nook(session)
    base = start_nook(session)
    start_client(session)
    session.vdp.release_display()
    session.child.expect(r"wintest: disconnected\r*\n", timeout=10)
    session.child.expect(PROMPT, timeout=10)
    assert session.sh("echo $?") == "0"
    events = session.vdp.wait_context_events(base + 3)
    assert events[base:] == ACQUIRE + RELEASE, events[base:]
    check_clean(session)


def allocations(session, pids):
    """Get the sizes of the heap allocations some processes own.

    @param session The session.
    @param pids The process IDs.

    @return A sorted list of (pid, size).
    """
    def sample():
        owned = re.findall(r"^  0x[0-9a-f]+: (\d+) bytes owned by (\d+)$",
                           session.sh("dumpMemoryAllocations"), re.M)
        return sorted((int(pid), int(size)) for size, pid in owned
                      if int(pid) in pids)
    return settled(sample)


def kill_client(session):
    """Start wintest wait on UART1 and kill it with Ctrl-C.

    @param session The session.
    """
    start_client(session)
    session.child.sendcontrol("c")
    session.child.expect(PROMPT, timeout=10)


def test_nook_killed_clients_leave_nothing_behind(session):
    # The first client caches wintest's overlay, so the allocations are
    # counted after it.
    boot_nook(session)
    base = start_nook(session)
    owners = {1, sysnook_pid(session)}
    kill_client(session)
    before = allocations(session, owners)
    for _client in range(16):
        kill_client(session)
    after = allocations(session, owners)
    assert after == before, (before, after)
    assert session.sh("wintest hello") == HELLO
    quit_nook(session, base)
    check_clean(session)


def clients_running(session):
    """Count the wintest processes in ps.

    @param session The session.

    @return The count.
    """
    return [name for _pid, _user, name in ps_entries(session)].count(
        "wintest")


def test_nook_eight_clients_fit_beside_the_desktop(session):
    # The clients print nothing:  more background processes printing on UART1
    # at once than the console has buffers stall waiting for one.
    boot_nook(session)
    base = start_nook(session, demo=False)
    for _client in range(8):
        session.sh("wintest hold &")
    assert clients_running(session) == 8
    quit_nook(session, base)
    deadline = time.time() + 15
    while clients_running(session) > 0:
        assert time.time() < deadline, "clients still running"
        time.sleep(0.5)
    check_clean(session)


# Found in plan 6:  input the VDP sends while the driver writes a long repaint.

def test_nook_mouse_packets_sent_during_long_repaints_are_never_lost(session):
    # A VDP reading 5 KB a second keeps the driver writing each repaint of
    # window 2's cells for a while, and FakeVdp sends a mouse packet for every
    # 16 bytes it reads, as the Agon's VDP did while the driver wrote.
    boot_nook(session, read_rate=5000)
    vdp = session.vdp
    base = start_nook(session, "--timing --move=live")
    vdp.mouse(300, 45)
    vdp.mouse(300, 45, buttons=1)
    vdp.flood([(300 + (step % 60) * 2, 45 + step * 210 // 300)
               for step in range(300)] + [(390, 255)])
    deadline = time.time() + 60
    while vdp.flooding:
        assert time.time() < deadline, f"{len(vdp.flooding)} left to send"
        time.sleep(0.1)
    vdp.mouse(390, 255)
    wait_pixels(vdp, window_points(moved(FRAMES[1], 90, 210), BODIES[1]),
                timeout=20)
    quit_nook(session, base)
    check_clean(session)


# Nook plan 2:  windows.

ACCENT = gr.BLUE
FACE = gr.WHITE


def run_client(session, commands):
    """Start wintest run with commands in the background on UART1.

    @param session The session.
    @param commands wintest run's commands.
    """
    session.child.sendline("wintest run " + commands + " &")


def expect_line(session, line, timeout=10):
    """Wait for a line on UART1.

    @param session The session.
    @param line The line.
    @param timeout Seconds to wait.
    """
    session.child.expect(re.escape(line) + r"\r*\n", timeout=timeout)


def text_pixels(vdp, text, color, background):
    """Return a line of text in the driver's font as rows of palette
    indices.

    @param vdp The FakeVdp.
    @param text The text.
    @param color The palette index of its set pixels.
    @param background The palette index under its clear pixels.

    @return The rows.
    """
    glyphs = [glyph_on(vdp, ord(ch), color, background) for ch in text]
    return [sum((glyph[row] for glyph in glyphs), []) for row in range(8)]


def decorated_points(frame, bar=ACCENT):
    """Return sample points of a decorated window:  its border's corners, its
    title bar's, its face's, and the desktop just outside it.

    @param frame The window's (left, top, width, height).
    @param bar The palette index of its border and title bar.

    @return A dictionary of (x, y) to palette index.
    """
    left, top, width, height = frame
    right, bottom = left + width, top + height
    return {(left, top): bar, (right - 1, top): bar, (left, bottom - 1): bar,
            (right - 1, bottom - 1): bar, (left + 1, top + 1): bar,
            (right - 2, top + FRAME_TOP - 1): bar,
            (left + 1, top + FRAME_TOP): FACE,
            (right - 2 - INSET, bottom - 2): FACE,
            (left - 1, top): DESKTOP, (right, top): DESKTOP,
            (left, top - 1): DESKTOP, (left, bottom): DESKTOP}


def face_points(frame):
    """Return sample points of a decorated window's face, which keep their
    color whether the window is active or not.

    @param frame The window's (left, top, width, height).

    @return A dictionary of (x, y) to palette index.
    """
    left, top, width, height = frame
    return {(left + 1, top + FRAME_TOP): FACE, (left + 5, top + height - 5): FACE,
            (left + width - 2 - INSET, top + height - 2): FACE}


def wintest_pid(session):
    """Get the process ID of the wintest running on UART1.

    @param session The session.

    @return The process ID.
    """
    pids = [pid for pid, _user, name in ps_entries(session)
            if name == "wintest"]
    assert len(pids) == 1, pids
    return pids[0]


def test_nook_a_clients_window_is_drawn_exactly(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,200,100 Hello")
    expect_line(session, "wintest: window 1")
    wait_pixels(vdp, decorated_points((40, 40, 200, 100)))
    settle(vdp)
    assert vdp.pixels(45, 44, 30, 8) == text_pixels(vdp, "Hello", gr.WHITE,
                                                     ACCENT)
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_client_cant_change_another_clients_window(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,200,100 Hello")
    expect_line(session, "wintest: window 1")
    wait_pixels(vdp, decorated_points((40, 40, 200, 100)))
    settle(vdp)
    before = bytes(vdp.canvas)
    assert session.sh("wintest run destroy 1 title 1 Mine exit") == \
        "wintest: title 1: Operation not permitted"
    time.sleep(1)
    assert bytes(vdp.canvas) == before
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_killed_client_takes_its_windows_with_it(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    initial = bytes(vdp.canvas)
    run_client(session, "create 40,40,200,100 One create 150,90,200,120 Two")
    expect_line(session, "wintest: window 1")
    expect_line(session, "wintest: window 2")
    points = face_points((40, 40, 200, 100))
    points.update(face_points((150, 90, 200, 120)))
    wait_pixels(vdp, points)
    # Printed in the background, it could split the echo of ps.
    expect_line(session, "FOCUS 2 activated")
    output = session.sh("kill %d" % wintest_pid(session))
    assert output.startswith("Termination of process"), output
    wait_canvas(vdp, initial)
    quit_nook(session, base)
    check_clean(session)


def click(vdp, point):
    """Press and let go of the left button at a point.

    @param vdp The FakeVdp.
    @param point The (x, y).
    """
    vdp.mouse(*point)
    vdp.mouse(*point, buttons=1)
    vdp.mouse(*point)


def button_rect(frame, button):
    """Return a caption button's rectangle:  0 minimize, 1 maximize, 2 close.

    @param frame The window's (left, top, width, height).
    @param button The button.

    @return The button's (left, top, width, height).
    """
    left, top, width, _height = frame
    return (left + width - BORDER - (3 - button) * 18, top + BORDER, 18, 14)


def drawing(vdp):
    """Count the drawing commands FakeVdp has received.  Viewports are left
    out:  the driver sends one only when it changes.

    @param vdp The FakeVdp.

    @return A tuple of the plot, gcol and gtext counts.
    """
    return tuple(vdp.count_events(kind) for kind in ("plot", "gcol", "gtext"))


def settle(vdp, quiet=0.5, timeout=30, frames=True):
    """Wait until FakeVdp has received no drawing commands for a while and,
    unless what is drawn isn't sysnook's frames, until the last frame drawn
    has been finished, which sysnook's --finish makes it do.

    @param vdp The FakeVdp.
    @param quiet Seconds without a drawing command.
    @param timeout Seconds to wait in all.
    @param frames Whether to wait for the last frame to be finished.

    @return The drawing command counts at the end.
    """
    deadline = time.time() + timeout
    last = drawing(vdp)
    since = time.time()
    while (time.time() - since < quiet) or \
            (frames and not vdp.frame_finished()):
        assert time.time() < deadline, "FakeVdp is still being drawn on"
        time.sleep(0.05)
        now = drawing(vdp)
        if now != last:
            last, since = now, time.time()
    return last


def changed_pixels(before, after):
    """Return the pixels that differ between two canvases.

    @param before One canvas, as bytes.
    @param after The other.

    @return A set of (x, y).
    """
    return {(index % 640, index // 640) for index in range(len(before))
            if before[index] != after[index]}


def inside(points, rect):
    """Determine whether every point is inside a rectangle."""
    left, top, width, height = rect
    return all(left <= x < left + width and top <= y < top + height
               for x, y in points)


def test_nook_a_title_too_long_for_its_window_ends_in_dots(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    # 118 pixels inside the border, less 54 of buttons and a 4-pixel inset,
    # holds 10 characters of 6.
    run_client(session, "create 40,40,120,60 ABCDEFGHIJKLMN")
    expect_line(session, "wintest: window 1")
    wait_pixels(vdp, decorated_points((40, 40, 120, 60)))
    settle(vdp)
    assert vdp.pixels(45, 44, 60, 8) == text_pixels(vdp, "ABCDEFG...",
                                                     gr.WHITE, ACCENT)
    assert vdp.pixels(105, 41, 1, 14) == [[ACCENT]] * 14
    quit_nook(session, base)
    check_clean(session)


def test_nook_close_asks_the_owner_and_only_when_released_on_it(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,200,100 One create 300,200,200,100 Two")
    expect_line(session, "wintest: window 1")
    expect_line(session, "wintest: window 2")
    wait_pixels(vdp, decorated_points((300, 200, 200, 100)))
    one_close = button_rect((40, 40, 200, 100), 2)
    two_close = button_rect((300, 200, 200, 100), 2)
    # Released off the button, the press does nothing; the next close is the
    # first thing printed.
    vdp.mouse(one_close[0] + 9, one_close[1] + 7)
    vdp.mouse(one_close[0] + 9, one_close[1] + 7, buttons=1)
    vdp.mouse(100, 100, buttons=1)
    vdp.mouse(100, 100)
    click(vdp, (two_close[0] + 9, two_close[1] + 7))
    session.child.expect(r"CLOSE_REQUESTED (\d+)\r*\n", timeout=10)
    assert session.child.match.group(1) == "2"
    click(vdp, (one_close[0] + 9, one_close[1] + 7))
    expect_line(session, "CLOSE_REQUESTED 1")
    time.sleep(1)
    points = face_points((40, 40, 200, 100))
    points.update(face_points((300, 200, 200, 100)))
    wait_pixels(vdp, points)
    quit_nook(session, base)
    check_clean(session)


def test_nook_close_turns_red_under_the_pointer_repainting_only_itself(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    frame = (40, 40, 200, 100)
    run_client(session, "create 40,40,200,100 Hello")
    expect_line(session, "wintest: window 1")
    wait_pixels(vdp, decorated_points(frame))
    close = button_rect(frame, 2)
    vdp.mouse(100, 100)
    before_counts = settle(vdp)
    before = bytes(vdp.canvas)
    vdp.mouse(close[0] + 9, close[1] + 7)
    wait_pixels(vdp, {(close[0], close[1]): gr.LIGHT_RED,
                      (close[0] + 17, close[1] + 13): gr.LIGHT_RED})
    hover_counts = settle(vdp)
    hovered = bytes(vdp.canvas)
    changed = changed_pixels(before, hovered)
    assert changed and inside(changed, close), sorted(changed)[:8]
    # Motion within the button repaints nothing.
    vdp.mouse(close[0] + 3, close[1] + 3)
    assert settle(vdp) == hover_counts
    vdp.mouse(100, 100)
    wait_canvas(vdp, before)
    out_counts = settle(vdp)
    vdp.mouse(close[0] + 9, close[1] + 7)
    wait_canvas(vdp, hovered)
    again_counts = settle(vdp)
    vdp.mouse(close[0] + 9, close[1] + 7, buttons=1)
    wait_pixels(vdp, {(close[0], close[1]): gr.DARK_GRAY})
    pressed_counts = settle(vdp)
    vdp.mouse(100, 100, buttons=1)
    vdp.mouse(100, 100)
    deltas = [tuple(after - start for after, start in zip(end, begin))
              for begin, end in ((before_counts, hover_counts),
                                 (hover_counts, out_counts),
                                 (out_counts, again_counts),
                                 (again_counts, pressed_counts))]
    # One button:  its face and its glyph's two lines, in two colors.
    assert deltas == [(3, 2, 0)] * 4, deltas
    quit_nook(session, base)
    check_clean(session)


KEYCODE_F4 = 162


def test_nook_a_raised_window_is_repainted_where_it_was_covered(session):
    # Yellow window 2 covers part of window 1's body until window 1 is
    # clicked; then that part is window 1's white body.
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,200,100 One create 100,10,200,100 Two "
               "face 2 0xFFFF55FF")
    expect_line(session, "wintest: face 2: ok")
    wait_pixels(vdp, {(150, 80): gr.YELLOW, (60, 120): FACE})
    click(vdp, (60, 120))
    expect_line(session, "FOCUS 1 activated")
    wait_pixels(vdp, {(150, 80): FACE, (250, 80): gr.YELLOW})
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_click_activates_the_window_under_it_and_tells_both(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    # Window 2 covers the right of window 1's title bar until window 1 is
    # clicked.
    run_client(session, "create 40,40,200,100 One create 100,10,200,100 Two")
    for line in ("wintest: window 1", "wintest: window 2",
                 "FOCUS 1 activated", "FOCUS 1 deactivated",
                 "FOCUS 2 activated"):
        expect_line(session, line)
    wait_pixels(vdp, {(150, 11): TITLE_BAR, (150, 45): FACE,
                      (50, 41): INACTIVE_BAR, (40, 40): INACTIVE_BORDER})
    click(vdp, (60, 120))
    expect_line(session, "FOCUS 2 deactivated")
    expect_line(session, "FOCUS 1 activated")
    wait_pixels(vdp, {(150, 45): TITLE_BAR, (50, 41): TITLE_BAR,
                      (40, 40): TITLE_BAR, (150, 11): INACTIVE_BAR,
                      (299, 109): INACTIVE_BORDER})
    # The title bars are filled before their titles are drawn.
    settle(vdp)
    assert vdp.pixels(45, 44, 18, 8) == text_pixels(vdp, "One", gr.WHITE,
                                                     TITLE_BAR), "One"
    assert vdp.pixels(105, 14, 18, 8) == text_pixels(vdp, "Two",
                                                      INACTIVE_BORDER,
                                                      INACTIVE_BAR), "Two"
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_topmost_window_stays_above_a_window_activated_after_it(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 100,100,200,100 Top layer=topmost "
               "create 40,40,200,100 Normal")
    expect_line(session, "wintest: window 1")
    expect_line(session, "wintest: window 2")
    # Created after it, the normal window is still behind the topmost one.
    wait_pixels(vdp, {(150, 101): INACTIVE_BAR, (60, 41): TITLE_BAR})
    click(vdp, (60, 120))
    time.sleep(1)
    wait_pixels(vdp, {(150, 101): INACTIVE_BAR, (120, 120): FACE,
                      (60, 41): TITLE_BAR})
    quit_nook(session, base)
    check_clean(session)


def test_nook_ctrl_q_goes_to_the_application_and_nook_keeps_running(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,200,100 Keys")
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated")
    vdp.ctrl("q")
    expect_line(session, "KEY 1 q ctrl")
    expect_line(session, "KEY 1 q ctrl up")
    time.sleep(1)
    assert vdp.context_events()[base:] == ACQUIRE
    assert session.sh("wintest hello") == HELLO
    quit_nook(session, base)
    check_clean(session)


def test_nook_alt_f4_asks_only_the_active_windows_owner_to_close(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,200,100 First")
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated")
    run_client(session, "create 300,200,200,100 Second")
    expect_line(session, "wintest: window 2")
    # Two processes print these, so they come in either order.
    seen = set()
    while len(seen) < 2:
        session.child.expect(r"(FOCUS 1 deactivated|FOCUS 2 activated)\r*\n",
                             timeout=10)
        seen.add(session.child.match.group(1))
    vdp.key_packet(0, KEYMOD_ALT_LEFT, KEYCODE_F4)
    vdp.key_packet(0, KEYMOD_ALT_LEFT, KEYCODE_F4, down=0)
    expect_line(session, "CLOSE_REQUESTED 2")
    assert session.child.expect([pexpect.TIMEOUT, r"\S"], timeout=2) == 0, \
        session.child.after
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_popup_closes_on_a_press_outside_it(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    initial = bytes(vdp.canvas)
    run_client(session, "create 200,200,120,80 Menu layer=popup flags=plain "
               "closes")
    expect_line(session, "wintest: window 1")
    wait_pixels(vdp, {(200, 200): FACE, (319, 279): FACE})
    click(vdp, (50, 50))
    expect_line(session, "CLOSE_REQUESTED 1")
    wait_canvas(vdp, initial)
    quit_nook(session, base)
    check_clean(session)


KEYCODE_TAB = 142


def test_nook_maximize_fills_the_work_area_and_restore_brings_the_frame_back(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 0,458,640,22 Bar layer=topmost "
               "flags=plain,bottom create 40,40,200,100 Window")
    expect_line(session, "wintest: window 1")
    expect_line(session, "wintest: window 2")
    expect_line(session, "FOCUS 2 activated")
    wait_pixels(vdp, decorated_points((40, 40, 200, 100)))
    vdp.mouse(400, 300)
    settle(vdp)
    before = bytes(vdp.canvas)
    maximize = button_rect((40, 40, 200, 100), 1)
    click(vdp, (maximize[0] + 9, maximize[1] + 7))
    expect_line(session, "RESIZED 2 638x442")
    expect_line(session, "STATE 2 maximized")
    # The work area is the screen less the bar's 22 pixels at the bottom.
    wait_pixels(vdp, {(0, 0): TITLE_BAR, (639, 0): TITLE_BAR,
                      (0, 457): TITLE_BAR, (639, 457): TITLE_BAR,
                      (1, 15): FACE, (638, 456): FACE, (0, 458): FACE,
                      (320, 479): FACE})
    restore = button_rect((0, 0, 640, 458), 1)
    click(vdp, (restore[0] + 9, restore[1] + 7))
    expect_line(session, "RESIZED 2 198x84")
    expect_line(session, "STATE 2 normal")
    vdp.mouse(400, 300)
    wait_canvas(vdp, before)
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_reserving_window_resizes_a_maximized_one(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,200,100 Window state 1 maximized")
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated")
    expect_line(session, "RESIZED 1 638x464")
    expect_line(session, "STATE 1 maximized")
    wait_pixels(vdp, {(0, 479): TITLE_BAR, (639, 479): TITLE_BAR})
    run_client(session, "create 0,458,640,22 Bar layer=topmost "
               "flags=plain,bottom")
    expect_line(session, "RESIZED 1 638x442")
    wait_pixels(vdp, {(0, 457): TITLE_BAR, (639, 457): TITLE_BAR,
                      (0, 458): FACE, (639, 479): FACE})
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_double_click_on_a_title_bar_maximizes(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,200,100 Window")
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated")
    wait_pixels(vdp, decorated_points((40, 40, 200, 100)))
    # Two clicks a second apart are two clicks.  sysnook times a press when
    # it handles it, so the gap leaves room for the first to be handled late.
    click(vdp, (100, 45))
    time.sleep(1.0)
    click(vdp, (100, 45))
    assert session.child.expect([pexpect.TIMEOUT, r"\S"], timeout=2) == 0, \
        session.child.after
    # Two within 500 ms and 4 pixels are a double-click.
    click(vdp, (100, 45))
    click(vdp, (103, 47))
    expect_line(session, "RESIZED 1 638x464")
    expect_line(session, "STATE 1 maximized")
    wait_pixels(vdp, {(0, 0): TITLE_BAR, (639, 479): TITLE_BAR})
    quit_nook(session, base)
    check_clean(session)


def test_nook_minimize_hides_a_window_and_alt_tab_shows_it_again(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    initial = bytes(vdp.canvas)
    run_client(session, "create 40,40,200,100 Window")
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated")
    wait_pixels(vdp, decorated_points((40, 40, 200, 100)))
    vdp.mouse(400, 300)
    settle(vdp)
    shown = bytes(vdp.canvas)
    minimize = button_rect((40, 40, 200, 100), 0)
    click(vdp, (minimize[0] + 9, minimize[1] + 7))
    expect_line(session, "STATE 1 minimized")
    expect_line(session, "FOCUS 1 deactivated")
    vdp.mouse(400, 300)
    wait_canvas(vdp, initial)
    vdp.key_packet(9, KEYMOD_ALT_LEFT, KEYCODE_TAB)
    vdp.key_packet(9, KEYMOD_ALT_LEFT, KEYCODE_TAB, down=0)
    expect_line(session, "STATE 1 normal")
    expect_line(session, "FOCUS 1 activated")
    wait_canvas(vdp, shown)
    quit_nook(session, base)
    check_clean(session)


def test_nook_the_right_alt_tabs_between_windows_as_the_left_does(session):
    boot_nook(session)
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,200,100 One create 300,200,200,100 Two")
    expect_line(session, "FOCUS 2 activated")
    for modifier in (KEYMOD_ALT_RIGHT, KEYMOD_ALT_LEFT):
        session.vdp.key_packet(9, modifier, KEYCODE_TAB)
        session.vdp.key_packet(9, modifier, KEYCODE_TAB, down=0)
        expect_line(session, "FOCUS 2 deactivated")
        expect_line(session, "FOCUS 1 activated")
        session.vdp.key_packet(9, modifier, KEYCODE_TAB)
        session.vdp.key_packet(9, modifier, KEYCODE_TAB, down=0)
        expect_line(session, "FOCUS 1 deactivated")
        expect_line(session, "FOCUS 2 activated")
    quit_nook(session, base)
    check_clean(session)


def test_nook_dragging_a_maximized_window_restores_it_under_the_pointer(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, "--move=live", demo=False)
    run_client(session, "create 40,40,200,100 Window state 1 maximized")
    expect_line(session, "STATE 1 maximized")
    wait_pixels(vdp, {(0, 0): TITLE_BAR, (639, 479): TITLE_BAR})
    # Grabbed at x 320, half way along 640 pixels:  restored, the 200-pixel
    # window keeps the pointer half way along, at offset 100.
    drag(vdp, (320, 7), (300, 207))
    expect_line(session, "RESIZED 1 198x84")
    expect_line(session, "STATE 1 normal")
    wait_pixels(vdp, decorated_points((200, 200, 200, 100)))
    quit_nook(session, base)
    check_clean(session)


ZONES = {
    # name: (edges moved, press point in a frame (x, y, w, h))
    "left": ("l", lambda x, y, w, h: (x, y + h // 2)),
    "right": ("r", lambda x, y, w, h: (x + w - 1, y + h // 2)),
    "top": ("t", lambda x, y, w, h: (x + w // 2, y)),
    "bottom": ("b", lambda x, y, w, h: (x + w // 2, y + h - 1)),
    "top-left": ("lt", lambda x, y, w, h: (x, y)),
    "top-right": ("rt", lambda x, y, w, h: (x + w - 1, y)),
    "bottom-left": ("lb", lambda x, y, w, h: (x, y + h - 1)),
    "bottom-right": ("rb", lambda x, y, w, h: (x + w - 1, y + h - 1)),
    # Within 10 pixels of a corner, a side resizes both ways.
    "right-near-top": ("rt", lambda x, y, w, h: (x + w - 1, y + 5)),
}


def resized(frame, edges, delta_x, delta_y):
    """Return a frame with edges moved:  l, r, t, b."""
    left, top, width, height = frame
    if "l" in edges:
        left, width = left + delta_x, width - delta_x
    if "r" in edges:
        width += delta_x
    if "t" in edges:
        top, height = top + delta_y, height - delta_y
    if "b" in edges:
        height += delta_y
    return (left, top, width, height)


def test_nook_each_grab_zone_resizes_its_edges_and_holds_the_others(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    frame = (100, 100, 300, 200)
    run_client(session, "create 100,100,300,200 Zones")
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated")
    wait_pixels(vdp, decorated_points(frame))
    for name, (edges, at) in ZONES.items():
        start = at(*frame)
        drag(vdp, start, (start[0] + 20, start[1] + 20))
        frame = resized(frame, edges, 20, 20)
        expect_line(session, "RESIZED 1 %dx%d" % (frame[2] - 2,
                                                   frame[3] - FRAME_TOP - 1))
        wait_pixels(vdp, decorated_points(frame))
        vdp.mouse(600, 400)
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_left_edge_dragged_past_the_minimum_holds_the_right_edge(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, "--resize=live", demo=False)
    run_client(session, "create 100,100,300,200 Narrow")
    expect_line(session, "FOCUS 1 activated")
    wait_pixels(vdp, decorated_points((100, 100, 300, 200)))
    drag(vdp, (100, 200), (400, 200))
    expect_line(session, "RESIZED 1 %dx184" % (MIN_WIDTH - 2))
    wait_pixels(vdp, decorated_points((400 - MIN_WIDTH, 100, MIN_WIDTH, 200)))
    quit_nook(session, base)
    check_clean(session)


def test_nook_a_grab_zone_reaches_2_pixels_outside_the_border(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    # Window 2 is in front; window 1 is under the right of it.
    run_client(session, "create 300,80,200,200 Under "
               "create 100,100,200,150 Front")
    expect_line(session, "FOCUS 2 activated")
    wait_pixels(vdp, face_points((100, 100, 200, 150)))
    # The right border is column 299, and column 301, 2 pixels outside it,
    # still resizes window 2.
    drag(vdp, (301, 175), (321, 175))
    expect_line(session, "RESIZED 2 218x134")
    wait_pixels(vdp, {(319, 175): TITLE_BAR, (320, 175): FACE})
    # The right border is now column 319.  Column 322, 3 pixels outside it,
    # is window 1's, and a press there activates it.
    click(vdp, (322, 175))
    expect_line(session, "FOCUS 2 deactivated")
    expect_line(session, "FOCUS 1 activated")
    quit_nook(session, base)
    check_clean(session)


def drag_across_while_retitled(session, title, retitle):
    """Start nook in outline mode, have wintest make a window titled title
    and a second window over it, and drag the second window's outline across
    the first while wintest retitles it, if retitle is set.

    @param session The session.
    @param title The first window's title.
    @param retitle The title wintest gives it during the drag, or None.

    @return The canvas held during the drag after the retitle, and the
      canvas after the release, as bytes; the number of context commands
      before nook.
    """
    vdp = session.vdp
    base = run_nook(vdp, demo=False)
    script = "create 300,200,200,100 %s create 40,40,200,100 Mover" % title
    if retitle is not None:
        script += " sleep 3000 title 1 %s" % retitle
    run_client(session, script)
    expect_line(session, "wintest: window 2")
    wait_pixels(vdp, decorated_points((40, 40, 200, 100)))
    vdp.mouse(100, 45)
    vdp.mouse(100, 45, buttons=1)
    settle(vdp)
    pressed = with_outline(bytes(vdp.canvas), (40, 40, 200, 100))
    hold_drag(vdp, (100, 45), (370, 255))
    outline = moved((40, 40, 200, 100), 270, 210)
    wait_canvas(vdp, with_outline(pressed, outline))
    if retitle is not None:
        expect_line(session, "wintest: titled 1")
        time.sleep(1)
    held = bytes(vdp.canvas)
    assert held == with_outline(pressed, outline)
    vdp.mouse(370, 255)
    wait_pixels(vdp, {(310, 250): TITLE_BAR, (509, 349): TITLE_BAR})
    settle(vdp)
    return held, bytes(vdp.canvas), base


def test_nook_a_retitle_during_an_outline_drag_is_painted_on_release(
        session):
    boot_nook(session)
    vdp = session.vdp
    vdp_login_as(vdp, "root", "rootroot")
    _held, during, base = drag_across_while_retitled(session, "Before",
                                                     "Renamed")
    quit_nook(session, base)
    session.child.expect(r"wintest: disconnected\r*\n", timeout=10)
    vdp.expect(PROMPT + "$")
    _held, before, base = drag_across_while_retitled(session, "Renamed",
                                                     None)
    assert during == before, sorted(changed_pixels(during, before))[:8]
    quit_nook(session, base)
    check_clean(session)
