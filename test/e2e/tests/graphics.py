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

"""End-to-end tests of config.sys's GRAPHICS line and the display driver's
side of Graphics.h: the driver's registered name, the IPC grant to the
program the line names, and the driver's answer to every client message."""

import re
import time

from harness import PROMPT, VDP_CONFIG_SYS, message_args_reports, vdp_login
from tests.vdp_drv import assert_vdp_font


GRAPHICS_LINE = "GRAPHICS=/system/drivers/vdp.drv /system/commands/gfxprobe\n"


def boot(s, extra):
    """Boot with vdp.drv on a FakeVdp, extra config.sys lines, and log in.

    @param s The session.
    @param extra The lines to add to the FakeVdp config.sys.
    """
    s.restart_with_vdp(config_sys=VDP_CONFIG_SYS + extra)
    s.login()


def vdp_pid(s):
    """Get vdp.drv's process ID from ps.

    @param s The session.

    @return The process ID.
    """
    m = re.search(r"^(\d+) +\S+ vdp\.drv$", s.sh("ps"), re.M)
    assert m, "vdp.drv isn't in ps"
    return int(m.group(1))


def probe(s, mode):
    """Run gfxprobe as a system command.

    @param s The session.
    @param mode The gfxprobe mode: "find", "send", "all" or "fonts".

    @return The probe's output lines, without dirtest's status line, and the
      probe's exit status.
    """
    out = s.sh("dirtest system gfxprobe " + mode)
    m = re.search(r"^system spawn 0 status (\d+)$", out, re.M)
    assert m, f"gfxprobe didn't run: {out!r}"
    lines = [l for l in out.splitlines() if l.startswith("gfxprobe:")]
    return lines, int(m.group(1))


def test_graphics_line_registers_the_display_driver(s):
    boot(s, GRAPHICS_LINE)
    lines, status = probe(s, "find")
    assert lines == [f"gfxprobe: graphics driver is pid {vdp_pid(s)}"], lines
    assert status == 0


def test_graphics_client_may_message_the_display_driver(s):
    boot(s, GRAPHICS_LINE)
    lines, status = probe(s, "send")
    assert lines == [f"gfxprobe: graphics driver is pid {vdp_pid(s)}",
                     "gfxprobe: GET_ERROR not the display owner"], lines
    assert status == 0


def test_graphics_refuses_another_program(s):
    boot(s, "GRAPHICS=/system/drivers/vdp.drv /system/commands/passwd\n")
    lines, status = probe(s, "send")
    assert lines == [f"gfxprobe: graphics driver is pid {vdp_pid(s)}",
                     "gfxprobe: GET_ERROR device lost"], lines
    assert status == 1


def test_no_graphics_line_registers_no_display_driver(s):
    boot(s, "")
    lines, status = probe(s, "find")
    assert lines == ["gfxprobe: no graphics driver (-8)"], lines
    assert status == 1


def test_graphics_line_naming_no_device_registers_nothing(s):
    boot(s, "GRAPHICS=/system/drivers/none.drv /system/commands/gfxprobe\n")
    lines, status = probe(s, "find")
    assert lines == ["gfxprobe: no graphics driver (-8)"], lines
    assert status == 1


def test_graphics_line_without_a_program_registers_nothing(s):
    boot(s, "GRAPHICS=/system/drivers/vdp.drv\n")
    lines, status = probe(s, "find")
    assert lines == ["gfxprobe: no graphics driver (-8)"], lines
    assert status == 1


def test_only_the_first_graphics_line_is_used(s):
    boot(s, "GRAPHICS=/system/drivers/vdp.drv /system/commands/passwd\n"
         + GRAPHICS_LINE)
    lines, status = probe(s, "send")
    assert lines == [f"gfxprobe: graphics driver is pid {vdp_pid(s)}",
                     "gfxprobe: GET_ERROR device lost"], lines
    assert status == 1


def test_graphics_driver_answers_every_client_message(s):
    boot(s, GRAPHICS_LINE)
    lines, status = probe(s, "all")
    assert lines == [
        f"gfxprobe: graphics driver is pid {vdp_pid(s)}",
        "gfxprobe: OPEN_DISPLAY no error",
        "gfxprobe: GET_SURFACE_SIZE no error",
        "gfxprobe: UPLOAD_IMAGE not supported",
        "gfxprobe: DESTROY_IMAGE no error",
        "gfxprobe: GET_IMAGE_SIZE not supported",
        "gfxprobe: UPLOAD_FONT no error",
        "gfxprobe: DESTROY_FONT no error",
        "gfxprobe: SET_DEFAULT_FONT no error",
        "gfxprobe: SUBMIT not supported",
        "gfxprobe: PREUPLOAD not supported",
        "gfxprobe: DISCARD_PREUPLOAD no error",
        "gfxprobe: FINISH not supported",
        "gfxprobe: CAPTURE_RECT not supported",
        "gfxprobe: GET_ERROR no error",
        "gfxprobe: SET_CURSOR not supported",
        "gfxprobe: SHOW_CURSOR no error",
        "gfxprobe: WARP_CURSOR no error",
        "gfxprobe: WRONG_SIZE invalid state",
        "gfxprobe: CLOSE_DISPLAY no error",
    ], lines
    assert status == 0
    assert message_args_reports(s) == []
    assert s.vdp.errors == [], s.vdp.errors
    assert_vdp_font(s.vdp)


def test_graphics_font_slots_are_reused_and_destroyed_fonts_refused(s):
    boot(s, GRAPHICS_LINE)
    lines, status = probe(s, "fonts")
    uploads = []
    for ii in range(1, 5):
        uploads += [f"gfxprobe: upload {ii} no error 0xf101",
                    "gfxprobe: destroy no error"]
    assert lines == ([f"gfxprobe: graphics driver is pid {vdp_pid(s)}",
                      "gfxprobe: open no error"]
                     + uploads
                     + ["gfxprobe: default destroyed invalid handle",
                        "gfxprobe: close no error"]), lines
    assert status == 0
    assert message_args_reports(s) == []
    assert s.vdp.errors == [], s.vdp.errors
    assert 0xF101 not in s.vdp.fonts, sorted(s.vdp.fonts)
    assert_vdp_font(s.vdp)


OPENED = "gfxprobe: open no error 640x480"
ACQUIRE = [("select", 1)]
RELEASE = [("select", 0), ("delete", 1)]


def driver_line(s):
    """Get the line gfxprobe prints when it finds the driver.

    @param s The session.

    @return The line.
    """
    return f"gfxprobe: graphics driver is pid {vdp_pid(s)}"


def hold_on_vdp(s, steps):
    """Log in on the VDP console and run gfxprobe there with steps that open
    the display, waiting until it is owned.

    @param s The session.
    @param steps The gfxprobe steps.

    @return The number of context commands FakeVdp had received before.
    """
    vdp = s.vdp
    vdp_login(vdp)
    base = len(vdp.context_events())
    vdp.type("dirtest system gfxprobe do " + steps + "\r")
    assert vdp.wait_context_events(base + 1)[base:base + 1] == ACQUIRE
    return base


def release_on_vdp(s, held):
    """Take the display back with Ctrl+Alt+Backspace and check the VDP
    console's screen:  the held text, then the owner's revoked line, then
    dirtest's status and the prompt.

    @param s The session.
    @param held The lines gfxprobe printed while it owned the display.
    """
    s.vdp.release_display()
    s.vdp.expect("^" + "".join(re.escape(line) + "\r\n" for line in held)
                 + r"gfxprobe: revoked\r\nsystem spawn 0 status 0\r\n"
                 + PROMPT)


def test_graphics_a_second_client_is_busy_and_refused(s):
    boot(s, GRAPHICS_LINE)
    base = hold_on_vdp(s, "open open hold")
    lines, status = probe(s, "do open error")
    assert lines == [driver_line(s), "gfxprobe: open display busy",
                     "gfxprobe: error not the display owner"], lines
    assert status == 0
    assert s.vdp.context_events()[base:] == ACQUIRE
    logical = [d for _t, kind, d in s.vdp.events if kind == "logical"]
    assert logical == [0], logical

    release_on_vdp(s, [OPENED, OPENED])
    assert s.vdp.context_events()[base:] == ACQUIRE + RELEASE
    lines, status = probe(s, "do open close")
    assert lines == [driver_line(s), OPENED, "gfxprobe: close no error"], \
        lines
    assert message_args_reports(s) == []
    assert s.vdp.errors == [], s.vdp.errors


def test_graphics_closing_the_display_frees_it(s):
    boot(s, GRAPHICS_LINE)
    base = hold_on_vdp(s, "open close hold")
    s.vdp.wait_context_events(base + 3)
    assert s.vdp.context_events()[base:] == ACQUIRE + RELEASE
    lines, status = probe(s, "do open close")
    assert lines == [driver_line(s), OPENED, "gfxprobe: close no error"], \
        lines
    assert status == 0
    assert s.vdp.context_events()[base:] == (ACQUIRE + RELEASE) * 2
    assert_vdp_font(s.vdp)


def test_graphics_killing_the_owner_frees_the_display(s):
    boot(s, GRAPHICS_LINE)
    base = hold_on_vdp(s, "open hold")
    m = re.search(r"^(\d+) +\S+ gfxprobe$", s.sh("ps"), re.M)
    assert m, "gfxprobe isn't in ps"
    pid = int(m.group(1))
    assert s.sh("kill %d" % pid).splitlines()[0] == \
        "Termination of process %d successful." % pid
    s.vdp.wait_context_events(base + 3)
    assert s.vdp.context_events()[base:] == ACQUIRE + RELEASE
    lines, _status = probe(s, "do open close")
    assert lines == [driver_line(s), OPENED, "gfxprobe: close no error"], \
        lines


def test_graphics_an_owner_that_exits_frees_the_display(s):
    boot(s, GRAPHICS_LINE)
    base = len(s.vdp.context_events())
    lines, _status = probe(s, "do open")
    assert lines == [driver_line(s), OPENED], lines
    s.vdp.wait_context_events(base + 3)
    assert s.vdp.context_events()[base:] == ACQUIRE + RELEASE
    lines, _status = probe(s, "do open close")
    assert lines == [driver_line(s), OPENED, "gfxprobe: close no error"], \
        lines


def test_graphics_the_display_is_free_as_soon_as_its_owner_has_exited(s):
    boot(s, GRAPHICS_LINE)
    lines, _status = probe(s, "do open")
    assert lines == [driver_line(s), OPENED], lines
    lines, _status = probe(s, "do open close")
    assert lines == [driver_line(s), OPENED, "gfxprobe: close no error"], \
        lines


def test_graphics_an_owners_fonts_are_freed_when_it_exits(s):
    boot(s, GRAPHICS_LINE)
    lines, _status = probe(s, "do open font")
    assert lines == [driver_line(s), OPENED,
                     "gfxprobe: font no error 0xf101"], lines
    lines, _status = probe(s, "do open font font font close")
    assert lines == [driver_line(s), OPENED,
                     "gfxprobe: font no error 0xf101",
                     "gfxprobe: font no error 0xf102",
                     "gfxprobe: font no error 0xf103",
                     "gfxprobe: close no error"], lines
    assert 0xF101 not in s.vdp.fonts, sorted(s.vdp.fonts)


def test_graphics_console_text_is_held_while_owned_and_replayed(s):
    boot(s, GRAPHICS_LINE)
    hold_on_vdp(s, "open print held-text hold")
    time.sleep(1)
    assert all(line == "" for line in s.vdp.grid_lines()), \
        s.vdp.grid_lines()
    release_on_vdp(s, [OPENED, "gfxprobe: held-text"])
    lines = [line for line in s.vdp.grid_lines() if line]
    assert lines[:3] == [OPENED, "gfxprobe: held-text",
                         "gfxprobe: revoked"], lines
    assert_vdp_font(s.vdp)


def test_graphics_keys_typed_while_owned_are_dropped(s):
    boot(s, GRAPHICS_LINE)
    hold_on_vdp(s, "open hold")
    s.vdp.type("echo leaked\r")
    time.sleep(1)
    release_on_vdp(s, [OPENED])
    s.vdp.type("echo after\r")
    s.vdp.expect(r"\r\nafter\r\n" + PROMPT)
    assert "leaked" not in s.vdp.screen, s.vdp.screen


def test_graphics_a_long_burst_while_owned_replays_whole_lines(s):
    boot(s, GRAPHICS_LINE)
    hold_on_vdp(s, "open burst 30 hold")
    time.sleep(1)
    s.vdp.release_display()
    m = s.vdp.expect(r"^((?:[^\r\n]*\r\n)+)gfxprobe: revoked\r\n")
    replayed = m.group(1).split("\r\n")[:-1]
    assert 1 <= len(replayed) < 30, replayed
    for line in replayed:
        assert re.match(r"^gfxprobe: burst \d\d \.{16}$", line), replayed
    assert replayed[-1] == "gfxprobe: burst 29 " + "." * 16, replayed
    numbers = [int(line.split()[2]) for line in replayed]
    assert numbers == list(range(30 - len(replayed), 30)), numbers


def test_graphics_the_release_key_is_backspace_when_nobody_owns_the_display(s):
    boot(s, GRAPHICS_LINE)
    vdp_login(s.vdp)
    s.vdp.type("echo abx")
    s.vdp.release_display()
    s.vdp.type("c\r")
    s.vdp.expect(r"\r\nabc\r\n" + PROMPT)


def test_graphics_a_driver_restart_frees_the_display(s):
    boot(s, GRAPHICS_LINE)
    vdp = s.vdp
    base = len(vdp.context_events())
    s.child.sendline("dirtest system gfxprobe do open sleep 6 error open hold")
    assert vdp.wait_context_events(base + 1)[base:] == ACQUIRE
    vdp.disconnect()
    vdp.wait_connections(2)
    events = vdp.wait_context_events(base + 4, timeout=30)
    assert events[base:] == ACQUIRE + RELEASE + ACQUIRE, events[base:]
    vdp.release_display()
    s.child.expect(PROMPT, timeout=30)
    lines = [l.strip() for l in s.child.before.splitlines()
             if l.strip().startswith("gfxprobe:")]
    assert lines == [driver_line(s), OPENED,
                     "gfxprobe: error not the display owner", OPENED,
                     "gfxprobe: revoked"], lines
