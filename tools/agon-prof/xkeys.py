#!/usr/bin/env python3
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


"""Type at one X window with XSendEvent, never XTest, so that keys go only to
that window and not to whatever has the focus on the display.

Usage:  xkeys.py <window-id> <text>         (Python escapes:  \\r, \\x1b, ...)
        xkeys.py <window-id> --key <keysym>  (for example Up)
XKEYS_DELAY is the pause after each key, in seconds (default 0.03).
Modifiers are sent as real Shift_L/Control_L presses around the key:
setting only the event's state leaves the VDP's modifier state stuck.
"""
import ctypes, os, sys, time

x11 = ctypes.cdll.LoadLibrary("libX11.so.6")
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XKeycodeToKeysym.restype = ctypes.c_ulong
x11.XKeycodeToKeysym.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_int]
x11.XDefaultRootWindow.restype = ctypes.c_ulong
x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int,
                           ctypes.c_long, ctypes.c_void_p]
x11.XFlush.argtypes = [ctypes.c_void_p]


class XKeyEvent(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("serial", ctypes.c_ulong),
                ("send_event", ctypes.c_int), ("display", ctypes.c_void_p),
                ("window", ctypes.c_ulong), ("root", ctypes.c_ulong),
                ("subwindow", ctypes.c_ulong), ("time", ctypes.c_ulong),
                ("x", ctypes.c_int), ("y", ctypes.c_int),
                ("x_root", ctypes.c_int), ("y_root", ctypes.c_int),
                ("state", ctypes.c_uint), ("keycode", ctypes.c_uint),
                ("same_screen", ctypes.c_int), ("pad", ctypes.c_char * 128)]


KEY_PRESS, KEY_RELEASE, SHIFT, CONTROL = 2, 3, 1, 4
display = x11.XOpenDisplay(None)
window = int(sys.argv[1], 0)
root = x11.XDefaultRootWindow(display)
delay = float(os.environ.get("XKEYS_DELAY", "0.03"))


def event(kind, keycode, state):
    e = XKeyEvent(type=kind, display=display, window=window, root=root,
                  state=state, keycode=keycode, same_screen=1)
    x11.XSendEvent(display, window, 1, 1 if kind == KEY_PRESS else 2,
                   ctypes.byref(e))
    x11.XFlush(display)
    time.sleep(0.01)


def code(name):
    return x11.XKeysymToKeycode(display, x11.XStringToKeysym(name.encode()))


def tap(keysym, modifiers=()):
    keycode = x11.XKeysymToKeycode(display, keysym)
    mods = list(modifiers)
    if x11.XKeycodeToKeysym(display, keycode, 0) != keysym:
        mods.append("Shift_L")
    state = 0
    for m in mods:
        event(KEY_PRESS, code(m), state)
        state |= SHIFT if m == "Shift_L" else CONTROL
    event(KEY_PRESS, keycode, state)
    event(KEY_RELEASE, keycode, state)
    for m in reversed(mods):
        state &= ~(SHIFT if m == "Shift_L" else CONTROL)
        event(KEY_RELEASE, code(m), state)
    time.sleep(delay)


NAMES = {"\r": "Return", "\n": "Return", "\x1b": "Escape", "\t": "Tab",
         "\b": "BackSpace", " ": "space"}
if sys.argv[2] == "--key":
    tap(x11.XStringToKeysym(sys.argv[3].encode()))
else:
    for ch in sys.argv[2].encode().decode("unicode_escape"):
        if ch in NAMES:
            tap(x11.XStringToKeysym(NAMES[ch].encode()))
        elif ord(ch) < 0x20:
            tap(ord(ch) + 0x60, ("Control_L",))
        else:
            tap(ord(ch))
