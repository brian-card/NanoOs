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

"""End-to-end tests of console sessions, through sessiontest:  the window
server, registered from config.sys's GRAPHICS line, bridges a session to the
console port it runs on."""

import re
import time

from harness import PROMPT, VDP_CONFIG_SYS, ps_entries, vdp_login
from tests.ps import levels
from tests.vi import ViTerminal, vi_message


SESSION_CONFIG_SYS = (
    VDP_CONFIG_SYS
    + "GRAPHICS=/system/drivers/vdp.drv /system/commands/sessiontest\n")

OPENED = r"sessiontest: opened port (\d+) shell (\d+)"


def boot(s):
    """Boot with sessiontest on the GRAPHICS line and log in on UART1.

    @param s The Session.
    """
    s.restart_with_vdp(config_sys=SESSION_CONFIG_SYS)
    s.login()


def open_session(s, size=""):
    """Run sessiontest on UART1 and wait for the session's prompt.

    @param s The Session, logged in on UART1.
    @param size The rows and columns to pass, or "" for the default.

    @return The session's port and its shell's process ID.
    """
    s.child.sendline(("dirtest system sessiontest 1 " + size).rstrip())
    s.child.expect(OPENED, timeout=15)
    port, shell = int(s.child.match.group(1)), int(s.child.match.group(2))
    s.child.expect(PROMPT, timeout=15)
    return port, shell


def end_session(s, port):
    """Wait for sessiontest to report the session's end and exit.

    @param s The Session.
    @param port The session's port.
    """
    s.child.expect(r"sessiontest: session ended port %d\r*\n" % port,
                   timeout=15)
    s.child.expect(r"system spawn 0 status 0\r*\n", timeout=15)
    s.child.expect(PROMPT, timeout=15)


def test_session_opens_a_shell_with_no_login(s):
    boot(s)
    port, shell = open_session(s)
    assert port == 2, port
    # ps runs in the shell's slot, as the shell's user.
    out = s.sh("ps -l")
    assert re.search(r"^%d  root supervisor ps$" % shell, out, re.M), out
    assert s.sh("pwd") == "/root"
    assert s.sh("echo $USER $HOME $PWD") == "root /root /root"
    s.child.sendline("exit")
    end_session(s, port)


def test_session_shell_and_its_commands_run_at_a_consoles_levels(s):
    boot(s)
    # The slot the session's shell takes was last used at user level.
    assert s.sh("looseLoop &") == ""
    pid = [p for p, _u, name in ps_entries(s) if name == "looseLoop"][0]
    assert s.sh("kill %d" % pid) == "Termination of process %d successful." % pid
    port, shell = open_session(s)
    # ps runs in the shell's slot, at the shell's level.
    out = s.sh("ps -l")
    assert re.search(r"^%d  root supervisor ps$" % shell, out, re.M), out
    assert s.sh("looseLoop &") == ""
    assert ("root", "user", "looseLoop") in levels(s.sh("ps -l"))

    vdp_login(s.vdp)
    s.child.sendline("looseLoop")
    time.sleep(1)
    s.vdp.type("ps -l\r")
    s.vdp.expect(r"%d  root supervisor looseLoop" % shell)
    s.vdp.expect(PROMPT)
    s.child.sendcontrol("c")
    s.child.expect(PROMPT, timeout=10)

    vi = ViTerminal(s, "vi /root/session.txt")
    vi.wait(lambda screen: vi_message(screen)
            == '"/root/session.txt" [New File]')
    vi.keys("ihello\x1b")
    vi.wait_lines(["hello"])
    vi.keys(":q!\r")
    vi.wait_exit()
    s.child.sendline("exit")
    end_session(s, port)


def test_a_resize_report_through_the_bridge_sets_the_sessions_size(s):
    boot(s)
    port, _shell = open_session(s)
    assert s.sh("termtest size") == "24 80"
    s.child.sendline("termtest report")
    s.child.expect_exact("\x1b[?2048h", timeout=10)
    s.child.send("\x1b[48;30;100;0;0t")
    s.child.expect_exact("\x1b[?2048l", timeout=10)
    s.child.expect(r"\A\^\[\[48;30;100;0;0t\r*\n" + PROMPT, timeout=10)
    assert s.sh("termtest size") == "30 100"
    s.child.sendline("exit")
    end_session(s, port)


def test_exiting_the_shell_ends_the_session_and_frees_its_port(s):
    boot(s)
    port, shell = open_session(s)
    s.child.sendline("exit")
    end_session(s, port)
    entries = ps_entries(s)
    assert "sessiontest" not in [name for _p, _u, name in entries], entries
    assert shell not in [pid for pid, _u, _name in entries], entries
    again, _shell = open_session(s)
    assert again == port
    s.child.sendline("exit")
    end_session(s, again)


def test_killing_the_bridge_ends_the_session_and_its_processes(s):
    boot(s)
    port, shell = open_session(s)
    assert s.sh("looseLoop &") == ""
    bridge = [p for p, _u, name in ps_entries(s) if name == "sessiontest"][0]
    s.child.sendline("kill %d" % bridge)
    # A killed program's exit status, as dirtest prints it, is 0.
    s.child.expect(r"system spawn 0 status 0\r*\n", timeout=15)
    s.child.expect(PROMPT, timeout=15)
    entries = ps_entries(s)
    names = [name for _p, _u, name in entries]
    assert "looseLoop" not in names, entries
    assert "sessiontest" not in names, entries
    assert shell not in [pid for pid, _u, _name in entries], entries
    again, _shell = open_session(s)
    assert again == port
    s.child.sendline("exit")
    end_session(s, again)


def test_a_session_is_refused_when_every_port_is_in_use(s):
    boot(s)
    out = s.sh("dirtest system sessiontest fill", timeout=60)
    lines = [l for l in out.splitlines() if l.startswith("sessiontest:")]
    opened = [int(m.group(1)) for m in
              (re.fullmatch(r"sessiontest: opened port (\d+)", l)
               for l in lines) if m]
    assert opened[:6] == [2, 3, 4, 5, 6, 7], lines
    assert lines == ["sessiontest: opened port %d" % p for p in range(2, 8)] + [
        "sessiontest: open failed: No space left on device",
        "sessiontest: ended port 2",
        "sessiontest: opened port 2",
        "sessiontest: ended 6 sessions"], lines
    assert re.search(r"^system spawn 0 status 0$", out, re.M), out
    entries = ps_entries(s)
    assert [name for _p, _u, name in entries].count("mush") == 0, entries
