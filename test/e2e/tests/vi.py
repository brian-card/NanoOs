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

"""End-to-end tests of vi."""

import re
import time

import pexpect

from vtscreen import VtScreen
from fakevdp import KEYCODE_UP
from harness import (ed_input, ed_session, ll_names, PROMPT, small_memory_size,
                     vdp_login, write_oversized_file)


def test_vi_draws_on_the_vdp_console(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    rows, columns = vdp.grid_rows, vdp.grid_columns

    def shows(lines, status=None):
        def check(grid):
            return (grid[:rows - 1] == lines + ["~"] * (rows - 1 - len(lines))
                    and (status is None
                         or (grid[rows - 1][:vi_ruler_start(columns)].rstrip()
                             == status)))
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
    # The ruler shows the last line and the cursor on its last character.
    ruler = " " * vi_ruler_start(columns) + "%d,3" % (count + 2)
    vdp.wait_grid(lambda grid: grid[:rows - 1] == screen_rows[-(rows - 1):]
                  and grid[rows - 1] == ruler)
    vdp.type("gg")
    vdp.wait_grid(lambda grid: grid[:rows - 1] == screen_rows[:rows - 1])
    vdp.type(":wq\r")
    vdp.expect(r"%dL, \d+C written" % (count + 2))
    vdp.expect(PROMPT)
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
        """Read output until the status line's message, left of the ruler,
        is exactly some text.

        @param text The text.
        @param timeout Seconds to wait.
        """
        self.wait(lambda screen: vi_message(screen) == text, timeout)

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


def vi_ruler_start(columns):
    """Return the column vi's ruler starts at on the status line.

    @param columns The number of columns on the screen.
    """
    return max(columns - 18, (columns + 1) // 2)


def vi_message(screen):
    """Return the message on a VtScreen's status line, left of vi's ruler.

    @param screen The VtScreen.
    """
    line = screen.line(screen.rows - 1)
    return line[:vi_ruler_start(screen.columns)].rstrip()


def vi_ruler(screen):
    """Return vi's ruler on a VtScreen's status line.

    @param screen The VtScreen.
    """
    return screen.line(screen.rows - 1)[vi_ruler_start(screen.columns):]


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
    assert vi_message(vi.screen) == written, str(vi.screen)
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


def test_vi_toggles_case(s):
    long_line = "abcdeFGHIJ" * 30
    lines = ["Hello, World", "abc", "", long_line]
    s.restart_with_file("/etc/case.txt", "".join(l + "\n" for l in lines))
    s.login()
    vi = ViTerminal(s, "vi /etc/case.txt")
    vi.wait_lines(vi_rows(lines, 24, 80))

    def show(keys, at, line, index=None):
        lines[at] = line
        vi.keys(keys)
        expected = vi_rows(lines, 24, 80)
        cursor = (None if index is None
                  else (sum(vi_line_rows(l, 80) for l in lines[:at])
                        + index // 80, index % 80))
        vi.wait(lambda screen: (screen.lines()[:len(expected)] == expected)
                and ((cursor is None)
                     or ((screen.row, screen.column) == cursor)))

    def toggled(line, start, end):
        return line[:start] + line[start:end].swapcase() + line[end:]

    show("~", 0, "hello, World", 1)
    show("6~", 0, "hELLO, World", 7)
    # Toggling stops at the line's end and leaves the cursor on its last
    # character.
    show(".", 0, "hELLO, wORLD", 11)
    show("~", 0, "hELLO, wORLd", 11)
    show("u", 0, "hELLO, wORLD", 11)
    show("j0~~~~", 1, "ABc", 2)
    vi.keys("j~")
    vi.settle()
    assert vi.screen.lines()[:4] == vi_rows(lines, 24, 80)[:4]
    line = toggled(lines[3], 100, 120)
    show("j0100l20~", 3, line, 120)
    # The count runs on past the end of the window onto the long line.
    line = toggled(line, 30, 230)
    show("030l200~", 3, line, 230)
    line = toggled(line, 298, 300)
    show("$h5~", 3, line, 299)
    show("0.", 3, toggled(line, 0, 5), 5)
    show("u", 3, line)
    vi.keys(":wq\r")
    vi.wait_exit()
    assert s.sh("cat /etc/case.txt", timeout=30) == "\n".join(lines)


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


def test_vi_shows_the_cursor_position_on_the_status_line(s):
    s.login()
    vi = ViTerminal(s, "vi /etc/ruler.txt")
    vi.wait_status('"/etc/ruler.txt" [New File]')

    def ruler(keys, text):
        vi.keys(keys)
        vi.wait(lambda screen: vi_ruler(screen) == text)

    ruler("", "0,0-1")
    ruler("i\tab\r" + vi_long_line(300) + "\x1b", "2,300")
    ruler("0200l", "2,201")
    ruler("k0", "1,1-8")
    ruler("l", "1,2-9")
    ruler("$", "1,3-10")
    vi.keys("a")
    # Insert mode shows only its mode on the status line.
    vi.wait(lambda screen: screen.line(23) == "-- INSERT --")
    ruler("\x1b", "1,3-10")
    # On a narrow screen the ruler starts in the middle.
    vi.keys("\x1b[48;24;30;0;0t")
    vi.settle()
    vi.screen = VtScreen(24, 30)
    ruler("\x0cj", "2,3")
    assert vi.screen.line(23) == " " * 15 + "2,3"
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
