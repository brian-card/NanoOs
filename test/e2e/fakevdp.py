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


"""A stand-in for the Agon Light 2 VDP on the simulator's UART0.

The simulator connects UART0 to the Unix socket named by NANO_OS_SIM_UART0.
FakeVdp listens on that socket, parses the VDU stream vdp.drv sends, and sends
keyboard packets back.  Like the real VDP, it ignores everything but general
polls until it has answered one.

Contexts (VDU 23,0,&C8) are modelled for select and delete only.  A context
holds the text cursor, text viewport, font, text colours, cursor behaviour
and visibility, the logical-coordinates flag, and the graphics state; the
grid of characters on the screen and the canvas of pixels are shared, as the
real VDP's pixels are.  A context's own stack (commands 2 to 7) isn't
modelled; those commands are recorded as errors.

Graphics are modelled in pixel coordinates only, on a canvas of palette
indices:  PLOT lines, points, triangle, rectangle and circle fills, circle
outlines, copies and bitmaps, VDU 16, 18, 24 and 29, and text at the graphics
cursor (VDU 5).  Lines and rectangles are exact; circles and triangles are
approximations whose edges don't match the real VDP's, so tests check only
points well inside or outside them.  Bitmaps are drawn as if the paint mode
were 0, and a bitmap colour that isn't exactly a palette entry's becomes the
nearest by RGB distance rather than by the real VDP's HSV distance.
"""

import os
import re
import socket
import struct
import threading
import time

GENERAL_POLL = 0x80
KEYCODE_SPACE = 1
KEYCODE_0 = 2
KEYCODE_a = 22
KEYCODE_A = 48
KEYCODE_DELETE = 130
KEYCODE_BACKSPACE = 132
KEYCODE_HOME = 133
KEYCODE_END = 135
KEYCODE_RETURN = 143
KEYCODE_UP = 150
KEYCODE_DOWN = 152
KEYCODE_LEFT = 154
KEYCODE_RIGHT = 156
KEYCODE_KEYPAD_0 = 12
KEYCODE_KEYPAD_PERIOD = 88
# What the VDP reports for keypad 0-9 and . while its Num Lock is off.
KEYCODE_KEYPAD_NAVIGATION = [129, 136, 153, 149, 155, 158, 157, 134, 151,
                             147, 131]
KEYCODE_SCROLLLOCK = 139
KEYCODE_NUMLOCK = 140
KEYCODE_CAPSLOCK = 141
KEYMOD_CTRL = 1 << 0
KEYMOD_ALT_LEFT = 1 << 2
KEYMOD_CAPS_LOCK = 1 << 4
KEYMOD_NUM_LOCK = 1 << 5
KEYMOD_SCROLL_LOCK = 1 << 6
LED_SCROLL_LOCK = 1
LED_CAPS_LOCK = 2
LED_NUM_LOCK = 4
KEYBOARD_LAYOUT = 0x81
MOUSE = 0x89
BUFFERED = 0xA0
BUFFERED_WRITE = 0
BUFFERED_CLEAR = 2
MOUSE_ENABLE = 0
MOUSE_DISABLE = 1
MOUSE_RESET = 2
MOUSE_SET_CURSOR = 3
MOUSE_SET_POSITION = 4
PACKET_MOUSE = 0x89
SPRITES = 27
CURSOR_BEHAVIOUR = 16
SCROLL = 7
SCROLL_DOWN = 2
SCROLL_UP = 3
SELECT_BITMAP = 0x20
CREATE_BITMAP = 0x21
MAKE_CURSOR = 0x40
FORMAT_RGBA2222 = 1
BITMAP_BASE_ID = 0xFA00
AUDIO = 0x85
AUDIO_RESET = 10
AUDIO_SAMPLE_RATE = 13
KEYSTATE = 0x88
FONT = 0x95
FONT_SELECT = 0
FONT_FROM_BUFFER = 1
FONT_CLEAR = 4
SYSTEM_FONT = 0xFFFF
UDG_RESET = 0x91
CONTROL_KEYS = 0x98
LEGACY_MODES = 0xC1
COPPER = 0xC4
COPPER_RESET_SIGNALS = 4
CONSOLE_MODE = 0xFE
SPRITES_RESET = 16
LOGICAL_COORDS = 0xC0
CONTEXT = 0xC8
CONTEXT_SELECT = 0
CONTEXT_DELETE = 1
PACKET_MODE = 0x86
COLOUR_DEPTH = 16
BUFFERED_CALL = 1
BUFFERED_CONSOLIDATE = 0x0E
FLUSH_DRAWING_QUEUE = 0xCA
FORMAT_RGBA8888 = 0
# VDU 23,16 bit 6, which stops text at the graphics cursor wrapping.
BEHAVIOUR_GRAPHICS_NO_WRAP = 0x40
# The state each VDP context keeps of its own.
CONTEXT_FIELDS = ("grid_row", "grid_column", "grid_columns", "grid_rows",
                  "viewport", "font", "cursor_behaviour", "cursor_enabled",
                  "logical_coords", "text_fg", "text_bg", "gfg", "gbg",
                  "gfg_mode", "gbg_mode", "gviewport", "origin", "points",
                  "user_point", "vdu5", "graphics_font")
DEFAULT_GRAPHICS_FG = 15
DEFAULT_GRAPHICS_BG = 0
# The colours the driver sets the palette to, which the canvas's indices
# stand for until VDU 19 changes one.
CGA_PALETTE = [(0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00),
               (0x00, 0xAA, 0xAA), (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA),
               (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA), (0x55, 0x55, 0x55),
               (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
               (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55),
               (0xFF, 0xFF, 0xFF)]
DEFAULT_TEXT_FG = 15
DEFAULT_TEXT_BG = 0
ALL_BUFFERS = 0xFFFF
LAYOUT_UK = 0
# The real VDP's UART receive buffer, which bytes sent while it is busy
# overflow:  the eZ80 stops waiting on CTS after about 17 ms.
RX_BUFFER_SIZE = 256
FLOOD_BYTES_PER_PACKET = 16
LAYOUT_US = 1
# The size, in pixels, of the screen mode vdp.drv uses, and of the system
# font's glyphs.
SCREEN_WIDTH = 640
SCREEN_HEIGHT = 480
SYSTEM_GLYPH_SIZE = 8
# What the VDP's default UK layout gives for the keys of a US keyboard whose
# characters differ between the two layouts.
UK_CHARACTER_FOR_US_KEY = {"|": "~", "\\": "#", "@": '"', '"': "@"}


def keycode(ch):
    """Return the FabGL keycode the VDP reports for a character's key.

    @param ch The character.

    @return The keycode, or 0 for a character with no dedicated key here.
    """
    if "a" <= ch <= "z":
        return KEYCODE_a + ord(ch) - ord("a")
    if "A" <= ch <= "Z":
        return KEYCODE_A + ord(ch) - ord("A")
    if "0" <= ch <= "9":
        return KEYCODE_0 + ord(ch) - ord("0")
    return {" ": KEYCODE_SPACE, "\r": KEYCODE_RETURN,
            "\b": KEYCODE_BACKSPACE}.get(ch, 0)


class FakeVdp:
    """A fake VDP serving one connection at a time on a Unix socket."""

    def __init__(self, path, answer_after=0.0, answer=True, mouse=True,
                 mouse_reset_seconds=0.0, read_rate=None):
        """Start listening.

        @param path The path of the Unix socket to create.
        @param answer_after Seconds after the first poll before polls are
          answered.
        @param answer False to never answer a poll, but record everything sent
          as if listening, to see what the driver does when it gives up.
        @param mouse Whether a mouse is plugged in, which a mouse reset detects.
        @param mouse_reset_seconds Seconds a mouse reset takes: polls received
          meanwhile are answered when it ends.  Unlike the real VDP, this one
          keeps reading meanwhile, so that output sent early shows up.
        @param read_rate The most bytes a second to read, as a slow link and
          a busy VDP would, or None for as fast as they come.
        """
        self.path = path
        self.answer_after = answer_after
        self.answer = answer
        self.lock = threading.Condition()
        self.conn = None
        self.connections = 0
        self.first_poll = None
        self.polls = []        # times of the general polls received
        self.poll_cookies = [] # their cookies
        self.answered = None   # time of the first poll answered
        self.events = []       # (time, kind, detail) once listening
        self.screen = ""       # text drawn since the last clear
        self.cursor = 0        # index in screen of the next character drawn
        # The screen as rows and columns of the selected font's glyphs, and
        # the cell the next character is drawn in.  grid_column is
        # grid_columns while a wrap is held back.
        self.grid_columns = SCREEN_WIDTH // SYSTEM_GLYPH_SIZE
        self.grid_rows = SCREEN_HEIGHT // SYSTEM_GLYPH_SIZE
        self._grid_clear()
        self.dropped = 0       # bytes ignored before listening
        self.layout = LAYOUT_UK
        self.palette = {}      # logical color -> (red, green, blue) set by VDU 19
        self.nearest = {}      # 2-bit (red, green, blue) -> palette index
        # Like the real VDP's, these outlive a connection.
        self.buffers = {}      # buffer ID -> list of streams written to it
        self.buffer_writes = []  # (buffer ID, length) of each write
        self.bitmaps = {}      # bitmap ID -> (width, height, RGBA2222 bytes)
        self.bitmap = BITMAP_BASE_ID
        self.cursors = {}      # cursor ID -> (bitmap, hotspot x, hotspot y)
        self.fonts = {}        # font ID -> (width, height, glyph bytes)
        self.font = None       # ID of the selected font, None for the system's
        # FabGL's lock states, which only the lock keys change, and the LEDs.
        self.num_lock = False
        self.caps_lock = False
        self.scroll_lock = False
        self.leds = 0
        self.cursor_behaviour = 0  # VDU 23,16 flags; bit 0 is scroll protect
        # The text viewport set by VDU 28 as (left, top, right, bottom) cells,
        # or None for the whole screen.
        self.viewport = None
        self.cursor_enabled = True
        self.logical_coords = True
        self.text_fg = DEFAULT_TEXT_FG
        self.text_bg = DEFAULT_TEXT_BG
        self.canvas = bytearray(SCREEN_WIDTH * SCREEN_HEIGHT)
        self._graphics_reset()
        # Seconds to hold back replies to polls once listening.
        self.poll_delay = 0.0
        self.context_id = 0
        self.contexts = {}     # context ID -> its fields, for inactive ones
        self.mode = 0
        self.mouse_enabled = False
        self.mouse_cursor = None
        self.mouse_position = (0, 0)
        self.mouse_buttons = 0
        self.send_lock = threading.Lock()
        self.mouse_plugged_in = mouse
        self.mouse_detected = False
        self.mouse_visible = False
        self.mouse_port_suspended = False
        self.mouse_reset_seconds = mouse_reset_seconds
        self.mouse_reset_until = 0.0
        self.mouse_reset_answered = None  # time of the poll answered after it
        self.busy_bytes = 0    # bytes received during the last mouse reset
        self.received = 0      # bytes received in all, as they are carried out
        # Whether text glyphs fill their background.  As on the real VDP, this
        # belongs to the canvas, not to a context:  VDU 5 turns it off, and
        # only VDU 4 or a mode change turns it back on.
        self.text_background = True
        self.read_rate = read_rate
        self.flooding = []     # pointer positions to send as bytes are read
        self.listening = not answer
        self.errors = []
        if os.path.exists(path):
            os.unlink(path)
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(path)
        self.server.listen(1)
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def _serve(self):
        while True:
            try:
                conn, _ = self.server.accept()
            except OSError:
                return
            with self.lock:
                self.conn = conn
                self.connections += 1
                self.lock.notify_all()
            self._read(conn)

    def _read(self, conn):
        self._parse(self._bytes(conn))

    def _parse(self, stream):
        """Carry out VDU bytes.

        @param stream An iterator of the bytes.
        """
        try:
            for byte in stream:
                if byte == 23:
                    self._vdu23(stream)
                elif byte == 22:
                    mode = next(stream)
                    if self.listening:
                        with self.lock:
                            # Changing the mode resets the palette, font, and
                            # viewports.
                            self.text_background = True
                            self.palette = {}
                            self.nearest = {}
                            self.font = None
                            self.viewport = None
                            self.logical_coords = True
                            self.cursor_enabled = True
                            self.text_fg = DEFAULT_TEXT_FG
                            self.text_bg = DEFAULT_TEXT_BG
                            self._graphics_reset()
                            self.mode = mode
                        self._event("mode", mode)
                elif byte == 17:
                    colour = next(stream)
                    if self.listening:
                        with self.lock:
                            if colour & 0x80:
                                self.text_bg = colour & 0x7F
                            else:
                                self.text_fg = colour
                        self._event("colour", colour)
                elif byte == 19:
                    logical, physical, red, green, blue = (
                        next(stream) for _ in range(5))
                    if not self.listening:
                        pass
                    elif physical != 255:
                        self.errors.append(f"VDU 19,{logical},{physical}")
                    else:
                        with self.lock:
                            self.palette[logical] = (red, green, blue)
                            self.nearest = {}
                elif byte == 28:
                    left, bottom, right, top = (next(stream) for _ in range(4))
                    if self.listening:
                        self._set_viewport(left, top, right, bottom)
                elif byte == 18:
                    mode, colour = next(stream), next(stream)
                    if self.listening:
                        self._gcol(mode, colour)
                elif byte == 24:
                    left, bottom, right, top = [self._signed(stream)
                                                for _ in range(4)]
                    if self.listening:
                        self._graphics_viewport(left, top, right, bottom)
                elif byte == 25:
                    command = next(stream)
                    point_x = self._signed(stream)
                    point_y = self._signed(stream)
                    if self.listening:
                        self._plot(command, point_x, point_y)
                elif byte == 29:
                    origin = (self._signed(stream), self._signed(stream))
                    if self.listening:
                        self._set_origin(origin)
                elif not self.listening:
                    self.dropped += 1
                elif byte == 26:
                    self._event("viewports")
                    with self.lock:
                        self.viewport = None
                        self.cursor = 0
                        self.grid_row = self.grid_column = 0
                        self.gviewport = (0, 0, SCREEN_WIDTH - 1,
                                          SCREEN_HEIGHT - 1)
                        self.origin = (0, 0)
                        self._push_point(0, 0)
                        self.lock.notify_all()
                    self._send_mode_packet()
                elif byte in (4, 5):
                    with self.lock:
                        self.vdu5 = byte == 5
                        self.text_background = byte == 4
                    self._event("vdu", byte)
                    self._send_mode_packet()
                elif byte == 16:
                    self._event("clg")
                    with self.lock:
                        self.grid = [[" "] * self.grid_columns
                                     for _ in range(self.grid_rows)]
                        self._fill(self.gviewport, self.gbg, self.gbg_mode)
                        self._push_point(0, 0)
                        self.lock.notify_all()
                elif byte == 12:
                    self._event("cls")
                    with self.lock:
                        self.screen = ""
                        self.cursor = 0
                        self._grid_clear()
                        self._fill((0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1),
                                   self.text_bg, 0)
                        self.lock.notify_all()
                elif byte == 30:
                    self._event("home")
                    with self.lock:
                        self.cursor = 0
                        self.grid_row = self.grid_column = 0
                        self.lock.notify_all()
                elif byte == 27:
                    self._glyph(next(stream))
                elif byte == 31:
                    column, row = next(stream), next(stream)
                    self._check_no_viewport(f"VDU 31,{column},{row}")
                    if column < self.grid_columns and row < self.grid_rows:
                        with self.lock:
                            self.grid_row, self.grid_column = row, column
                    self._event("tab", (column, row))
                else:
                    self._text(byte)
        except StopIteration:
            pass

    def _bytes(self, conn):
        while True:
            size = 4096
            if self.flooding:
                size = FLOOD_BYTES_PER_PACKET
            elif self.read_rate is not None:
                size = 64
            try:
                data = conn.recv(size)
            except OSError:
                return
            if not data:
                return
            if self.read_rate is not None:
                time.sleep(len(data) / self.read_rate)
            if self.flooding:
                self.mouse(*self.flooding.pop(0), buttons=1)
            busy = time.time() < self.mouse_reset_until
            for byte in data:
                if busy:
                    self.busy_bytes += 1
                    if self.busy_bytes == RX_BUFFER_SIZE + 1:
                        self.errors.append(
                            "more than the receive buffer was sent during "
                            "the mouse reset")
                self.received += 1
                yield byte

    def _vdu23(self, stream):
        first = next(stream)
        if first == 0:
            command = next(stream)
            if command == GENERAL_POLL:
                self._poll(next(stream))
            elif command == BUFFERED:
                self._buffered(stream)
            elif command == MOUSE:
                self._mouse(stream)
            elif command in (LEGACY_MODES, CONTROL_KEYS, CONSOLE_MODE):
                self._reset_event(command, next(stream))
            elif command == UDG_RESET:
                self._reset_event(command)
            elif command == COPPER:
                sub = next(stream)
                if sub != COPPER_RESET_SIGNALS:
                    self.errors.append(f"VDU 23,0,&C4,{sub}")
                self._reset_event(command, sub)
            elif command == AUDIO:
                channel = next(stream)
                sub = next(stream)
                if sub == AUDIO_RESET:
                    self._reset_event(command, ("reset", channel))
                elif sub == AUDIO_SAMPLE_RATE:
                    self._reset_event(command,
                                      ("rate", channel, self._word(stream)))
                else:
                    self.errors.append(f"VDU 23,0,&85,{channel},{sub}")
            elif command == FONT:
                self._font(stream)
            elif command == CONTEXT:
                self._context(next(stream), next(stream))
            elif command == FLUSH_DRAWING_QUEUE:
                if self.listening:
                    self._event("flush")
            elif command == LOGICAL_COORDS:
                setting = next(stream)
                if self.listening:
                    with self.lock:
                        self.logical_coords = setting != 0
                    self._event("logical", setting)
            elif command == KEYSTATE:
                state = (self._word(stream), self._word(stream), next(stream))
                if state[2] != 255:
                    with self.lock:
                        self.leds = state[2]
                        self.lock.notify_all()
                self._reset_event(command, state)
            elif command == KEYBOARD_LAYOUT:
                layout = next(stream)
                if self.listening:
                    with self.lock:
                        self.layout = layout
                    self._event("layout", layout)
            else:
                self.errors.append(f"VDU 23,0,{command:#x}")
        elif first == 1:
            mode = next(stream)
            if self.listening:
                if mode in (0, 1):
                    with self.lock:
                        self.cursor_enabled = mode == 1
                self._event("cursor", mode)
        elif first == SCROLL:
            extent, direction, movement = (next(stream) for _ in range(3))
            if self.listening:
                self._scroll(extent, direction, movement)
        elif first == SPRITES:
            self._sprites(stream)
        elif first == CURSOR_BEHAVIOUR:
            setting, mask = next(stream), next(stream)
            if self.listening:
                with self.lock:
                    self.cursor_behaviour = (
                        (self.cursor_behaviour & mask) ^ setting)
                self._event("behaviour", (setting, mask))
                self._send_mode_packet()
        else:
            self.errors.append(f"VDU 23,{first}")

    def _context(self, command, context_id):
        """Select or delete a context, as VDU 23,0,&C8 does.  Selecting an ID
        that doesn't exist yet copies the current context, and selecting
        sends a mode packet back.  Deleting the active context does nothing.

        @param command CONTEXT_SELECT or CONTEXT_DELETE.
        @param context_id The context's ID.
        """
        if not self.listening:
            return
        if command == CONTEXT_SELECT:
            with self.lock:
                self.contexts[self.context_id] = {
                    field: getattr(self, field) for field in CONTEXT_FIELDS}
                for field, value in self.contexts.get(
                        context_id, self.contexts[self.context_id]).items():
                    setattr(self, field, value)
                self.contexts.pop(context_id, None)
                self.context_id = context_id
                self.lock.notify_all()
            self._send_mode_packet()
            self._event("context", ("select", context_id))
        elif command == CONTEXT_DELETE:
            with self.lock:
                if context_id != self.context_id:
                    self.contexts.pop(context_id, None)
            self._event("context", ("delete", context_id))
        else:
            self.errors.append(f"VDU 23,0,&C8,{command}")

    def _send_mode_packet(self):
        """Send the mode packet the real VDP sends after a context select,
        VDU 24, 26, 4 and 5, VDU 23,16, and a font select."""
        with self.lock:
            conn = self.conn
            packet = bytes([PACKET_MODE, 8,
                            SCREEN_WIDTH & 0xFF, SCREEN_WIDTH >> 8,
                            SCREEN_HEIGHT & 0xFF, SCREEN_HEIGHT >> 8,
                            self.grid_columns, self.grid_rows,
                            COLOUR_DEPTH, self.mode])
        if conn is not None:
            try:
                self._send(conn, packet)
            except OSError:
                pass

    def _send(self, conn, data):
        """Send bytes to the simulator whole, so that a packet the server
        thread sends and one a test sends can't interleave.

        @param conn The connection.
        @param data The bytes.
        """
        with self.send_lock:
            conn.sendall(data)

    def _send_mouse_packet(self, conn, position, buttons, wheel, delta):
        """Send the mouse packet the real VDP sends.

        @param conn The connection.
        @param position The pointer's (column, row).
        @param buttons The button bits.
        @param wheel The wheel delta.
        @param delta The pointer's movement since the last packet.
        """
        data = struct.pack("<HHBbhh", position[0], position[1], buttons,
                           wheel, delta[0], delta[1])
        self._send(conn, bytes([PACKET_MOUSE, len(data)]) + data)

    def _reset_event(self, command, detail=None):
        if self.listening:
            self._event("reset", (command, detail))

    def _word(self, stream):
        return next(stream) | (next(stream) << 8)

    def _signed(self, stream):
        word = self._word(stream)
        return word - 0x10000 if word & 0x8000 else word

    def _buffered(self, stream):
        buffer_id = self._word(stream)
        command = next(stream)
        if command == BUFFERED_WRITE:
            length = self._word(stream)
            data = bytes([next(stream) for _ in range(length)])
            with self.lock:
                self.buffers.setdefault(buffer_id, []).append(data)
            if self.listening:
                with self.lock:
                    self.buffer_writes.append((buffer_id, length))
        elif command == BUFFERED_CLEAR and buffer_id == ALL_BUFFERS:
            with self.lock:
                self.buffers.clear()
                self.bitmaps.clear()
                self.cursors.clear()
                self.fonts.clear()
            self._reset_event(BUFFERED, buffer_id)
        elif command == BUFFERED_CLEAR:
            with self.lock:
                self.buffers.pop(buffer_id, None)
                self.bitmaps.pop(buffer_id, None)
                self.fonts.pop(buffer_id, None)
                self.cursors.pop(buffer_id, None)
        elif command == BUFFERED_CONSOLIDATE:
            with self.lock:
                if buffer_id in self.buffers:
                    self.buffers[buffer_id] = [
                        b"".join(self.buffers[buffer_id])]
        elif command == BUFFERED_CALL:
            with self.lock:
                data = b"".join(self.buffers.get(buffer_id, []))
            if buffer_id not in self.buffers:
                self.errors.append(f"call of missing buffer {buffer_id}")
            self._event("call", buffer_id)
            self._parse(iter(data))
        else:
            self.errors.append(f"VDU 23,0,&A0,{buffer_id};{command}")

    def _font(self, stream):
        command = next(stream)
        font_id = self._word(stream)
        if command == FONT_FROM_BUFFER:
            width, height, _ascent, flags = (next(stream) for _ in range(4))
            streams = self.buffers.get(font_id, [])
            size = ((width + 7) // 8) * height * 256
            if flags != 0:
                self.errors.append(f"font {font_id} flags {flags}")
            elif len(streams) != 1:
                self.errors.append(
                    f"font {font_id} from {len(streams)} streams")
            elif len(streams[0]) != size:
                self.errors.append(
                    f"font {font_id} is {len(streams[0])} bytes, "
                    f"not {width}x{height}")
            else:
                with self.lock:
                    self.fonts[font_id] = (width, height, streams[0])
        elif command == FONT_SELECT:
            next(stream)
            if font_id != SYSTEM_FONT and font_id not in self.fonts:
                self.errors.append(f"font {font_id} doesn't exist")
            elif self.vdu5:
                with self.lock:
                    self.graphics_font = (None if font_id == SYSTEM_FONT
                                          else font_id)
                self._event("graphics_font", font_id)
                self._send_mode_packet()
            else:
                with self.lock:
                    self.font = None if font_id == SYSTEM_FONT else font_id
                    width, height = (
                        (SYSTEM_GLYPH_SIZE, SYSTEM_GLYPH_SIZE)
                        if self.font is None else self.fonts[font_id][:2])
                    self.grid_columns = SCREEN_WIDTH // width
                    self.grid_rows = SCREEN_HEIGHT // height
                    self._grid_clear()
                self._event("font", font_id)
                self._send_mode_packet()
        elif command == FONT_CLEAR:
            with self.lock:
                self.fonts.pop(font_id, None)
        else:
            self.errors.append(f"VDU 23,0,&95,{command}")

    def glyph(self, ch):
        """Return a character's glyph in the selected font as text.

        @param ch The character.

        @return One string per row, '#' for a set pixel and '.' for a clear
          one, or None if the system font is selected.
        """
        with self.lock:
            if self.font is None:
                return None
            width, height, data = self.fonts[self.font]
        row_bytes = (width + 7) // 8
        start = ord(ch) * row_bytes * height
        rows = []
        for y in range(height):
            bits = int.from_bytes(
                data[start + y * row_bytes:start + (y + 1) * row_bytes], "big")
            rows.append("".join(
                "#" if bits & (1 << (row_bytes * 8 - 1 - x)) else "."
                for x in range(width)))
        return rows

    def _sprites(self, stream):
        command = next(stream)
        if command == SELECT_BITMAP:
            self.bitmap = self._word(stream)
        elif command == CREATE_BITMAP:
            width = self._word(stream)
            height = self._word(stream)
            if height == 0:
                self._capture(width)
                return
            fmt = next(stream)
            streams = self.buffers.get(self.bitmap, [])
            size = width * height * (4 if fmt == FORMAT_RGBA8888 else 1)
            if fmt not in (FORMAT_RGBA2222, FORMAT_RGBA8888):
                self.errors.append(f"bitmap {self.bitmap} format {fmt}")
            elif len(streams) != 1:
                self.errors.append(
                    f"bitmap {self.bitmap} from {len(streams)} streams")
            elif len(streams[0]) != size:
                self.errors.append(
                    f"bitmap {self.bitmap} is {len(streams[0])} bytes, "
                    f"not {width}x{height} in format {fmt}")
            else:
                with self.lock:
                    self.bitmaps[self.bitmap] = (width, height, streams[0],
                                                 fmt)
        elif command == SPRITES_RESET:
            with self.lock:
                self.bitmaps.clear()
                self.cursors.clear()
            self._reset_event(SPRITES)
        elif command == MAKE_CURSOR:
            hot_x = next(stream)
            hot_y = next(stream)
            if self.bitmap not in self.bitmaps:
                self.errors.append(f"cursor from missing bitmap {self.bitmap}")
            else:
                with self.lock:
                    self.cursors[self.bitmap] = (
                        self.bitmaps[self.bitmap], hot_x, hot_y)
        else:
            self.errors.append(f"VDU 23,27,{command:#x}")

    def _mouse(self, stream):
        command = next(stream)
        # These follow the real VDP's enableMouse, disableMouse, and
        # MOUSE_RESET, including that a failed reset hides nothing, that
        # enabling an enabled mouse shows the pointer without a new check, and
        # that disabling suspends the port, holding the clock line a reset
        # needs.
        if command == MOUSE_RESET:
            with self.lock:
                self.mouse_detected = (self.mouse_plugged_in
                                       and not self.mouse_port_suspended)
                if self.mouse_detected:
                    self.mouse_visible = True
                self.mouse_reset_until = time.time() + self.mouse_reset_seconds
                self.busy_bytes = 0
            self._event("mouse", "reset")
        elif command == MOUSE_DISABLE:
            with self.lock:
                self.mouse_visible = False
                if self.mouse_enabled:
                    self.mouse_port_suspended = True
                self.mouse_enabled = False
            self._event("mouse", "disable")
        elif command == MOUSE_ENABLE:
            with self.lock:
                if not self.mouse_enabled:
                    self.mouse_port_suspended = False
                    self.mouse_enabled = self.mouse_detected
                self.mouse_visible = self.mouse_enabled
        elif command == MOUSE_SET_POSITION:
            position = (self._word(stream), self._word(stream))
            with self.lock:
                conn = self.conn
                delta = (position[0] - self.mouse_position[0],
                         position[1] - self.mouse_position[1])
                self.mouse_position = position
                buttons = self.mouse_buttons
            self._event("warp", position)
            if conn is not None:
                try:
                    self._send_mouse_packet(conn, position, buttons, 0, delta)
                except OSError:
                    pass
        elif command == MOUSE_SET_CURSOR:
            cursor = self._word(stream)
            if not self.mouse_enabled:
                pass  # ignored, as the real VDP does
            elif cursor > 255 and cursor not in self.cursors:
                self.errors.append(f"cursor {cursor} doesn't exist")
            else:
                with self.lock:
                    self.mouse_cursor = cursor
                    self.mouse_visible = True
                self._event("pointer", cursor)
        else:
            self.errors.append(f"VDU 23,0,&89,{command}")

    def pointer_image(self, cursor=None):
        """Return the mouse pointer, or another cursor, as text, one string
        per row.

        @param cursor The ID of the cursor, or None for the pointer's.

        @return '.' for a transparent pixel, 'B' for opaque black, 'W' for
          opaque white, and '?' for anything else, or None if the cursor
          isn't a custom one.
        """
        with self.lock:
            if cursor is None:
                cursor = self.mouse_cursor
            if cursor not in self.cursors:
                return None
            (width, height, data, fmt), _hot_x, _hot_y = (
                self.cursors[cursor])
        rows = []
        for pixel_row in range(height):
            row = ""
            for pixel in self._cursor_pixels(data, fmt, width, pixel_row):
                rgb, alpha = pixel
                if alpha == 0:
                    row += "."
                elif alpha == 3 and rgb == (0, 0, 0):
                    row += "B"
                elif alpha == 3 and rgb == (3, 3, 3):
                    row += "W"
                else:
                    row += "?"
            rows.append(row)
        return rows

    @staticmethod
    def _cursor_pixels(data, fmt, width, pixel_row):
        """Return one row of a cursor's pixels as 2-bit channels.

        @param data The bitmap's bytes.
        @param fmt FORMAT_RGBA2222 or FORMAT_RGBA8888.
        @param width The bitmap's width.
        @param pixel_row The row.
        @return A list of ((red, green, blue), alpha), each channel 0-3.
        """
        pixels = []
        if fmt == FORMAT_RGBA8888:
            start = pixel_row * width * 4
            for index in range(start, start + width * 4, 4):
                red, green, blue, alpha = data[index:index + 4]
                pixels.append(((red >> 6, green >> 6, blue >> 6), alpha >> 6))
            return pixels
        for pixel in data[pixel_row * width:(pixel_row + 1) * width]:
            # The VDP's RGBA2222 packs red in the low bits, alpha in the high.
            pixels.append(((pixel & 3, (pixel >> 2) & 3, (pixel >> 4) & 3),
                           pixel >> 6))
        return pixels

    def pointer_hotspot(self):
        """Return the hotspot of the pointer's cursor.

        @return (column, row), or None if no custom pointer is shown.
        """
        with self.lock:
            if self.mouse_cursor not in self.cursors:
                return None
            _bitmap, hot_x, hot_y = self.cursors[self.mouse_cursor]
        return (hot_x, hot_y)

    def flood(self, points):
        """Hold the left button and move the pointer through points, one
        mouse packet for each FLOOD_BYTES_PER_PACKET bytes read, as the real
        VDP sends packets while the driver is still writing a long repaint.

        @param points The (column, row) of each packet.
        """
        self.flooding = list(points)

    def mouse(self, column, row, buttons=0, wheel=0):
        """Move the mouse and set its buttons the way the real VDP reports
        it, with one mouse packet.

        @param column The pointer's column, in pixels.
        @param row The pointer's row, in pixels.
        @param buttons The button bits.
        @param wheel The wheel delta.
        """
        with self.lock:
            conn = self.conn
            delta = (column - self.mouse_position[0],
                     row - self.mouse_position[1])
            self.mouse_position = (column, row)
            self.mouse_buttons = buttons
        self._send_mouse_packet(conn, (column, row), buttons, wheel, delta)

    def _poll(self, cookie):
        now = time.time()
        with self.lock:
            self.polls.append(now)
            self.poll_cookies.append(cookie)
            if self.first_poll is None:
                self.first_poll = now
            due = self.answer and (now - self.first_poll >= self.answer_after)
            if due and now < self.mouse_reset_until:
                conn = self.conn

                def answer_after_reset():
                    with self.lock:
                        self._answer_poll(conn, cookie)
                        if self.mouse_reset_answered is None:
                            self.mouse_reset_answered = time.time()
                        self.lock.notify_all()
                threading.Timer(self.mouse_reset_until - now,
                                answer_after_reset).start()
            elif due and self.answered is not None and self.poll_delay > 0:
                conn = self.conn

                def answer_later():
                    with self.lock:
                        self._answer_poll(conn, cookie)
                        self.lock.notify_all()
                threading.Timer(self.poll_delay, answer_later).start()
            elif due:
                self._answer_poll(self.conn, cookie)
                if self.mouse_reset_until and self.mouse_reset_answered is None:
                    self.mouse_reset_answered = now
                if self.answered is None:
                    self.listening = True
                    self.answered = now
            self.lock.notify_all()

    def _answer_poll(self, conn, cookie):
        """Answer a general poll.  The connection may have closed since the
        poll arrived, as when a test restarts the simulator, and that mustn't
        end the thread serving the next one.

        @param conn The connection the poll came on.
        @param cookie The poll's cookie.
        """
        try:
            self._send(conn, bytes([0x80, 0x01, cookie]))
        except OSError:
            pass

    def _event(self, kind, detail=None):
        with self.lock:
            self.events.append((time.time(), kind, detail))
            self.lock.notify_all()

    def _set_viewport(self, left, top, right, bottom):
        """Set the text viewport, as VDU 28 does.

        @param left The leftmost column.
        @param top The top row.
        @param right The rightmost column.
        @param bottom The bottom row.
        """
        with self.lock:
            if (left <= right < self.grid_columns
                    and top <= bottom < self.grid_rows):
                self.viewport = (left, top, right, bottom)
            else:
                self.errors.append(
                    f"VDU 28,{left},{bottom},{right},{top} is off the screen")

    def _check_no_viewport(self, what):
        """Record an error for drawing or moving the cursor while a text
        viewport is set, since the real VDP then measures from the viewport's
        corner, which this grid doesn't model.

        @param what The VDU command, for the error.
        """
        if self.viewport is not None:
            self.errors.append(f"{what} inside the text viewport")

    def _scroll(self, extent, direction, movement):
        """Scroll the text viewport by a character row, as VDU 23,7 does
        with an extent of 0 and a movement of 0, blanking the row scrolled in.

        @param extent The VDU 23,7 extent.
        @param direction The VDU 23,7 direction.
        @param movement The VDU 23,7 movement, in pixels.
        """
        if (extent != 0 or direction not in (SCROLL_DOWN, SCROLL_UP)
                or movement != 0):
            self.errors.append(f"VDU 23,7,{extent},{direction},{movement}")
            return
        with self.lock:
            left, top, right, bottom = self.viewport or (
                0, 0, self.grid_columns - 1, self.grid_rows - 1)
            rows = [self.grid[row][left:right + 1]
                    for row in range(top, bottom + 1)]
            blank = [" "] * (right - left + 1)
            if direction == SCROLL_DOWN:
                rows = [blank] + rows[:-1]
            else:
                rows = rows[1:] + [blank]
            for row, cells in zip(range(top, bottom + 1), rows):
                self.grid[row][left:right + 1] = cells
            self.lock.notify_all()
        self._event("scroll", direction)

    def _grid_clear(self):
        self.grid = [[" "] * self.grid_columns for _ in range(self.grid_rows)]
        self.grid_row = self.grid_column = 0

    def _grid_line_feed(self):
        if self.grid_row + 1 < self.grid_rows:
            self.grid_row += 1
        else:
            del self.grid[0]
            self.grid.append([" "] * self.grid_columns)

    def _grid_draw(self, ch):
        """Draw a glyph on the grid the way the VDP does.  With scroll
        protection on, a glyph in the last column wraps at once, except on
        the bottom row, where the wrap waits for the next glyph."""
        if self.grid_column >= self.grid_columns:
            self.grid_column = 0
            self._grid_line_feed()
        self.grid[self.grid_row][self.grid_column] = ch
        self.grid_column += 1
        held = (self.cursor_behaviour & 1) and (
            self.grid_row == self.grid_rows - 1)
        if self.grid_column >= self.grid_columns and not held:
            self.grid_column = 0
            self._grid_line_feed()

    def _glyph(self, byte):
        if self.vdu5:
            self._graphics_glyph(byte)
            return
        with self.lock:
            self._check_no_viewport(f"glyph {byte}")
            if not self.text_background:
                self.errors.append(f"text glyph {byte} drawn without its "
                                   "background, as after VDU 5")
            self.screen = (self.screen[:self.cursor] + chr(byte)
                           + self.screen[self.cursor + 1:])
            self.cursor += 1
            self._grid_draw(chr(byte))
            self.events.append((time.time(), "text", byte))
            self.lock.notify_all()

    def _text(self, byte):
        if self.vdu5 and (byte < 32 or byte == 127):
            self.errors.append(f"control code {byte} at the graphics cursor")
            self._event("control", byte)
            return
        if byte not in (8, 10, 13):
            self._glyph(byte)
            return
        with self.lock:
            self._check_no_viewport(f"VDU {byte}")
            if byte == 8:
                if self.cursor > 0 and self.screen[self.cursor - 1] != "\n":
                    self.cursor -= 1
                if self.grid_column > 0:
                    self.grid_column -= 1
                elif self.grid_row > 0:
                    self.grid_row -= 1
                    self.grid_column = self.grid_columns - 1
            else:
                self.screen += chr(byte)
                self.cursor = len(self.screen)
                if byte == 13:
                    self.grid_column = 0
                else:
                    self._grid_line_feed()
            self.events.append((time.time(), "text", byte))
            self.lock.notify_all()

    def _graphics_reset(self):
        """Give the active context the graphics state a mode change gives
        it.  Call with the lock held, or before the reader starts."""
        self.gfg = DEFAULT_GRAPHICS_FG
        self.gbg = DEFAULT_GRAPHICS_BG
        self.gfg_mode = 0
        self.gbg_mode = 0
        # The graphics viewport as inclusive screen (left, top, right,
        # bottom), the origin, the last three points in screen coordinates,
        # newest first, and the last point as given, before the origin.
        self.gviewport = (0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1)
        self.origin = (0, 0)
        self.points = ((0, 0), (0, 0), (0, 0))
        self.user_point = (0, 0)
        self.vdu5 = False
        self.graphics_font = None

    def _push_point(self, point_x, point_y):
        """Make a point, given before the origin, the newest of the last
        three.  Call with the lock held.

        @param point_x The point's X coordinate.
        @param point_y The point's Y coordinate.
        """
        self.user_point = (point_x, point_y)
        screen = (point_x + self.origin[0], point_y + self.origin[1])
        self.points = (screen, self.points[0], self.points[1])

    def _check_pixel_coords(self, what):
        """Record an error for a graphics command in logical coordinates,
        which this model doesn't scale.

        @param what The command, for the error.

        @return True if the command can be carried out.
        """
        if self.logical_coords:
            self.errors.append(f"{what} in logical coordinates")
            return False
        return True

    def _gcol(self, mode, colour):
        """Set a graphics colour and its paint mode, as VDU 18 does."""
        if mode > 7:
            self.errors.append(f"VDU 18,{mode},{colour}")
            return
        with self.lock:
            if colour < 64:
                self.gfg, self.gfg_mode = colour % COLOUR_DEPTH, mode
            elif 128 <= colour < 192:
                self.gbg, self.gbg_mode = (colour - 128) % COLOUR_DEPTH, mode
            else:
                self.errors.append(f"VDU 18,{mode},{colour}")
        self._event("gcol", (mode, colour))

    def _graphics_viewport(self, left, top, right, bottom):
        """Set the graphics viewport, as VDU 24 does in pixel coordinates,
        where its second corner word is the bottom, the larger Y.  The real
        VDP ignores a viewport that isn't wholly on the screen."""
        if not self._check_pixel_coords("VDU 24"):
            return
        origin_x, origin_y = self.origin
        rect = (left + origin_x, top + origin_y, right + origin_x,
                bottom + origin_y)
        with self.lock:
            if (0 <= rect[0] <= rect[2] < SCREEN_WIDTH
                    and 0 <= rect[1] <= rect[3] < SCREEN_HEIGHT):
                self.gviewport = rect
            else:
                self.errors.append(f"VDU 24 {rect} is off the screen")
        self._event("gviewport", rect)
        self._send_mode_packet()

    def _set_origin(self, origin):
        """Set the graphics origin, as VDU 29 does in pixel coordinates."""
        if self.logical_coords and origin != (0, 0):
            self.errors.append(f"VDU 29 {origin} in logical coordinates")
        with self.lock:
            self.origin = origin
        self._event("origin", origin)

    def _paint(self, column, row, colour, mode):
        """Paint a pixel inside the graphics viewport.  Call with the lock
        held.

        @param column The screen X coordinate.
        @param row The screen Y coordinate.
        @param colour The palette index.
        @param mode The paint mode:  0 set, 1 OR, 2 AND, 3 XOR, 4 invert.
        """
        left, top, right, bottom = self.gviewport
        if not (left <= column <= right and top <= row <= bottom):
            return
        index = row * SCREEN_WIDTH + column
        old = self.canvas[index]
        if mode == 0:
            new = colour
        elif mode == 1:
            new = old | colour
        elif mode == 2:
            new = old & colour
        elif mode == 3:
            new = old ^ colour
        elif mode == 4:
            new = old ^ (COLOUR_DEPTH - 1)
        else:
            self.errors.append(f"paint mode {mode}")
            return
        self.canvas[index] = new & (COLOUR_DEPTH - 1)

    def _fill(self, rect, colour, mode):
        """Paint every pixel of an inclusive rectangle.  Call with the lock
        held."""
        left, top, right, bottom = rect
        for row in range(min(top, bottom), max(top, bottom) + 1):
            for column in range(min(left, right), max(left, right) + 1):
                self._paint(column, row, colour, mode)

    def _line_points(self, start, end):
        """Return the pixels of a line, both ends included, in order."""
        (start_x, start_y), (end_x, end_y) = start, end
        distance_x, distance_y = abs(end_x - start_x), -abs(end_y - start_y)
        step_x = 1 if start_x < end_x else -1
        step_y = 1 if start_y < end_y else -1
        error = distance_x + distance_y
        points = []
        while True:
            points.append((start_x, start_y))
            if (start_x, start_y) == (end_x, end_y):
                return points
            doubled = 2 * error
            if doubled >= distance_y:
                error += distance_y
                start_x += step_x
            if doubled <= distance_x:
                error += distance_x
                start_y += step_y

    def _plot(self, command, point_x, point_y):
        """Carry out VDU 25,command,point_x;point_y; in pixel coordinates."""
        if not self._check_pixel_coords(f"PLOT {command:#x}"):
            return
        mode, operation = command & 7, command & 0xF8
        with self.lock:
            if mode < 4:
                point_x += self.user_point[0]
                point_y += self.user_point[1]
            self._push_point(point_x, point_y)
            last, second, third = self.points
            colour_mode = mode & 3
            if colour_mode == 0:
                self.lock.notify_all()
                return
            if colour_mode == 1:
                colour, paint = self.gfg, self.gfg_mode
            elif colour_mode == 3:
                colour, paint = self.gbg, self.gbg_mode
            else:
                colour, paint = 0, 4
            if operation in (0x00, 0x08, 0x20, 0x28):
                points = self._line_points(second, last)
                if operation in (0x20, 0x28):
                    points = points[1:]
                if operation in (0x08, 0x28):
                    points = points[:-1]
                for line_x, line_y in points:
                    self._paint(line_x, line_y, colour, paint)
            elif operation == 0x40:
                self._paint(last[0], last[1], colour, paint)
            elif operation == 0x50:
                self._triangle(third, second, last, colour, paint)
            elif operation == 0x60:
                self._fill((second[0], second[1], last[0], last[1]), colour,
                           paint)
            elif operation in (0x90, 0x98):
                radius = ((last[0] - second[0]) ** 2
                          + (last[1] - second[1]) ** 2) ** 0.5
                self._circle(second, radius, operation == 0x98, colour, paint)
            elif operation == 0xB8:
                self._copy(mode, third, second, last)
            elif operation == 0xE8:
                self._draw_bitmap(last)
            else:
                self.errors.append(f"PLOT {command:#x}")
            self.lock.notify_all()
        self._event("plot", (command, point_x, point_y))

    def _triangle(self, first, second, third, colour, paint):
        """Fill start triangle, edges included.  Call with the lock held."""
        def side(start, end, point):
            return ((end[0] - start[0]) * (point[1] - start[1])
                    - (end[1] - start[1]) * (point[0] - start[0]))
        columns = [first[0], second[0], third[0]]
        rows = [first[1], second[1], third[1]]
        for row in range(min(rows), max(rows) + 1):
            for column in range(min(columns), max(columns) + 1):
                sides = (side(first, second, (column, row)),
                         side(second, third, (column, row)),
                         side(third, first, (column, row)))
                if (all(value >= 0 for value in sides)
                        or all(value <= 0 for value in sides)):
                    self._paint(column, row, colour, paint)

    def _circle(self, centre, radius, filled, colour, paint):
        """Draw or fill a circle.  Call with the lock held."""
        centre_x, centre_y = centre
        reach = int(radius) + 1
        for row in range(centre_y - reach, centre_y + reach + 1):
            for column in range(centre_x - reach, centre_x + reach + 1):
                distance = ((column - centre_x) ** 2
                            + (row - centre_y) ** 2) ** 0.5
                if (filled and distance <= radius) or (
                        not filled and abs(distance - radius) <= 0.5):
                    self._paint(column, row, colour, paint)

    def _copy(self, mode, corner, other, destination):
        """Copy, or with mode 1 or 5 move, the rectangle between two corners
        so that its bottom-left corner lands on a point, as the real VDP's
        plotCopyMove does:  the source is cut to the screen without moving
        the destination, and only the destination is clipped.  Call with the
        lock held."""
        width = abs(corner[0] - other[0])
        height = abs(corner[1] - other[1])
        source_x = min(corner[0], other[0])
        source_y = min(corner[1], other[1])
        dest_x, dest_y = destination[0], destination[1] - height
        left = max(source_x, 0)
        top = max(source_y, 0)
        right = min(source_x + width, SCREEN_WIDTH - 1)
        bottom = min(source_y + height, SCREEN_HEIGHT - 1)
        snapshot = bytes(self.canvas)
        for row in range(bottom - top + 1):
            for column in range(right - left + 1):
                colour = snapshot[(top + row) * SCREEN_WIDTH + left + column]
                self._paint(dest_x + column, dest_y + row, colour, 0)
        if mode in (1, 5):
            self.errors.append("PLOT move, whose clearing isn't modelled")

    def _palette_rgb(self, index):
        """Return the RGB of a palette index."""
        return self.palette.get(index, CGA_PALETTE[index])

    def _nearest_index(self, red, green, blue):
        """Return the palette index of a 2-bit-per-channel colour:  the
        entry it is exactly, or the nearest by RGB distance."""
        cached = self.nearest.get((red, green, blue))
        if cached is not None:
            return cached
        best, best_distance = 0, None
        for index in range(COLOUR_DEPTH):
            entry = [channel >> 6 for channel in self._palette_rgb(index)]
            distance = ((entry[0] - red) ** 2 + (entry[1] - green) ** 2
                        + (entry[2] - blue) ** 2)
            if best_distance is None or distance < best_distance:
                best, best_distance = index, distance
        self.nearest[(red, green, blue)] = best
        return best

    def _draw_bitmap(self, point):
        """Draw the selected bitmap with its top-left corner at a point,
        skipping pixels whose alpha is 0.  Call with the lock held."""
        if self.bitmap not in self.bitmaps:
            self.errors.append(f"plot of missing bitmap {self.bitmap}")
            return
        width, height, data, fmt = self.bitmaps[self.bitmap]
        for row in range(height):
            for column in range(width):
                if fmt == FORMAT_RGBA8888:
                    start = (row * width + column) * 4
                    red, green, blue, alpha = data[start:start + 4]
                    red, green, blue = red >> 6, green >> 6, blue >> 6
                else:
                    pixel = data[row * width + column]
                    red, green, blue = (pixel & 3, (pixel >> 2) & 3,
                                        (pixel >> 4) & 3)
                    alpha = pixel >> 6
                if alpha == 0:
                    continue
                self._paint(point[0] + column, point[1] + row,
                            self._nearest_index(red, green, blue), 0)

    def _capture(self, buffer_id):
        """Make an RGBA2222 bitmap of the screen between the last two
        points, both included, as VDU 23,27,&21,id;0; does."""
        with self.lock:
            (first_x, first_y), (second_x, second_y) = self.points[:2]
            left = max(min(first_x, second_x), 0)
            right = min(max(first_x, second_x), SCREEN_WIDTH - 1)
            top = max(min(first_y, second_y), 0)
            bottom = min(max(first_y, second_y), SCREEN_HEIGHT - 1)
            data = bytearray()
            for row in range(top, bottom + 1):
                for column in range(left, right + 1):
                    index = self.canvas[row * SCREEN_WIDTH + column]
                    red, green, blue = (channel >> 6 for channel in
                                        self._palette_rgb(index))
                    data.append(red | (green << 2) | (blue << 4) | 0xC0)
            self.buffers[buffer_id] = [bytes(data)]
            self.bitmaps[buffer_id] = (right - left + 1, bottom - top + 1,
                                       bytes(data), FORMAT_RGBA2222)
        self._event("capture", buffer_id)

    def _graphics_glyph(self, byte):
        """Draw a glyph at the graphics cursor in the graphics font, its
        set pixels in the graphics foreground, and move the cursor right."""
        with self.lock:
            if not self.cursor_behaviour & BEHAVIOUR_GRAPHICS_NO_WRAP:
                self.errors.append("text at the graphics cursor can wrap")
            if self.graphics_font is None:
                self.errors.append("text at the graphics cursor in the "
                                   "system font")
                return
            width, height, data = self.fonts[self.graphics_font]
            row_bytes = (width + 7) // 8
            start = byte * row_bytes * height
            cursor_x, cursor_y = self.points[0]
            for row in range(height):
                bits = int.from_bytes(data[start + row * row_bytes:
                                           start + (row + 1) * row_bytes],
                                      "big")
                for column in range(width):
                    if bits & (1 << (row_bytes * 8 - 1 - column)):
                        self._paint(cursor_x + column, cursor_y + row,
                                    self.gfg, self.gfg_mode)
            self.points = ((cursor_x + width, cursor_y),) + self.points[1:]
            self.events.append((time.time(), "gtext", byte))
            self.lock.notify_all()

    def pixel(self, column, row):
        """Return the palette index of a pixel of the canvas."""
        with self.lock:
            return self.canvas[row * SCREEN_WIDTH + column]

    def pixels(self, left, top, width, height):
        """Return a rectangle of the canvas as rows of palette indices."""
        with self.lock:
            return [list(self.canvas[row * SCREEN_WIDTH + left:
                                     row * SCREEN_WIDTH + left + width])
                    for row in range(top, top + height)]

    def count_events(self, kind):
        """Return how many events of a kind have been received."""
        with self.lock:
            return sum(1 for _time, event_kind, _detail in self.events
                       if event_kind == kind)

    def expect(self, pattern, timeout=15):
        """Wait until the screen text matches a regular expression.

        @param pattern The regular expression.
        @param timeout Seconds to wait.

        @return The match.
        """
        deadline = time.time() + timeout
        with self.lock:
            while True:
                match = re.search(pattern, self.screen)
                if match:
                    return match
                left = deadline - time.time()
                if left <= 0:
                    raise AssertionError(
                        f"VDP screen never matched {pattern!r}; "
                        f"screen ends {self.screen[-200:]!r}")
                self.lock.wait(left)

    def grid_lines(self):
        """Return each row of the grid without trailing spaces."""
        with self.lock:
            return ["".join(row).rstrip() for row in self.grid]

    def wait_grid(self, check, timeout=15):
        """Wait until the grid passes a check.

        @param check A function that takes the list of grid_lines() and
          returns whether it shows what is wanted.
        @param timeout Seconds to wait.
        """
        deadline = time.time() + timeout
        while not check(self.grid_lines()):
            if time.time() > deadline:
                raise AssertionError(
                    f"VDP grid never matched: {self.grid_lines()!r}")
            with self.lock:
                self.lock.wait(0.1)

    def wait_connections(self, count, timeout=15):
        """Wait until the simulator has connected a number of times.

        @param count The number of connections to wait for.
        @param timeout Seconds to wait.
        """
        deadline = time.time() + timeout
        with self.lock:
            while self.connections < count:
                left = deadline - time.time()
                if left <= 0:
                    raise AssertionError(
                        f"the simulator connected {self.connections} time(s), "
                        f"not {count}")
                self.lock.wait(left)

    def _modifiers(self):
        return ((KEYMOD_CAPS_LOCK if self.caps_lock else 0)
                | (KEYMOD_NUM_LOCK if self.num_lock else 0)
                | (KEYMOD_SCROLL_LOCK if self.scroll_lock else 0))

    def lock_key(self, code):
        """Press and release a lock key the way FabGL handles it: the release
        toggles the lock and sets every LED to the lock states, and both
        packets carry the modifiers from before the toggle.

        @param code KEYCODE_NUMLOCK, KEYCODE_CAPSLOCK, or KEYCODE_SCROLLLOCK.
        """
        with self.lock:
            conn = self.conn
            modifiers = self._modifiers()
            if code == KEYCODE_NUMLOCK:
                self.num_lock = not self.num_lock
            elif code == KEYCODE_CAPSLOCK:
                self.caps_lock = not self.caps_lock
            else:
                self.scroll_lock = not self.scroll_lock
            self.leds = ((LED_SCROLL_LOCK if self.scroll_lock else 0)
                         | (LED_CAPS_LOCK if self.caps_lock else 0)
                         | (LED_NUM_LOCK if self.num_lock else 0))
        self._send(conn, bytes([0x81, 0x04, 0, modifiers, code, 1,
                            0x81, 0x04, 0, modifiers, code, 0]))
        time.sleep(0.01)

    def keypad(self, ch):
        """Press and release a keypad key the way FabGL reports it under its
        own Num Lock.

        @param ch One of 0-9 and ., naming the key.
        """
        index = 10 if ch == "." else int(ch)
        with self.lock:
            conn = self.conn
            modifiers = self._modifiers()
            if self.num_lock:
                code = (KEYCODE_KEYPAD_PERIOD if ch == "."
                        else KEYCODE_KEYPAD_0 + index)
                ascii_value = ord(ch)
            else:
                code = KEYCODE_KEYPAD_NAVIGATION[index]
                ascii_value = 0
        self._send(conn, bytes([0x81, 0x04, ascii_value, modifiers, code, 1,
                            0x81, 0x04, ascii_value, modifiers, code, 0]))
        time.sleep(0.01)

    def wait_leds(self, leds, timeout=10):
        """Wait until the keyboard's LEDs are a given set.

        @param leds The LED_* bits expected.
        @param timeout Seconds to wait.
        """
        deadline = time.time() + timeout
        with self.lock:
            while self.leds != leds:
                left = deadline - time.time()
                if left <= 0:
                    raise AssertionError(
                        f"the LEDs are {self.leds:#x}, not {leds:#x}")
                self.lock.wait(left)

    def key(self, ch):
        """Press and release the key for a character on a US keyboard.

        @param ch The character.
        """
        code = keycode(ch)
        with self.lock:
            conn = self.conn
            if self.layout == LAYOUT_UK:
                ch = UK_CHARACTER_FOR_US_KEY.get(ch, ch)
        ascii_value = ord(ch)
        self._send(conn, bytes([0x81, 0x04, ascii_value, 0, code, 1,
                            0x81, 0x04, ascii_value, 0, code, 0]))

    def press(self, code):
        """Press and release a key that has no ASCII value.

        @param code The FabGL keycode of the key.
        """
        with self.lock:
            conn = self.conn
        self._send(conn, bytes([0x81, 0x04, 0, 0, code, 1,
                            0x81, 0x04, 0, 0, code, 0]))
        time.sleep(0.01)

    def ctrl(self, ch):
        """Press and release Ctrl and a letter, which types its control
        character.

        @param ch The letter, in lower case.
        """
        code = keycode(ch)
        control = ord(ch.upper()) - 64
        with self.lock:
            conn = self.conn
            modifiers = self._modifiers() | KEYMOD_CTRL
        self._send(conn, bytes([0x81, 0x04, control, modifiers, code, 1,
                                0x81, 0x04, control, modifiers, code, 0]))

    def release_display(self):
        """Press and release Ctrl+Alt+Backspace, which takes the display back
        from its owner.  Outside ownership it is an ordinary Backspace."""
        with self.lock:
            conn = self.conn
            modifiers = self._modifiers() | KEYMOD_CTRL | KEYMOD_ALT_LEFT
        self._send(conn, bytes([0x81, 0x04, 8, modifiers, KEYCODE_BACKSPACE, 1,
                            0x81, 0x04, 8, modifiers, KEYCODE_BACKSPACE, 0]))
        time.sleep(0.01)

    def context_events(self):
        """Return the context selects and deletes received, in order.

        @return A list of ("select", id) and ("delete", id).
        """
        with self.lock:
            return [detail for _time, kind, detail in self.events
                    if kind == "context"]

    def wait_context_events(self, count, timeout=15):
        """Wait until a number of context selects and deletes have arrived.

        @param count The number to wait for.
        @param timeout Seconds to wait.

        @return The list context_events() returns.
        """
        deadline = time.time() + timeout
        while len(self.context_events()) < count:
            if time.time() > deadline:
                raise AssertionError(
                    f"only these context commands arrived: "
                    f"{self.context_events()!r}")
            with self.lock:
                self.lock.wait(0.1)
        return self.context_events()

    def type(self, text):
        """Type a string, one key at a time.

        @param text The characters to type.
        """
        for ch in text:
            self.key(ch)
            time.sleep(0.01)

    def disconnect(self):
        """Drop the current connection, as if the VDP had gone away."""
        with self.lock:
            conn = self.conn
        conn.shutdown(socket.SHUT_RDWR)
        conn.close()

    def clear(self):
        """Forget the text drawn so far."""
        with self.lock:
            self.screen = ""
            self.cursor = 0

    def close(self):
        """Stop listening, drop the connection, and wait for the serving
        thread to end.  Shutting the sockets down wakes a thread blocked in
        accept or recv; a thread left in accept takes the connection of the
        next FakeVdp whose listening socket gets the same descriptor."""
        with self.lock:
            sockets = [self.server, self.conn]
        for sock in sockets:
            if sock is None:
                continue
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass
        if self.thread is not threading.current_thread():
            self.thread.join(5)
        if os.path.exists(self.path):
            os.unlink(self.path)
