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
import struct
import time

import gfxrecords as gr
from harness import (PROMPT, VDP_CONFIG_SYS, SkipTest, message_args_reports,
                     vdp_login)
from fakevdp import KEYCODE_a
from tests.vdp_drv import VDP_FONT_ID, assert_vdp_font


GRAPHICS_LINE = "GRAPHICS=/system/drivers/vdp.drv /system/commands/gfxprobe\n"


def boot(s, extra):
    """Boot with vdp.drv on a FakeVdp, extra config.sys lines, and log in.

    @param s The session.
    @param extra The lines to add to the FakeVdp config.sys.
    """
    s.restart_with_vdp(config_sys=VDP_CONFIG_SYS + extra)
    s.login()
    wait_set_up(s.vdp)


def wait_set_up(vdp, timeout=30):
    """Wait until vdp.drv has set up the screen, which it finishes as it
    selects its font, so that it lets a client open the display.

    @param vdp The FakeVdp.
    @param timeout Seconds to wait.
    """
    deadline = time.time() + timeout
    while vdp.font != VDP_FONT_ID:
        assert time.time() < deadline, "vdp.drv never selected its font"
        time.sleep(0.1)


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
        "gfxprobe: UPLOAD_IMAGE no error",
        "gfxprobe: DESTROY_IMAGE no error",
        "gfxprobe: GET_IMAGE_SIZE invalid handle",
        "gfxprobe: UPLOAD_FONT no error",
        "gfxprobe: DESTROY_FONT no error",
        "gfxprobe: SET_DEFAULT_FONT no error",
        "gfxprobe: SUBMIT no error",
        "gfxprobe: PREUPLOAD no error",
        "gfxprobe: DISCARD_PREUPLOAD no error",
        "gfxprobe: FINISH no error",
        "gfxprobe: CAPTURE_RECT no error",
        "gfxprobe: GET_ERROR no error",
        "gfxprobe: SET_CURSOR no error",
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


def test_graphics_keys_typed_while_owned_never_reach_the_shell(s):
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


# Drawing.  Each test writes its command records and pixels into /etc,
# where gfxprobe, as a system command, may read them, then has gfxprobe own
# the display, carry out steps, finish, and hold the display while the test
# looks at FakeVdp's canvas.

FINISHED = r"gfxprobe: finish ([a-z ]+) (\d+) ms\r*\n"


def boot_with_files(session, files, memory=None):
    """Write files into /etc, boot with vdp.drv and the GRAPHICS line, and
    log in on UART1.

    @param session The session.
    @param files A dictionary of file name to bytes.
    @param memory The simulator's memory size, or None for the default.
    """
    for name, data in files.items():
        session.write_file("/etc/" + name, data)
    session.restart_with_vdp(config_sys=VDP_CONFIG_SYS + GRAPHICS_LINE)
    if memory is not None:
        session.restart_with_memory(memory)
    session.login()
    wait_set_up(session.vdp)


def own(session, steps):
    """Run gfxprobe on UART1 to open the display, carry out steps, finish,
    and hold the display, waiting until it has finished.

    @param session The session.
    @param steps The steps between "open" and "finish".

    @return gfxprobe's lines so far, the finish line's milliseconds left
      out.
    """
    session.child.sendline("dirtest system gfxprobe do open " + steps
                     + " finish hold")
    session.child.expect(FINISHED, timeout=30)
    lines = [line.strip() for line in session.child.before.splitlines()
             if line.strip().startswith("gfxprobe:")]
    return lines + ["gfxprobe: finish " + session.child.match.group(1)]


def disown(session):
    """Take the display back from gfxprobe, wait for it to exit, and check
    that neither FakeVdp nor the checked build reported a problem.

    @param session The session.
    """
    session.vdp.release_display()
    session.child.expect(r"gfxprobe: revoked\r*\n", timeout=30)
    session.child.expect(PROMPT, timeout=30)
    assert session.vdp.errors == [], session.vdp.errors
    assert message_args_reports(session) == []


def assert_pixels(vdp, expected):
    """Check pixels of a FakeVdp's canvas.

    @param vdp The FakeVdp.
    @param expected A dictionary of (x, y) to palette index.
    """
    actual = {point: vdp.pixel(*point) for point in expected}
    assert actual == expected, {point: (actual[point], expected[point])
                                for point in expected
                                if actual[point] != expected[point]}


def submitted(session, files, extra=""):
    """Boot, have gfxprobe submit the file "a", finish, and hold.

    @param session The session.
    @param files The files to write, "a" among them.
    @param extra Steps after the submit.

    @return gfxprobe's lines.
    """
    boot_with_files(session, files)
    return own(session, "submit /etc/a " + extra)


def test_graphics_clears_fill_exactly_the_viewport_and_scissor(session):
    records = (gr.viewport(50, 40, 300, 200) + gr.bg(gr.BLUE) + gr.clear()
               + gr.origin(100, 100) + gr.push_scissor(10, 10, 20, 20)
               + gr.bg(gr.RED) + gr.clear() + gr.pop_scissor())
    lines = submitted(session, {"a": records})
    assert lines[1:] == [OPENED, "gfxprobe: submit no error",
                         "gfxprobe: finish no error"], lines
    assert_pixels(session.vdp, {
        (49, 100): gr.BLACK, (50, 100): gr.BLUE, (349, 100): gr.BLUE,
        (350, 100): gr.BLACK, (100, 39): gr.BLACK, (100, 40): gr.BLUE,
        (100, 239): gr.BLUE, (100, 240): gr.BLACK,
        (109, 115): gr.BLUE, (110, 115): gr.RED, (129, 115): gr.RED,
        (130, 115): gr.BLUE, (115, 109): gr.BLUE, (115, 110): gr.RED,
        (115, 129): gr.RED, (115, 130): gr.BLUE})
    disown(session)
    assert message_args_reports(session) == []


def test_graphics_nested_scissors_intersect_and_a_fifth_push_is_an_error(session):
    records = (gr.bg(gr.RED) + gr.push_scissor(0, 0, 100, 100)
               + gr.push_scissor(50, 50, 100, 100) + gr.clear()
               + gr.push_scissor(0, 0, 640, 480) * 3)
    lines = submitted(session, {"a": records}, "error error")
    assert lines[2:] == ["gfxprobe: submit no error",
                         "gfxprobe: error invalid state",
                         "gfxprobe: error no error",
                         "gfxprobe: finish no error"], lines
    assert_pixels(session.vdp, {(50, 50): gr.RED, (99, 99): gr.RED,
                          (49, 50): gr.BLACK, (50, 49): gr.BLACK,
                          (100, 99): gr.BLACK, (99, 100): gr.BLACK})
    disown(session)


def test_graphics_a_bad_record_stops_the_buffer(session):
    files = {"a": gr.bg(gr.RED) + gr.clear() + gr.rect(1, 1, 5, 5)[:4],
             "b": gr.bg(gr.GREEN) + gr.clear() + bytes([0x7F]),
             "c": gr.bg(gr.CYAN) + gr.clear() + gr.image(0xF200, 0, 0)
             + gr.bg(gr.BLUE) + gr.clear(),
             "s": b"submit /etc/a error submit /etc/b submit /etc/c error"}
    boot_with_files(session, files)
    lines = own(session, "@/etc/s")
    assert lines[2:] == ["gfxprobe: submit invalid state",
                         "gfxprobe: error invalid state",
                         "gfxprobe: submit invalid state",
                         "gfxprobe: submit invalid handle",
                         "gfxprobe: error invalid state",
                         "gfxprobe: finish no error"], lines
    assert_pixels(session.vdp, {(300, 300): gr.CYAN})
    disown(session)


def test_graphics_an_unchanged_clip_is_sent_once(session):
    records = gr.viewport(10, 10, 50, 50) + gr.bg(gr.RED) + gr.clear()
    boot_with_files(session, {"a": records})
    before = session.vdp.count_events("gviewport")
    own(session, "submit /etc/a submit /etc/a")
    assert session.vdp.count_events("gviewport") - before == 1
    disown(session)


def test_graphics_primitives_draw_with_exact_edges(session):
    records = (gr.fg(gr.RED) + gr.rect(10, 10, 20, 10)
               + gr.fg(gr.GREEN) + gr.rect(40, 10, 20, 10, filled=False)
               + gr.fg(gr.BLUE) + gr.line(70, 10, 90, 10)
               + gr.fg(gr.YELLOW) + gr.triangle((100, 10), (140, 10),
                                                (100, 50))
               + gr.circle(200, 50, 10) + gr.circle(250, 50, 10, False)
               + gr.origin(300, 0) + gr.fg(gr.RED) + gr.rect(0, 0, 4, 1)
               + gr.fg(gr.BLUE) + gr.blend(gr.BLEND_OR)
               + gr.rect(2, 0, 4, 1) + gr.blend(gr.BLEND_AND)
               + gr.fg(gr.MAGENTA) + gr.rect(0, 0, 1, 1)
               + gr.blend(gr.BLEND_XOR) + gr.fg(gr.WHITE)
               + gr.rect(1, 0, 1, 1))
    lines = submitted(session, {"a": records})
    assert lines[2:] == ["gfxprobe: submit no error",
                         "gfxprobe: finish no error"], lines
    assert_pixels(session.vdp, {
        (10, 10): gr.RED, (29, 19): gr.RED, (30, 10): gr.BLACK,
        (10, 20): gr.BLACK, (9, 10): gr.BLACK,
        (40, 10): gr.GREEN, (59, 10): gr.GREEN, (59, 19): gr.GREEN,
        (40, 19): gr.GREEN, (45, 15): gr.BLACK, (60, 10): gr.BLACK,
        (40, 20): gr.BLACK,
        (70, 10): gr.BLUE, (90, 10): gr.BLUE, (91, 10): gr.BLACK,
        (105, 15): gr.YELLOW, (135, 45): gr.BLACK,
        (200, 50): gr.YELLOW, (200, 62): gr.BLACK,
        (260, 50): gr.YELLOW, (250, 50): gr.BLACK,
        # Red AND magenta, red XOR white, red OR blue, black OR blue.
        (300, 0): gr.RED & gr.MAGENTA, (301, 0): gr.RED ^ gr.WHITE,
        (302, 0): gr.RED | gr.BLUE, (304, 0): gr.BLUE})
    disown(session)


def glyph_pixels(vdp, font_id, code, colour):
    """Return a glyph of a font FakeVdp holds as rows of palette indices.

    @param vdp The FakeVdp.
    @param font_id The font's ID.
    @param code The glyph's character code.
    @param colour The palette index of its set pixels, on black.

    @return The rows.
    """
    width, height, data = vdp.fonts[font_id]
    row_bytes = (width + 7) // 8
    rows = []
    for row in range(height):
        start = (code * height + row) * row_bytes
        bits = int.from_bytes(data[start:start + row_bytes], "big")
        rows.append([colour if bits & (1 << (row_bytes * 8 - 1 - column))
                     else gr.BLACK for column in range(width)])
    return rows


def test_graphics_text_draws_control_bytes_as_glyphs(session):
    records = (gr.fg(gr.WHITE) + gr.text(300, 100, b"A\n\r\x07\x7f")
               + gr.font(0xF101) + gr.text(300, 120, b"A"))
    boot_with_files(session, {"a": records})
    lines = own(session, "font submit /etc/a")
    assert lines[2:] == ["gfxprobe: font no error 0xf101",
                         "gfxprobe: submit no error",
                         "gfxprobe: finish no error"], lines
    for column, code in enumerate(b"A\n\r\x07\x7f"):
        assert session.vdp.pixels(300 + 6 * column, 100, 6, 8) == \
            glyph_pixels(session.vdp, 0xF100, code, gr.WHITE), chr(code)
    assert session.vdp.pixels(300, 120, 8, 8) == \
        glyph_pixels(session.vdp, 0xF101, ord("A"), gr.WHITE)
    assert session.vdp.count_events("gtext") == 6
    disown(session)
    assert_vdp_font(session.vdp)


# A 4x4 Graphics.h RGBA2222 image, red in its top two bits and alpha in its
# bottom two:  opaque blue but for a transparent second column.
BLUE_2222 = (0 << 6) | (0 << 4) | (2 << 2) | 3
STRIPED = bytes([BLUE_2222, 0, BLUE_2222, BLUE_2222] * 4)


def test_graphics_images_draw_with_transparent_pixels_skipped(session):
    files = {"p": STRIPED,
             "q": bytes([0xAA, 0, 0, 0, 0xAA, 0]),
             "r": bytes([0, 0xAA, 0xAA, 0xFF, 0xAA, 0xAA, 0xAA, 0]),
             "a": gr.bg(gr.RED) + gr.clear() + gr.image(0xF200, 10, 10)
             + gr.image(0xF201, 20, 10) + gr.image(0xF202, 30, 10)}
    files["s"] = (b"image rgba2222 4 4 /etc/p size 0xf200 image rgb888 2 1 "
                  b"/etc/q image rgba8888 2 1 /etc/r submit /etc/a")
    boot_with_files(session, files)
    lines = own(session, "@/etc/s")
    assert lines[2:] == ["gfxprobe: image no error 0xf200",
                         "gfxprobe: size no error 4x4",
                         "gfxprobe: image no error 0xf201",
                         "gfxprobe: image no error 0xf202",
                         "gfxprobe: submit no error",
                         "gfxprobe: finish no error"], lines
    assert session.vdp.pixels(10, 10, 5, 4) == [
        [gr.BLUE, gr.RED, gr.BLUE, gr.BLUE, gr.RED]] * 4
    assert session.vdp.pixels(20, 10, 3, 1) == [[gr.RED, gr.GREEN, gr.RED]]
    assert session.vdp.pixels(30, 10, 3, 1) == [[gr.CYAN, gr.RED, gr.RED]]
    disown(session)


def test_graphics_a_sub_image_draws_only_its_part(session):
    files = {"p": bytes([BLUE_2222] * 16),
             "a": gr.sub_image(0xF200, (1, 1, 2, 2), 50, 50)
             + gr.fg(gr.RED) + gr.rect(100, 100, 2, 2)}
    boot_with_files(session, files)
    lines = own(session, "image rgba2222 4 4 /etc/p submit /etc/a")
    assert lines[3:] == ["gfxprobe: submit no error",
                         "gfxprobe: finish no error"], lines
    assert session.vdp.pixels(48, 48, 6, 6) == (
        [[gr.BLACK] * 6] * 2 + [[gr.BLACK] * 2 + [gr.BLUE] * 2
                                + [gr.BLACK] * 2] * 2 + [[gr.BLACK] * 6] * 2)
    assert_pixels(session.vdp, {(100, 100): gr.RED, (101, 101): gr.RED})
    disown(session)


def test_graphics_a_destroyed_image_is_refused(session):
    boot_with_files(session, {"a": gr.image(0xF200, 0, 0),
                        "s": b"image rgba2222 1 1 - destroy 0xf200 "
                        b"size 0xf200 submit /etc/a"})
    lines = own(session, "@/etc/s")
    assert lines[2:] == ["gfxprobe: image no error 0xf200",
                         "gfxprobe: destroy no error",
                         "gfxprobe: size invalid handle 0x0",
                         "gfxprobe: submit invalid handle",
                         "gfxprobe: finish no error"], lines
    disown(session)


def test_graphics_a_large_image_uploads_in_blocks(session):
    if session.sim_bin.endswith("-checked"):
        raise SkipTest("sharing 70 KB copies twice that into the checked "
                       "build's 64 KB arena")
    boot_with_files(session, {"a": gr.image(0xF200, 0, 0)}, memory=512 * 1024)
    lines = own(session, "image rgba8888 160 112 pattern submit /etc/a")
    assert lines[2:] == ["gfxprobe: image no error 0xf200",
                         "gfxprobe: submit no error",
                         "gfxprobe: finish no error"], lines
    writes = [length for buffer_id, length in session.vdp.buffer_writes
              if buffer_id == 0xF200]
    assert writes == [16383 * 4, 160 * 112 * 4 - 16383 * 4], writes
    assert_pixels(session.vdp, {(column, row): (column + row) % 16
                                for column, row in
                                [(0, 0), (1, 0), (159, 0), (0, 111),
                                 (159, 111), (80, 56), (103, 102)]})
    disown(session)


def test_graphics_a_new_owner_can_fill_every_image_slot(session):
    boot(session, GRAPHICS_LINE)
    base = len(session.vdp.context_events())
    for _attempt in range(2):
        lines, _status = probe(session, "do open fill")
        assert lines == [driver_line(session), OPENED,
                         "gfxprobe: fill 16 out of handles"], lines
    session.vdp.wait_context_events(base + 6)
    images = [buffer_id for buffer_id in session.vdp.buffers
              if 0xF200 <= buffer_id < 0xF210]
    assert images == [], images


def paint_quarters(left, top):
    """Return records that paint a 20x20 block of four colours.

    @param left The block's left column.
    @param top The block's top row.
    """
    return (gr.fg(gr.RED) + gr.rect(left, top, 10, 10)
            + gr.fg(gr.GREEN) + gr.rect(left + 10, top, 10, 10)
            + gr.fg(gr.BLUE) + gr.rect(left, top + 10, 10, 10)
            + gr.fg(gr.YELLOW) + gr.rect(left + 10, top + 10, 10, 10))


def expected_copy(delta_x, delta_y):
    """Return the 40x40 neighbourhood of a block painted by paint_quarters
    at (10, 10) and then copied by an offset.

    @param delta_x How far right the copy goes.
    @param delta_y How far down the copy goes.

    @return Rows of palette indices.
    """
    area = [[gr.BLACK] * 40 for _ in range(40)]
    for row in range(20):
        for column in range(20):
            colour = [[gr.RED, gr.GREEN], [gr.BLUE, gr.YELLOW]][row // 10][
                column // 10]
            area[10 + row][10 + column] = colour
    copied = [cells[10:30] for cells in area[10:30]]
    for row in range(20):
        for column in range(20):
            area[10 + delta_y + row][10 + delta_x + column] = \
                copied[row][column]
    return area


COPY_OFFSETS = [(5, 3), (-5, 3), (5, -3), (-5, -3)]


def test_graphics_overlapping_copies_move_exact_pixels(session):
    records = b""
    for index, (delta_x, delta_y) in enumerate(COPY_OFFSETS):
        left = 100 + 50 * index
        records += paint_quarters(left, 100)
        records += gr.copy_rect((left, 100, 20, 20), left + delta_x,
                                100 + delta_y)
    submitted(session, {"a": records})
    for index, offset in enumerate(COPY_OFFSETS):
        assert session.vdp.pixels(90 + 50 * index, 90, 40, 40) == \
            expected_copy(*offset), offset
    disown(session)


def test_graphics_copies_are_clipped_and_cut_to_the_screen(session):
    records = (gr.fg(gr.RED) + gr.rect(50, 300, 10, 10)
               + gr.push_scissor(0, 320, 640, 160)
               + gr.copy_rect((50, 300, 10, 10), 55, 318) + gr.pop_scissor()
               + gr.fg(gr.GREEN) + gr.rect(0, 200, 10, 10)
               + gr.copy_rect((-5, 200, 10, 10), 100, 200))
    submitted(session, {"a": records})
    assert_pixels(session.vdp, {(57, 319): gr.BLACK, (57, 320): gr.RED,
                          (64, 327): gr.RED, (64, 328): gr.BLACK,
                          (104, 200): gr.BLACK, (105, 200): gr.GREEN,
                          (109, 209): gr.GREEN, (110, 200): gr.BLACK})
    disown(session)


def test_graphics_scrolling_clears_the_uncovered_strip(session):
    records = (gr.fg(gr.RED) + gr.rect(300, 300, 40, 20)
               + gr.fg(gr.BLUE) + gr.rect(300, 320, 40, 20)
               + gr.fg(gr.RED) + gr.rect(400, 300, 20, 40)
               + gr.fg(gr.BLUE) + gr.rect(420, 300, 20, 40)
               + gr.bg(gr.GREEN) + gr.scroll_rect((300, 300, 40, 40), 0, 10)
               + gr.scroll_rect((400, 300, 40, 40), -10, 0))
    submitted(session, {"a": records})
    column = [row[0] for row in session.vdp.pixels(310, 299, 1, 42)]
    assert column == ([gr.BLACK] + [gr.GREEN] * 10 + [gr.RED] * 20
                      + [gr.BLUE] * 10 + [gr.BLACK]), column
    assert session.vdp.pixels(399, 310, 42, 1)[0] == (
        [gr.BLACK] + [gr.RED] * 10 + [gr.BLUE] * 20 + [gr.GREEN] * 10
        + [gr.BLACK])
    disown(session)


def test_graphics_a_preload_draws_at_each_origin_it_is_called_at(session):
    files = {"a": gr.fg(gr.RED) + gr.rect(0, 0, 10, 10),
             "b": gr.origin(100, 100) + gr.call(0xF300) + gr.origin(200, 150)
             + gr.call(0xF300) + gr.origin(0, 0) + gr.rect(300, 300, 2, 2)}
    boot_with_files(session, files)
    lines = own(session, "preload /etc/a submit /etc/b")
    assert lines[2:] == ["gfxprobe: preload no error 0xf300",
                         "gfxprobe: submit no error",
                         "gfxprobe: finish no error"], lines
    assert_pixels(session.vdp, {(100, 100): gr.RED, (109, 109): gr.RED,
                          (110, 110): gr.BLACK, (200, 150): gr.RED,
                          (209, 159): gr.RED, (0, 0): gr.BLACK,
                          # The owner's own foreground, not the preload's.
                          (300, 300): gr.WHITE})
    disown(session)


def test_graphics_a_preload_may_not_change_the_clip_or_origin(session):
    files = {"a": gr.origin(1, 1) + gr.rect(0, 0, 1, 1),
             "b": gr.push_scissor(0, 0, 10, 10) + gr.rect(0, 0, 1, 1)}
    boot_with_files(session, files)
    lines = own(session, "preload /etc/a preload /etc/b")
    assert lines[2:] == ["gfxprobe: preload invalid state 0x0",
                         "gfxprobe: preload invalid state 0x0",
                         "gfxprobe: finish no error"], lines
    disown(session)


def test_graphics_finish_waits_for_the_vdp(session):
    boot_with_files(session, {"a": gr.bg(gr.RED) + gr.clear()})
    session.vdp.poll_delay = 0.4
    session.child.sendline("dirtest system gfxprobe do open submit /etc/a "
                           "finish hold")
    session.child.expect(FINISHED, timeout=30)
    assert session.child.match.group(1) == "no error"
    waited = int(session.child.match.group(2))
    assert 400 <= waited < 5000, waited
    assert_pixels(session.vdp, {(0, 0): gr.RED, (639, 479): gr.RED})
    disown(session)


def test_graphics_a_capture_draws_back_the_same_pixels(session):
    files = {"a": gr.fg(gr.RED) + gr.rect(10, 10, 4, 3) + gr.fg(gr.BLUE)
             + gr.rect(14, 10, 4, 3) + gr.fg(gr.YELLOW)
             + gr.rect(10, 13, 8, 3),
             "b": gr.push_scissor(290, 290, 40, 40) + gr.bg(gr.CYAN)
             + gr.clear() + gr.pop_scissor() + gr.image(0xF200, 300, 300),
             "s": b"submit /etc/a capture 10 10 8 6 size 0xf200 submit /etc/b"}
    boot_with_files(session, files)
    lines = own(session, "@/etc/s")
    assert lines[2:] == ["gfxprobe: submit no error",
                         "gfxprobe: capture no error 0xf200",
                         "gfxprobe: size no error 8x6",
                         "gfxprobe: submit no error",
                         "gfxprobe: finish no error"], lines
    size = session.vdp.bitmaps[0xF200][:2]
    assert size == (8, 6), size
    assert session.vdp.pixels(300, 300, 8, 6) == \
        session.vdp.pixels(10, 10, 8, 6)
    assert session.vdp.pixels(10, 13, 8, 1)[0] == [gr.YELLOW] * 8
    assert_pixels(session.vdp, {(308, 300): gr.CYAN, (300, 306): gr.CYAN})
    disown(session)


def test_graphics_an_owners_images_and_preloads_are_freed_when_it_exits(session):
    boot_with_files(session, {"a": gr.rect(0, 0, 1, 1)})
    base = len(session.vdp.context_events())
    lines, _status = probe(session,
                           "do open image rgba2222 1 1 - preload /etc/a")
    assert lines[2:] == ["gfxprobe: image no error 0xf200",
                         "gfxprobe: preload no error 0xf300"], lines
    session.vdp.wait_context_events(base + 3)
    written = {buffer_id for buffer_id, _length in session.vdp.buffer_writes}
    assert {0xF200, 0xF300} <= written, written
    assert (0xF200 not in session.vdp.buffers
            and 0xF300 not in session.vdp.buffers), \
        sorted(session.vdp.buffers)
    assert 0xF200 not in session.vdp.bitmaps


def test_graphics_revoking_answers_a_waiting_finish(session):
    boot_with_files(session, {})
    session.vdp.poll_delay = 3
    base = len(session.vdp.context_events())
    session.child.sendline("dirtest system gfxprobe do open finish hold")
    session.vdp.wait_context_events(base + 1)
    time.sleep(0.5)
    session.vdp.release_display()
    session.child.expect(FINISHED, timeout=30)
    assert session.child.match.group(1) == "not the display owner"
    session.child.expect(r"gfxprobe: revoked\r*\n", timeout=30)
    session.child.expect(PROMPT, timeout=30)
    # The revoked finish's poll is answered while the next one waits, and
    # mustn't end it.
    session.vdp.poll_delay = 5
    lines, _status = probe(session, "do open finish close")
    assert lines[:2] == [driver_line(session), OPENED], lines
    match = re.match(r"gfxprobe: finish no error (\d+) ms$", lines[2])
    assert match and int(match.group(1)) >= 4000, lines
    assert lines[3:] == ["gfxprobe: close no error"], lines
    assert session.vdp.errors == [], session.vdp.errors


def test_graphics_killing_an_owner_waiting_on_finish_frees_the_display(session):
    boot_with_files(session, {})
    vdp_login(session.vdp)
    session.vdp.poll_delay = 3
    base = len(session.vdp.context_events())
    session.vdp.type("dirtest system gfxprobe do open finish\r")
    session.vdp.wait_context_events(base + 1)
    match = re.search(r"^(\d+) +\S+ gfxprobe$", session.sh("ps"), re.M)
    assert match, "gfxprobe isn't in ps"
    pid = int(match.group(1))
    assert session.sh("kill %d" % pid).splitlines()[0] == \
        "Termination of process %d successful." % pid
    session.vdp.wait_context_events(base + 3)
    time.sleep(3)
    lines, _status = probe(session, "do open finish close")
    assert lines[:2] == [driver_line(session), OPENED], lines
    assert re.match(r"gfxprobe: finish no error \d+ ms$", lines[2]), lines
    assert lines[3:] == ["gfxprobe: close no error"], lines
    assert session.vdp.errors == [], session.vdp.errors
    assert message_args_reports(session) == []


def test_graphics_the_display_is_busy_until_the_driver_has_set_it_up(session):
    session.restart_with_vdp(config_sys=VDP_CONFIG_SYS + GRAPHICS_LINE,
                             answer_after=12)
    session.login()
    lines, _status = probe(session, "do open")
    assert lines[1:] == ["gfxprobe: open display busy"], lines
    wait_set_up(session.vdp, timeout=40)
    lines, _status = probe(session, "do open close")
    assert lines[1:] == [OPENED, "gfxprobe: close no error"], lines
    assert session.vdp.errors == [], session.vdp.errors


READY = "gfxprobe: ready"
EVENTS = r"gfxprobe: events \d+\r*\n"


def start_owner(session, steps):
    """Run gfxprobe on UART1 to open the display, take the pointer event the
    driver's move of the pointer to the middle of the screen sends, print
    "ready", and carry out steps, waiting until it is ready.

    @param session The session.
    @param steps The steps after "ready".
    """
    session.child.sendline("dirtest system gfxprobe do open events 1 5000 "
                           "print ready " + steps)
    session.child.expect(re.escape(READY) + r"\r*\n", timeout=30)
    lines = [line.strip() for line in session.child.before.splitlines()
             if line.strip().startswith("gfxprobe:")]
    assert lines[-2:] == [CENTRE, "gfxprobe: events 1"], lines


def printed(session, pattern):
    """Wait for gfxprobe to print a line matching a pattern.

    @param session The session.
    @param pattern The pattern.

    @return The gfxprobe lines since the last line waited for, up to and
      including the one that matched.
    """
    session.child.expect(pattern, timeout=30)
    text = session.child.before + session.child.match.group(0)
    return [line.strip() for line in text.splitlines()
            if line.strip().startswith("gfxprobe:")]


def pointer(point_x, point_y, buttons=0, wheel=0):
    """Return the line gfxprobe prints for a pointer event.

    @param point_x The column.
    @param point_y The row.
    @param buttons The button bits.
    @param wheel The wheel delta.
    """
    return (f"gfxprobe: pointer {point_x} {point_y} buttons {buttons} "
            f"wheel {wheel}")


CENTRE = pointer(320, 240)


def test_graphics_pointer_moves_and_clicks_arrive_exactly(session):
    boot_with_files(session, {})
    start_owner(session, "events 4 10000 hold")
    session.vdp.mouse(100, 200)
    session.vdp.mouse(101, 201, buttons=1)
    session.vdp.mouse(102, 202, buttons=0, wheel=-2)
    session.vdp.mouse(639, 479, buttons=4, wheel=3)
    assert printed(session, EVENTS) == [
        pointer(100, 200), pointer(101, 201, 1), pointer(102, 202, 0, -2),
        pointer(639, 479, 4, 3), "gfxprobe: events 4"]
    disown(session)


def test_graphics_motion_while_the_owner_is_busy_arrives_coalesced(session):
    boot_with_files(session, {})
    start_owner(session, "sleep 2 events 3 1500 hold")
    session.vdp.mouse(1, 2)
    # The first move goes out at once; the rest wait behind it, arriving at a
    # mouse's 200 reports a second.
    time.sleep(0.5)
    for step in range(100):
        session.vdp.mouse(step * 3, step * 2)
        time.sleep(0.005)
    assert printed(session, EVENTS) == [
        pointer(1, 2), pointer(297, 198), "gfxprobe: events 2"]
    disown(session)


def test_graphics_keys_typed_while_owned_arrive_as_key_events(session):
    boot_with_files(session, {})
    vdp_login(session.vdp)
    start_owner(session, "events 4 10000 hold")
    session.vdp.key("a")
    session.vdp.key("b")
    assert printed(session, EVENTS) == [
        f"gfxprobe: key 97 keycode {KEYCODE_a} modifiers 0x0 down 1",
        f"gfxprobe: key 97 keycode {KEYCODE_a} modifiers 0x0 down 0",
        f"gfxprobe: key 98 keycode {KEYCODE_a + 1} modifiers 0x0 down 1",
        f"gfxprobe: key 98 keycode {KEYCODE_a + 1} modifiers 0x0 down 0",
        "gfxprobe: events 4"]
    disown(session)
    session.vdp.type("echo after\r")
    session.vdp.expect(r"\r\nafter\r\n" + PROMPT)
    assert "abecho" not in session.vdp.screen, session.vdp.screen


def test_graphics_after_closing_keys_reach_the_shell_and_the_mouse_is_ignored(
    session
):
    boot_with_files(session, {})
    vdp_login(session.vdp)
    start_owner(session, "close print closed events 1 1500")
    session.child.expect(r"gfxprobe: closed\r*\n", timeout=30)
    session.vdp.mouse(5, 6)
    session.vdp.type("echo back\r")
    assert printed(session, EVENTS) == ["gfxprobe: events 0"]
    session.vdp.expect(r"\r\nback\r\n" + PROMPT)
    session.child.expect(PROMPT, timeout=30)
    assert session.vdp.errors == [], session.vdp.errors


def test_graphics_an_owner_that_exits_with_events_unread_leaks_nothing(
    session
):
    boot_with_files(session, {})
    start_owner(session, "sleep 2")
    session.vdp.mouse(7, 8)
    session.child.expect(r"system spawn 0 status 0\r*\n", timeout=30)
    session.child.expect(PROMPT, timeout=30)
    start_owner(session, "events 1 10000 hold")
    session.vdp.mouse(9, 10)
    assert printed(session, EVENTS) == [pointer(9, 10), "gfxprobe: events 1"]
    disown(session)


def test_graphics_a_revoked_owner_sitting_on_events_holds_up_nobody(session):
    boot_with_files(session, {})
    vdp = session.vdp
    hold_on_vdp(session, "open sleep 6")
    vdp.mouse(7, 8)
    time.sleep(0.5)
    vdp.release_display()
    # The wait ends well before the first probe wakes.
    start_owner(session, "events 1 2000 hold")
    vdp.mouse(9, 10)
    assert printed(session, EVENTS) == [pointer(9, 10), "gfxprobe: events 1"]
    disown(session)
    # The first probe exits once it wakes, which completes what it sat on,
    # and the next graphics message frees it.
    vdp.expect(r"system spawn 0 status 0\r\n" + PROMPT)
    start_owner(session, "close")
    session.child.expect(PROMPT, timeout=30)
    assert message_args_reports(session) == []


# A 4x4 Graphics.h RGBA2222 cursor, red in its top bits and alpha in its
# bottom two:  a white diagonal edged in black on a transparent ground.
WHITE_2222 = 0xFF
BLACK_2222 = 0x03
CURSOR_PIXELS = bytes([WHITE_2222, BLACK_2222, 0, 0,
                       BLACK_2222, WHITE_2222, BLACK_2222, 0,
                       0, BLACK_2222, WHITE_2222, BLACK_2222,
                       0, 0, BLACK_2222, WHITE_2222])
CURSOR_ROWS = ["WB..", "BWB.", ".BWB", "..BW"]


def test_graphics_an_image_becomes_the_cursor_with_its_hotspot(session):
    boot_with_files(session, {"p": CURSOR_PIXELS})
    driver_cursor = session.vdp.pointer_image()
    lines = own(session, "image rgba2222 4 4 /etc/p cursor 0xf200 2 1")
    assert lines[2:] == ["gfxprobe: image no error 0xf200",
                         "gfxprobe: cursor no error",
                         "gfxprobe: finish no error"], lines
    assert session.vdp.pointer_image() == CURSOR_ROWS
    assert session.vdp.pointer_hotspot() == (2, 1)
    disown(session)
    assert session.vdp.pointer_image() == driver_cursor
    assert len(driver_cursor) == 18


def mouse_disables(vdp):
    """Count the times the mouse was disabled.

    @param vdp The FakeVdp.
    """
    with vdp.lock:
        return sum(1 for _t, kind, detail in vdp.events
                   if (kind, detail) == ("mouse", "disable"))


def test_graphics_hiding_the_cursor_never_disables_the_mouse(session):
    boot_with_files(session, {
        "p": CURSOR_PIXELS,
        "s": b"image rgba2222 4 4 /etc/p cursor 0xf200 0 0 show 0 finish "
             b"print hidden events 1 10000 show 1 finish print shown "
             b"events 1 10000 show 0 finish print hidden hold"})
    disables = mouse_disables(session.vdp)
    driver_cursor = session.vdp.pointer_image()
    start_owner(session, "@/etc/s")
    session.child.expect(r"gfxprobe: hidden\r*\n", timeout=30)
    assert session.vdp.pointer_image() == ["."]
    session.vdp.mouse(1, 1)
    session.child.expect(r"gfxprobe: shown\r*\n", timeout=30)
    assert session.vdp.pointer_image() == CURSOR_ROWS
    session.vdp.mouse(2, 2)
    session.child.expect(r"gfxprobe: hidden\r*\n", timeout=30)
    assert session.vdp.pointer_image() == ["."]
    assert mouse_disables(session.vdp) == disables
    # Released while hidden, the console gets the driver's cursor back.
    disown(session)
    assert session.vdp.pointer_image() == driver_cursor


def test_graphics_warping_moves_the_pointer_and_reports_it(session):
    boot_with_files(session, {})
    start_owner(session, "warp 300 200 events 1 10000 hold")
    assert printed(session, EVENTS) == [
        "gfxprobe: warp no error", pointer(300, 200), "gfxprobe: events 1"]
    assert session.vdp.mouse_position == (300, 200)
    disown(session)


def test_graphics_destroying_the_cursors_image_brings_back_the_drivers(
    session
):
    boot_with_files(session, {
        "p": CURSOR_PIXELS,
        "s": b"image rgba2222 4 4 /etc/p cursor 0xf201 0 0 cursor 0xf200 0 0 "
             b"destroy 0xf200"})
    driver_cursor = session.vdp.pointer_image()
    lines = own(session, "@/etc/s")
    assert lines[2:] == ["gfxprobe: image no error 0xf200",
                         "gfxprobe: cursor invalid handle",
                         "gfxprobe: cursor no error",
                         "gfxprobe: destroy no error",
                         "gfxprobe: finish no error"], lines
    assert session.vdp.pointer_image() == driver_cursor
    disown(session)
