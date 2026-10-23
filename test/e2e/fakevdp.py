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
KEYCODE_BACKSPACE = 132
KEYCODE_RETURN = 143


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

    def __init__(self, path, answer_after=0.0, answer=True):
        """Start listening.

        @param path The path of the Unix socket to create.
        @param answer_after Seconds after the first poll before polls are
          answered.
        @param answer False to never answer a poll, but record everything sent
          as if listening, to see what the driver does when it gives up.
        """
        self.path = path
        self.answer_after = answer_after
        self.answer = answer
        self.lock = threading.Condition()
        self.conn = None
        self.connections = 0
        self.first_poll = None
        self.polls = []        # times of the general polls received
        self.answered = None   # time of the first poll answered
        self.events = []       # (time, kind, detail) once listening
        self.screen = ""       # text drawn since the last clear
        self.dropped = 0       # bytes ignored before listening
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
                elif not self.listening:
                    self.dropped += 1
                elif byte == 12:
                    self._event("cls")
                    with self.lock:
                        self.screen = ""
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
            else:
                self.errors.append(f"VDU 23,0,{command:#x}")
        elif first == 1:
            mode = next(stream)
            if self.listening:
                self._event("cursor", mode)
        else:
            self.errors.append(f"VDU 23,{first}")

    def _poll(self, cookie):
        now = time.time()
        with self.lock:
            self.polls.append(now)
            if self.first_poll is None:
                self.first_poll = now
            due = self.answer and (now - self.first_poll >= self.answer_after)
            if due:
                self.conn.sendall(bytes([0x80, 0x01, cookie]))
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
                self.screen = self.screen[:-1]
            else:
                self.screen += chr(byte)
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
        """Press and release the key for a character.

        @param ch The character.
        """
        ascii_value = ord(ch)
        code = keycode(ch)
        with self.lock:
            conn = self.conn
        conn.sendall(bytes([0x81, 0x04, ascii_value, 0, code, 1,
                            0x81, 0x04, ascii_value, 0, code, 0]))

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
