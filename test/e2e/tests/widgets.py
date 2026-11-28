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



"""End-to-end tests of Nook's widgets (plan 5):  templates, each widget
kind's look and input, focus, anchors, menus and the canvas.  Each check of
a window's body compares it pixel for pixel with BodyModel's drawing of what
sysnook should paint."""

import time

import pexpect

import gfxrecords as gr
from fakevdp import (KEYCODE_DELETE, KEYCODE_DOWN, KEYCODE_END,
                     KEYCODE_ESCAPE, KEYCODE_F10, KEYCODE_HOME, KEYCODE_LEFT,
                     KEYCODE_RETURN, KEYCODE_RIGHT, KEYCODE_UP,
                     KEYMOD_SHIFT, FakeVdp)
KEYCODE_TAB = 142
from harness import heap_at_prompt, settled
from tests.nook import (BORDER, FRAME_TOP, boot_nook, button_rect,
                        check_clean, click,
                        expect_line, quit_nook, run_client, settle,
                        start_nook, wait_pixels)
from tests.vdp_drv import VDP_FONT_ID

FACE = gr.WHITE
TEXT = gr.BLACK
DARK = gr.DARK_GRAY
LIGHT = gr.LIGHT_GRAY
ACCENT = gr.BLUE
ON_ACCENT = gr.WHITE
GLYPH_WIDTH = 6
GLYPH_HEIGHT = 8
# How far in from a group box's left edge its caption starts, and how far
# the gap in its line reaches past each end of the caption.
GROUP_CAPTION_INSET = 6
GROUP_CAPTION_GAP = 1
# A check box's or radio button's box, how far its text is right of it, and
# how far in a radio button's square is.
BOX_SIZE = 9
BOX_GAP = 4
RADIO_INSET = 2
# How far in from a button's edge its focus outline is, and the width of the
# default button's border.
FOCUS_INSET = 2
DEFAULT_BORDER = 2
# How far in from a text field's edge its text starts, and its caret's
# height.
FIELD_INSET = 4
CARET_HEIGHT = 10
LIST_ITEM_HEIGHT = 10
LIST_TEXT_INSET = 2
SCROLL_BAR_WIDTH = 10
KEYCODE_PAGEUP = 146
KEYCODE_PAGEDOWN = 148
MENU_INSET = 2
TITLE_PAD = 6
ITEM_HEIGHT = 12
SEPARATOR_HEIGHT = 5
CHECK_GUTTER = 12
SHORTCUT_GAP = 12
ITEM_PAD = 6


def body_of(frame):
    """Return the body of a decorated window:  its frame less the border and
    title bar."""
    left, top, width, height = frame
    return (left + BORDER, top + FRAME_TOP, width - 2 * BORDER,
            height - FRAME_TOP - BORDER)


class BodyModel:
    """What a window's body should look like:  rows of palette indices,
    drawn the way vdp.drv draws sysnook's records."""

    def __init__(self, vdp, frame, face=FACE):
        """Start a body of a decorated window filled with its face.

        @param vdp The FakeVdp, whose font glyphs are drawn from.
        @param frame The window's (left, top, width, height).
        @param face The palette index of its face.
        """
        self.vdp = vdp
        self.left, self.top, self.width, self.height = body_of(frame)
        self.rows = [[face] * self.width for _ in range(self.height)]
        self.clip = (self.left, self.top, self.width, self.height)

    def scissor(self, rect):
        """Clip what is drawn next to a rectangle inside the body, as a
        widget's own rectangle clips it.

        @param rect The (x, y, width, height) in screen coordinates, or None
          for the whole body.
        """
        self.clip = rect or (self.left, self.top, self.width, self.height)

    def _plot(self, x, y, color):
        clip_x, clip_y, clip_w, clip_h = self.clip
        if (clip_x <= x < clip_x + clip_w) and (clip_y <= y < clip_y + clip_h) \
                and (self.left <= x < self.left + self.width) \
                and (self.top <= y < self.top + self.height):
            self.rows[y - self.top][x - self.left] = color

    def fill(self, rect, color):
        """Fill a rectangle.

        @param rect The (x, y, width, height) in screen coordinates.
        @param color The palette index.
        """
        x, y, width, height = rect
        for row in range(y, y + height):
            for column in range(x, x + width):
                self._plot(column, row, color)

    def outline(self, rect, color):
        """Draw the 1-pixel edge of a rectangle.

        @param rect The (x, y, width, height) in screen coordinates.
        @param color The palette index.
        """
        x, y, width, height = rect
        for column in range(x, x + width):
            self._plot(column, y, color)
            self._plot(column, y + height - 1, color)
        for row in range(y, y + height):
            self._plot(x, row, color)
            self._plot(x + width - 1, row, color)

    def text(self, x, y, text, color):
        """Draw text in the driver's font, leaving its clear pixels alone.

        @param x The left edge of its first glyph.
        @param y Its top edge.
        @param text The text.
        @param color The palette index of its set pixels.
        """
        width, height, data = self.vdp.fonts[VDP_FONT_ID]
        row_bytes = (width + 7) // 8
        for index, ch in enumerate(text):
            for row in range(height):
                start = (ord(ch) * height + row) * row_bytes
                bits = int.from_bytes(data[start:start + row_bytes], "big")
                for column in range(width):
                    if bits & (1 << (row_bytes * 8 - 1 - column)):
                        self._plot(x + index * width + column, y + row, color)

    def line(self, start, end, color):
        """Draw a line, both ends included, as the VDP plots it.

        @param start The (x, y) of one end.
        @param end The (x, y) of the other.
        @param color The palette index.
        """
        for x, y in FakeVdp._line_points(None, start, end):
            self._plot(x, y, color)

    def button(self, rect, text, pressed=False, on=False, default=False,
               focused=False, disabled=False):
        """Draw a push or toggle button as sysnook does.

        @param rect The button's (x, y, width, height) in screen coordinates.
        @param text Its text.
        @param pressed Whether it is held down with the pointer on it.
        @param on Whether it is a toggle button that is in.
        @param default Whether it is the default button.
        @param focused Whether it shows the focus.
        @param disabled Whether it is disabled.
        """
        x, y, width, height = rect
        face, ink = LIGHT, (DARK if disabled else TEXT)
        if pressed:
            face, ink = DARK, ON_ACCENT
        elif on:
            face, ink = ACCENT, ON_ACCENT
        self.scissor(rect)
        self.fill(rect, face)
        if default:
            for inset in range(DEFAULT_BORDER):
                self.outline((x + inset, y + inset, width - 2 * inset,
                              height - 2 * inset), ACCENT)
        else:
            self.outline(rect, DARK)
        self.text(x + (width - len(text) * GLYPH_WIDTH) // 2,
                  y + (height - GLYPH_HEIGHT) // 2, text, ink)
        if focused:
            self.outline((x + FOCUS_INSET, y + FOCUS_INSET,
                          width - 2 * FOCUS_INSET, height - 2 * FOCUS_INSET),
                         ACCENT)
        self.scissor(None)

    def box(self, rect, text, radio=False, value=False, pressed=False,
            focused=False, disabled=False):
        """Draw a check box or radio button as sysnook does.

        @param rect Its (x, y, width, height) in screen coordinates.
        @param text Its text.
        @param radio Whether it is a radio button.
        @param value Whether it is checked or set.
        @param pressed Whether it is held down with the pointer on it.
        @param focused Whether it shows the focus.
        @param disabled Whether it is disabled.
        """
        x, y, width, height = rect
        ink = DARK if disabled else TEXT
        box_x, box_y = x, y + (height - BOX_SIZE) // 2
        self.scissor(rect)
        self.fill((box_x, box_y, BOX_SIZE, BOX_SIZE),
                  LIGHT if pressed else FACE)
        self.outline((box_x, box_y, BOX_SIZE, BOX_SIZE), DARK)
        if value and not radio:
            self.line((box_x + 2, box_y + 4), (box_x + 4, box_y + 6), ink)
            self.line((box_x + 4, box_y + 6), (box_x + 6, box_y + 2), ink)
        elif value:
            self.fill((box_x + RADIO_INSET, box_y + RADIO_INSET,
                       BOX_SIZE - 2 * RADIO_INSET,
                       BOX_SIZE - 2 * RADIO_INSET), ink)
        left = x + BOX_SIZE + BOX_GAP
        top = y + (height - GLYPH_HEIGHT) // 2
        self.text(left, top, text, ink)
        if focused:
            self.outline((left - 1, top - 1, len(text) * GLYPH_WIDTH + 2,
                          GLYPH_HEIGHT + 2), ACCENT)
        self.scissor(None)

    def field(self, rect, text, caret=None, scroll=0, password=False,
              disabled=False):
        """Draw a text field as sysnook does.

        @param rect Its (x, y, width, height) in screen coordinates.
        @param text Its text.
        @param caret The byte its caret is before if it has the focus, or
          None.
        @param scroll The first byte shown.
        @param password Whether it shows stars.
        @param disabled Whether it is disabled.
        """
        x, y, width, height = rect
        shown = (width - 2 * FIELD_INSET) // GLYPH_WIDTH
        self.scissor(rect)
        self.fill(rect, FACE)
        self.outline(rect, ACCENT if caret is not None else DARK)
        visible = text[scroll:scroll + shown]
        if password:
            visible = "*" * len(visible)
        left = x + FIELD_INSET
        self.text(left, y + (height - GLYPH_HEIGHT) // 2, visible,
                  DARK if disabled else TEXT)
        if caret is not None:
            self.fill((left + (caret - scroll) * GLYPH_WIDTH - 1,
                       y + (height - CARET_HEIGHT) // 2, 1, CARET_HEIGHT),
                      TEXT)
        self.scissor(None)

    def label(self, rect, align, text, color=TEXT):
        """Draw a label as sysnook does:  its text aligned in its rectangle,
        centered from top to bottom, clipped to the rectangle.

        @param rect The label's (x, y, width, height) in screen coordinates.
        @param align "left", "center" or "right".
        @param text Its text.
        @param color The palette index of its text.
        """
        x, y, width, height = rect
        length = len(text) * GLYPH_WIDTH
        if align == "center":
            x += (width - length) // 2
        elif align == "right":
            x += width - length
        self.scissor(rect)
        self.text(x, y + (height - GLYPH_HEIGHT) // 2, text, color)
        self.scissor(None)

    def group(self, rect, caption, color=TEXT, face=FACE):
        """Draw a group box as sysnook does:  a line around it from half its
        caption's height down, broken where the caption is.

        @param rect The group box's (x, y, width, height) in screen
          coordinates.
        @param caption Its caption.
        @param color The palette index of its caption.
        @param face The palette index of its window's face.
        """
        x, y, width, height = rect
        half = GLYPH_HEIGHT // 2
        self.scissor(rect)
        self.outline((x, y + half, width, height - half), DARK)
        if caption:
            left = x + GROUP_CAPTION_INSET
            self.fill((left - GROUP_CAPTION_GAP, y,
                       len(caption) * GLYPH_WIDTH + 2 * GROUP_CAPTION_GAP,
                       GLYPH_HEIGHT), face)
            self.text(left, y, caption, color)
        self.scissor(None)

    def arrow(self, bar_x, box_top, up):
        """Draw one of a scroll bar's arrows as sysnook does.

        @param bar_x The scroll bar's left edge.
        @param box_top The top of the arrow's box.
        @param up Whether it points up.
        """
        self.outline((bar_x, box_top, SCROLL_BAR_WIDTH, SCROLL_BAR_WIDTH),
                     DARK)
        for row in range(3):
            half = row if up else 2 - row
            self.fill((bar_x + 4 - half, box_top + 4 + row, 2 * half + 1, 1),
                      DARK)

    def list(self, rect, items, top=0, selection=None, focused=False,
             thumb=None):
        """Draw a list as sysnook does.

        @param rect Its (x, y, width, height) in screen coordinates.
        @param items Its items.
        @param top The first item shown.
        @param selection The selected item's index, or None.
        @param focused Whether it has the focus.
        @param thumb The thumb's (offset from the track's top, length), which
          a list with more items than rows must give.
        """
        x, y, width, height = rect
        rows = max((height - 2) // LIST_ITEM_HEIGHT, 1)
        bar = len(items) > rows
        rows_width = width - 2 - (SCROLL_BAR_WIDTH if bar else 0)
        fits = (rows_width - LIST_TEXT_INSET) // GLYPH_WIDTH
        self.fill(rect, FACE)
        for row in range(rows):
            index = top + row
            if index >= len(items):
                break
            row_top = y + 1 + row * LIST_ITEM_HEIGHT
            ink = TEXT
            if index == selection:
                self.fill((x + 1, row_top, rows_width, LIST_ITEM_HEIGHT),
                          ACCENT)
                ink = ON_ACCENT
            self.text(x + 1 + LIST_TEXT_INSET,
                      row_top + (LIST_ITEM_HEIGHT - GLYPH_HEIGHT) // 2,
                      items[index][:fits], ink)
        if bar:
            bar_x = x + width - 1 - SCROLL_BAR_WIDTH
            self.fill((bar_x, y + 1, SCROLL_BAR_WIDTH, height - 2), LIGHT)
            self.arrow(bar_x, y + 1, True)
            self.arrow(bar_x, y + height - 1 - SCROLL_BAR_WIDTH, False)
            offset, length = thumb
            self.fill((bar_x + 1, y + 1 + SCROLL_BAR_WIDTH + offset,
                       SCROLL_BAR_WIDTH - 2, length), DARK)
        self.outline(rect, ACCENT if focused else DARK)

    @staticmethod
    def titles(rect, titles):
        """Return where a menu bar's titles are.

        @param rect The bar's (x, y, width, height) in screen coordinates.
        @param titles Its titles, each with an '&' before its marked letter.

        @return A list of each title's (x, y, width, height).
        """
        x, y, _width, height = rect
        x += MENU_INSET
        places = []
        for title in titles:
            width = len(title.replace("&", "")) * GLYPH_WIDTH + 2 * TITLE_PAD
            places.append((x, y, width, height))
            x += width
        return places

    def menu_bar(self, rect, titles, open_index=None):
        """Draw a menu bar as sysnook does.

        @param rect Its (x, y, width, height) in screen coordinates.
        @param titles Its titles, each with an '&' before its marked letter.
        @param open_index The index of the open menu, or None.
        """
        self.fill(rect, LIGHT)
        top = rect[1] + (rect[3] - GLYPH_HEIGHT) // 2
        for index, (title, place) in enumerate(
                zip(titles, self.titles(rect, titles))):
            ink = TEXT
            if index == open_index:
                self.fill(place, ACCENT)
                ink = ON_ACCENT
            left = place[0] + TITLE_PAD
            shown = title.replace("&", "")
            self.text(left, top, shown, ink)
            mark = title.index("&")
            self.fill((left + mark * GLYPH_WIDTH, top + GLYPH_HEIGHT,
                       GLYPH_WIDTH, 1), ink)

    def dropdown(self, title, items, highlight=None):
        """Draw an open menu's dropdown as sysnook does.

        @param title The (x, y, width, height) of the menu's title.
        @param items Its items, each (text, shortcut, flags) with flags
          holding "d" for disabled and "c" for checked, or None for a
          separator.
        @param highlight The index of the highlighted item, or None.

        @return The dropdown's (x, y, width, height).
        """
        texts = [item[0] for item in items if item is not None]
        shortcuts = [item[1] for item in items if item is not None]
        width = (2 + CHECK_GUTTER + max(map(len, texts)) * GLYPH_WIDTH
                 + ITEM_PAD)
        longest = max(map(len, shortcuts))
        if longest > 0:
            width += SHORTCUT_GAP + longest * GLYPH_WIDTH
        height = 2 + sum(SEPARATOR_HEIGHT if item is None else ITEM_HEIGHT
                         for item in items)
        rect = (title[0], title[1] + title[3], width, height)
        x, y = rect[0], rect[1]
        self.fill(rect, FACE)
        self.outline(rect, DARK)
        top = y + 1
        for index, item in enumerate(items):
            if item is None:
                self.fill((x + 2, top + SEPARATOR_HEIGHT // 2, width - 4, 1),
                          DARK)
                top += SEPARATOR_HEIGHT
                continue
            text, shortcut, flags = item
            ink = DARK if "d" in flags else TEXT
            if index == highlight:
                self.fill((x + 1, top, width - 2, ITEM_HEIGHT), ACCENT)
                ink = ON_ACCENT
            if "c" in flags:
                self.line((x + 4, top + 5), (x + 6, top + 7), ink)
                self.line((x + 6, top + 7), (x + 9, top + 3), ink)
            text_top = top + (ITEM_HEIGHT - GLYPH_HEIGHT) // 2
            self.text(x + 1 + CHECK_GUTTER, text_top, text, ink)
            self.text(x + width - 1 - ITEM_PAD - len(shortcut) * GLYPH_WIDTH,
                      text_top, shortcut, ink)
            top += ITEM_HEIGHT
        return rect

    def check(self, vdp, timeout=30):
        """Wait until the canvas shows the body exactly.

        @param vdp The FakeVdp.
        @param timeout Seconds to wait.
        """
        deadline = time.time() + timeout
        while True:
            actual = vdp.pixels(self.left, self.top, self.width, self.height)
            if actual == self.rows:
                return
            if time.time() > deadline:
                wrong = [(self.left + column, self.top + row,
                          actual[row][column], self.rows[row][column])
                         for row in range(self.height)
                         for column in range(self.width)
                         if actual[row][column] != self.rows[row][column]]
                raise AssertionError(f"{len(wrong)} pixels differ "
                                     f"(x, y, actual, expected): {wrong[:8]}")
            time.sleep(0.2)


def start_widgets(session, commands, options=""):
    """Start nook with no demonstration windows and a wintest that carries
    out widget commands.

    @param session The session.
    @param commands wintest run's commands.
    @param options nook's options.

    @return The number of context commands before nook.
    """
    session.write_file("/etc/wintest", commands)
    boot_nook(session)
    base = start_nook(session, options, demo=False)
    run_client(session, "@/etc/wintest")
    return base


def screen(body, rect):
    """Turn a rectangle in a body into screen coordinates.

    @param body The body's (left, top, width, height).
    @param rect The (x, y, width, height) in the body.

    @return The rectangle in screen coordinates.
    """
    return (body[0] + rect[0], body[1] + rect[1], rect[2], rect[3])


def decoded(vdp, x, y, length):
    """Read a line of text back from the canvas.

    @param vdp The FakeVdp.
    @param x The left edge of its first glyph.
    @param y Its top edge.
    @param length The number of glyphs.

    @return The text, with "?" for a glyph the font has none like.
    """
    return "".join(ch or "?" for ch, _fg, _bg in
                   vdp.read_cells(x, y, length, 1, VDP_FONT_ID)[0])


# Step 1:  templates, labels and group boxes.

LABELS_FRAME = (40, 40, 260, 120)


def test_widgets_labels_in_each_alignment_and_a_group_box_are_drawn_exactly(
        session):
    base = start_widgets(
        session,
        "label 1 10,10,120,14 left Left label 2 10,30,120,14 center Center "
        "label 3 10,50,120,14 right Right "
        "label 4 10,70,120,14 left Disabled flags=disabled "
        "group 5 140,10,100,80 Options "
        "window 40,40,260,120 Widgets")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    body = body_of(LABELS_FRAME)
    model = BodyModel(vdp, LABELS_FRAME)
    model.label(screen(body, (10, 10, 120, 14)), "left", "Left")
    model.label(screen(body, (10, 30, 120, 14)), "center", "Center")
    model.label(screen(body, (10, 50, 120, 14)), "right", "Right")
    model.label(screen(body, (10, 70, 120, 14)), "left", "Disabled", DARK)
    model.group(screen(body, (140, 10, 100, 80)), "Options")
    model.check(vdp)
    assert decoded(vdp, body[0] + 10, body[1] + 13, 4) == "Left"
    assert decoded(vdp, body[0] + 10 + 120 - 30, body[1] + 53, 5) == "Right"
    assert decoded(vdp, body[0] + 146, body[1] + 10, 7) == "Options"
    quit_nook(session, base)
    check_clean(session)


def label_commands(count, first=1):
    """Return wintest commands for a number of labels, one under another.

    @param count The number of labels.
    @param first The first one's identifier.

    @return The commands.
    """
    return "".join(f"label {first + index} 0,{index * 2},60,8 left L{index} "
                   for index in range(count))


def refused(session):
    """Measure the heap after each of six refused adds.  Free memory then
    holds, or moves by up to 48 bytes with the overlay cache and the shell;
    keeping any of a refused template lowers each measurement after it.

    @param session The session.

    @return The measurements.
    """
    heaps = []
    for _round in range(6):
        expect_line(session, "wintest: add 1: Out of memory", timeout=20)
        heaps.append(settled(lambda: heap_at_prompt(session)))
    assert max(heaps[4:6]) >= max(heaps[1:3]) - 48, heaps
    return heaps


def test_widgets_a_33rd_widget_is_refused_and_nothing_of_it_is_kept(session):
    base = start_widgets(
        session,
        "label 1 10,10,120,14 left Kept window 40,40,260,120 Kept "
        "sleep 3000 " + (label_commands(33, 2) + "add 1 sleep 3000 ") * 6
        + label_commands(31, 2) + "add 1 sleep 2000 " + label_commands(1, 40)
        + "add 1 wait 60000")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    body = body_of(LABELS_FRAME)
    model = BodyModel(vdp, LABELS_FRAME)
    model.label(screen(body, (10, 10, 120, 14)), "left", "Kept")
    model.check(vdp)
    settle(vdp)
    canvas = bytes(vdp.canvas)
    heaps = refused(session)
    now = bytes(vdp.canvas)
    changed = [(index % 640, index // 640, canvas[index], now[index])
               for index in range(len(now)) if now[index] != canvas[index]]
    assert not changed, (len(changed), changed[:8])
    # The refused widgets left room for exactly 31 more, and no 33rd.
    expect_line(session, "wintest: add 1: ok", timeout=20)
    expect_line(session, "wintest: add 1: Out of memory", timeout=20)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_template_past_the_budget_is_refused_and_nothing_kept(
        session):
    base = start_widgets(
        session,
        label_commands(2) + "window 40,40,260,120 Small "
        "sleep 3000 " + (label_commands(12, 3) + "add 1 sleep 3000 ") * 6
        + label_commands(1, 3) + "add 1 wait 60000",
        "--budget=400")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    body = body_of(LABELS_FRAME)
    model = BodyModel(vdp, LABELS_FRAME)
    for index in range(2):
        model.label(screen(body, (0, index * 2, 60, 8)), "left", f"L{index}")
    model.check(vdp)
    settle(vdp)
    canvas = bytes(vdp.canvas)
    heaps = refused(session)
    now = bytes(vdp.canvas)
    changed = [(index % 640, index // 640, canvas[index], now[index])
               for index in range(len(now)) if now[index] != canvas[index]]
    assert not changed, (len(changed), changed[:8])
    # Nothing of the refused templates was charged, so one more label fits.
    expect_line(session, "wintest: add 1: ok", timeout=20)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_template_overwritten_once_given_repaints_the_same(session):
    # wintest overwrites a template's buffer as soon as the window is made.
    base = start_widgets(
        session,
        "label 1 10,10,120,14 center Kept group 2 140,10,100,80 Box "
        "window 40,40,260,120 Widgets sleep 3000 state 1 minimized "
        "sleep 2000 state 1 normal")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    body = body_of(LABELS_FRAME)
    model = BodyModel(vdp, LABELS_FRAME)
    model.label(screen(body, (10, 10, 120, 14)), "center", "Kept")
    model.group(screen(body, (140, 10, 100, 80)), "Box")
    model.check(vdp)
    expect_line(session, "STATE 1 minimized", timeout=20)
    expect_line(session, "STATE 1 normal", timeout=20)
    model.check(vdp)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_text_set_on_a_label_is_drawn_and_read_back(session):
    base = start_widgets(
        session,
        "label 7 10,10,120,14 left Before window 40,40,260,120 Widgets "
        "sleep 2000 set 1 7 text After_it get 1 7 text get 1 9 text")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    expect_line(session, "wintest: set 1: ok", timeout=20)
    expect_line(session, "wintest: get 1 7 text After it")
    expect_line(session, "wintest: get 1: No such entry found")
    body = body_of(LABELS_FRAME)
    model = BodyModel(vdp, LABELS_FRAME)
    model.label(screen(body, (10, 10, 120, 14)), "left", "After it")
    model.check(vdp)
    quit_nook(session, base)
    check_clean(session)


# Step 2:  buttons and boxes.

BUTTONS_FRAME = (40, 40, 300, 160)
BUTTONS = (
    "button 1 10,10,60,14 OK button 2 80,10,60,14 Go flags=default "
    "button 3 150,10,60,14 No flags=disabled "
    "toggle 4 10,30,60,14 1 On toggle 5 80,30,60,14 0 Off "
    "check 6 10,50,80,10 1 Checked check 7 100,50,80,10 0 Clear "
    "check 8 190,50,80,10 1 Gray flags=disabled "
    "radio 9 10,70,80,10 1 1 First radio 10 100,70,80,10 1 0 Second "
    "window 40,40,300,160 Buttons ")
# Each widget's place in the body.
PLACES = {1: (10, 10, 60, 14), 2: (80, 10, 60, 14), 3: (150, 10, 60, 14),
          4: (10, 30, 60, 14), 5: (80, 30, 60, 14), 6: (10, 50, 80, 10),
          7: (100, 50, 80, 10), 8: (190, 50, 80, 10), 9: (10, 70, 80, 10),
          10: (100, 70, 80, 10)}


def buttons_model(vdp, values=None, focus=None, pressed=None):
    """Return the model of the buttons window:  each widget in its state.

    @param vdp The FakeVdp.
    @param values The value of each of the toggle buttons, check boxes and
      radio buttons, by identifier, if not the template's.
    @param focus The identifier of the widget with the focus, or None.
    @param pressed The identifier of the widget held down, or None.

    @return The BodyModel.
    """
    current = {4: True, 5: False, 6: True, 7: False, 8: True, 9: True,
               10: False}
    current.update(values or {})
    body = body_of(BUTTONS_FRAME)
    model = BodyModel(vdp, BUTTONS_FRAME)
    place = {id_: screen(body, rect) for id_, rect in PLACES.items()}
    model.button(place[1], "OK", pressed=(pressed == 1), focused=(focus == 1))
    model.button(place[2], "Go", default=True, pressed=(pressed == 2),
                 focused=(focus == 2))
    model.button(place[3], "No", disabled=True)
    model.button(place[4], "On", on=current[4], pressed=(pressed == 4),
                 focused=(focus == 4))
    model.button(place[5], "Off", on=current[5], pressed=(pressed == 5),
                 focused=(focus == 5))
    for id_, text in ((6, "Checked"), (7, "Clear")):
        model.box(place[id_], text, value=current[id_],
                  pressed=(pressed == id_), focused=(focus == id_))
    model.box(place[8], "Gray", value=current[8], disabled=True)
    for id_, text in ((9, "First"), (10, "Second")):
        model.box(place[id_], text, radio=True, value=current[id_],
                  pressed=(pressed == id_), focused=(focus == id_))
    return model


def center(id_):
    """Return the screen point at the middle of a widget of the buttons
    window."""
    body = body_of(BUTTONS_FRAME)
    x, y, width, height = screen(body, PLACES[id_])
    return (x + width // 2, y + height // 2)


def quiet(session, seconds=2):
    """Check that wintest prints nothing more for a while.

    @param session The session.
    @param seconds How long.
    """
    assert session.child.expect([pexpect.TIMEOUT, r"\S"],
                                timeout=seconds) == 0, session.child.after


def test_widgets_buttons_and_boxes_are_drawn_exactly_in_each_state(session):
    base = start_widgets(session, BUTTONS + "set 1 7 focus 0")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    expect_line(session, "wintest: set 1: ok", timeout=20)
    buttons_model(vdp, focus=7).check(vdp)
    # Held, a button is pressed; off it, it isn't; let go off it, nothing.
    x, y = center(1)
    vdp.mouse(x, y)
    vdp.mouse(x, y, buttons=1)
    buttons_model(vdp, focus=1, pressed=1).check(vdp)
    vdp.mouse(x, y + 40, buttons=1)
    buttons_model(vdp, focus=1).check(vdp)
    vdp.mouse(x, y + 40)
    expect_line(session, "FOCUS 1 activated")
    quiet(session)
    buttons_model(vdp, focus=1).check(vdp)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_buttons_and_boxes_send_exactly_their_commands(session):
    base = start_widgets(session, BUTTONS)
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated")
    for id_, line in ((1, "COMMAND 1 1 0"), (2, "COMMAND 1 2 0"),
                      (5, "COMMAND 1 5 1"), (4, "COMMAND 1 4 0"),
                      (7, "COMMAND 1 7 1"), (6, "COMMAND 1 6 0"),
                      (10, "COMMAND 1 10 1")):
        click(vdp, center(id_))
        expect_line(session, line)
    # A disabled button or check box takes no click.
    click(vdp, center(3))
    click(vdp, center(8))
    quiet(session)
    buttons_model(vdp, values={4: False, 5: True, 6: False, 7: True,
                               9: False, 10: True}, focus=10).check(vdp)
    # Space clicks the widget with the focus.
    click(vdp, center(6))
    expect_line(session, "COMMAND 1 6 1")
    vdp.type_paced(" ")
    expect_line(session, "COMMAND 1 6 0")
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_value_set_and_got_follows_the_group(session):
    base = start_widgets(session, BUTTONS + "set 1 10 value 1 get 1 9 value "
                         "get 1 10 value set 1 6 value 0 get 1 6 value")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    expect_line(session, "wintest: set 1: ok", timeout=20)
    expect_line(session, "wintest: get 1 9 value 0")
    expect_line(session, "wintest: get 1 10 value 1")
    expect_line(session, "wintest: set 1: ok")
    expect_line(session, "wintest: get 1 6 value 0")
    buttons_model(vdp, values={6: False, 9: False, 10: True}).check(vdp)
    quit_nook(session, base)
    check_clean(session)


# Step 3:  text fields.

FIELD_FRAME = (40, 40, 260, 80)
FIELD = (10, 10, 120, 14)


def field_model(vdp, text, caret=None, scroll=0, password=False,
                rect=FIELD):
    """Return the model of the text field window.

    @param vdp The FakeVdp.
    @param text The field's text.
    @param caret Its caret if it has the focus, or None.
    @param scroll The first byte shown.
    @param password Whether it shows stars.
    @param rect Its place in the body.

    @return The BodyModel.
    """
    model = BodyModel(vdp, FIELD_FRAME)
    model.field(screen(body_of(FIELD_FRAME), rect), text, caret, scroll,
                password)
    return model


def field_point(rect=FIELD):
    """Return a screen point inside the text field, right of its text."""
    x, y, width, height = screen(body_of(FIELD_FRAME), rect)
    return (x + width - 3, y + height // 2)


def start_field(session, field="field 1 10,10,120,14 20 -", after=""):
    """Start nook and a wintest whose window has a text field, then give
    the field the focus with a click on it.

    @param session The session.
    @param field The wintest command for the field.
    @param after wintest's commands after the window is made.

    @return The number of context commands before nook.
    """
    base = start_widgets(session,
                         field + " window 40,40,260,80 Field " + after)
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated", timeout=20)
    click(session.vdp, field_point())
    return base


def test_widgets_a_text_field_edits_exactly_as_typed(session):
    base = start_field(session, after="wait 20000 get 1 1 text")
    vdp = session.vdp
    field_model(vdp, "", caret=0).check(vdp)
    vdp.type_paced("abcdef")
    vdp.press(KEYCODE_LEFT)
    vdp.press(KEYCODE_LEFT)
    vdp.type_paced("\b")
    vdp.press(KEYCODE_HOME)
    vdp.press(KEYCODE_DELETE)
    vdp.press(KEYCODE_END)
    vdp.type_paced("Z")
    field_model(vdp, "bcefZ", caret=5).check(vdp)
    expect_line(session, "wintest: get 1 1 text bcefZ", timeout=30)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_keys_in_one_input_message_give_one_change(session):
    base = start_field(session)
    vdp = session.vdp
    field_model(vdp, "", caret=0).check(vdp)
    vdp.keys_at_once("abcdefghijkl")
    expect_line(session, "CHANGED 1 1 12")
    quiet(session)
    field_model(vdp, "abcdefghijkl", caret=12).check(vdp)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_typing_past_the_most_leaves_exactly_the_most(session):
    base = start_field(session, "field 1 10,10,120,14 5 -",
                       "wait 15000 get 1 1 text")
    vdp = session.vdp
    vdp.type_paced("abcdefg")
    field_model(vdp, "abcde", caret=5).check(vdp)
    expect_line(session, "wintest: get 1 1 text abcde", timeout=30)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_password_field_shows_stars_and_gives_the_text(session):
    base = start_field(session, "field 1 10,10,120,14 20 - flags=password",
                       "wait 15000 get 1 1 text")
    vdp = session.vdp
    vdp.type_paced("secret")
    field_model(vdp, "secret", caret=6, password=True).check(vdp)
    expect_line(session, "wintest: get 1 1 text secret", timeout=30)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_text_field_scrolls_to_keep_its_caret_in_it(session):
    narrow = (10, 10, 50, 14)
    base = start_widgets(session, "field 1 10,10,50,14 20 - "
                         "window 40,40,260,80 Field")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated", timeout=20)
    click(vdp, field_point(narrow))
    # 50 pixels less the insets show 7 glyphs.
    vdp.type_paced("abcdefghij")
    field_model(vdp, "abcdefghij", caret=10, scroll=3, rect=narrow).check(vdp)
    vdp.press(KEYCODE_HOME)
    field_model(vdp, "abcdefghij", caret=0, scroll=0, rect=narrow).check(vdp)
    quit_nook(session, base)
    check_clean(session)


def viewport_changes(session, labels):
    """Count the viewport changes FakeVdp is sent while a window of labels
    whose texts fit is minimized and restored, after it is first painted.

    @param session The session.
    @param labels The number of labels.

    @return The count.
    """
    base = start_widgets(session, label_commands(labels)
                         + "window 40,40,260,120 Labels sleep 3000 "
                         "state 1 minimized sleep 2000 state 1 normal")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    settle(vdp)
    before = sum(kind == "gviewport" for _t, kind, _d in vdp.events)
    expect_line(session, "STATE 1 normal", timeout=20)
    settle(vdp)
    after = sum(kind == "gviewport" for _t, kind, _d in vdp.events)
    quit_nook(session, base)
    check_clean(session)
    return after - before


def test_widgets_widgets_that_fit_change_the_viewport_no_more_than_one(
        session):
    # The VDP answers each viewport change with a mode packet, and a frame
    # of them for every widget overruns the UART's receive ring.
    one = viewport_changes(session, 1)
    assert viewport_changes(session, 32) == one, one


LIST_FRAME = (40, 40, 260, 140)
LIST = (10, 10, 120, 102)
ITEMS = [f"item{index}" for index in range(30)]
# A 102-pixel list shows 10 rows; its 80-pixel track has a 26-pixel thumb,
# which travels 54 pixels over the 20 items it can scroll.
THUMB_LENGTH = 26


def thumb(top):
    """Return the thumb of the 30-item list:  (offset, length)."""
    return ((80 - THUMB_LENGTH) * top // 20, THUMB_LENGTH)


def list_model(vdp, items=None, top=0, selection=None, focused=True,
               thumb_at=None):
    """Return the model of the list window.

    @param vdp The FakeVdp.
    @param items Its items, ITEMS if None.
    @param top The first item shown.
    @param selection The selected item's index, or None.
    @param focused Whether it has the focus.
    @param thumb_at The thumb's (offset, length), thumb(top) if None.

    @return The BodyModel.
    """
    items = ITEMS if items is None else items
    model = BodyModel(vdp, LIST_FRAME)
    model.list(screen(body_of(LIST_FRAME), LIST), items, top, selection,
               focused, thumb_at or thumb(top))
    return model


def list_point(row, column=20):
    """Return the screen point on a row of the list."""
    x, y, _width, _height = screen(body_of(LIST_FRAME), LIST)
    return (x + column, y + 1 + row * LIST_ITEM_HEIGHT + LIST_ITEM_HEIGHT // 2)


def start_list(session, selection="0", items=None, after=""):
    """Start nook and a wintest whose window has a list, then give the list
    the focus with a click on its first row.

    @param session The session.
    @param selection The list's selection word.
    @param items Its items, ITEMS if None.
    @param after wintest's commands after the window is made.

    @return The number of context commands before nook.
    """
    words = ",".join(ITEMS if items is None else items)
    base = start_widgets(session, f"list 1 10,10,120,102 {selection} {words} "
                         "window 40,40,260,140 List " + after)
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated", timeout=20)
    return base


def test_widgets_page_down_shows_the_next_ten_items_exactly(session):
    base = start_list(session)
    vdp = session.vdp
    click(vdp, list_point(0))
    list_model(vdp, selection=0).check(vdp)
    assert thumb(0) == (0, 26) and thumb(10) == (27, 26)
    vdp.press(KEYCODE_PAGEDOWN)
    expect_line(session, "CHANGED 1 1 10")
    list_model(vdp, top=10, selection=10).check(vdp)
    vdp.press(KEYCODE_PAGEDOWN)
    expect_line(session, "CHANGED 1 1 20")
    list_model(vdp, top=20, selection=20).check(vdp)
    vdp.press(KEYCODE_PAGEUP)
    expect_line(session, "CHANGED 1 1 10")
    list_model(vdp, top=10, selection=10).check(vdp)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_list_keys_move_the_selection_and_enter_chooses_it(session):
    base = start_list(session, "none", after="wait 40000 get 1 1 text "
                      "get 1 1 value")
    vdp = session.vdp
    list_model(vdp, focused=False).check(vdp)
    click(vdp, list_point(3))
    expect_line(session, "CHANGED 1 1 3")
    vdp.press(KEYCODE_DOWN)
    expect_line(session, "CHANGED 1 1 4")
    vdp.press(KEYCODE_UP)
    vdp.press(KEYCODE_UP)
    expect_line(session, "CHANGED 1 1 3")
    expect_line(session, "CHANGED 1 1 2")
    vdp.press(KEYCODE_END)
    expect_line(session, "CHANGED 1 1 29")
    list_model(vdp, top=20, selection=29).check(vdp)
    vdp.press(KEYCODE_HOME)
    expect_line(session, "CHANGED 1 1 0")
    vdp.press(KEYCODE_DOWN)
    expect_line(session, "CHANGED 1 1 1")
    vdp.press(KEYCODE_RETURN)
    expect_line(session, "COMMAND 1 1 1")
    list_model(vdp, top=0, selection=1).check(vdp)
    expect_line(session, "wintest: get 1 1 text item1", timeout=60)
    expect_line(session, "wintest: get 1 1 value 1")
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_double_click_on_an_item_chooses_it(session):
    base = start_list(session)
    vdp = session.vdp
    click(vdp, list_point(0))
    list_model(vdp, selection=0).check(vdp)
    click(vdp, list_point(4))
    click(vdp, list_point(4))
    expect_line(session, "CHANGED 1 1 4")
    expect_line(session, "COMMAND 1 1 4")
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_the_wheel_and_the_scroll_bar_scroll_a_list(session):
    base = start_list(session)
    vdp = session.vdp
    click(vdp, list_point(0))
    list_model(vdp, selection=0).check(vdp)
    vdp.mouse(*list_point(2))
    vdp.mouse(*list_point(2), wheel=-1)
    list_model(vdp, top=3, selection=0).check(vdp)
    x, y, width, height = screen(body_of(LIST_FRAME), LIST)
    bar = x + width - 1 - SCROLL_BAR_WIDTH // 2
    click(vdp, (bar, y + height - 3))
    list_model(vdp, top=4, selection=0).check(vdp)
    click(vdp, (bar, y + 3))
    list_model(vdp, top=3, selection=0).check(vdp)
    click(vdp, (bar, y + height - SCROLL_BAR_WIDTH - 3))
    list_model(vdp, top=13, selection=0).check(vdp)
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_items_set_to_a_shorter_list_clamp_the_selection(session):
    base = start_list(session, "25", after="wait 8000 set 1 1 items a,b,c "
                      "get 1 1 value get 1 1 text")
    vdp = session.vdp
    list_model(vdp, top=16, selection=25, focused=False).check(vdp)
    expect_line(session, "wintest: set 1: ok", timeout=30)
    expect_line(session, "wintest: get 1 1 value 2")
    expect_line(session, "wintest: get 1 1 text c")
    list_model(vdp, items=["a", "b", "c"], selection=2,
               focused=False).check(vdp)
    quit_nook(session, base)
    check_clean(session)


MENU_FRAME = (40, 40, 260, 140)
MENU_TITLES = ["&File", "&Edit", "&Help"]
MENUS = ("&File,10=Open:Ctrl+O,11=Save:Ctrl+S,12=Autosave/c,-,13=Print/d,"
         "14=Quit|&Edit,20=Undo/d,21=Cut,22=Copy|&Help,30=About")
MENU_ITEMS = [
    [("Open", "Ctrl+O", ""), ("Save", "Ctrl+S", ""), ("Autosave", "", "c"),
     None, ("Print", "", "d"), ("Quit", "", "")],
    [("Undo", "", "d"), ("Cut", "", ""), ("Copy", "", "")],
    [("About", "", "")],
]
MENU_FIELD = (150, 60, 90, 14)


def menu_model(vdp, open_index=None, highlight=None, items=None,
               caret=None, text=""):
    """Return the model of the menu window:  its bar, a text field, and the
    open menu's dropdown.

    @param vdp The FakeVdp.
    @param open_index The index of the open menu, or None.
    @param highlight The index of its highlighted item, or None.
    @param items The open menu's items, MENU_ITEMS' if None.
    @param caret The field's caret if it has the focus, or None.
    @param text The field's text.

    @return The BodyModel and, with a menu open, the dropdown's rectangle.
    """
    model = BodyModel(vdp, MENU_FRAME)
    body = body_of(MENU_FRAME)
    bar = screen(body, (0, 0, body[2], 12))
    model.menu_bar(bar, MENU_TITLES, open_index)
    model.field(screen(body, MENU_FIELD), text, caret)
    model.scissor(None)
    rect = None
    if open_index is not None:
        rect = model.dropdown(model.titles(bar, MENU_TITLES)[open_index],
                              items or MENU_ITEMS[open_index], highlight)
    return model, rect


def menu_title(index):
    """Return the screen point in the middle of a menu's title."""
    body = body_of(MENU_FRAME)
    bar = screen(body, (0, 0, body[2], 12))
    x, y, width, height = BodyModel.titles(bar, MENU_TITLES)[index]
    return (x + width // 2, y + height // 2)


def menu_item(open_index, index):
    """Return the screen point in the middle of an open menu's item."""
    x, y = menu_title(open_index)
    top = y + 6 + 1
    for item in MENU_ITEMS[open_index][:index]:
        top += SEPARATOR_HEIGHT if item is None else ITEM_HEIGHT
    height = SEPARATOR_HEIGHT if MENU_ITEMS[open_index][index] is None \
        else ITEM_HEIGHT
    return (x, top + height // 2)


def start_menus(session, after=""):
    """Start nook and a wintest whose window has a menu bar and a text
    field.

    @param session The session.
    @param after wintest's commands after the window is made.

    @return The number of context commands before nook.
    """
    body = body_of(MENU_FRAME)
    field = ",".join(str(value) for value in MENU_FIELD)
    base = start_widgets(session, f"menu 1 0,0,{body[2]},12 {MENUS} "
                         f"field 2 {field} 20 - "
                         "window 40,40,260,140 Menus " + after)
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated", timeout=20)
    return base


def test_widgets_a_menu_bar_and_its_dropdown_are_drawn_exactly(session):
    base = start_menus(session)
    vdp = session.vdp
    menu_model(vdp)[0].check(vdp)
    click(vdp, menu_title(0))
    menu_model(vdp, open_index=0)[0].check(vdp)
    vdp.mouse(*menu_item(0, 1))
    menu_model(vdp, open_index=0, highlight=1)[0].check(vdp)
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_release_on_a_menu_item_chooses_it_once(session):
    base = start_menus(session, "wait 35000 set 1 1 menuitem 0x0D00")
    vdp = session.vdp
    vdp.mouse(*menu_title(0))
    vdp.mouse(*menu_title(0), buttons=1)
    vdp.mouse(*menu_item(0, 1), buttons=1)
    vdp.mouse(*menu_item(0, 1))
    expect_line(session, "COMMAND 1 1 11")
    menu_model(vdp)[0].check(vdp)
    quiet(session)
    click(vdp, menu_title(0))
    for index in (3, 4):
        vdp.mouse(*menu_item(0, index), buttons=1)
        vdp.mouse(*menu_item(0, index))
    menu_model(vdp, open_index=0)[0].check(vdp)
    quiet(session)
    expect_line(session, "wintest: set 1: ok", timeout=60)
    vdp.mouse(*menu_item(0, 4))
    enabled = [item if index != 4 else ("Print", "", "")
               for index, item in enumerate(MENU_ITEMS[0])]
    menu_model(vdp, open_index=0, highlight=4, items=enabled)[0].check(vdp)
    vdp.mouse(*menu_item(0, 4), buttons=1)
    vdp.mouse(*menu_item(0, 4))
    expect_line(session, "COMMAND 1 1 13")
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_moving_over_a_title_opens_it_and_a_press_outside_closes(
        session):
    base = start_menus(session)
    vdp = session.vdp
    click(vdp, menu_title(0))
    menu_model(vdp, open_index=0)[0].check(vdp)
    vdp.mouse(*menu_title(1))
    menu_model(vdp, open_index=1)[0].check(vdp)
    x, y, width, height = screen(body_of(MENU_FRAME), MENU_FIELD)
    click(vdp, (x + width - 3, y + height // 2))
    menu_model(vdp)[0].check(vdp)
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_menu_keys_choose_and_escape_gives_keys_back(session):
    base = start_menus(session)
    vdp = session.vdp
    x, y, width, height = screen(body_of(MENU_FRAME), MENU_FIELD)
    click(vdp, (x + width - 3, y + height // 2))
    menu_model(vdp, caret=0)[0].check(vdp)
    vdp.press(KEYCODE_F10)
    menu_model(vdp, open_index=0, caret=0)[0].check(vdp)
    vdp.press(KEYCODE_RIGHT)
    vdp.press(KEYCODE_DOWN)
    menu_model(vdp, open_index=1, highlight=1, caret=0)[0].check(vdp)
    vdp.press(KEYCODE_RETURN)
    expect_line(session, "COMMAND 1 1 21")
    menu_model(vdp, caret=0)[0].check(vdp)
    vdp.alt("f")
    menu_model(vdp, open_index=0, caret=0)[0].check(vdp)
    vdp.press(KEYCODE_ESCAPE)
    vdp.type_paced("x")
    expect_line(session, "CHANGED 1 2 1")
    menu_model(vdp, caret=1, text="x")[0].check(vdp)
    vdp.alt()
    menu_model(vdp, open_index=0, caret=1, text="x")[0].check(vdp)
    vdp.press(KEYCODE_LEFT)
    menu_model(vdp, open_index=2, caret=1, text="x")[0].check(vdp)
    vdp.press(KEYCODE_ESCAPE)
    menu_model(vdp, caret=1, text="x")[0].check(vdp)
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_tab_follows_the_template_skips_disabled_and_wraps(session):
    base = start_widgets(session, "button 1 10,10,40,16 A "
                         "button 2 60,10,40,16 B flags=disabled "
                         "field 3 10,40,100,14 20 - label 5 10,70,60,10 left L "
                         "check 4 120,40,60,12 0 C "
                         "window 40,40,260,120 Tabs")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated", timeout=20)
    steps = [(0, "COMMAND 1 1 0"), (0, "CHANGED 1 3 1"), (0, "COMMAND 1 4 1"),
             (0, "COMMAND 1 1 0"), (KEYMOD_SHIFT, "COMMAND 1 4 0"),
             (KEYMOD_SHIFT, "CHANGED 1 3 2")]
    for modifiers, line in steps:
        vdp.press(KEYCODE_TAB, modifiers)
        vdp.type_paced(" ")
        expect_line(session, line)
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_enter_and_escape_press_the_default_and_cancel_buttons(
        session):
    base = start_field(session, after="",
                       field="field 1 10,10,120,14 20 - "
                       "button 2 10,40,50,16 OK flags=default "
                       "button 3 70,40,50,16 Cancel flags=cancel")
    vdp = session.vdp
    vdp.type_paced("ab")
    # Both keys may come in one input message, which gives one CHANGED.
    expect_line(session, "CHANGED 1 1 2")
    vdp.press(KEYCODE_RETURN)
    expect_line(session, "COMMAND 1 2 0")
    vdp.press(KEYCODE_ESCAPE)
    expect_line(session, "COMMAND 1 3 0")
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def anchors_model(vdp, frame, field, button):
    """Return the model of the anchors window.

    @param vdp The FakeVdp.
    @param frame The window's frame.
    @param field The text field's rectangle in the body.
    @param button The button's rectangle in the body.

    @return The BodyModel.
    """
    model = BodyModel(vdp, frame)
    body = body_of(frame)
    model.field(screen(body, field), "")
    model.scissor(None)
    model.button(screen(body, button), "OK")
    return model


def test_widgets_maximizing_applies_each_widgets_anchors_exactly(session):
    base = start_widgets(session, "field 1 10,10,120,14 20 - anchor=ltr "
                         "button 2 200,40,50,16 OK anchor=tr "
                         "window 40,40,260,120 Anchors sleep 4000 "
                         "state 1 maximized")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    anchors_model(vdp, (40, 40, 260, 120), (10, 10, 120, 14),
                  (200, 40, 50, 16)).check(vdp)
    expect_line(session, "RESIZED 1 638x464", timeout=30)
    # The body grows from 258 to 638 pixels wide:  the field keeps its
    # 128-pixel right margin and the button its 8.
    anchors_model(vdp, (0, 0, 640, 480), (10, 10, 500, 14),
                  (580, 40, 50, 16)).check(vdp)
    quit_nook(session, base)
    check_clean(session)


CANVAS_FRAME = (40, 40, 260, 120)


def start_canvas(session, after=""):
    """Start nook and a wintest whose window has a 100x60 canvas 10 pixels
    into its body.

    @param session The session.
    @param after wintest's commands after the window is made.

    @return The number of context commands before nook.
    """
    base = start_widgets(session, "canvas 1 10,10,100,60 "
                         "window 40,40,260,120 Canvas " + after)
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated", timeout=20)
    return base


def test_widgets_a_canvas_reports_the_pointer_in_its_own_coordinates(session):
    base = start_canvas(session)
    vdp = session.vdp
    x, y, _width, _height = screen(body_of(CANVAS_FRAME), (10, 10, 100, 60))
    vdp.mouse(x + 10, y + 10)
    vdp.mouse(x + 10, y + 10, buttons=1)
    expect_line(session, "POINTER 1 1 10,10 1")
    vdp.mouse(x + 140, y + 70, buttons=1)
    expect_line(session, "POINTER 1 1 140,70 1")
    vdp.mouse(x + 140, y + 70)
    expect_line(session, "POINTER 1 1 140,70 0")
    vdp.mouse(x + 20, y + 20)
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_canvas_refuses_records_it_may_not_hold(session):
    # A call of one of the window server's preuploads, a rectangle cut
    # short, then a red 10x10 square:  only the square is drawn.
    base = start_canvas(session, "wait 2000 rawcanvas 1 1 100100 "
                        "rawcanvas 1 1 09 "
                        "rawcanvas 1 1 04FF0000AA09000000000A000A0001")
    vdp = session.vdp
    expect_line(session, "wintest: rawcanvas 1: Invalid argument", timeout=20)
    expect_line(session, "wintest: rawcanvas 1: Invalid argument")
    expect_line(session, "wintest: rawcanvas 1: ok")
    model = BodyModel(vdp, CANVAS_FRAME)
    x, y, _width, _height = screen(body_of(CANVAS_FRAME), (10, 10, 100, 60))
    model.fill((x, y, 10, 10), gr.RED)
    model.check(vdp)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_timing_counts_no_frame_of_a_held_button_or_widget(session):
    # Only moves and resizes are timed:  a held caption button or widget
    # must count no frame of any kind.
    base = start_widgets(session, "button 1 10,10,50,16 OK "
                         "window 40,40,260,80 Held", "--timing")
    vdp = session.vdp
    expect_line(session, "wintest: window 1")
    expect_line(session, "FOCUS 1 activated", timeout=20)
    close = button_rect((40, 40, 260, 80), 2)
    inside = (close[0] + 9, close[1] + 7)
    left, top, _width, _height = screen(body_of((40, 40, 260, 80)),
                                        (10, 10, 50, 16))
    held = (left + 25, top + 8)
    for point in (inside, held):
        vdp.mouse(*point)
        vdp.mouse(*point, buttons=1)
        for _step in range(3):
            vdp.mouse(point[0], point[1] + 40, buttons=1)
            settle(vdp)
            vdp.mouse(*point, buttons=1)
            settle(vdp)
        vdp.mouse(point[0], point[1] + 40, buttons=1)
        vdp.mouse(point[0], point[1] + 40)
        settle(vdp)
    quit_nook(session, base)
    for kind in ("move", "resize", "echo"):
        match = vdp.expect(r"sysnook: %s frames (\d+) " % kind)
        assert int(match.group(1)) == 0, match.group(0)
    check_clean(session)


def test_widgets_wintest_demo_opens_every_kind_of_widget(session):
    # Another client's window comes first, as a terminal's would, so the
    # demo's windows are 2 and 3, and its cells must go to window 3.
    boot_nook(session)
    base = start_nook(session, demo=False)
    vdp = session.vdp
    run_client(session, "create 500,400,100,60 First")
    expect_line(session, "wintest: window 1", timeout=30)
    session.child.sendline("wintest demo &")
    expect_line(session, "wintest: window 2", timeout=30)
    expect_line(session, "wintest: window 3", timeout=30)
    expect_line(session, "wintest: cells .: ok", timeout=30)
    # The last is the Dialogs window, whose buttons open the dialogs.
    expect_line(session, "wintest: window 4", timeout=30)
    expect_line(session, "FOCUS 3 activated", timeout=30)
    expect_line(session, "FOCUS 4 activated", timeout=30)
    settle(vdp)
    # Row 1, column 0 of the canvas's cells.
    wait_pixels(vdp, {(327, 69): gr.RED})
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_activating_a_window_repaints_only_what_changed(session):
    # Nothing covers the demo's first window, so activating it with a click
    # on an empty part of its body redraws its title, the title of the
    # Dialogs window, which was active, and no widget.
    boot_nook(session)
    base = start_nook(session, demo=False)
    vdp = session.vdp
    session.child.sendline("wintest demo &")
    expect_line(session, "wintest: cells .: ok", timeout=30)
    expect_line(session, "FOCUS 3 activated", timeout=30)
    settle(vdp)
    before = len(vdp.events)
    click(vdp, (250, 250))
    expect_line(session, "FOCUS 1 activated")
    settle(vdp)
    drawn = "".join(chr(data) for _time, kind, data in vdp.events[before:]
                    if kind == "gtext")
    assert sorted(drawn) == sorted("All widgets" + "Dialogs"), drawn
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_window_losing_activation_hides_its_focus(session):
    # The demo's Name field, focused, in the active window shows an accent
    # border and a caret; once the other window is clicked, neither.
    boot_nook(session)
    base = start_nook(session, demo=False)
    vdp = session.vdp
    session.child.sendline("wintest demo &")
    expect_line(session, "wintest: cells .: ok", timeout=30)
    expect_line(session, "FOCUS 2 activated", timeout=30)
    settle(vdp)
    field = (89, 59, 208, 14)
    caret = (field[0] + FIELD_INSET - 1, field[1] + (14 - CARET_HEIGHT) // 2)
    click(vdp, (field[0] + 100, field[1] + 7))
    expect_line(session, "FOCUS 1 activated")
    wait_pixels(vdp, {field[:2]: ACCENT, caret: TEXT})
    click(vdp, (450, 150))
    expect_line(session, "FOCUS 2 activated")
    wait_pixels(vdp, {field[:2]: DARK, caret: FACE})
    quit_nook(session, base)
    check_clean(session)




def test_widgets_a_covered_grid_change_repaints_nothing_over_it(session):
    # Window 2 covers the middle of a row of window 1's text grid, with a
    # label on that row; writing the whole row draws what of it shows, and
    # nothing of window 2.
    base = start_widgets(session, "grid 24,24 60x20 "
                         "label 1 10,20,100,10 left Covered "
                         "window 100,40,300,200 Over sleep 4000 "
                         "put 1 5,0,0x0F ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJ")
    vdp = session.vdp
    expect_line(session, "wintest: window 2")
    settle(vdp)
    before = len(vdp.events)
    time.sleep(6)
    settle(vdp)
    drawn = "".join(chr(data) for _time, kind, data in vdp.events[before:]
                    if kind == "gtext")
    assert "ABCDEFGHIJKL" in drawn, drawn
    assert "Covered" not in drawn, drawn
    # Row 5 of the grid, inside window 2's body, is still window 2's.
    assert vdp.pixels(250, 82, 1, 1)[0][0] == FACE
    quit_nook(session, base)
    check_clean(session)


def test_widgets_moving_the_highlight_redraws_only_its_two_items(session):
    base = start_menus(session)
    vdp = session.vdp
    click(vdp, menu_title(0))
    vdp.mouse(*menu_item(0, 0))
    menu_model(vdp, open_index=0, highlight=0)[0].check(vdp)
    settle(vdp)
    before = len(vdp.events)
    vdp.mouse(*menu_item(0, 1))
    menu_model(vdp, open_index=0, highlight=1)[0].check(vdp)
    settle(vdp)
    drawn = "".join(chr(data) for _time, kind, data in vdp.events[before:]
                    if kind == "gtext")
    assert sorted(drawn) == sorted("Open" + "Ctrl+O" + "Save" + "Ctrl+S"), \
        drawn
    # The dropdown's face behind each of the two rows, and the new
    # highlight:  nothing of the window under the dropdown.
    fills = [data for _time, kind, data in vdp.events[before:]
             if (kind == "plot") and (data[0] == 101)]
    assert len(fills) == 3, fills
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_the_right_alt_opens_menus_as_the_left_does(session):
    base = start_menus(session)
    vdp = session.vdp
    vdp.alt("f", right=True)
    menu_model(vdp, open_index=0)[0].check(vdp)
    vdp.press(KEYCODE_ESCAPE)
    menu_model(vdp)[0].check(vdp)
    vdp.alt(right=True)
    menu_model(vdp, open_index=0)[0].check(vdp)
    vdp.press(KEYCODE_ESCAPE)
    menu_model(vdp)[0].check(vdp)
    quiet(session)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_field_typed_into_quickly_reports_one_change(session):
    # Keys closer together than the 250 ms a field waits to go quiet give one
    # CHANGED, once typing stops, with the final length.
    base = start_field(session)
    vdp = session.vdp
    for ch in "abcde":
        vdp.key(ch)
        time.sleep(0.08)
    expect_line(session, "CHANGED 1 1 5")
    quiet(session)
    field_model(vdp, "abcde", caret=5).check(vdp)
    quit_nook(session, base)
    check_clean(session)


def test_widgets_a_held_change_goes_out_before_the_command_that_follows(
        session):
    base = start_field(session, after="",
                       field="field 1 10,10,120,14 20 - "
                       "button 2 10,40,50,16 OK flags=default")
    vdp = session.vdp
    vdp.keys_at_once("abc")
    vdp.press(KEYCODE_RETURN)
    expect_line(session, "CHANGED 1 1 3")
    expect_line(session, "COMMAND 1 2 0")
    quiet(session)
    quit_nook(session, base)
    check_clean(session)
