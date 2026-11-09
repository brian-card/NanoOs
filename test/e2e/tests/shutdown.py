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

"""End-to-end tests of shutdown."""

import pexpect

from tests.login import PASSWD_FILE


def test_shutdown_halts(s):
    s.login()
    s.child.sendline("shutdown -h")
    assert s.child.expect([pexpect.EOF, pexpect.TIMEOUT], timeout=10) == 0, \
        "shutdown -h did not terminate the simulator"


def reboot_twice(s, user="root", password="rootroot"):
    """Reboot from a login shell twice, then run a command.

    @param s The Session.
    @param user The user to log in as.
    @param password The user's password.
    """
    for _ in range(2):
        s.login(user, password)
        s.child.sendline("shutdown -r")
    s.login(user, password)
    assert s.sh("echo after-reboot") == "after-reboot"


def test_shutdown_reboots(s):
    reboot_twice(s)


def test_shutdown_reboots_with_a_passwd_file(s):
    s.restart_with_file("/etc/passwd", PASSWD_FILE)
    reboot_twice(s, "alice", "wonderland")


def test_shutdown_reboots_with_a_config_sys(s):
    s.restart_with_config_sys("DEVICE=/system/drivers/loopback.drv 1\n")
    reboot_twice(s)
