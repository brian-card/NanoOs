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

"""End-to-end tests of the kernel's handling of /etc/config.sys."""

import pexpect

from harness import PROMPT


def test_config_sys_console_runs_its_program_instead_of_getty(s):
    s.restart_with_config_sys("CONSOLE=UART1 /usr/bin/echo from-config-sys\n")
    i = s.child.expect(["from-config-sys", "login: "], timeout=15)
    assert i == 0, "getty ran instead of the CONSOLE line's program"


def test_config_sys_console_port_name_ignores_case(s):
    s.restart_with_config_sys("console=uart1 /usr/bin/echo lower-case-port\n")
    i = s.child.expect(["lower-case-port", "login: "], timeout=15)
    assert i == 0, "getty ran instead of the CONSOLE line's program"


def test_config_sys_console_on_unknown_port_falls_back_to_getty(s):
    s.restart_with_config_sys("CONSOLE=UART7 /usr/bin/echo wrong-port\n")
    i = s.child.expect(["wrong-port", "login: "], timeout=15)
    assert i == 1, "the program for a port that doesn't exist ran"
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("rootroot")
    s.child.expect(PROMPT, timeout=10)


def test_config_sys_console_on_a_device_forwards_its_output(s):
    s.restart_with_config_sys(
        "DEVICE=/usr/lib/loopback.drv 1\n"
        "CONSOLE=/usr/lib/loopback.drv /usr/bin/echo via-forwarding-port\n")
    i = s.child.expect(["via-forwarding-port", "login: "], timeout=15)
    assert i == 0, "getty ran instead of the forwarded console's program"


def test_config_sys_uart_and_device_consoles_each_get_a_getty(s):
    s.restart_with_config_sys(
        "DEVICE=/usr/lib/loopback.drv 1\n"
        "CONSOLE=UART1 /usr/bin/getty\n"
        "CONSOLE=/usr/lib/loopback.drv /usr/bin/getty\n")
    seen = set()
    while seen != {"tty0", "tty1"}:
        s.child.expect(r"(tty\d)", timeout=15)
        seen.add(s.child.match.group(1))
    s.login()


def test_config_sys_hardware_grant_keeps_a_drivers_uart(s):
    s.restart_with_config_sys(
        "DEVICE=/usr/lib/loopback.drv 1\n"
        "HARDWARE=/usr/lib/loopback.drv UART1\n"
        "CONSOLE=/usr/lib/loopback.drv /usr/bin/echo granted-uart1\n")
    i = s.child.expect(["granted-uart1", "login: "], timeout=15)
    assert i == 0, "the driver's output didn't reach UART1"


def test_config_sys_hardware_grant_limits_a_driver_to_its_uart(s):
    s.restart_with_config_sys(
        "DEVICE=/usr/lib/loopback.drv 1\n"
        "HARDWARE=/usr/lib/loopback.drv UART0\n"
        "CONSOLE=UART1 /usr/bin/getty\n"
        "CONSOLE=/usr/lib/loopback.drv /usr/bin/echo granted-uart0\n")
    i = s.child.expect(["granted-uart0", "login: "], timeout=15)
    assert i == 1, "a driver granted only UART0 wrote to UART1"
    i = s.child.expect(["granted-uart0", pexpect.TIMEOUT], timeout=3)
    assert i == 1, "a driver granted only UART0 wrote to UART1"


def test_uart_console_left_out_of_config_sys_is_owned_by_no_one(s):
    # The memory manager owns every console port until it has printed its boot
    # message, so a port no shell takes must be released back to no one.
    vdp = s.restart_with_vdp(config_sys=(
        "DEVICE=/usr/lib/vdp.drv\n"
        "HARDWARE=/usr/lib/vdp.drv UART0\n"
        "CONSOLE=/usr/lib/vdp.drv /usr/bin/getty\n"))
    vdp.expect("login: ")
    s.child.sendcontrol("c")
    i = s.child.expect([r"Killing process \d+|Could not send message|"
                        r"Could not communicate", pexpect.TIMEOUT], timeout=3)
    assert i == 1, "Ctrl-C on the unused UART signalled " + s.child.after
