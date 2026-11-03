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


"""Tests of the terminal and VDP models the end-to-end tests check against.

    python3 test/e2e/test_models.py [-v]

They need no simulator, so they run in a moment and say whether a failing
end-to-end test is the program or the model.
"""

import os
import tempfile
import unittest

from fakevdp import FakeVdp
from vtscreen import VtScreen


def screen_with_rows(rows=5, columns=4):
    """Make a VtScreen whose rows read "r0", "r1", and so on.

    @param rows The number of rows.
    @param columns The number of columns.

    @return The VtScreen.
    """
    screen = VtScreen(rows, columns)
    screen.feed("\r\n".join("r%d" % row for row in range(rows)))
    return screen


class VtScreenInsertDeleteTest(unittest.TestCase):
    """ESC[nL and ESC[nM in the VT model."""

    def test_insert_line_moves_rows_down_and_loses_the_bottom(self):
        screen = screen_with_rows()
        screen.feed("\x1b[2;3H\x1b[L")
        self.assertEqual(["r0", "", "r1", "r2", "r3"], screen.lines())
        self.assertEqual((1, 0), (screen.row, screen.column))

    def test_insert_lines_with_a_count(self):
        screen = screen_with_rows()
        screen.feed("\x1b[2;1H\x1b[2L")
        self.assertEqual(["r0", "", "", "r1", "r2"], screen.lines())

    def test_insert_more_lines_than_remain_blanks_to_the_bottom(self):
        screen = screen_with_rows()
        screen.feed("\x1b[4;1H\x1b[9L")
        self.assertEqual(["r0", "r1", "r2", "", ""], screen.lines())
        self.assertEqual(5, len(screen.cells))

    def test_delete_line_moves_rows_up_and_blanks_the_bottom(self):
        screen = screen_with_rows()
        screen.feed("\x1b[2;3H\x1b[M")
        self.assertEqual(["r0", "r2", "r3", "r4", ""], screen.lines())
        self.assertEqual((1, 0), (screen.row, screen.column))

    def test_delete_lines_with_a_count(self):
        screen = screen_with_rows()
        screen.feed("\x1b[1;1H\x1b[3M")
        self.assertEqual(["r3", "r4", "", "", ""], screen.lines())

    def test_delete_more_lines_than_remain_blanks_to_the_bottom(self):
        screen = screen_with_rows()
        screen.feed("\x1b[5;1H\x1b[0M")
        self.assertEqual(["r0", "r1", "r2", "r3", ""], screen.lines())
        screen.feed("\x1b[2;1H\x1b[9M")
        self.assertEqual(["r0", "", "", "", ""], screen.lines())
        self.assertEqual(5, len(screen.cells))

    def test_insert_and_delete_cancel_a_pending_wrap(self):
        screen = screen_with_rows(3, 4)
        screen.feed("\x1b[1;1Habcd\x1b[Lx")
        self.assertEqual(["x", "abcd", "r1"], screen.lines())


class BytesConnection:
    """A connection whose only data is some bytes, for FakeVdp._read."""

    def __init__(self, data):
        """@param data The bytes to deliver."""
        self.data = bytes(data)

    def recv(self, size):
        data, self.data = self.data[:size], self.data[size:]
        return data


class FakeVdpScrollTest(unittest.TestCase):
    """VDU 28, VDU 26, and VDU 23,7 in the fake VDP."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.vdp = FakeVdp(os.path.join(self.directory.name, "vdp"),
                           answer=False)
        rows = []
        for row in range(5):
            rows += [31, 0, row] + list(b"r%d" % row)
        rows += [31, 0, self.vdp.grid_rows - 1] + list(b"bottom")
        self.send(rows)

    def tearDown(self):
        self.vdp.close()
        self.directory.cleanup()

    def send(self, data):
        """Have the fake VDP read bytes.

        @param data The bytes.
        """
        self.vdp._read(BytesConnection(data))

    def viewport_from(self, row):
        """Return VDU 28 for a viewport from a row to the bottom.

        @param row The top row.
        """
        return [28, 0, self.vdp.grid_rows - 1, self.vdp.grid_columns - 1, row]

    def test_scrolling_down_inserts_a_blank_row_and_loses_the_bottom(self):
        self.send(self.viewport_from(1) + [23, 7, 0, 2, 0] * 2
                  + [26, 31, 0, 1])
        lines = self.vdp.grid_lines()
        self.assertEqual(["r0", "", "", "r1", "r2", "r3", "r4"], lines[:7])
        self.assertEqual("", lines[-1])
        self.assertEqual((1, 0), (self.vdp.grid_row, self.vdp.grid_column))
        self.assertIsNone(self.vdp.viewport)
        self.assertEqual([], self.vdp.errors)

    def test_scrolling_up_deletes_a_row_and_blanks_the_bottom(self):
        self.send(self.viewport_from(2) + [23, 7, 0, 3, 0] + [26, 31, 0, 2])
        lines = self.vdp.grid_lines()
        self.assertEqual(["r0", "r1", "r3", "r4", ""], lines[:5])
        self.assertEqual(["bottom", ""], lines[-2:])
        self.assertEqual([], self.vdp.errors)

    def test_scrolling_without_a_viewport_scrolls_the_whole_screen(self):
        self.send([23, 7, 0, 3, 0])
        self.assertEqual(["r1", "r2"], self.vdp.grid_lines()[:2])
        self.assertEqual([], self.vdp.errors)

    def test_reset_homes_the_cursor(self):
        self.send(self.viewport_from(3) + [26])
        self.assertEqual((0, 0), (self.vdp.grid_row, self.vdp.grid_column))
        self.assertEqual([], self.vdp.errors)

    def test_unsupported_scrolls_and_viewports_are_errors(self):
        self.send([23, 7, 1, 3, 0,  23, 7, 0, 0, 0,  23, 7, 0, 3, 8,
                   28, 0, self.vdp.grid_rows, 0, 0])
        self.assertEqual(4, len(self.vdp.errors), self.vdp.errors)
        self.assertEqual("r0", self.vdp.grid_lines()[0])

    def test_drawing_inside_a_viewport_is_an_error(self):
        self.send(self.viewport_from(1) + list(b"x") + [31, 0, 0, 13])
        self.assertEqual(3, len(self.vdp.errors), self.vdp.errors)


if __name__ == "__main__":
    unittest.main()
