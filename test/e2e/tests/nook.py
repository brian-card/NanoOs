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

import gfxrecords as gr
from harness import (PROMPT, VDP_CONFIG_SYS, heap_at_prompt,
                     message_args_reports, ps_entries)
from tests.graphics import (ACQUIRE, RELEASE, assert_pixels, glyph_pixels,
                            wait_set_up)
from tests.vdp_drv import VDP_FONT_ID


NOOK_LINE = "GRAPHICS=/system/drivers/vdp.drv /system/commands/sysnook\n"
HOMES = ("/home", "/home/user1", "/home/user2")

# The layout in Wm.h and sysnook's main.c.
TITLE_HEIGHT = 12
GRIP = 8
KEEP_TITLE = 32
MIN_WIDTH = 64
MIN_HEIGHT = TITLE_HEIGHT + 16
CELL = 20
FRAMES = [(24, 24, 200, 150), (260, 40, 180, 130), (300, 200, 320, 252)]
BODIES = [gr.LIGHT_GRAY, gr.YELLOW, gr.WHITE]
DESKTOP = gr.CYAN
TITLE_BAR = gr.BLUE
GRIP_COLOUR = gr.DARK_GRAY
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
    """Wait until pixels of a FakeVdp's canvas have colours.

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


def grip_point(frame):
    """Return the bottom-right pixel of a frame, which is its grip's."""
    left, top, width, height = frame
    return (left + width - 1, top + height - 1)


def start_nook(session, options="", user=None):
    """Log in on the VDP console, run nook there, and wait until the
    window manager has painted the screen.

    @param session The session.
    @param options nook's options.
    @param user (name, password) to log in as, or None for root.

    @return The number of context commands FakeVdp had received before.
    """
    vdp = session.vdp
    vdp_login_as(vdp, *(user or ("root", "rootroot")))
    base = len(vdp.context_events())
    vdp.type(("nook " + options).strip() + "\r")
    assert vdp.wait_context_events(base + 1)[base:base + 1] == ACQUIRE
    wait_pixels(vdp, {grip_point(FRAMES[2]): GRIP_COLOUR})
    return base


def quit_nook(session, base):
    """Press Ctrl+Q and wait until the display has been given back and the
    VDP console shows its prompt again.

    @param session The session.
    @param base The number of context commands before nook.
    """
    vdp = session.vdp
    vdp.ctrl("q")
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
    assert message_args_reports(session) == []


# Step 1:  nook, the display, and the console.

def test_nook_ctrl_q_gives_the_console_back_cleared_to_its_prompt(session):
    boot_nook(session)
    vdp = session.vdp
    vdp_login_as(vdp, "root", "rootroot")
    vdp.type("echo before\r")
    vdp.expect(r"\r\nbefore\r\n" + PROMPT)
    prompt = [line for line in vdp.grid_lines() if line][-1]
    base = len(vdp.context_events())
    vdp.type("nook\r")
    assert vdp.wait_context_events(base + 1)[base:base + 1] == ACQUIRE
    wait_pixels(vdp, {grip_point(FRAMES[2]): GRIP_COLOUR, (0, 0): DESKTOP,
                      (639, 479): DESKTOP})
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
    base = start_nook(session, user=("user1", "user1user1"))
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

def glyph_on(vdp, code, colour, background):
    """Return a glyph of the driver's font as rows of palette indices.

    @param vdp The FakeVdp.
    @param code The character code.
    @param colour The palette index of its set pixels.
    @param background The palette index under its clear pixels.

    @return The rows.
    """
    return [[background if pixel == gr.BLACK else pixel for pixel in row]
            for row in glyph_pixels(vdp, VDP_FONT_ID, code, colour)]


def cell_fill(row, column):
    """Return the fill colour of a cell of window 2."""
    return CELL_FILLS[(row + column) & 3]


def window_points(frame, body):
    """Return sample points of a plain window:  its title bar, body and grip.

    @param frame The window's (left, top, width, height).
    @param body The palette index of its body, or None to leave it out.

    @return A dictionary of (x, y) to palette index.
    """
    left, top, width, height = frame
    right, bottom = left + width, top + height
    points = {(right - 12, top + 1): TITLE_BAR, (left, top): TITLE_BAR,
              (right - GRIP, bottom - GRIP): GRIP_COLOUR,
              (right - 1, bottom - 1): GRIP_COLOUR}
    if body is not None:
        points[(left + 5, bottom - 5)] = body
        points[(right - GRIP - 1, bottom - GRIP - 1)] = body
    return points


def assert_windows(vdp, frames):
    """Check sample pixels of the desktop and the three windows, each at a
    frame, window 2 full of cells, and each window's title.

    @param vdp The FakeVdp.
    @param frames The windows' frames.
    """
    expected = {}
    for number, frame in enumerate(frames):
        body = BODIES[number] if number < 2 else None
        expected.update(window_points(frame, body))
    left, top = frames[2][0], frames[2][1] + TITLE_HEIGHT
    expected.update({
        # Row 1, column 0:  fill and outline corners.
        (left + 10, top + 30): cell_fill(1, 0),
        (left, top + CELL): CELL_EDGE, (left + 19, top + 39): CELL_EDGE,
        # Row 11, column 15, just inside the grip's corner.
        (left + 311, top + 231): cell_fill(11, 15)})
    for point in ((0, 0), (639, 479), (250, 300)):
        expected[point] = DESKTOP
    assert_pixels(vdp, expected)
    for number, frame in enumerate(frames):
        assert vdp.pixels(frame[0] + 4, frame[1] + 2, 6, 8) == glyph_on(
            vdp, ord("W"), gr.WHITE, TITLE_BAR), number
    # The row of text across row 0:  its third character, over cell 0.
    assert vdp.pixels(left + 12, top + 6, 6, 8) == glyph_on(
        vdp, ROW_TEXT[2], gr.WHITE, cell_fill(0, 0))


def test_nook_paints_its_windows_exactly(session):
    boot_nook(session)
    base = start_nook(session)
    assert_windows(session.vdp, FRAMES)
    quit_nook(session, base)
    check_clean(session)


def test_nook_cells_painted_by_call_match_cells_sent_as_records(session):
    boot_nook(session)
    vdp = session.vdp
    calls = vdp.count_events("call")
    base = start_nook(session)
    records = bytes(vdp.canvas)
    assert vdp.count_events("call") == calls
    quit_nook(session, base)
    vdp.expect(PROMPT + "$")
    base = len(vdp.context_events())
    vdp.type("nook --content=call\r")
    assert vdp.wait_context_events(base + 1)[base:base + 1] == ACQUIRE
    wait_pixels(vdp, {grip_point(FRAMES[2]): GRIP_COLOUR})
    assert_windows(vdp, FRAMES)
    assert bytes(vdp.canvas) == records
    # One call of a preloaded row for each of the 12 rows.
    assert vdp.count_events("call") == calls + 12
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
    assert vdp.pixels(frame[0] + 4, frame[1] + 2, 6, 8) == glyph_on(
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
    left, top = FRAMES[2][0], FRAMES[2][1] + TITLE_HEIGHT
    # Window 2's cells beside and below window 1, and where it was.
    expected[(left + 10, top + 30)] = cell_fill(1, 0)
    expected[(left + 310, top + 230)] = cell_fill(11, 15)
    expected[(FRAMES[1][0] + 5, FRAMES[1][1] + 5)] = DESKTOP
    wait_pixels(vdp, expected)
    drag(vdp, (390, 255), (300, 45))
    wait_canvas(vdp, initial)
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
    # Grabbed 170 pixels into its title bar, below window 0, window 1 would
    # go to x -170.
    drag(vdp, (430, 45), (0, 245))
    wait_pixels(vdp, {(KEEP_TITLE - 1, 241): TITLE_BAR,
                      (KEEP_TITLE, 241): DESKTOP,
                      (KEEP_TITLE - 1, 240 + FRAMES[1][3] - 1): GRIP_COLOUR})
    # Window 0, grabbed 4 pixels in, would go to x 635 and y 473.
    drag(vdp, (FRAMES[0][0] + 4, FRAMES[0][1] + 6), (639, 479))
    wait_pixels(vdp, {(640 - KEEP_TITLE, 479): TITLE_BAR,
                      (639 - KEEP_TITLE, 479): DESKTOP,
                      (639, 480 - TITLE_HEIGHT): TITLE_BAR,
                      (639, 479 - TITLE_HEIGHT): DESKTOP})
    quit_nook(session, base)
    check_clean(session)


# Step 4:  live resizes.

def test_nook_a_window_resized_live_grows_and_shrinks_exactly(session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, "--resize=live")
    anchored = {(310, 242): cell_fill(1, 0)}
    # Grabbed 5 and 4 pixels from its corner, window 2 grows to 340x274.
    drag(vdp, (615, 448), (635, 470))
    grown = (300, 200, 340, 274)
    expected = window_points(grown, None)
    expected.update(anchored)
    expected.update({
        (639, 201): TITLE_BAR, (630, 300): gr.WHITE, (625, 460): gr.WHITE,
        # The old grip's place shows the cell under it.
        (615, 448): cell_fill(11, 15), (612, 444): cell_fill(11, 15)})
    wait_pixels(vdp, expected)
    # Grabbed 5 and 5 pixels from its corner, it shrinks to 155x135.
    drag(vdp, (635, 469), (450, 330))
    shrunk = (300, 200, 155, 135)
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
    drag(vdp, (615, 448), (0, 0))
    smallest = (300, 200, MIN_WIDTH, MIN_HEIGHT)
    expected = window_points(smallest, None)
    expected.update({
        (363, 201): TITLE_BAR, (364, 201): DESKTOP,
        (305, 226): cell_fill(0, 0), (305, 228): DESKTOP,
        (299, 226): DESKTOP, (364, 226): DESKTOP})
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
    base = len(vdp.context_events())
    vdp.type(("nook " + options).strip() + "\r")
    assert vdp.wait_context_events(base + 1)[base:base + 1] == ACQUIRE
    wait_pixels(vdp, {grip_point(FRAMES[2]): GRIP_COLOUR})
    return base


def outline_then_live(session, options, start, end, outline):
    """Drag in outline mode, checking the outline before the release, then
    check that the canvas after the release equals live mode's after the same
    drag.

    @param session The session.
    @param options The nook options for outline mode.
    @param start The (x, y) of the press.
    @param end The (x, y) of the release.
    @param outline The frame the outline should have at the end.
    """
    vdp = session.vdp
    base = start_nook(session, options)
    initial = bytes(vdp.canvas)
    hold_drag(vdp, start, end)
    wait_canvas(vdp, with_outline(initial, outline))
    vdp.mouse(*end)
    wait_pixels(vdp, {grip_point(outline): GRIP_COLOUR})
    time.sleep(1)
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
                      moved(FRAMES[1], 90, 210))


def test_nook_an_outline_resize_changes_nothing_but_the_outline_until_release(
        session):
    boot_nook(session)
    outline_then_live(session, "--resize=outline", (615, 448), (450, 330),
                      (300, 200, 155, 134))


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
    hold_drag(vdp, (615, 448), (450, 330))
    wait_canvas(vdp, with_outline(initial, (300, 200, 155, 134)))
    revoke(session, base)
    check_clean(session)


# Step 10:  leaks.

def test_nook_gives_back_all_the_memory_it_used(session):
    boot_nook(session)
    vdp = session.vdp
    vdp_login_as(vdp, "root", "rootroot")
    heaps = []
    for _run in range(18):
        base = len(vdp.context_events())
        vdp.type("nook --content=call --move=live\r")
        assert vdp.wait_context_events(base + 1)[base:base + 1] == ACQUIRE
        wait_pixels(vdp, {grip_point(FRAMES[2]): GRIP_COLOUR})
        drag(vdp, (300, 45), (390, 255))
        wait_pixels(vdp, window_points(moved(FRAMES[1], 90, 210), BODIES[1]))
        quit_nook(session, base)
        vdp.expect(PROMPT + "$")
        heaps.append(heap_at_prompt(session))
    # The first runs fill the kernel's caches, fragment the heap, and give
    # long-lived processes a capability entry for each PID the free queue hands
    # out in turn; free memory then swings by up to 32 bytes with the shells'
    # own state.  A leak of 16 bytes a run loses 96 over the six runs between
    # these windows.
    assert min(heaps[15:18]) >= min(heaps[9:12]) - 48, heaps
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
    base = start_nook(session, user=("user1", "user1user1"))
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
    owned = re.findall(r"^  0x[0-9a-f]+: (\d+) bytes owned by (\d+)$",
                       session.sh("dumpMemoryAllocations"), re.M)
    return sorted((int(pid), int(size)) for size, pid in owned
                  if int(pid) in pids)


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
    assert allocations(session, owners) == before
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
    base = start_nook(session)
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
