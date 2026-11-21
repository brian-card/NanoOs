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



"""Encoders for GraphicsProtocol.h's command records, so that tests can hand
gfxprobe any buffer of records, broken ones included."""

import struct

# GfxCmdType tags.
SET_VIEWPORT, SET_ORIGIN, PUSH_SCISSOR, POP_SCISSOR = 0, 1, 2, 3
SET_FG_COLOR, SET_BG_COLOR, SET_BLEND, SET_FONT, CLEAR = 4, 5, 6, 7, 8
DRAW_RECT, DRAW_LINE, DRAW_TRIANGLE, DRAW_CIRCLE = 9, 10, 11, 12
DRAW_IMAGE, DRAW_SUB_IMAGE, DRAW_TEXT, CALL = 13, 14, 15, 16
COPY_RECT, SCROLL_RECT = 17, 18

# GfxBlendMode values.
BLEND_OPAQUE, BLEND_AND, BLEND_OR, BLEND_XOR, BLEND_INVERT = range(5)

# The CGA colours as GfxColor values, in palette order.
CGA_COLORS = [0x000000FF, 0x0000AAFF, 0x00AA00FF, 0x00AAAAFF, 0xAA0000FF,
              0xAA00AAFF, 0xAA5500FF, 0xAAAAAAFF, 0x555555FF, 0x5555FFFF,
              0x55FF55FF, 0x55FFFFFF, 0xFF5555FF, 0xFF55FFFF, 0xFFFF55FF,
              0xFFFFFFFF]
BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LIGHT_GRAY = range(8)
DARK_GRAY, LIGHT_BLUE, LIGHT_GREEN, LIGHT_CYAN = range(8, 12)
LIGHT_RED, LIGHT_MAGENTA, YELLOW, WHITE = range(12, 16)


def shorts(tag, *values):
    """Return a record of a tag and 16-bit values."""
    return bytes([tag]) + struct.pack("<%dh" % len(values), *values)


def viewport(left, top, width, height):
    return shorts(SET_VIEWPORT, left, top, width, height)


def origin(origin_x, origin_y):
    return shorts(SET_ORIGIN, origin_x, origin_y)


def push_scissor(left, top, width, height):
    return shorts(PUSH_SCISSOR, left, top, width, height)


def pop_scissor():
    return bytes([POP_SCISSOR])


def fg(index):
    """A SET_FG_COLOR record of a CGA colour, by palette index."""
    return bytes([SET_FG_COLOR]) + struct.pack("<I", CGA_COLORS[index])


def bg(index):
    """A SET_BG_COLOR record of a CGA colour, by palette index."""
    return bytes([SET_BG_COLOR]) + struct.pack("<I", CGA_COLORS[index])


def blend(mode):
    return bytes([SET_BLEND, mode])


def font(handle):
    return bytes([SET_FONT]) + struct.pack("<H", handle)


def clear():
    return bytes([CLEAR])


def rect(left, top, width, height, filled=True):
    return shorts(DRAW_RECT, left, top, width, height) + bytes([int(filled)])


def line(start_x, start_y, end_x, end_y):
    return shorts(DRAW_LINE, start_x, start_y, end_x, end_y)


def triangle(first, second, third, filled=True):
    return shorts(DRAW_TRIANGLE, *first, *second, *third) + bytes(
        [int(filled)])


def circle(centre_x, centre_y, radius, filled=True):
    return shorts(DRAW_CIRCLE, centre_x, centre_y, radius) + bytes(
        [int(filled)])


def image(handle, left, top):
    return bytes([DRAW_IMAGE]) + struct.pack("<Hhh", handle, left, top)


def sub_image(handle, source, left, top):
    return bytes([DRAW_SUB_IMAGE]) + struct.pack("<H6h", handle, *source,
                                                   left, top)


def text(left, top, data):
    return shorts(DRAW_TEXT, left, top) + bytes([len(data)]) + bytes(data)


def call(handle):
    return bytes([CALL]) + struct.pack("<H", handle)


def copy_rect(source, left, top):
    return shorts(COPY_RECT, *source, left, top)


def scroll_rect(area, delta_x, delta_y):
    return shorts(SCROLL_RECT, *area, delta_x, delta_y)
