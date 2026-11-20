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

from harness import VDP_CONFIG_SYS, message_args_reports
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
                     "gfxprobe: GET_ERROR not supported"], lines
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
        "gfxprobe: OPEN_DISPLAY not supported",
        "gfxprobe: CLOSE_DISPLAY no error",
        "gfxprobe: GET_SURFACE_SIZE not supported",
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
        "gfxprobe: GET_ERROR not supported",
        "gfxprobe: SET_CURSOR not supported",
        "gfxprobe: SHOW_CURSOR no error",
        "gfxprobe: WARP_CURSOR no error",
        "gfxprobe: WRONG_SIZE invalid state",
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
    assert lines == ([f"gfxprobe: graphics driver is pid {vdp_pid(s)}"]
                     + uploads
                     + ["gfxprobe: default destroyed invalid handle"]), lines
    assert status == 0
    assert message_args_reports(s) == []
    assert s.vdp.errors == [], s.vdp.errors
    assert 0xF101 not in s.vdp.fonts, sorted(s.vdp.fonts)
    assert_vdp_font(s.vdp)
