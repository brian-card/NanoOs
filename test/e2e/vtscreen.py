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


"""A model of a VT100-style terminal screen for checking what programs draw.

It understands the NanoOs terminal subset: printable characters, carriage
return, line feed, backspace, ESC[r;cH and ESC[r;cf (move the cursor),
ESC[K (erase to the end of the row), and ESC[2J (erase the screen).  Writing in
the last column leaves a wrap pending until the next printable character, as
xterm does.  Every other escape sequence is ignored.
"""


class VtScreen:
    """The cells and cursor of a terminal screen."""

    def __init__(self, rows=24, columns=80):
        """Start with a blank screen and the cursor at the top left.

        @param rows The number of rows.
        @param columns The number of columns.
        """
        self.rows = rows
        self.columns = columns
        self.cells = [[" "] * columns for _ in range(rows)]
        self.row = 0
        self.column = 0
        self.wrap_pending = False
        self.state = "text"
        self.sequence = ""

    def feed(self, text):
        """Process output written to the terminal.

        @param text The output.
        """
        for ch in text:
            self._char(ch)

    def line(self, row):
        """Return a row's text without trailing spaces.

        @param row The row, from 0.
        """
        return "".join(self.cells[row]).rstrip()

    def lines(self):
        """Return every row's text without trailing spaces."""
        return [self.line(row) for row in range(self.rows)]

    def __str__(self):
        return "\n".join("%2d|%s" % (row, line)
                         for row, line in enumerate(self.lines()))

    def _char(self, ch):
        if self.state == "escape":
            if ch == "[":
                self.state = "csi"
                self.sequence = ""
            else:
                self.state = "text"
        elif self.state == "csi":
            if "\x40" <= ch <= "\x7e":
                self.state = "text"
                self._csi(self.sequence, ch)
            else:
                self.sequence += ch
        elif ch == "\x1b":
            self.state = "escape"
        elif ch == "\r":
            self.column = 0
            self.wrap_pending = False
        elif ch == "\n":
            self._line_feed()
        elif ch == "\b":
            if self.column > 0:
                self.column -= 1
            self.wrap_pending = False
        elif ch >= " ":
            if self.wrap_pending:
                self.column = 0
                self._line_feed()
                self.wrap_pending = False
            self.cells[self.row][self.column] = ch
            if self.column == self.columns - 1:
                self.wrap_pending = True
            else:
                self.column += 1

    def _line_feed(self):
        if self.row == self.rows - 1:
            del self.cells[0]
            self.cells.append([" "] * self.columns)
        else:
            self.row += 1

    def _csi(self, sequence, final):
        if sequence.startswith("?"):
            return
        numbers = [int(part) if part else 0 for part in sequence.split(";")]
        if final in "Hf":
            row = numbers[0] if numbers[0] else 1
            column = numbers[1] if (len(numbers) > 1 and numbers[1]) else 1
            self.row = min(row, self.rows) - 1
            self.column = min(column, self.columns) - 1
            self.wrap_pending = False
        elif (final == "K") and (numbers[0] == 0):
            for column in range(self.column, self.columns):
                self.cells[self.row][column] = " "
        elif (final == "J") and (numbers[0] == 2):
            self.cells = [[" "] * self.columns for _ in range(self.rows)]
