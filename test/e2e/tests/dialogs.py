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
################################################################################



"""End-to-end tests of Nook's dialogs (plan 6):  owned and modal windows,
applications' own dialogs, and the stock message, input and file
dialogs."""

import os
import subprocess
import tempfile
import time

import gfxrecords as gr
import harness
from fakevdp import (KEYCODE_BACKSPACE, KEYCODE_DOWN, KEYCODE_END,
                     KEYCODE_ESCAPE, KEYCODE_HOME, KEYCODE_RETURN,
                     KEYMOD_ALT_LEFT)
from tests.nook import (ACCENT, BORDER, DESKTOP, FACE, FRAME_TOP,
                        INACTIVE_BAR, MIN_WIDTH, boot_nook, button_rect,
                        check_clean, click, decorated_points, expect_line,
                        quit_nook, run_client, settle, start_nook,
                        wait_canvas, wait_pixels)
from tests.widgets import (BodyModel, GLYPH_HEIGHT, GLYPH_WIDTH, body_of,
                           decoded, quiet)

KEYCODE_TAB = 142


def centered(owner, width, height):
    """Return the frame sysnook gives a window centered on its owner.

    @param owner The owner's (left, top, width, height).
    @param width The window's width.
    @param height The window's height.

    @return The window's (left, top, width, height).
    """
    return (owner[0] + (owner[2] - width) // 2,
            owner[1] + (owner[3] - height) // 2, width, height)


def window_points(frame, bar=ACCENT):
    """Return sample points of a decorated window that lie on it:  its
    border's corners, its title bar's and its face's.

    @param frame The window's (left, top, width, height).
    @param bar The palette index of its border and title bar.

    @return A dictionary of (x, y) to palette index.
    """
    return {point: color for point, color in
            decorated_points(frame, bar).items() if color != DESKTOP}


def body_point(frame, x, y):
    """Return a screen point inside a decorated window's body.

    @param frame The window's (left, top, width, height).
    @param x The column in the body.
    @param y The row in the body.

    @return The (x, y) on the screen.
    """
    return (frame[0] + BORDER + x, frame[1] + FRAME_TOP + y)


def close_point(frame):
    """Return the middle of a decorated window's close button.

    @param frame The window's (left, top, width, height).

    @return The (x, y) on the screen.
    """
    rect = button_rect(frame, 2)
    return (rect[0] + (rect[2] // 2), rect[1] + (rect[3] // 2))


def minimize_point(frame):
    """Return the middle of a decorated window's minimize button.

    @param frame The window's (left, top, width, height).

    @return The (x, y) on the screen.
    """
    rect = button_rect(frame, 0)
    return (rect[0] + (rect[2] // 2), rect[1] + (rect[3] // 2))


def boot_with(session, **scripts):
    """Write wintest scripts into the image, since a mush line is too short
    for most of them and a file can be written only before the simulator
    boots, then boot it for nook.

    @param session The session.
    @param scripts Each script's commands, by its name in /etc.
    """
    for name, commands in scripts.items():
        session.write_file("/etc/" + name, commands)
    boot_nook(session)


def run_script(session, name):
    """Start wintest run with the commands of a script boot_with wrote.

    @param session The session.
    @param name The script's name.
    """
    run_client(session, "@/etc/" + name)


# Step 1:  owned and modal windows.

OWNER = (40, 40, 300, 200)
OTHER = (400, 300, 200, 120)
ELSEWHERE = (400, 20, 200, 100)
# The owner's button, at 10,10 in its body, is outside its dialog.
PUSH = (10, 10, 60, 14)


OWNER_AND_DIALOG = ("button 1 10,10,60,14 Push window 40,40,300,200 Owner "
                    "button 1 10,10,60,14 Other window 400,300,200,120 Other "
                    "create 0,0,160,80 Dialog owner=1 flags=modal")


def start_owner_and_dialog(session, **scripts):
    """Start nook and a wintest with an owner window and another window,
    both with a button, and a modal dialog owned by the first.

    @param session The session.
    @param scripts More scripts for boot_with.

    @return The number of context commands before nook.
    """
    boot_with(session, dialogs=OWNER_AND_DIALOG, **scripts)
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    for line in ("wintest: window 1", "wintest: window 2", "wintest: window 3",
                 "FOCUS 2 deactivated", "FOCUS 3 activated"):
        expect_line(session, line)
    return base


def test_dialogs_a_press_on_a_modal_dialogs_owner_activates_the_dialog(
        session):
    base = start_owner_and_dialog(
        session, other="button 1 10,10,60,14 Else window 400,20,200,100 Else")
    vdp = session.vdp
    dialog = centered(OWNER, 160, 80)
    wait_pixels(vdp, window_points(dialog))
    push = body_point(OWNER, 40, 17)
    # The owner's button takes no press, and its client gets no COMMAND.
    click(vdp, push)
    quiet(session)
    # Another window of the same client still takes presses and keys.
    click(vdp, body_point(OTHER, 40, 17))
    expect_line(session, "FOCUS 3 deactivated")
    expect_line(session, "FOCUS 2 activated")
    expect_line(session, "COMMAND 2 1 0")
    vdp.type("a")
    expect_line(session, "KEY 2 a")
    # A press on the owner again activates the dialog, and keys go to it.
    click(vdp, push)
    expect_line(session, "FOCUS 2 deactivated")
    expect_line(session, "FOCUS 3 activated")
    vdp.type("b")
    expect_line(session, "KEY 3 b")
    expect_line(session, "KEY 3 b up")
    quiet(session)
    wait_pixels(vdp, window_points(dialog))
    wait_pixels(vdp, {(OWNER[0] + 2, OWNER[1] + 2): INACTIVE_BAR})
    # Another client's window takes input too.
    run_script(session, "other")
    expect_line(session, "wintest: window 4")
    expect_line(session, "FOCUS 3 deactivated")
    click(vdp, body_point(ELSEWHERE, 40, 17))
    expect_line(session, "COMMAND 4 1 0")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_a_dialog_of_a_dialog_takes_the_input_until_it_closes(
        session):
    boot_with(session, dialogs="closes button 1 10,10,60,14 Push "
              "window 40,40,300,200 Owner "
              "create 0,0,200,120 A owner=1 flags=modal "
              "create 0,0,120,60 B owner=2 flags=modal "
              "create 400,300,200,120 Other")
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    for line in ("wintest: window 1", "wintest: window 2", "wintest: window 3",
                 "wintest: window 4", "FOCUS 4 activated"):
        expect_line(session, line)
    first = centered(OWNER, 200, 120)
    second = centered(first, 120, 60)
    push = body_point(OWNER, 40, 17)
    # A press on the owner, or on A, activates B.
    click(vdp, push)
    expect_line(session, "FOCUS 4 deactivated")
    expect_line(session, "FOCUS 3 activated")
    click(vdp, body_point(first, 5, 100))
    quiet(session)
    wait_pixels(vdp, window_points(second))
    # Closing B makes A take the input.
    click(vdp, close_point(second))
    expect_line(session, "CLOSE_REQUESTED 3")
    expect_line(session, "FOCUS 2 activated")
    click(vdp, push)
    quiet(session)
    wait_pixels(vdp, {(first[0] + 2, first[1] + 2): ACCENT})
    # Closing A makes the owner take the input.
    click(vdp, close_point(first))
    expect_line(session, "CLOSE_REQUESTED 2")
    expect_line(session, "FOCUS 1 activated")
    click(vdp, push)
    expect_line(session, "COMMAND 1 1 0")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_destroying_an_owner_destroys_its_dialogs_and_their_pixels(
        session):
    boot_with(session, dialogs="create 40,40,300,200 Owner "
              "create 0,0,200,120 A owner=1 flags=modal "
              "create 0,0,120,60 B owner=2 flags=modal wait 8000 destroy 1")
    vdp = session.vdp
    base = start_nook(session, demo=False)
    vdp.mouse(600, 460)
    settle(vdp)
    initial = bytes(vdp.canvas)
    run_script(session, "dialogs")
    for line in ("wintest: window 1", "wintest: window 2", "wintest: window 3"):
        expect_line(session, line)
    second = centered(centered(OWNER, 200, 120), 120, 60)
    wait_pixels(vdp, window_points(second))
    wait_canvas(vdp, initial)
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_an_owner_of_another_clients_is_refused(session):
    boot_with(session, other="create 0,0,100,60 Dialog owner=1 flags=modal "
              "create 0,0,100,60 Missing owner=9 flags=modal")
    base = start_nook(session, demo=False)
    run_client(session, "create 40,40,300,200 Owner")
    expect_line(session, "wintest: window 1")
    run_script(session, "other")
    expect_line(session, "wintest: create: Operation not permitted")
    expect_line(session, "wintest: create: No such entry found")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_minimizing_an_owner_hides_its_windows_and_restoring_shows_them(
        session):
    boot_with(session, dialogs="create 40,40,300,200 Owner "
              "create 0,0,160,80 Tool owner=1")
    vdp = session.vdp
    base = start_nook(session, demo=False)
    vdp.mouse(600, 460)
    settle(vdp)
    initial = bytes(vdp.canvas)
    run_script(session, "dialogs")
    for line in ("wintest: window 1", "wintest: window 2",
                 "FOCUS 2 activated"):
        expect_line(session, line)
    tool = centered(OWNER, 160, 80)
    wait_pixels(vdp, window_points(tool))
    # The owner takes presses, since its window isn't modal; minimizing it
    # minimizes the window it owns.
    click(vdp, minimize_point(OWNER))
    expect_line(session, "STATE 1 minimized")
    expect_line(session, "STATE 2 minimized")
    vdp.mouse(600, 460)
    wait_canvas(vdp, initial)
    vdp.key_packet(9, KEYMOD_ALT_LEFT, KEYCODE_TAB)
    vdp.key_packet(9, KEYMOD_ALT_LEFT, KEYCODE_TAB, down=0)
    expect_line(session, "STATE 1 normal")
    expect_line(session, "STATE 2 normal")
    expect_line(session, "FOCUS 1 activated")
    # Restored, the owned window is above its owner again, which is active.
    wait_pixels(vdp, {(tool[0] + 2, tool[1] + 2): INACTIVE_BAR,
                      (tool[0] + 20, tool[1] + 40): FACE})
    wait_pixels(vdp, {(OWNER[0] + 2, OWNER[1] + 2): ACCENT})
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_minimizing_a_modal_dialog_minimizes_its_owner_with_it(
        session):
    base = start_owner_and_dialog(session)
    vdp = session.vdp
    dialog = centered(OWNER, 160, 80)
    wait_pixels(vdp, window_points(dialog))
    click(vdp, minimize_point(dialog))
    expect_line(session, "STATE 1 minimized")
    expect_line(session, "STATE 3 minimized")
    vdp.key_packet(9, KEYMOD_ALT_LEFT, KEYCODE_TAB)
    vdp.key_packet(9, KEYMOD_ALT_LEFT, KEYCODE_TAB, down=0)
    expect_line(session, "STATE 1 normal")
    expect_line(session, "STATE 3 normal")
    expect_line(session, "FOCUS 3 activated")
    wait_pixels(vdp, window_points(dialog))
    quit_nook(session, base)
    check_clean(session)



def stop_and_copy_tree(session, files, directory):
    """Stop the simulator and copy a tree of empty files into its image in
    one go, since a file at a time is slow.

    @param session The session.
    @param files The files' names.
    @param directory Where they go, as a process names it.
    """
    session.child.terminate(force=True)
    with tempfile.TemporaryDirectory() as root:
        tree = os.path.join(root, os.path.basename(directory))
        os.mkdir(tree)
        for name in files:
            open(os.path.join(tree, name), "w").close()
        subprocess.run(["mcopy", "-s", "-o", "-i",
                        session.img_copy + "@@2097152", tree,
                        "::" + os.path.dirname(harness.image_path(directory))],
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"),
                       check=True)


# Step 2:  applications' own dialogs.

# The owner is centered like FILE_OWNER, so that a file dialog over the
# application's dialog has FILE_FRAME.
APP_DIALOG = (
    "create 20,20,600,400 Owner "
    "field 1 10,10,120,14 20 - check 2 10,30,80,12 0 Check "
    "button 3 10,60,60,14 OK flags=default "
    "button 4 80,60,60,14 Cancel flags=cancel "
    "button 5 150,60,60,14 Browse ")
APP_FRAME = centered((20, 20, 600, 400), 240, 100)


def test_dialogs_an_applications_dialog_ends_only_on_its_ending_buttons(
        session):
    boot_with(session, dialogs=APP_DIALOG + "dialog 1 0,0,240,100 Dialog "
              "get 2 1 text get 2 2 value destroy 2 "
              "button 4 80,60,60,14 Cancel flags=cancel "
              "dialog 1 0,0,240,100 Again")
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    expect_line(session, "wintest: window 2")
    # Browse has no flag:  its COMMAND goes to the callback and the dialog
    # stays.
    click(vdp, body_point(APP_FRAME, 180, 67))
    expect_line(session, "COMMAND 2 5 0")
    click(vdp, body_point(APP_FRAME, 40, 17))
    vdp.type_paced("hi")
    click(vdp, body_point(APP_FRAME, 14, 36))
    expect_line(session, "COMMAND 2 2 1")
    click(vdp, body_point(APP_FRAME, 40, 67))
    expect_line(session, "wintest: dialog 2: 3")
    expect_line(session, "wintest: get 2 1 text hi")
    expect_line(session, "wintest: get 2 2 value 1")
    expect_line(session, "wintest: window 3")
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: dialog 3: 0")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_browse_opens_a_file_dialog_over_the_applications_dialog(
        session):
    boot_with(session, dialogs=APP_DIALOG
              + "dialog 1 0,0,240,100 Dialog browse=5")
    vdp = session.vdp
    session.sh("mkdir /tmp/t")
    session.sh("touch /tmp/t/b.txt")
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    expect_line(session, "wintest: window 2")
    click(vdp, body_point(APP_FRAME, 180, 67))
    expect_line(session, "COMMAND 2 5 0")
    wait_for(lambda: look_in(vdp) == "Look in: /root/")
    click(vdp, NAME_POINT)
    vdp.type_paced("/tmp/t/b.txt\r")
    expect_line(session, "wintest: browse: ok /tmp/t/b.txt")
    # The application's dialog takes the input again.
    click(vdp, body_point(APP_FRAME, 14, 36))
    expect_line(session, "COMMAND 2 2 1")
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: dialog 2: 0")
    quit_nook(session, base)
    check_clean(session)


# Step 3:  message dialogs.

LINE_HEIGHT = GLYPH_HEIGHT + 4
MARGIN = 12
ICON = 16
BUTTON_WIDTH = 60
BUTTON_HEIGHT = 14
BUTTON_GAP = 6
MESSAGE_OWNER = (40, 40, 400, 300)


def message_layout(owner, lines, icon, labels):
    """Return where a message's parts are, as sysnook lays them out.

    @param owner The owner's frame.
    @param lines The message's lines.
    @param icon Whether it has an icon.
    @param labels Its buttons' texts.

    @return The frame, the left of the lines, and the buttons' rectangles in
      the body.
    """
    left = MARGIN + ((ICON + MARGIN) if icon else 0)
    count = len(labels)
    width = max(left + max(len(line) for line in lines) * GLYPH_WIDTH + MARGIN,
                2 * MARGIN + count * BUTTON_WIDTH + (count - 1) * BUTTON_GAP,
                MIN_WIDTH - 2 * BORDER)
    height = max(len(lines) * LINE_HEIGHT, ICON if icon else 0)
    top = MARGIN + height + MARGIN
    frame = centered(owner, width + 2 * BORDER,
                     top + BUTTON_HEIGHT + MARGIN + FRAME_TOP + BORDER)
    first = width - MARGIN - count * BUTTON_WIDTH - (count - 1) * BUTTON_GAP
    buttons = [(first + index * (BUTTON_WIDTH + BUTTON_GAP), top,
                BUTTON_WIDTH, BUTTON_HEIGHT) for index in range(count)]
    return frame, left, buttons


def draw_icon(model, left, top, icon):
    """Draw a message's icon as FakeVdp fills vdp.drv's circle or triangle.

    @param model The BodyModel.
    @param left The icon's left edge on the screen.
    @param top Its top edge.
    @param icon "information", "warning", "error" or "question".
    """
    if icon == "warning":
        corners = ((left + 8, top), (left + 15, top + 15), (left, top + 15))

        def side(start, end, point):
            return ((end[0] - start[0]) * (point[1] - start[1])
                    - (end[1] - start[1]) * (point[0] - start[0]))
        for row in range(top, top + 16):
            for column in range(left, left + 16):
                sides = [side(corners[index], corners[(index + 1) % 3],
                              (column, row)) for index in range(3)]
                if all(value >= 0 for value in sides) or all(
                        value <= 0 for value in sides):
                    model._plot(column, row, gr.YELLOW)
        model.text(left + 8 - GLYPH_WIDTH // 2, top + 8 - GLYPH_HEIGHT // 2,
                   "!", gr.BLACK)
        return
    face = gr.RED if icon == "error" else ACCENT
    for row in range(top, top + 16):
        for column in range(left, left + 16):
            if ((column - left - 8) ** 2 + (row - top - 8) ** 2) ** 0.5 <= 7:
                model._plot(column, row, face)
    mark = {"information": "i", "error": "x", "question": "?"}[icon]
    model.text(left + 8 - GLYPH_WIDTH // 2, top + 8 - GLYPH_HEIGHT // 2, mark,
               gr.WHITE)


def message_model(vdp, lines, icon, labels, owner=MESSAGE_OWNER):
    """Return the model of a message and its frame.

    @param vdp The FakeVdp.
    @param lines The message's lines.
    @param icon Its icon's name, or None.
    @param labels Its buttons' texts.
    @param owner The owner's frame.

    @return The BodyModel and the frame.
    """
    frame, left, buttons = message_layout(owner, lines, icon is not None,
                                          labels)
    model = BodyModel(vdp, frame)
    body = body_of(frame)
    if icon is not None:
        draw_icon(model, body[0] + MARGIN, body[1] + MARGIN, icon)
    for index, line in enumerate(lines):
        model.label((body[0] + left, body[1] + MARGIN + index * LINE_HEIGHT,
                     len(line) * GLYPH_WIDTH, LINE_HEIGHT), "left", line)
    for index, label in enumerate(labels):
        x, y, width, height = buttons[index]
        model.button((body[0] + x, body[1] + y, width, height), label,
                     default=(index == 0), focused=(index == 0))
    return model, frame


def button_point(frame, lines, icon, labels, index):
    """Return the middle of one of a message's buttons on the screen."""
    _frame, _left, buttons = message_layout(MESSAGE_OWNER, lines, icon,
                                            labels)
    x, y, width, height = buttons[index]
    return body_point(frame, x + width // 2, y + height // 2)


def test_dialogs_a_message_is_drawn_exactly_and_answers_with_its_button(
        session):
    boot_with(session, dialogs="create 40,40,400,300 Owner "
              "message 1 information okcancel Note Hello_there,_world. "
              "message 1 information okcancel Note Hello_again.")
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    lines = ["Hello there, world."]
    model, frame = message_model(vdp, lines, "information", ["OK", "Cancel"])
    model.check(vdp)
    body = body_of(frame)
    assert decoded(vdp, body[0] + 40, body[1] + 14, 19) == lines[0]
    click(vdp, button_point(frame, lines, True, ["OK", "Cancel"], 0))
    expect_line(session, "wintest: message: ok")
    lines = ["Hello again."]
    model, frame = message_model(vdp, lines, "information", ["OK", "Cancel"])
    model.check(vdp)
    click(vdp, button_point(frame, lines, True, ["OK", "Cancel"], 1))
    expect_line(session, "wintest: message: cancel")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_a_message_wraps_at_the_word_that_doesnt_fit(session):
    boot_with(session, dialogs="create 40,40,400,300 Owner message 1 none ok "
              "Wrap The_quick_brown_fox_jumps_over_the_lazy_dog_and_keeps_"
              "running_far_away")
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    lines = ["The quick brown fox jumps over the lazy",
             "dog and keeps running far away"]
    model, frame = message_model(vdp, lines, None, ["OK"])
    model.check(vdp)
    body = body_of(frame)
    for index, line in enumerate(lines):
        assert decoded(vdp, body[0] + MARGIN,
                       body[1] + MARGIN + 2 + index * LINE_HEIGHT,
                       len(line)) == line
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    expect_line(session, "wintest: message: ok")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_each_icon_and_button_set_is_drawn_and_answers_exactly(
        session):
    boot_with(session, dialogs="create 40,40,400,300 Owner "
              "message 1 warning ok Warn Careful. "
              "message 1 error yesno Error Failed. "
              "message 1 question yesnocancel Ask Sure? "
              "message 1 question yesnocancel Ask Really? "
              "message 1 error yesno Error Failed_again. "
              "message 1 information ok Info Done.")
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    # Escape presses the cancel button:  OK alone, No, or Cancel.
    for icon, labels, text, result in (
            ("warning", ["OK"], "Careful.", "ok"),
            ("error", ["Yes", "No"], "Failed.", "no"),
            ("question", ["Yes", "No", "Cancel"], "Sure?", "cancel")):
        model, _frame = message_model(vdp, [text], icon, labels)
        model.check(vdp)
        vdp.key_packet(27, 0, KEYCODE_ESCAPE)
        expect_line(session, "wintest: message: " + result)
    # Each button gives its own result, and the close button cancel.
    lines = ["Really?"]
    labels = ["Yes", "No", "Cancel"]
    model, frame = message_model(vdp, lines, "question", labels)
    model.check(vdp)
    click(vdp, button_point(frame, lines, True, labels, 1))
    expect_line(session, "wintest: message: no")
    lines = ["Failed again."]
    model, frame = message_model(vdp, lines, "error", ["Yes", "No"])
    model.check(vdp)
    click(vdp, button_point(frame, lines, True, ["Yes", "No"], 0))
    expect_line(session, "wintest: message: yes")
    model, frame = message_model(vdp, ["Done."], "information", ["OK"])
    model.check(vdp)
    click(vdp, close_point(frame))
    expect_line(session, "wintest: message: cancel")
    quit_nook(session, base)
    check_clean(session)


# Step 4:  input dialogs.

INPUT_FRAME = centered(MESSAGE_OWNER, 264 + 2 * BORDER,
                       MARGIN + LINE_HEIGHT + 4 + BUTTON_HEIGHT + MARGIN
                       + BUTTON_HEIGHT + MARGIN + FRAME_TOP + BORDER)


def test_dialogs_an_input_dialog_gives_back_exactly_what_was_typed(session):
    boot_with(session, dialogs="create 40,40,400,300 Owner "
              "input 1 Ask Your_name: Bob input 1 Ask Your_name: Bob")
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    body = body_of(INPUT_FRAME)
    wait_pixels(vdp, {(body[0] + MARGIN + 4, body[1] + MARGIN + LINE_HEIGHT
                       + 4 + 3): ACCENT})
    assert decoded(vdp, body[0] + MARGIN, body[1] + MARGIN + 2, 10) \
        == "Your name:"
    # The initial text is selected, so typing replaces it.
    vdp.type_paced("Ann")
    vdp.key_packet(8, 0, KEYCODE_BACKSPACE)
    vdp.type_paced("e")
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    expect_line(session, "wintest: input: ok Ane")
    # Cancel leaves the caller's text as it was.
    vdp.type_paced("Zed")
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: input: cancel Bob")
    quit_nook(session, base)
    check_clean(session)


# Step 5:  file dialogs.

FILE_OWNER = (20, 20, 600, 400)
FILE_FRAME = centered(FILE_OWNER, 400 + 2 * BORDER, 220 + FRAME_TOP + BORDER)
FILE_BODY = body_of(FILE_FRAME)
NAME_POINT = (FILE_BODY[0] + 52 + 100, FILE_BODY[1] + 172 + 7)
ROWS = 14


def read_text(vdp, x, y, columns):
    """Read text from the canvas with the blanks at its end taken off, and
    what a scroll bar beside it decodes to."""
    return decoded(vdp, x, y, columns).rstrip(" ?")


def list_rows(vdp, left, width):
    """Read the rows a file dialog's list shows.

    @param vdp The FakeVdp.
    @param left The list's left edge in the body.
    @param width Its width.

    @return The rows' texts, up to the first empty one.
    """
    rows = []
    for row in range(ROWS):
        text = read_text(vdp, FILE_BODY[0] + left + 3,
                         FILE_BODY[1] + 24 + row * 10,
                         (width - 2 - 2) // GLYPH_WIDTH)
        if text == "":
            break
        rows.append(text)
    return rows


def entries(vdp):
    """Read the entries a file dialog shows."""
    return list_rows(vdp, 116, 276)


def places(vdp):
    """Read the places a file dialog shows."""
    return list_rows(vdp, 8, 100)


def look_in(vdp):
    """Read a file dialog's Look in label."""
    return read_text(vdp, FILE_BODY[0] + 116, FILE_BODY[1] + 10, 46)


def name(vdp):
    """Read a file dialog's name field."""
    return read_text(vdp, FILE_BODY[0] + 56, FILE_BODY[1] + 175, 55)


def wait_for(check, timeout=30):
    """Wait until something about the canvas is true.

    @param check What should become true.
    @param timeout Seconds to wait.
    """
    deadline = time.time() + timeout
    while not check():
        assert time.time() < deadline, "timed out"
        time.sleep(0.2)


def wait_entries(vdp, expected, path):
    """Wait until a file dialog shows a directory's entries.

    @param vdp The FakeVdp.
    @param expected The entries.
    @param path The directory.
    """
    try:
        wait_for(lambda: (entries(vdp) == expected)
                 and (look_in(vdp) == "Look in: " + path))
    except AssertionError:
        raise AssertionError((look_in(vdp), entries(vdp)))


def wait_places(vdp, expected):
    """Wait until a file dialog shows its places, which it draws after a
    directory's entries.

    @param vdp The FakeVdp.
    @param expected The places.
    """
    try:
        wait_for(lambda: places(vdp) == expected)
    except AssertionError:
        raise AssertionError(places(vdp))


def row_point(left, row):
    """Return the middle of a row of one of a file dialog's lists."""
    return (FILE_BODY[0] + left + 30, FILE_BODY[1] + 24 + row * 10 + 4)


def double_click(vdp, point):
    """Press and let go twice at a point."""
    click(vdp, point)
    click(vdp, point)


def make_tree(session):
    """Make /tmp/t:  two directories, one holding a file, and two files."""
    for command in ("mkdir /tmp/t", "mkdir /tmp/t/zdir", "mkdir /tmp/t/adir",
                    "touch /tmp/t/b.txt", "touch /tmp/t/a.log",
                    "touch /tmp/t/zdir/inner.txt"):
        session.sh(command)


OWNER_WINDOW = "create 20,20,600,400 Owner "


def test_dialogs_an_open_dialog_lists_directories_first_and_moves_between(
        session):
    boot_with(session, dialogs=OWNER_WINDOW + "open 1 /tmp/t/")
    vdp = session.vdp
    make_tree(session)
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    tree = ["../", "adir/", "zdir/", "a.log", "b.txt"]
    wait_entries(vdp, tree, "/tmp/t/")
    wait_places(vdp, ["~/", "/etc/", "/tmp/", "/usr/"])
    # zdir is selected first, so that the double click's first press
    # changes nothing and its second comes soon after even under load.
    vdp.key_packet(0, 0, KEYCODE_DOWN)
    double_click(vdp, row_point(116, 2))
    wait_entries(vdp, ["../", "inner.txt"], "/tmp/t/zdir/")
    vdp.key_packet(0, 0, KEYCODE_HOME)
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    wait_entries(vdp, tree, "/tmp/t/")
    vdp.key_packet(0, 0, KEYCODE_END)
    wait_for(lambda: name(vdp) == "b.txt")
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    expect_line(session, "wintest: open: ok /tmp/t/b.txt")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_dotdot_is_listed_only_where_the_parent_may_be_listed(
        session):
    boot_with(session, dialogs=OWNER_WINDOW + "open 1 /root/sub/")
    vdp = session.vdp
    session.sh("mkdir /root/sub")
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    wait_entries(vdp, ["../"], "/root/sub/")
    vdp.key_packet(0, 0, KEYCODE_HOME)
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    wait_for(lambda: look_in(vdp) == "Look in: /root/")
    # Root may not list /, so its home has no "../".
    assert "../" not in entries(vdp)
    assert "sub/" in entries(vdp)
    # A place is listed when it is clicked.
    wait_places(vdp, ["~/", "/etc/", "/tmp/", "/usr/"])
    click(vdp, row_point(8, 2))
    wait_for(lambda: look_in(vdp) == "Look in: /tmp/")
    assert "../" not in entries(vdp)
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: open: cancel /root/sub/")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_a_directory_the_application_may_not_list_shows_why(session):
    boot_with(session, dialogs=OWNER_WINDOW + "drop /tmp/ open 1 /tmp/t/")
    vdp = session.vdp
    make_tree(session)
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    expect_line(session, "wintest: drop /tmp/: ok")
    # Its initial directory can't be listed, so it starts at the first
    # place, and /tmp/ isn't one any more.
    wait_for(lambda: look_in(vdp) == "Look in: /root/")
    wait_places(vdp, ["~/", "/etc/", "/usr/"])
    click(vdp, NAME_POINT)
    vdp.type_paced("/tmp/t/\r")
    wait_for(lambda: look_in(vdp) == "/tmp/t/: permission denied")
    assert entries(vdp) == []
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: open: cancel /tmp/t/")
    quit_nook(session, base)
    check_clean(session)


MANY = ["f%07d" % index for index in range(200)]


def test_dialogs_a_long_directory_ends_with_dots_after_what_fits(session):
    boot_with(session, dialogs=OWNER_WINDOW + "open 1 /tmp/many/")
    stop_and_copy_tree(session, MANY, "/tmp/many")
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    # 1 KB holds "../", 112 names of 9 bytes and "...".
    shown = ["../"] + MANY[:112] + ["..."]
    wait_entries(vdp, shown[:ROWS], "/tmp/many/")
    vdp.key_packet(0, 0, KEYCODE_END)
    wait_for(lambda: entries(vdp) == shown[-ROWS:])
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: open: cancel /tmp/many/")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_saving_over_a_file_asks_first(session):
    boot_with(session, dialogs=OWNER_WINDOW + "save 1 /tmp/t/b.txt "
              "save 1 /tmp/t/a.log")
    vdp = session.vdp
    make_tree(session)
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    expect_line(session, "wintest: window 1")
    wait_entries(vdp, ["../", "adir/", "zdir/", "a.log", "b.txt"], "/tmp/t/")
    assert name(vdp) == "b.txt"
    # A save dialog's places are only those it may create files in.
    wait_places(vdp, ["~/", "/etc/", "/tmp/", "/usr/"])
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    lines = ["Replace b.txt?"]
    labels = ["Yes", "No"]
    model, frame = message_model(vdp, lines, "question", labels,
                                 owner=FILE_FRAME)
    model.check(vdp)
    click(vdp, button_point(frame, lines, True, labels, 1))
    quiet(session)
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    model.check(vdp)
    click(vdp, button_point(frame, lines, True, labels, 0))
    expect_line(session, "wintest: save: ok /tmp/t/b.txt")
    # A name that doesn't exist is saved to without asking, and typing
    # replaces the initial name, which is selected.
    wait_for(lambda: name(vdp) == "a.log")
    vdp.type_paced("new.txt\r")
    expect_line(session, "wintest: save: ok /tmp/t/new.txt")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_a_suffix_shows_only_its_files_and_none_shows_every_file(
        session):
    boot_with(session, dialogs=OWNER_WINDOW + "open 1 /tmp/t/ suffix=.txt "
              "open 1 /tmp/t/")
    vdp = session.vdp
    make_tree(session)
    base = start_nook(session, demo=False)
    run_script(session, "dialogs")
    wait_entries(vdp, ["../", "adir/", "zdir/", "b.txt"], "/tmp/t/")
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: open: cancel /tmp/t/")
    wait_entries(vdp, ["../", "adir/", "zdir/", "a.log", "b.txt"], "/tmp/t/")
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: open: cancel /tmp/t/")
    quit_nook(session, base)
    check_clean(session)


def test_dialogs_a_users_places_are_its_capabilities_and_home_has_no_dotdot(
        session):
    boot_with(session, dialogs=OWNER_WINDOW + "open 1 /home/user1/ "
              "save 1 /home/user1/sub/")
    vdp = session.vdp
    session.child.sendline("exit")
    session.login("user1", "user1user1")
    session.sh("mkdir /home/user1/sub")
    base = start_nook(session, user=("user1", "user1user1"), demo=False)
    run_script(session, "dialogs")
    # user1 may not list /home/, so its home has no "../".
    wait_for(lambda: look_in(vdp) == "Look in: /home/user1/")
    assert entries(vdp)[0] == "sub/", entries(vdp)
    wait_places(vdp, ["~/", "/etc/", "/tmp/", "/usr/bin/"])
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: open: cancel /home/user1/")
    # A subdirectory has one, and a save dialog leaves out the read-only
    # places.
    wait_entries(vdp, ["../"], "/home/user1/sub/")
    wait_places(vdp, ["~/", "/tmp/"])
    click(vdp, row_point(116, 0))
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    wait_for(lambda: look_in(vdp) == "Look in: /home/user1/")
    assert entries(vdp)[0] == "sub/", entries(vdp)
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: save: cancel /home/user1/sub/")
    quit_nook(session, base)
    session.child.sendline("exit")
    session.login()
    check_clean(session)


# Step 6:  events kept while a dialog call waits.

KEEP_SCRIPT = ("button 1 10,10,60,14 Push window 40,40,300,200 Owner "
               "button 1 10,10,60,14 Other window 400,300,200,120 Other ")


def kept_events(session, clicks, finish):
    """Click another window of the client while its dialog call waits, then
    end the call, and check the events winNextEvent gives afterwards.

    @param session The session.
    @param clicks How many clicks.
    @param finish What ends the call, given the FakeVdp.
    """
    vdp = session.vdp
    for _ in range(clicks):
        click(vdp, body_point(OTHER, 40, 17))
        time.sleep(0.3)
    finish(vdp)
    kept = (["FOCUS 2 activated"] + ["COMMAND 2 1 0"] * clicks
            + ["FOCUS 2 deactivated"])
    if len(kept) > 8:
        kept = ["OVERFLOW"] + kept[-8:]
    return kept


def test_dialogs_events_for_other_windows_wait_for_a_message_to_end(session):
    boot_with(session, dialogs=KEEP_SCRIPT + "message 1 none ok Wait Wait.",
              more=KEEP_SCRIPT + "message 4 none ok Wait Again.")
    base = start_nook(session, demo=False)
    for clicks, script, lines in ((3, "dialogs", ["Wait."]),
                                  (10, "more", ["Again."])):
        run_script(session, script)
        expect_line(session, "wintest: window %d" % (2 if clicks == 3 else 5))
        model, frame = message_model(session.vdp, lines, None, ["OK"],
                                     owner=OWNER)
        model.check(session.vdp)
        kept = kept_events(session, clicks, lambda vdp: click(
            vdp, button_point(frame, lines, False, ["OK"], 0)))
        expect_line(session, "wintest: message: ok")
        for line in kept:
            expect_line(session, line.replace(" 2 ", " %d " % (
                2 if clicks == 3 else 5)))
    quit_nook(session, base)
    check_clean(session)


# wintest demo's Dialogs window.

DEMO_DIALOGS = (316, 288, 160, 140)


def test_dialogs_wintest_demo_opens_each_dialog_from_its_dialogs_window(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    session.child.sendline("wintest demo &")
    expect_line(session, "wintest: window 3", timeout=30)
    expect_line(session, "FOCUS 3 activated", timeout=30)
    wait_pixels(vdp, window_points(DEMO_DIALOGS))
    # Message...:  Yes is its default button.
    click(vdp, body_point(DEMO_DIALOGS, 80, 18))
    expect_line(session, "COMMAND 3 1 0")
    lines = ["Save the changes to the document before",
             "closing it?"]
    model, _frame = message_model(vdp, lines, "question",
                                  ["Yes", "No", "Cancel"],
                                  owner=DEMO_DIALOGS)
    model.check(vdp)
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    expect_line(session, "wintest: message: yes")
    # Find... is an application's own dialog, whose texts are read back;
    # the message was window 4.
    click(vdp, body_point(DEMO_DIALOGS, 80, 106))
    expect_line(session, "COMMAND 3 5 0")
    expect_line(session, "wintest: window 5")
    find = centered(DEMO_DIALOGS, 262, 100)
    wait_pixels(vdp, window_points(find))
    click(vdp, body_point(find, 40, 31))
    vdp.type_paced("abc")
    vdp.key_packet(13, 0, KEYCODE_RETURN)
    expect_line(session, "wintest: dialog 5: 4")
    expect_line(session, "wintest: get 5 2 text abc")
    expect_line(session, "wintest: get 5 3 value 0")
    quit_nook(session, base)
    check_clean(session)


def viewports_over(vdp, start, rect):
    """List the viewports drawn since an event that overlap a rectangle.

    @param vdp The FakeVdp.
    @param start The index of the first event.
    @param rect The (left, top, width, height).

    @return The viewports, as FakeVdp's (left, top, right, bottom).
    """
    left, top, width, height = rect
    with vdp.lock:
        events = list(vdp.events[start:])
    return [detail for _when, kind, detail in events
            if (kind == "gviewport") and (detail[0] < left + width)
            and (detail[2] >= left) and (detail[1] < top + height)
            and (detail[3] >= top)]


def test_dialogs_a_dialog_opening_is_drawn_once_with_nothing_over_it(
        session):
    boot_nook(session)
    vdp = session.vdp
    base = start_nook(session, demo=False)
    session.child.sendline("wintest demo &")
    expect_line(session, "wintest: window 3", timeout=30)
    wait_pixels(vdp, window_points(DEMO_DIALOGS))
    point = body_point(DEMO_DIALOGS, 80, 18)
    vdp.mouse(*point)
    settle(vdp)
    with vdp.lock:
        start = len(vdp.events)
    click(vdp, point)
    expect_line(session, "COMMAND 3 1 0")
    lines = ["Save the changes to the document before", "closing it?"]
    model, frame = message_model(vdp, lines, "question",
                                 ["Yes", "No", "Cancel"], owner=DEMO_DIALOGS)
    model.check(vdp)
    settle(vdp)
    whole = (frame[0], frame[1], frame[0] + frame[2] - 1,
             frame[1] + frame[3] - 1)
    drawn = viewports_over(vdp, start, frame)
    first = drawn.index(whole)
    icon = (frame[0] + BORDER + MARGIN, frame[1] + FRAME_TOP + MARGIN,
            frame[0] + BORDER + MARGIN + ICON - 1,
            frame[1] + FRAME_TOP + MARGIN + ICON - 1)
    # Its frame and face, its icon's canvas, then its widgets:  nothing of
    # its owner, and nothing of it drawn again.
    assert drawn[first:] == [whole, icon, whole], drawn[first:]
    vdp.key_packet(27, 0, KEYCODE_ESCAPE)
    expect_line(session, "wintest: message: cancel")
    quit_nook(session, base)
    check_clean(session)
