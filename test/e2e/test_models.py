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
import socket
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


class FakeVdpContextTest(unittest.TestCase):
    """VDU 23,0,&C8 context select and delete in the VDP model."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.vdp = FakeVdp(os.path.join(self.directory.name, "vdp"),
                           answer=False)
        self.send([17, 4, 17, 129, 31, 5, 3, 23, 1, 1])

    def tearDown(self):
        self.vdp.close()
        self.directory.cleanup()

    def send(self, data):
        """Have the fake VDP read bytes.

        @param data The bytes.
        """
        self.vdp._read(BytesConnection(data))

    def state(self):
        """Return the fields of the active context that the tests set.

        @return (text_fg, text_bg, grid_row, grid_column, logical_coords,
          cursor_enabled).
        """
        vdp = self.vdp
        return (vdp.text_fg, vdp.text_bg, vdp.grid_row, vdp.grid_column,
                vdp.logical_coords, vdp.cursor_enabled)

    def test_selecting_back_restores_the_first_context_exactly(self):
        self.send([23, 0, 0xC8, 0, 1])
        self.assertEqual((4, 1, 3, 5, True, True), self.state())
        self.send([17, 2, 17, 128, 31, 0, 0, 23, 0, 0xC0, 0, 23, 1, 0]
                  + list(b"x"))
        self.assertEqual((2, 0, 0, 1, False, False), self.state())
        self.send([23, 0, 0xC8, 0, 0])
        self.assertEqual(0, self.vdp.context_id)
        self.assertEqual((4, 1, 3, 5, True, True), self.state())
        self.send([23, 0, 0xC8, 0, 1])
        self.assertEqual((2, 0, 0, 1, False, False), self.state())
        self.assertEqual("x", self.vdp.grid_lines()[0])
        self.assertEqual([], self.vdp.errors)

    def test_deleting_the_active_context_does_nothing(self):
        self.send([23, 0, 0xC8, 0, 1, 17, 2, 23, 0, 0xC8, 1, 1])
        self.send([23, 0, 0xC8, 0, 0, 23, 0, 0xC8, 0, 1])
        self.assertEqual(2, self.vdp.text_fg)

    def test_a_deleted_context_comes_back_as_a_copy(self):
        self.send([23, 0, 0xC8, 0, 1, 17, 2, 23, 0, 0xC8, 0, 0])
        self.send([23, 0, 0xC8, 1, 1, 23, 0, 0xC8, 0, 1])
        self.assertEqual((4, 1, 3, 5, True, True), self.state())
        selects = [detail for _time, kind, detail in self.vdp.events
                   if kind == "context"]
        self.assertEqual([("select", 1), ("select", 0), ("delete", 1),
                          ("select", 1)], selects)


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


def word(value):
    """Return a 16-bit VDU argument's two bytes, low byte first."""
    return [value & 0xFF, (value >> 8) & 0xFF]


def plot(command, point_x, point_y):
    """Return the bytes of VDU 25,command,point_x;point_y;."""
    return [25, command] + word(point_x) + word(point_y)


RED, BLUE, WHITE, BLACK = 4, 1, 15, 0


class FakeVdpMouseTest(unittest.TestCase):
    """The fake VDP's mouse packets and cursors."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.vdp = FakeVdp(os.path.join(self.directory.name, "vdp"),
                           answer=False)
        self.driver, self.vdp.conn = socket.socketpair()

    def tearDown(self):
        self.driver.close()
        self.vdp.conn.close()
        self.vdp.close()
        self.directory.cleanup()

    def send(self, data):
        """Have the fake VDP read bytes.

        @param data The bytes.
        """
        self.vdp._read(BytesConnection(data))

    def received(self, size):
        """Read what the fake VDP sent the driver.

        @param size The number of bytes to read.
        @return The bytes.
        """
        data = b""
        while len(data) < size:
            data += self.driver.recv(size - len(data))
        return data

    def test_a_mouse_packet_carries_position_buttons_wheel_and_deltas(self):
        self.vdp.mouse(10, 20)
        self.received(12)
        self.vdp.mouse(300, 200, buttons=5, wheel=-1)
        self.assertEqual(bytes([0x89, 10] + word(300) + word(200) + [5, 0xFF]
                               + word(290) + word(180)),
                         self.received(12))

    def test_setting_the_position_is_answered_with_a_mouse_packet(self):
        self.vdp.mouse(10, 20, buttons=1)
        self.received(12)
        self.send([23, 0, 0x89, 4] + word(600) + word(470))
        self.assertEqual(bytes([0x89, 10] + word(600) + word(470) + [1, 0]
                               + word(590) + word(450)),
                         self.received(12))
        self.assertEqual((600, 470), self.vdp.mouse_position)
        self.assertEqual(1, self.vdp.count_events("warp"))

    def test_a_cursor_can_come_from_an_rgba8888_bitmap(self):
        self.send([23, 0, 0x89, 2, 23, 0, 0x89, 0])
        self.send([23, 0, 0xA0] + word(0xF200) + [0] + word(8)
                  + [0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0])
        self.send([23, 27, 0x20] + word(0xF200) + [23, 27, 0x21] + word(2)
                  + word(1) + [0] + [23, 27, 0x40, 1, 0]
                  + [23, 0, 0x89, 3] + word(0xF200))
        self.assertEqual(["W."], self.vdp.pointer_image())
        self.assertEqual((1, 0), self.vdp.pointer_hotspot())
        self.assertEqual([], self.vdp.errors)


class FakeVdpCanvasTest(unittest.TestCase):
    """The fake VDP's pixels, in pixel coordinates."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.vdp = FakeVdp(os.path.join(self.directory.name, "vdp"),
                           answer=False)
        self.send([23, 0, 0xC0, 0])

    def tearDown(self):
        self.vdp.close()
        self.directory.cleanup()

    def send(self, data):
        """Have the fake VDP read bytes.

        @param data The bytes.
        """
        self.vdp._read(BytesConnection(data))

    def assert_pixels(self, expected):
        """Check pixels of the canvas.

        @param expected A dictionary of (x, y) to palette index.
        """
        actual = {point: self.vdp.pixel(*point) for point in expected}
        self.assertEqual(expected, actual)

    def test_a_filled_rectangle_covers_both_corners_exactly(self):
        self.send([18, 0, RED] + plot(4, 10, 20) + plot(0x65, 19, 29))
        self.assert_pixels({(10, 20): RED, (19, 29): RED, (19, 20): RED,
                            (9, 20): BLACK, (20, 20): BLACK,
                            (10, 19): BLACK, (10, 30): BLACK})
        self.assertEqual([], self.vdp.errors)

    def test_a_line_includes_both_ends_unless_the_last_is_left_out(self):
        self.send([18, 0, RED] + plot(4, 5, 5) + plot(5, 15, 5))
        self.send(plot(4, 5, 7) + plot(0x0D, 15, 7))
        self.assert_pixels({(4, 5): BLACK, (5, 5): RED, (15, 5): RED,
                            (16, 5): BLACK, (5, 7): RED, (14, 7): RED,
                            (15, 7): BLACK})

    def test_the_graphics_viewport_clips_with_bottom_as_the_second_corner(
            self):
        self.send([24] + word(10) + word(29) + word(19) + word(20))
        self.send([18, 0, RED] + plot(4, 0, 0) + plot(0x65, 639, 479))
        self.assert_pixels({(9, 25): BLACK, (10, 25): RED, (19, 25): RED,
                            (20, 25): BLACK, (15, 19): BLACK, (15, 20): RED,
                            (15, 29): RED, (15, 30): BLACK})
        self.assertEqual(1, self.vdp.count_events("gviewport"))

    def test_a_viewport_off_the_screen_is_ignored(self):
        self.send([24] + word(10) + word(500) + word(19) + word(20))
        self.assertEqual((0, 0, 639, 479), self.vdp.gviewport)
        self.assertEqual(1, len(self.vdp.errors))

    def test_clearing_fills_the_viewport_with_the_background(self):
        self.send([18, 0, 128 + BLUE, 24] + word(10) + word(29) + word(19)
                  + word(20) + [16])
        self.assert_pixels({(10, 20): BLUE, (19, 29): BLUE, (9, 20): BLACK,
                            (20, 29): BLACK, (19, 30): BLACK})

    def test_the_origin_offsets_points(self):
        self.send([29] + word(100) + word(50) + [18, 0, RED] + plot(4, 1, 2)
                  + plot(0x65, 1, 2))
        self.assert_pixels({(101, 52): RED, (1, 2): BLACK})

    def test_xor_paints_on_the_palette_index(self):
        self.send([18, 0, RED] + plot(4, 0, 0) + plot(0x65, 3, 0))
        self.send([18, 3, WHITE] + plot(4, 2, 0) + plot(0x65, 5, 0))
        self.assert_pixels({(1, 0): RED, (2, 0): RED ^ WHITE,
                            (4, 0): WHITE})

    def test_a_triangle_fills_inside_and_not_outside(self):
        self.send([18, 0, RED] + plot(4, 100, 100) + plot(4, 200, 100)
                  + plot(0x55, 100, 200))
        self.assert_pixels({(120, 120): RED, (101, 190): RED,
                            (190, 190): BLACK, (99, 150): BLACK})

    def test_circles_have_the_radius_of_the_relative_point(self):
        self.send([18, 0, RED] + plot(4, 100, 100) + plot(0x99, 10, 0))
        self.assert_pixels({(100, 100): RED, (105, 100): RED,
                            (100, 112): BLACK, (112, 100): BLACK})
        self.send([18, 0, BLUE] + plot(4, 300, 100) + plot(0x91, 10, 0))
        self.assert_pixels({(310, 100): BLUE, (300, 90): BLUE,
                            (300, 100): BLACK, (305, 100): BLACK})

    def test_a_copy_lands_with_its_bottom_left_on_the_point(self):
        self.send([18, 0, RED] + plot(4, 50, 50) + plot(0x65, 53, 52))
        self.send([18, 0, BLUE] + plot(4, 50, 50) + plot(0x45, 50, 50))
        self.send(plot(4, 50, 50) + plot(4, 53, 52) + plot(0xBE, 60, 72))
        self.assertEqual([[BLUE, RED, RED, RED, BLACK]] + [[RED] * 4
                         + [BLACK]] * 2 + [[BLACK] * 5],
                         self.vdp.pixels(60, 70, 5, 4))
        self.assertEqual(BLACK, self.vdp.pixel(60, 69))

    def test_a_copy_draws_only_inside_the_viewport(self):
        self.send([18, 0, RED] + plot(4, 0, 0) + plot(0x65, 9, 9))
        self.send([24] + word(20) + word(100) + word(24) + word(0))
        self.send(plot(4, 0, 0) + plot(4, 9, 9) + plot(0xBE, 20, 9))
        self.assert_pixels({(20, 0): RED, (24, 9): RED, (25, 0): BLACK})

    def test_bitmaps_skip_pixels_whose_alpha_is_0(self):
        # The VDP's RGBA2222 has red in the low bits and alpha in the high.
        opaque_red = 0x02 | 0xC0
        self.send([18, 0, WHITE] + plot(4, 0, 0) + plot(0x65, 9, 9))
        self.send([23, 0, 0xA0] + word(0xF200) + [0] + word(2)
                  + [opaque_red, 0x02])
        self.send([23, 27, 0x20] + word(0xF200) + [23, 27, 0x21] + word(2)
                  + word(1) + [1] + plot(0xED, 4, 4))
        self.assert_pixels({(4, 4): RED, (5, 4): WHITE})
        pixels = bytes([0xAA, 0, 0, 0xFF, 0, 0, 0xAA, 0])
        self.send([23, 0, 0xA0] + word(0xF201) + [0] + word(8) + list(pixels))
        self.send([23, 27, 0x20] + word(0xF201) + [23, 27, 0x21] + word(2)
                  + word(1) + [0] + plot(0xED, 4, 6))
        self.assert_pixels({(4, 6): RED, (5, 6): WHITE})
        self.assertEqual([], self.vdp.errors)

    def test_a_capture_is_the_screen_between_the_last_two_points(self):
        self.send([18, 0, RED] + plot(4, 10, 10) + plot(0x65, 11, 10))
        self.send(plot(4, 10, 10) + plot(4, 12, 11) + [23, 27, 0x21]
                  + word(0xF202) + word(0))
        self.assertEqual((3, 2), self.vdp.bitmaps[0xF202][:2])
        self.send([23, 27, 0x20] + word(0xF202) + plot(0xED, 100, 100))
        self.assertEqual([[RED, RED, BLACK], [BLACK] * 3],
                         self.vdp.pixels(100, 100, 3, 2))

    def test_a_buffer_call_carries_out_its_bytes(self):
        body = [18, 0, RED] + plot(4, 1, 1) + plot(0x65, 2, 2)
        self.send([23, 0, 0xA0] + word(0xF300) + [0] + word(len(body)) + body)
        self.assertEqual(BLACK, self.vdp.pixel(1, 1))
        self.send([29] + word(10) + word(0) + [23, 0, 0xA0] + word(0xF300)
                  + [1])
        self.assert_pixels({(11, 1): RED, (12, 2): RED, (1, 1): BLACK})

    def upload_font(self):
        """Upload an 8x8 font to buffer 0xF100 whose glyphs are each one row
        of a set pixel per bit of the character code, and select it at the
        graphics cursor, which can't wrap."""
        glyphs = []
        for code in range(256):
            glyphs += [code] + [0] * 7
        self.send([23, 0, 0xA0] + word(0xF100) + [0] + word(len(glyphs))
                  + glyphs + [23, 0, 0x95, 1] + word(0xF100) + [8, 8, 8, 0])
        self.send([5, 23, 16, 0x40, 0xBF, 23, 0, 0x95, 0] + word(0xF100)
                  + [0])

    def test_text_at_the_graphics_cursor_draws_glyphs_in_the_foreground(
            self):
        self.upload_font()
        self.send([18, 0, RED] + plot(4, 10, 20) + [0x81, 27, 10])
        self.assertEqual([RED] + [BLACK] * 6 + [RED]
                         + [BLACK] * 4 + [RED, BLACK, RED, BLACK],
                         self.vdp.pixels(10, 20, 16, 1)[0])
        self.assertEqual(BLACK, self.vdp.pixel(10, 21))
        self.assertEqual([], self.vdp.errors)

    def test_control_codes_at_the_graphics_cursor_are_errors(self):
        self.upload_font()
        self.send(plot(4, 10, 20) + [10])
        self.assertEqual(["control code 10 at the graphics cursor"],
                         self.vdp.errors)

    def test_graphics_text_that_can_wrap_is_an_error(self):
        self.upload_font()
        self.send([23, 16, 0, 0xBF, 0x41])
        self.assertEqual(["text at the graphics cursor can wrap"],
                         self.vdp.errors)


class FakeVdpGridColorTest(unittest.TestCase):
    """The colors of the fake VDP's console text grid."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.vdp = FakeVdp(os.path.join(self.directory.name, "vdp"),
                           answer=False)

    def tearDown(self):
        self.vdp.close()
        self.directory.cleanup()

    def send(self, data):
        """Have the fake VDP read bytes.

        @param data The bytes.
        """
        self.vdp._read(BytesConnection(data))

    def test_text_colors_set_the_colors_of_each_cell_drawn(self):
        self.send([17, RED, 17, 128 + BLUE] + list(b"ab") + [17, WHITE]
                  + list(b"c"))
        self.assertEqual([("a", RED, BLUE), ("b", RED, BLUE),
                          ("c", WHITE, BLUE), (" ", WHITE, BLACK)],
                         self.vdp.grid_cells()[0][:4])

    def test_the_bell_draws_nothing(self):
        self.send(list(b"a\x07b"))
        self.assertEqual("ab", self.vdp.grid_lines()[0])
        self.assertEqual(1, self.vdp.count_events("bell"))

    def test_clearing_and_scrolling_blank_with_the_background(self):
        last = self.vdp.grid_rows - 1
        self.send([17, 128 + BLUE, 12, 31, 0, 1, 17, RED] + list(b"x")
                  + [31, 0, last, 17, 128 + RED, 10])
        cells = self.vdp.grid_cells()
        self.assertEqual([("x", RED, BLUE), (" ", WHITE, BLUE)], cells[0][:2])
        self.assertEqual([(" ", WHITE, BLUE)] * 2, cells[1][:2])
        self.assertEqual([(" ", RED, RED)] * self.vdp.grid_columns,
                         cells[last])


class FakeVdpReadCellsTest(unittest.TestCase):
    """Reading characters and colors back from the fake VDP's canvas."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.vdp = FakeVdp(os.path.join(self.directory.name, "vdp"),
                           answer=False)
        glyphs = []
        for code in range(256):
            glyphs += [code & 0xFC, (code & 0x03) << 6, 0x80] + [0] * 5
        self.send([23, 0, 0xC0, 0, 23, 0, 0xA0] + word(0xF100) + [0]
                  + word(len(glyphs)) + glyphs + [23, 0, 0x95, 1]
                  + word(0xF100) + [6, 8, 8, 0])
        self.send([5, 23, 16, 0x40, 0xBF, 23, 0, 0x95, 0] + word(0xF100)
                  + [0])

    def tearDown(self):
        self.vdp.close()
        self.directory.cleanup()

    def send(self, data):
        """Have the fake VDP read bytes.

        @param data The bytes.
        """
        self.vdp._read(BytesConnection(data))

    def test_text_drawn_on_the_canvas_reads_back_exactly(self):
        self.send([18, 0, BLUE] + plot(4, 12, 16) + plot(0x65, 12 + 24 - 1,
                                                          16 + 8 - 1))
        self.send([18, 0, RED] + plot(4, 12, 16) + list(b"Ab")
                  + [18, 0, WHITE] + list(b"c"))
        self.assertEqual([[("A", RED, BLUE), ("b", RED, BLUE),
                           ("c", WHITE, BLUE), (" ", None, BLUE)]],
                         self.vdp.read_cells(12, 16, 4, 1, 0xF100))
        self.assertEqual([], self.vdp.errors)


if __name__ == "__main__":
    unittest.main()
