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

"""End-to-end tests of terminals in Nook:  the window server's text grid
widget, which wintest drives directly, and term, which shows a console
session in one."""

import os
import re
import time

from harness import HOSTNAME, PROMPT, ps_entries
from tests.nook import (BORDER, FRAME_TOP, TITLE_BAR, boot_nook,
                        check_clean, corner_point, drag, expect_line,
                        quit_nook, run_client, run_nook, settle, start_nook,
                        vdp_login_as, wait_pixels)
from tests.vdp_drv import VDP_FONT_ID

CELL_WIDTH = 6
CELL_HEIGHT = 8
# The CGA palette index of each ANSI colour, as ansiToCgaMap maps it.
ANSI_TO_CGA = [0, 4, 2, 6, 1, 5, 3, 7, 8, 12, 10, 14, 9, 13, 11, 15]
# The VDU PLOT code of a copy, which a scroll of a grid nothing covers is.
PLOT_COPY = 0xBE


class GridModel:
    """What a text grid holds after a sequence of grid operations."""

    def __init__(self, columns, rows):
        """Make a blank grid, bright white on black.

        @param columns The number of columns.
        @param rows The number of rows.
        """
        self.columns = columns
        self.rows = rows
        self.chars = [[" "] * columns for _ in range(rows)]
        self.attributes = [[0x0F] * columns for _ in range(rows)]

    def put(self, row, column, attribute, text):
        """Carry out WIN_GRID_PUT."""
        for offset, ch in enumerate(text[:self.columns - column]):
            self.chars[row][column + offset] = ch
            self.attributes[row][column + offset] = attribute

    def clear(self, top, left, bottom, right, attribute):
        """Carry out WIN_GRID_CLEAR."""
        for row in range(top, bottom + 1):
            for column in range(left, right + 1):
                self.chars[row][column] = " "
                self.attributes[row][column] = attribute

    def scroll(self, top, bottom, count, attribute):
        """Carry out WIN_GRID_SCROLL:  up if count is positive."""
        for grid, blank in ((self.chars, " "), (self.attributes, attribute)):
            rows = grid[top:bottom + 1]
            blanks = [[blank] * self.columns for _ in range(abs(count))]
            rows = (rows[count:] + blanks) if count > 0 \
                else (blanks + rows[:count])
            grid[top:bottom + 1] = rows

    def cells(self):
        """Return the cells as FakeVdp.read_cells reads them back:  a blank
        cell has no foreground, and colours are CGA palette indices."""
        return [[(ch, None if ch == " " else ANSI_TO_CGA[attribute & 15],
                  ANSI_TO_CGA[attribute >> 4])
                 for ch, attribute in zip(chars, attributes)]
                for chars, attributes in zip(self.chars, self.attributes)]


def grid_origin(frame_x, frame_y):
    """Return the screen point of the top-left cell of a grid window wintest
    made at a point:  inside its border and title bar."""
    return frame_x + BORDER, frame_y + FRAME_TOP


def grid_frame(x, y, columns, rows):
    """Return the frame of a window wintest grid makes."""
    return (x, y, columns * CELL_WIDTH + 2 * BORDER,
            rows * CELL_HEIGHT + FRAME_TOP + BORDER)


def read_grid(vdp, x, y, columns, rows):
    """Read a grid window's cells back from the canvas."""
    left, top = grid_origin(x, y)
    return vdp.read_cells(left, top, columns, rows, VDP_FONT_ID)


def wait_grid_cells(vdp, x, y, model, timeout=30):
    """Wait until a grid window's canvas shows what a model holds.

    @param vdp The FakeVdp.
    @param x The window's left edge.
    @param y The window's top edge.
    @param model The GridModel.
    @param timeout Seconds to wait.
    """
    expected = model.cells()
    deadline = time.time() + timeout
    while True:
        actual = read_grid(vdp, x, y, model.columns, model.rows)
        if actual == expected:
            return
        if time.time() > deadline:
            wrong = [(row, column, actual[row][column], expected[row][column])
                     for row in range(model.rows)
                     for column in range(model.columns)
                     if actual[row][column] != expected[row][column]]
            raise AssertionError(f"{len(wrong)} cells differ: {wrong[:8]}")
        time.sleep(0.2)


def copies(vdp):
    """Count the copies of pixels FakeVdp has carried out."""
    with vdp.lock:
        return sum(1 for _time, kind, detail in vdp.events
                   if kind == "plot" and detail[0] == PLOT_COPY)


def start_grid(session, commands="", columns=80, rows=24, x=40, y=40):
    """Start nook with no demonstration windows and a wintest that makes a
    grid window, hides its cursor and carries out more commands.

    @param session The session.
    @param commands More wintest run commands.
    @param columns The grid's columns.
    @param rows The grid's rows.
    @param x The window's left edge.
    @param y The window's top edge.

    @return The number of context commands before nook.
    """
    session.write_file("/etc/wintest", f"grid {x},{y} {columns}x{rows} "
                       "cursor 1 0,0,0 " + commands)
    boot_nook(session)
    base = start_nook(session, demo=False)
    run_client(session, "@/etc/wintest")
    expect_line(session, "wintest: window 1")
    return base


# Step 2:  the text grid widget.

def test_terminal_grid_text_is_drawn_exactly_in_its_colours(session):
    base = start_grid(session, "put 1 0,0,0x0F hello put 1 1,5,0x1E Yellow "
                      "put 1 23,70,0x47 corner put 1 5,78,0x2F wrapped")
    model = GridModel(80, 24)
    model.put(0, 0, 0x0F, "hello")
    model.put(1, 5, 0x1E, "Yellow")
    model.put(23, 70, 0x47, "corner")
    model.put(5, 78, 0x2F, "wrapped")
    wait_grid_cells(session.vdp, 40, 40, model)
    quit_nook(session, base)
    check_clean(session)


def test_terminal_grid_cursor_is_its_cell_in_swapped_colours(session):
    base = start_grid(session, "put 1 2,3,0x1E abc cursor 1 2,4,1")
    model = GridModel(80, 24)
    model.put(2, 3, 0x1E, "abc")
    model.attributes[2][4] = 0xE1
    wait_grid_cells(session.vdp, 40, 40, model)
    quit_nook(session, base)
    check_clean(session)


def test_terminal_grid_row_repainted_never_shows_black_under_it(session):
    base = start_grid(session, "clear 1 0,0,0,79,0x40 sleep 4000 "
                      "put 1 0,0,0x4F a sleep 300 put 1 0,1,0x4F b "
                      "sleep 300 put 1 0,2,0x4F c")
    vdp = session.vdp
    model = GridModel(80, 24)
    model.clear(0, 0, 0, 79, 0x40)
    wait_grid_cells(vdp, 40, 40, model)
    left, top = grid_origin(40, 40)
    # A pixel of the row's background, well clear of the text put on it.
    point = (left + 60 * CELL_WIDTH + 2, top + 3)
    vdp.watch([point])
    model.put(0, 0, 0x4F, "abc")
    wait_grid_cells(vdp, 40, 40, model)
    painted = vdp.painted(point)
    assert painted and set(painted) == {ANSI_TO_CGA[4]}, painted
    quit_nook(session, base)
    check_clean(session)


def test_terminal_grid_row_of_a_maximized_window_never_shows_black_under_it(
        session):
    # Maximized, the body is 638x464:  106x58 cells and a 2-pixel margin to
    # their right.  Only the first row is read back, which reading all 58
    # rows of is too slow to see in the time before the puts.
    base = start_grid(session, "state 1 maximized sleep 3000 "
                      "clear 1 0,0,0,105,0x40 sleep 4000 "
                      "put 1 0,0,0x4F a sleep 300 put 1 0,1,0x4F b "
                      "sleep 300 put 1 0,2,0x4F c")
    vdp = session.vdp
    model = GridModel(106, 1)
    model.clear(0, 0, 0, 105, 0x40)
    wait_grid_cells(vdp, 0, 0, model)
    left, top = grid_origin(0, 0)
    point = (left + 60 * CELL_WIDTH + 2, top + 3)
    margin = (left + 106 * CELL_WIDTH + 1, top + 3)
    vdp.watch([point])
    model.put(0, 0, 0x4F, "abc")
    wait_grid_cells(vdp, 0, 0, model)
    painted = vdp.painted(point)
    assert painted and set(painted) == {ANSI_TO_CGA[4]}, painted
    assert vdp.pixel(*margin) == ANSI_TO_CGA[0], vdp.pixel(*margin)
    # wintest prints the events once its commands are done.
    expect_line(session, "STATE 1 maximized")
    quit_nook(session, base)
    check_clean(session)


def fill_rows(model, rows):
    """Return wintest commands that put a line of text on each of a number of
    rows, and put the same in a model."""
    commands = ""
    for row in range(rows):
        text = f"line{row:02d}"
        commands += f"put 1 {row},{row},0x0E {text} "
        model.put(row, row, 0x0E, text)
    return commands


def test_terminal_grid_scroll_of_an_uncovered_grid_copies_its_pixels(session):
    model = GridModel(80, 24)
    base = start_grid(session, fill_rows(model, 24) + "sleep 4000 "
                      "scroll 1 0,23,1,0x10")
    vdp = session.vdp
    wait_grid_cells(vdp, 40, 40, model)
    before = copies(vdp)
    model.scroll(0, 23, 1, 0x10)
    wait_grid_cells(vdp, 40, 40, model)
    settle(vdp)
    assert copies(vdp) - before == 1
    quit_nook(session, base)
    check_clean(session)


def cells_outside(cells, frame):
    """Blank out the cells of a grid window at 40,40 that a frame overlaps.

    @param cells Rows of cells.
    @param frame The (x, y, width, height) over the grid.

    @return The cells, with each one the frame overlaps None.
    """
    left, top = grid_origin(40, 40)
    x, y, width, height = frame
    return [[None if (left + column * CELL_WIDTH < x + width
                      and x < left + (column + 1) * CELL_WIDTH
                      and top + row * CELL_HEIGHT < y + height
                      and y < top + (row + 1) * CELL_HEIGHT) else cell
             for column, cell in enumerate(line)]
            for row, line in enumerate(cells)]


def frame_pixels(vdp_canvas, frame):
    """Return the pixels of a canvas snapshot inside a frame."""
    x, y, width, height = frame
    return [vdp_canvas[row * 640 + x:row * 640 + x + width]
            for row in range(y, y + height)]


def test_terminal_grid_scroll_of_a_covered_grid_repaints_it_exactly(session):
    model = GridModel(80, 24)
    above = (100, 60, 200, 100)
    base = start_grid(session, fill_rows(model, 24)
                      + "create %d,%d,%d,%d Above sleep 4000 " % above
                      + "scroll 1 2,20,-2,0x40")
    vdp = session.vdp
    expect_line(session, "wintest: window 2")
    settle(vdp)
    before = bytes(vdp.canvas)
    copied = copies(vdp)
    assert cells_outside(read_grid(vdp, 40, 40, 80, 24), above) \
        == cells_outside(model.cells(), above)
    model.scroll(2, 20, -2, 0x40)
    deadline = time.time() + 30
    while cells_outside(read_grid(vdp, 40, 40, 80, 24), above) \
            != cells_outside(model.cells(), above):
        assert time.time() < deadline, "the scroll was never painted"
        time.sleep(0.2)
    settle(vdp)
    assert copies(vdp) == copied
    assert frame_pixels(bytes(vdp.canvas), above) \
        == frame_pixels(before, above)
    quit_nook(session, base)
    check_clean(session)


def test_terminal_grid_burst_of_writes_is_painted_fewer_times_than_written(
        session):
    base = start_grid(session, "sleep 2000 burst 1 50")
    vdp = session.vdp
    settle(vdp)
    glyphs = vdp.count_events("gtext")
    model = GridModel(80, 24)
    for write in range(50):
        model.put(0, 0, 0x0F, f"write {write}")
    wait_grid_cells(vdp, 40, 40, model)
    settle(vdp)
    drawn = vdp.count_events("gtext") - glyphs
    every = sum(len(f"write {write}") for write in range(50))
    assert drawn < every, (drawn, every)
    quit_nook(session, base)
    check_clean(session)


def test_terminal_grid_resize_snaps_to_whole_cells_and_keeps_its_cells(
        session):
    model = GridModel(80, 24)
    base = start_grid(session, fill_rows(model, 24))
    vdp = session.vdp
    wait_grid_cells(vdp, 40, 40, model)
    settle(vdp)
    corner = corner_point(grid_frame(40, 40, 80, 24))
    drag(vdp, corner, (corner[0] + 17, corner[1] + 13))
    expect_line(session, "RESIZED 1 492x200 grid 82x25")
    bigger = GridModel(82, 25)
    for row in range(24):
        bigger.chars[row][:80] = model.chars[row]
        bigger.attributes[row][:80] = model.attributes[row]
    wait_grid_cells(vdp, 40, 40, bigger)
    corner = corner_point(grid_frame(40, 40, 82, 25))
    drag(vdp, corner, (corner[0] - 20, corner[1] - 30))
    expect_line(session, "RESIZED 1 468x168 grid 78x21")
    smaller = GridModel(78, 21)
    for row in range(21):
        smaller.chars[row] = model.chars[row][:78]
        smaller.attributes[row] = model.attributes[row][:78]
    wait_grid_cells(vdp, 40, 40, smaller)
    quit_nook(session, base)
    check_clean(session)


# Step 3:  term, the session's output.

SCREEN_COLUMNS = 640 // CELL_WIDTH
SCREEN_ROWS = 480 // CELL_HEIGHT


def start_term(session, options="", files=None):
    """Boot with Nook, log in on the VDP console, run nook there, and start
    term from UART1.

    @param session The session.
    @param options term's options.
    @param files A dictionary of path to contents to write before boot.

    @return The number of context commands before nook.
    """
    for path, data in (files or {}).items():
        session.write_file(path, data)
    boot_nook(session)
    base = start_nook(session, demo=False)
    session.child.sendline("term " + options + " &")
    return base


def test_terminal_term_runs_the_shell_in_its_window(session):
    base = start_term(session, "--at=40,40")
    vdp = session.vdp
    deadline = time.time() + 30
    while True:
        lines = ["".join(ch or "?" for ch, _fg, _bg in row).rstrip()
                 for row in read_grid(vdp, 40, 40, 80, 24)]
        if lines[0].startswith("mush root@"):
            break
        assert time.time() < deadline, lines[:3]
        time.sleep(0.5)
    vdp.type_paced("echo hi\r")
    deadline = time.time() + 30
    while True:
        lines = ["".join(ch or "?" for ch, _fg, _bg in row).rstrip()
                 for row in read_grid(vdp, 40, 40, 80, 24)]
        if (lines[1] == "hi") and lines[2].startswith("mush root@"):
            break
        assert time.time() < deadline, lines[:3]
        time.sleep(0.5)
    assert lines[0].endswith("# echo hi"), lines[:3]
    quit_nook(session, base)
    check_clean(session)


STREAMS_DIR = os.path.join(os.path.dirname(__file__), "..", "streams")
C, R = SCREEN_COLUMNS, SCREEN_ROWS
# The differential corpus:  for each name, the bytes catted on the VDP
# console and in a full-screen term.  Each starts by clearing the screen.
CORPUS = {
    "wraps": "\x1b[2J" + "A" * 250 + "\nshort line\n" + "B" * C + "\n"
             + "C" * (C - 1) + "\nend",
    "sgr": "\x1b[2J" + "".join(f"\x1b[{code}mX" for code in range(30, 38))
           + "\n" + "".join(f"\x1b[{code}mY" for code in range(40, 48))
           + "\n" + "".join(f"\x1b[{code}mZ" for code in range(90, 98))
           + "\n" + "".join(f"\x1b[{code}mW" for code in range(100, 108))
           + "\n\x1b[31;44mboth\x1b[39mfg\x1b[49mbg\x1b[1;32mbold"
           + "\x1b[38;5;3mext\x1b[0mreset\x1b[33m\x1b[mempty\x1b[97;100m"
           + "bright\x1b[m\n",
    "clear": "\x1b[2Jbefore\x1b[44m\x1b[2Jafter clear\x1b[0m\n",
    "cursor": "\x1b[2J\x1b[1;1HA\x1b[1;%dHB\x1b[%d;1HC\x1b[%d;%dHD"
              % (C, R, R, C)
              + "\x1b[99;999HE\x1b[0;0HF\x1b[5;5fG\x1b[HH\x1b[3H",
    "erase": "\x1b[2J" + "".join("row %02d %s\n" % (row, "x" * 60)
                                 for row in range(8))
             + "\x1b[2;10H\x1b[K\x1b[41m\x1b[4;20H\x1b[K\x1b[0m"
             + "\x1b[6;%dH\x1b[K\x1b[7;5H\x1b[1K\x1b[8;1H\x1b[0K" % C,
    "lines": "\x1b[2J" + "\n".join("row%02d" % row for row in range(R))
             + "\x1b[1;1H\x1b[2L\x1b[%d;1H\x1b[M\x1b[30;3H\x1b[5M" % R
             + "\x1b[%d;1H\x1b[9L\x1b[42m\x1b[10;1H\x1b[L\x1b[0m" % (R - 2),
    "tabs": "\x1b[2Ja\tb\tc\n" + "\t" * 15 + "x\n"
            + "\x1b[%d;%dH\ty" % (5, C - 2)
            + "\x1b[%d;1H" % R + "\t" * 14 + "z",
    "bottom": "\x1b[2J\x1b[%d;1H" % R + "L" * 300 + "\nafter\n"
              + "\x1b[%d;%dHQ\bR" % (R, C),
    "unknown": "\x1b[2J\x1b[?25l\x1b[5n\x1b[>c\x1b]0;title\x07text\x1b(B"
               + "\x1bM\x1b[1;2;3;4X\x1b[200~\x01\x02\x7f\x1b[?1049h"
               + "abc\b\bX\x07\x1b[3;3\nY\x1b[",
    "vi": None,
    "ll": None,
}


def corpus_stream(name):
    """Return a stream of the corpus, reading recorded ones from streams/."""
    if CORPUS[name] is not None:
        return CORPUS[name]
    data = open(os.path.join(STREAMS_DIR, name + ".txt")).read()
    return data if data.startswith("\x1b[2J") else "\x1b[2J" + data


def canonical_codes(vdp):
    """Map each character code to the code the decoder reads its glyph back
    as, which is the code itself unless another code has the same glyph."""
    with vdp.lock:
        width, height, data = vdp.fonts[VDP_FONT_ID]
    table = vdp.glyph_table(VDP_FONT_ID)
    row_bytes = (width + 7) // 8
    shift = row_bytes * 8 - width
    codes = []
    for code in range(256):
        start = code * row_bytes * height
        glyph = tuple(int.from_bytes(data[start + y * row_bytes:
                                          start + (y + 1) * row_bytes], "big")
                      >> shift for y in range(height))
        codes.append(table[glyph])
    return codes


def console_as_read(vdp):
    """Return the VDP console's grid as the decoder would read a term grid
    showing the same:  glyphs as the decoder reads them back, a cell drawn
    in one colour as a space with no foreground, and the cursor's cell in
    its colours swapped."""
    codes = canonical_codes(vdp)
    cells = vdp.grid_cells()
    with vdp.lock:
        cursor = (vdp.grid_row, min(vdp.grid_column, vdp.grid_columns - 1))
    read = []
    for row, line in enumerate(cells):
        read_line = []
        for column, (ch, fg, bg) in enumerate(line):
            if (row, column) == cursor:
                fg, bg = bg, fg
            ch = chr(codes[ord(ch)])
            if ch == " " or fg == bg:
                read_line.append((" ", None, bg))
            else:
                read_line.append((ch, fg, bg))
        read.append(read_line)
    return read


def wait_console_prompt(vdp, timeout=30):
    """Wait until the VDP console's grid shows the shell's prompt just
    before its cursor, wherever a stream left the cursor.

    @param vdp The FakeVdp.
    @param timeout Seconds to wait.
    """
    deadline = time.time() + timeout
    while True:
        with vdp.lock:
            line = "".join(vdp.grid[vdp.grid_row][:vdp.grid_column])
        if re.search(r"@%s:\S+[#$] $" % HOSTNAME, line):
            return
        assert time.time() < deadline, \
            f"the console never showed its prompt:  {vdp.grid_lines()}"
        time.sleep(0.2)


def differential(session, names):
    """Cat each stream of the corpus named on the VDP console, then in a term
    as big as the screen, and check that term's grid shows what the
    console's did, cell for cell and colour for colour.

    @param session The session.
    @param names The names of the streams.
    """
    files = {"/etc/t%d" % index: corpus_stream(name)
             for index, name in enumerate(names)}
    for path, data in files.items():
        session.write_file(path, data)
    boot_nook(session)
    vdp = session.vdp
    vdp_login_as(vdp, "root", "rootroot")
    screens = []
    for index in range(len(names)):
        vdp.type("cat /etc/t%d\r" % index)
        wait_console_prompt(vdp)
        settle(vdp, frames=False)
        screens.append(console_as_read(vdp))
    base = run_nook(vdp, demo=False)
    session.child.sendline("term --plain --size=%dx%d --at=0,0 &" % (C, R))
    deadline = time.time() + 30
    while not "".join(ch or "?" for ch, _fg, _bg in
                      vdp.read_cells(0, 0, C, 1, VDP_FONT_ID)[0]) \
            .startswith("mush root@"):
        assert time.time() < deadline, "term never showed its prompt"
        time.sleep(0.5)
    for index, name in enumerate(names):
        vdp.type_paced("cat /etc/t%d\r" % index)
        expected = screens[index]
        deadline = time.time() + 60
        while True:
            actual = vdp.read_cells(0, 0, C, R, VDP_FONT_ID)
            if actual == expected:
                break
            if time.time() > deadline:
                wrong = [(row, column, actual[row][column],
                          expected[row][column])
                         for row in range(R) for column in range(C)
                         if actual[row][column] != expected[row][column]]
                raise AssertionError(f"{name}: {len(wrong)} cells differ: "
                                     f"{wrong[:6]}")
            time.sleep(0.5)
    quit_nook(session, base)
    check_clean(session)


def test_terminal_term_matches_the_console_for_text_and_wraps(session):
    differential(session, ["wraps", "tabs", "bottom"])


def test_terminal_term_matches_the_console_for_colours(session):
    differential(session, ["sgr", "clear"])


def test_terminal_term_matches_the_console_for_cursor_and_erasing(session):
    differential(session, ["cursor", "erase", "lines"])


def test_terminal_term_matches_the_console_for_unknown_sequences(session):
    differential(session, ["unknown"])


def test_terminal_term_matches_the_console_for_vi_and_ll(session):
    differential(session, ["vi", "ll"])


# Step 4:  session grids:  input, resize and ending.

TERM_AT = (40, 40)


def term_lines(vdp, columns=80, rows=24, at=TERM_AT):
    """Read a term window's grid back from the canvas as lines of text,
    without trailing spaces, with "?" for a cell no glyph matches."""
    return ["".join(ch or "?" for ch, _fg, _bg in row).rstrip()
            for row in read_grid(vdp, at[0], at[1], columns, rows)]


def wait_lines(vdp, check, columns=80, rows=24, at=TERM_AT, timeout=30):
    """Wait until a term window's lines pass a check.

    @param vdp The FakeVdp.
    @param check A function of the lines that says whether they are what is
      wanted.
    @param columns The grid's columns.
    @param rows The grid's rows.
    @param at The window's top-left corner.
    @param timeout Seconds to wait.

    @return The lines.
    """
    deadline = time.time() + timeout
    while True:
        lines = term_lines(vdp, columns, rows, at)
        if check(lines):
            return lines
        assert time.time() < deadline, lines
        time.sleep(0.3)


def is_prompt(line):
    """Determine whether a line is the shell's prompt and nothing else."""
    return re.fullmatch(r"mush \S+@%s:\S+[#$]" % HOSTNAME, line) is not None


def start_term_at_prompt(session, options="--at=%d,%d" % TERM_AT):
    """Start nook and term, and wait for the shell's prompt in the term.

    @return The number of context commands before nook.
    """
    base = start_term(session, options)
    wait_lines(session.vdp, lambda lines: is_prompt(lines[0]))
    return base


def mush_count(session):
    """Count the shells running."""
    return sum(1 for _pid, _user, name in ps_entries(session)
               if name == "mush")


def wait_output(session, until, timeout=15):
    """Collect what wintest prints of its session's output, until it ends
    with some text.

    @param session The session.
    @param until The text.
    @param timeout Seconds to wait.

    @return The output collected, as wintest shows it.
    """
    output = ""
    deadline = time.time() + timeout
    while not output.endswith(until):
        left = deadline - time.time()
        assert left > 0, output
        session.child.expect(r"wintest: output ([^\r\n]*)\r*\n",
                             timeout=left)
        output += session.child.match.group(1)
    return output


def test_terminal_typing_goes_to_the_session_and_never_to_term(session):
    base = start_term_at_prompt(session)
    vdp = session.vdp
    vdp.type_paced("echo hi\r")
    lines = wait_lines(vdp,
                       lambda lines: lines[1] == "hi" and is_prompt(lines[2]))
    assert lines[0].endswith("# echo hi"), lines[:3]
    log = session.sh("cat /root/.log/term.log")
    assert "key" not in log, log
    quit_nook(session, base)
    check_clean(session)


def test_terminal_a_display_cant_send_its_session_input_or_a_size(session):
    boot_nook(session)
    base = start_nook(session, demo=False)
    run_client(session, "grid 40,40 60x10 session inject")
    expect_line(session, "wintest: window 1")
    expect_line(session, "wintest: inject input Operation not permitted")
    expect_line(session, "wintest: inject size Operation not permitted")
    vdp = session.vdp
    wait_output(session, "mush root@%s:/root# " % HOSTNAME)
    vdp.type_paced("echo ok\r")
    assert wait_output(session, "# ") == \
        "echo ok\\nok\\nmush root@%s:/root# " % HOSTNAME
    vdp.type_paced("exit\r")
    expect_line(session, "SESSION_ENDED 1", timeout=20)
    quit_nook(session, base)
    check_clean(session)


def test_terminal_only_a_session_grid_gets_a_session_and_only_one(session):
    boot_nook(session)
    base = start_nook(session, demo=False)
    shells = mush_count(session)
    assert session.sh("wintest run grid 40,40 20x5 twice exit") \
        == "wintest: grid: Operation not supported"
    run_client(session, "grid 40,40 20x5")
    expect_line(session, "wintest: window 1")
    time.sleep(1)
    assert mush_count(session) == shells
    run_client(session, "grid 200,40 20x5 session")
    expect_line(session, "wintest: window 2")
    wait_output(session, "mush root@%s:/root# " % HOSTNAME)
    deadline = time.time() + 15
    while mush_count(session) != shells + 1:
        assert time.time() < deadline, mush_count(session)
        time.sleep(0.5)
    quit_nook(session, base)
    check_clean(session)


def test_terminal_reports_from_a_client_are_only_the_real_size(session):
    session.write_file("/etc/wintest",
                       "grid 40,40 40x10 session wait 8000 reports 1 1")
    boot_nook(session)
    base = start_nook(session, demo=False)
    run_client(session, "@/etc/wintest")
    expect_line(session, "wintest: window 1")
    wait_output(session, "mush root@%s:/root# " % HOSTNAME)
    session.vdp.type_paced("termtest report\r")
    output = wait_output(session, "# ", timeout=30)
    # The console's mode escapes and termtest's output reach the display by
    # different paths, so only the report's bytes are in a fixed place.
    assert output.count("^[[?2048h") == 1 and output.count("^[[?2048l") == 1, \
        output
    assert output.replace("^[[?2048h", "").replace("^[[?2048l", "") == \
        "termtest report\\n^[[48;10;40;80;240t\\nmush root@%s:/root# " \
        % HOSTNAME
    quit_nook(session, base)
    check_clean(session)


def test_terminal_keys_typed_while_the_window_server_is_busy_all_arrive(
        session):
    base = start_term_at_prompt(session)
    vdp = session.vdp
    # Read slowly, the repaint of the resized window keeps the driver
    # writing while the keys are typed.
    vdp.read_rate = 5000
    resize_term(session, 30, 16)
    vdp.type_burst("echo abcdefghi")
    vdp.read_rate = None
    vdp.type_paced("\r")
    lines = wait_lines(vdp, lambda lines: is_prompt(lines[2]), columns=85,
                       rows=26, timeout=60)
    assert lines[:2] == [lines[0][:lines[0].index("#") + 1]
                         + " echo abcdefghi", "abcdefghi"], lines[:3]
    quit_nook(session, base)
    check_clean(session)


def test_terminal_ctrl_c_ends_a_loose_loop_in_term(session):
    base = start_term_at_prompt(session)
    vdp = session.vdp
    vdp.type_paced("looseLoop\r")
    time.sleep(2)
    vdp.ctrl("c")
    lines = wait_lines(vdp, lambda lines: is_prompt(lines[2]))
    assert lines[0].endswith("# looseLoop"), lines[:3]
    assert lines[1] == "^C", lines[:3]
    quit_nook(session, base)
    check_clean(session)


def resize_term(session, dx, dy, columns=80, rows=24):
    """Drag a term window's bottom-right corner, and wait for the session's
    shell to be told."""
    corner = corner_point(grid_frame(TERM_AT[0], TERM_AT[1], columns, rows))
    drag(session.vdp, corner, (corner[0] + dx, corner[1] + dy))


def test_terminal_termtest_size_follows_a_resize(session):
    base = start_term_at_prompt(session)
    vdp = session.vdp
    resize_term(session, 30, 16)
    # Painted at its new size, the window has told its session the size.
    wait_pixels(vdp, {corner_point(grid_frame(TERM_AT[0], TERM_AT[1], 85,
                                              26)): TITLE_BAR})
    vdp.type_paced("termtest size\r")
    lines = wait_lines(vdp,
                       lambda lines: len(lines) > 2 and is_prompt(lines[2]),
                       columns=85, rows=26)
    assert lines[1] == "26 85", lines[:3]
    quit_nook(session, base)
    check_clean(session)


def test_terminal_vi_redraws_to_fill_a_resized_term(session):
    base = start_term_at_prompt(session)
    vdp = session.vdp
    vdp.type_paced("vi /root/new.txt\r")
    wait_lines(vdp, lambda lines: "[New File]" in lines[23]
               and lines[1:23] == ["~"] * 22)
    resize_term(session, 30, 16)
    lines = wait_lines(vdp, lambda lines: lines[1:25] == ["~"] * 24
                       and lines[25].endswith("0,0-1"), columns=85, rows=26)
    assert lines[0] == "", lines[:2]
    vdp.type_paced(":q!\r")
    quit_nook(session, base)
    check_clean(session)


def desktop_at(vdp, frame):
    """Determine whether a frame's corners show the desktop."""
    left, top, width, height = frame
    from tests.nook import DESKTOP
    return all(vdp.pixel(x, y) == DESKTOP
               for x, y in ((left, top), (left + width - 1, top),
                            (left, top + height - 1),
                            (left + width - 1, top + height - 1)))


def wait_closed(session, shells):
    """Wait until the term window is gone and the shells are back to a
    number."""
    vdp = session.vdp
    frame = grid_frame(TERM_AT[0], TERM_AT[1], 80, 24)
    deadline = time.time() + 30
    while not desktop_at(vdp, frame) or mush_count(session) != shells:
        assert time.time() < deadline, mush_count(session)
        time.sleep(0.5)


def test_terminal_exit_closes_the_window_and_term_exits_0(session):
    boot_nook(session)
    base = start_nook(session, demo=False)
    shells = mush_count(session)
    session.child.sendline("term --at=%d,%d" % TERM_AT)
    vdp = session.vdp
    wait_lines(vdp, lambda lines: is_prompt(lines[0]))
    vdp.type_paced("exit\r")
    session.child.expect(PROMPT, timeout=30)
    assert session.sh("echo status $?") == "status 0"
    wait_closed(session, shells)
    quit_nook(session, base)
    check_clean(session)


def test_terminal_the_close_button_ends_the_shell_and_frees_its_slot(session):
    boot_nook(session)
    base = start_nook(session, demo=False)
    shells = mush_count(session)
    session.child.sendline("term --at=%d,%d &" % TERM_AT)
    vdp = session.vdp
    wait_lines(vdp, lambda lines: is_prompt(lines[0]))
    assert mush_count(session) == shells + 1
    frame = grid_frame(TERM_AT[0], TERM_AT[1], 80, 24)
    from tests.nook import button_rect, click
    x, y, width, height = button_rect(frame, 2)
    click(vdp, (x + width // 2, y + height // 2))
    wait_closed(session, shells)
    session.child.sendline("term --at=%d,%d &" % TERM_AT)
    wait_lines(vdp, lambda lines: is_prompt(lines[0]))
    quit_nook(session, base)
    check_clean(session)


def test_terminal_killing_term_ends_its_shell(session):
    boot_nook(session)
    base = start_nook(session, demo=False)
    shells = mush_count(session)
    session.child.sendline("term --at=%d,%d &" % TERM_AT)
    wait_lines(session.vdp, lambda lines: is_prompt(lines[0]))
    pids = [pid for pid, _user, name in ps_entries(session) if name == "term"]
    assert len(pids) == 1, pids
    output = session.sh("kill %d" % pids[0])
    assert output.startswith("Termination of process"), output
    wait_closed(session, shells)
    quit_nook(session, base)
    check_clean(session)


# Step 6:  what an echo costs.

def test_terminal_timing_reports_one_echo_per_key_typed_slowly(session):
    boot_nook(session)
    vdp = session.vdp
    vdp_login_as(vdp, "root", "rootroot")
    base = run_nook(vdp, "--timing", demo=False)
    session.child.sendline("term --at=%d,%d &" % TERM_AT)
    wait_lines(vdp, lambda lines: is_prompt(lines[0]))
    for ch in "echo hi\r":
        vdp.key(ch)
        time.sleep(0.5)
    wait_lines(vdp, lambda lines: lines[1] == "hi" and is_prompt(lines[2]))
    quit_nook(session, base)
    match = vdp.expect(r"sysnook: echo frames (\d+) median (\d+) ms "
                       r"max (\d+) ms")
    assert int(match.group(1)) == 8, match.group(0)
    assert int(match.group(2)) <= int(match.group(3)), match.group(0)
    check_clean(session)


def test_terminal_destroying_a_session_window_ends_its_shell(session):
    boot_nook(session)
    base = start_nook(session, demo=False)
    shells = mush_count(session)
    run_client(session, "grid 40,40 60x10 session closes")
    expect_line(session, "wintest: window 1")
    wait_output(session, "mush root@%s:/root# " % HOSTNAME)
    assert mush_count(session) == shells + 1
    frame = grid_frame(40, 40, 60, 10)
    from tests.nook import button_rect, click
    x, y, width, height = button_rect(frame, 2)
    click(session.vdp, (x + width // 2, y + height // 2))
    expect_line(session, "CLOSE_REQUESTED 1")
    deadline = time.time() + 30
    while mush_count(session) != shells:
        assert time.time() < deadline, mush_count(session)
        time.sleep(0.5)
    assert len([pid for pid, _user, name in ps_entries(session)
                if name == "wintest"]) == 1
    quit_nook(session, base)
    check_clean(session)
