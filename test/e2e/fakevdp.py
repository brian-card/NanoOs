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
"""

import os
import re
import socket
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
KEYBOARD_LAYOUT = 0x81
MOUSE = 0x89
BUFFERED = 0xA0
BUFFERED_WRITE = 0
BUFFERED_CLEAR = 2
MOUSE_ENABLE = 0
MOUSE_DISABLE = 1
MOUSE_RESET = 2
MOUSE_SET_CURSOR = 3
SPRITES = 27
SELECT_BITMAP = 0x20
CREATE_BITMAP = 0x21
MAKE_CURSOR = 0x40
FORMAT_RGBA2222 = 1
BITMAP_BASE_ID = 0xFA00
AUDIO = 0x85
AUDIO_RESET = 10
AUDIO_SAMPLE_RATE = 13
KEYSTATE = 0x88
UDG_RESET = 0x91
CONTROL_KEYS = 0x98
LEGACY_MODES = 0xC1
COPPER = 0xC4
COPPER_RESET_SIGNALS = 4
CONSOLE_MODE = 0xFE
SPRITES_RESET = 16
ALL_BUFFERS = 0xFFFF
LAYOUT_UK = 0
LAYOUT_US = 1
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
                 mouse_reset_seconds=0.0):
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
        self.dropped = 0       # bytes ignored before listening
        self.layout = LAYOUT_UK
        self.palette = {}      # logical color -> (red, green, blue) set by VDU 19
        # Like the real VDP's, these outlive a connection.
        self.buffers = {}      # buffer ID -> list of streams written to it
        self.bitmaps = {}      # bitmap ID -> (width, height, RGBA2222 bytes)
        self.bitmap = BITMAP_BASE_ID
        self.cursors = {}      # cursor ID -> (bitmap, hotspot x, hotspot y)
        self.mouse_enabled = False
        self.mouse_cursor = None
        self.mouse = mouse
        self.mouse_detected = False
        self.mouse_visible = False
        self.mouse_port_suspended = False
        self.mouse_reset_seconds = mouse_reset_seconds
        self.mouse_reset_until = 0.0
        self.mouse_reset_answered = None  # time of the poll answered after it
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
        stream = self._bytes(conn)
        try:
            for byte in stream:
                if byte == 23:
                    self._vdu23(stream)
                elif byte == 22:
                    mode = next(stream)
                    if self.listening:
                        with self.lock:
                            # Changing the mode resets the palette.
                            self.palette = {}
                        self._event("mode", mode)
                elif byte == 17:
                    colour = next(stream)
                    if self.listening:
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
                elif not self.listening:
                    self.dropped += 1
                elif byte == 12:
                    self._event("cls")
                    with self.lock:
                        self.screen = ""
                        self.cursor = 0
                        self.lock.notify_all()
                else:
                    self._text(byte)
        except StopIteration:
            pass

    def _bytes(self, conn):
        while True:
            try:
                data = conn.recv(4096)
            except OSError:
                return
            if not data:
                return
            yield from data

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
            elif command == KEYSTATE:
                self._reset_event(command, (self._word(stream),
                                            self._word(stream), next(stream)))
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
                self._event("cursor", mode)
        elif first == SPRITES:
            self._sprites(stream)
        else:
            self.errors.append(f"VDU 23,{first}")

    def _reset_event(self, command, detail=None):
        if self.listening:
            self._event("reset", (command, detail))

    def _word(self, stream):
        return next(stream) | (next(stream) << 8)

    def _buffered(self, stream):
        buffer_id = self._word(stream)
        command = next(stream)
        if command == BUFFERED_WRITE:
            length = self._word(stream)
            data = bytes(next(stream) for _ in range(length))
            with self.lock:
                self.buffers.setdefault(buffer_id, []).append(data)
        elif command == BUFFERED_CLEAR and buffer_id == ALL_BUFFERS:
            with self.lock:
                self.buffers.clear()
                self.bitmaps.clear()
                self.cursors.clear()
            self._reset_event(BUFFERED, buffer_id)
        elif command == BUFFERED_CLEAR:
            with self.lock:
                self.buffers.pop(buffer_id, None)
        else:
            self.errors.append(f"VDU 23,0,&A0,{buffer_id};{command}")

    def _sprites(self, stream):
        command = next(stream)
        if command == SELECT_BITMAP:
            self.bitmap = self._word(stream)
        elif command == CREATE_BITMAP:
            width = self._word(stream)
            height = self._word(stream)
            fmt = next(stream)
            streams = self.buffers.get(self.bitmap, [])
            if fmt != FORMAT_RGBA2222:
                self.errors.append(f"bitmap {self.bitmap} format {fmt}")
            elif len(streams) != 1:
                self.errors.append(
                    f"bitmap {self.bitmap} from {len(streams)} streams")
            elif len(streams[0]) != width * height:
                self.errors.append(
                    f"bitmap {self.bitmap} is {len(streams[0])} bytes, "
                    f"not {width}x{height}")
            else:
                with self.lock:
                    self.bitmaps[self.bitmap] = (width, height, streams[0])
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
                self.mouse_detected = (self.mouse
                                       and not self.mouse_port_suspended)
                if self.mouse_detected:
                    self.mouse_visible = True
                self.mouse_reset_until = time.time() + self.mouse_reset_seconds
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

    def pointer_image(self):
        """Return the mouse pointer as text, one string per row.

        @return '.' for a transparent pixel, 'B' for opaque black, 'W' for
          opaque white, and '?' for anything else, or None if no custom
          pointer is shown.
        """
        with self.lock:
            if self.mouse_cursor not in self.cursors:
                return None
            (width, height, data), _hx, _hy = self.cursors[self.mouse_cursor]
        rows = []
        for y in range(height):
            row = ""
            for pixel in data[y * width:(y + 1) * width]:
                # The VDP's RGBA2222 packs red in the low bits, alpha in the high.
                rgb = (pixel & 3, (pixel >> 2) & 3, (pixel >> 4) & 3)
                alpha = pixel >> 6
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
                        conn.sendall(bytes([0x80, 0x01, cookie]))
                        if self.mouse_reset_answered is None:
                            self.mouse_reset_answered = time.time()
                        self.lock.notify_all()
                threading.Timer(self.mouse_reset_until - now,
                                answer_after_reset).start()
            elif due:
                self.conn.sendall(bytes([0x80, 0x01, cookie]))
                if self.mouse_reset_until and self.mouse_reset_answered is None:
                    self.mouse_reset_answered = now
                if self.answered is None:
                    self.listening = True
                    self.answered = now
            self.lock.notify_all()

    def _event(self, kind, detail=None):
        with self.lock:
            self.events.append((time.time(), kind, detail))
            self.lock.notify_all()

    def _text(self, byte):
        with self.lock:
            if byte == 8:
                if self.cursor > 0 and self.screen[self.cursor - 1] != "\n":
                    self.cursor -= 1
            elif byte in (10, 13):
                self.screen += chr(byte)
                self.cursor = len(self.screen)
            else:
                self.screen = (self.screen[:self.cursor] + chr(byte)
                               + self.screen[self.cursor + 1:])
                self.cursor += 1
            self.events.append((time.time(), "text", byte))
            self.lock.notify_all()

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
        conn.sendall(bytes([0x81, 0x04, ascii_value, 0, code, 1,
                            0x81, 0x04, ascii_value, 0, code, 0]))

    def press(self, code):
        """Press and release a key that has no ASCII value.

        @param code The FabGL keycode of the key.
        """
        with self.lock:
            conn = self.conn
        conn.sendall(bytes([0x81, 0x04, 0, 0, code, 1,
                            0x81, 0x04, 0, 0, code, 0]))
        time.sleep(0.01)

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
        """Stop listening and drop the connection."""
        try:
            self.server.close()
        except OSError:
            pass
        with self.lock:
            if self.conn is not None:
                try:
                    self.conn.close()
                except OSError:
                    pass
        if os.path.exists(self.path):
            os.unlink(self.path)
