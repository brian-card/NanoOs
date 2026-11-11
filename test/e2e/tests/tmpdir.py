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

"""End-to-end tests of where vi and ed keep their temporary files."""

import os
import re
import subprocess

from harness import PROMPT, ed_session
from tests.capabilities import HOMES, boot_with, boot_with_home


def image_files(s, directory):
    """Stop the simulator and list the files in a directory of the image."""
    if s.child.isalive():
        s.child.terminate(force=True)
    out = subprocess.run(["mdir", "-b", "-i", s.img_copy + "@@2097152",
                          "::/userspace" + directory],
                         capture_output=True, text=True,
                         env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
    return sorted(os.path.basename(line.rstrip("/"))
                  for line in out.stdout.splitlines() if line.strip())


def start_ed(s):
    """Start ed on /etc/hostname and wait until it has read the file."""
    s.child.sendline("ed /etc/hostname")
    s.child.expect(r"\n7\r*\n", timeout=15)


def ed_files(names):
    """Return the ed temporary files among a directory's names."""
    return [name for name in names if re.fullmatch(r"ed\d+(\.b)?", name)]


def test_login_sets_tmpdir_to_a_directory_in_home(s):
    boot_with(s, directories=HOMES)
    s.login("user1", "user1user1")
    assert s.sh("echo $TMPDIR") == "/home/user1/tmp"
    assert "tmp/" in s.sh("ll /home/user1")


def test_a_user_who_fails_the_home_check_gets_tmp(s):
    boot_with_home(s, "/etc")
    assert s.sh("echo $TMPDIR") == "/tmp"


def test_two_users_editing_at_once_get_separate_files_in_their_homes(s):
    boot_with(s, directories=HOMES)
    vdp = s.restart_with_vdp()
    s.login("user1", "user1user1")
    start_ed(s)
    vdp.expect("login: ")
    vdp.type("user2\r")
    vdp.expect("password: ")
    vdp.type("user2user2\r")
    vdp.expect(PROMPT)
    vdp.type("ed /etc/hostname\r")
    vdp.expect(r"\n7\r?\n")
    user1 = ed_files(image_files(s, "/home/user1/tmp"))
    user2 = ed_files(image_files(s, "/home/user2/tmp"))
    assert len(user1) == 2 and len(user2) == 2, (user1, user2)
    assert user1[0] != user2[0], (user1, user2)
    assert ed_files(image_files(s, "/tmp")) == []


def test_ed_uses_tmpdir_when_it_is_set(s):
    boot_with(s, directories=HOMES + ("/home/user1/scratch",))
    s.login("user1", "user1user1")
    s.sh("export TMPDIR=/home/user1/scratch")
    start_ed(s)
    assert len(ed_files(image_files(s, "/home/user1/scratch"))) == 2
    assert ed_files(image_files(s, "/home/user1/tmp")) == []


def test_ed_removes_its_files_when_it_quits(s):
    boot_with(s, directories=HOMES)
    s.login("user1", "user1user1")
    ed_session(s, [("ed /etc/hostname", "7\n"), ("q", "")])
    assert ed_files(image_files(s, "/home/user1/tmp")) == []


def test_without_tmpdir_or_home_ed_uses_tmp(s):
    s.restart_with_config_sys("CONSOLE=UART1 /userspace/usr/bin/ed "
                              "/etc/hostname\n")
    s.child.expect(r"dynamic memory", timeout=15)
    s.child.expect(r"7\r*\n", timeout=15)
    assert len(ed_files(image_files(s, "/tmp"))) == 2


def test_with_an_empty_tmpdir_ed_uses_tmp_in_home(s):
    boot_with(s, directories=HOMES)
    s.login("user1", "user1user1")
    s.sh("export TMPDIR=")
    assert s.sh("echo [$TMPDIR]") == "[]"
    start_ed(s)
    assert len(ed_files(image_files(s, "/home/user1/tmp"))) == 2
    assert ed_files(image_files(s, "/home/user1")) == []
