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

"""End-to-end tests of passwd and POSIX_SPAWN_SYSTEM."""

import os
import subprocess

import pexpect

from harness import PROMPT, promptParts
from tests.login import assert_login_fails, passwdChecksum

PASSWD_FILE = (
    "root:x:0:0:Root User:/root:/usr/bin/mush\n"
    "alice:x:5:5:Alice:/root:/usr/bin/mush\n"
    "bob:x:6:6:Bob:/root:/usr/bin/mush\n"
    "carol:x:7:7:Carol:/root:/usr/bin/mush\n"
)

# The long line is longer than passwd's 64-byte copy buffer and has "bob:" just
# where its second piece starts, and the last line has no newline, so both have
# to be copied exactly as they are.
SHADOW_FILE = (
    "root:%s\n"
    "alice:%s\n"
    "padding:%s\n"
    "bob:%s"
) % (passwdChecksum("root", "toor"), passwdChecksum("alice", "wonderland"),
     "9" * 55 + "bob:" + "9" * 20, passwdChecksum("bob", "builder"))


def boot(s):
    """Boot again with PASSWD_FILE and SHADOW_FILE."""
    s.write_file("/secure/shadow", SHADOW_FILE)
    s.restart_with_file("/etc/passwd", PASSWD_FILE)


def shadow_after_shutdown(s):
    """Shut the simulator down and return /secure/shadow from the image."""
    s.child.sendline("shutdown -h")
    s.child.expect(pexpect.EOF, timeout=10)
    out = subprocess.run(["mtype", "-i", s.img_copy + "@@2097152",
                          "::/secure/shadow"], capture_output=True,
                         env=dict(os.environ, MTOOLS_SKIP_CHECK="1"),
                         check=True)
    return out.stdout.decode()


def change_password(s, command, old, new, again=None):
    """Run a passwd command and answer its prompts.

    @param s The Session.
    @param command The command line to run.
    @param old The current password to give, or None if none is asked for.
    @param new The new password.
    @param again The new password typed the second time, or new if None.

    @return The output from the first prompt to the shell prompt.
    """
    s.child.sendline(command)
    s.child.expect_exact(command, timeout=10)
    first = "Current password: " if old is not None else "New password: "
    idx = s.child.expect_exact([first, "Current password: ", "New password: "],
                               timeout=10)
    assert idx == 0, "passwd asked for the wrong password first"
    output = s.child.after
    if old is not None:
        s.child.sendline(old)
        s.child.expect_exact("New password: ", timeout=10)
        output += s.child.before + s.child.after
    s.child.sendline(new)
    s.child.expect_exact("Retype new password: ", timeout=10)
    output += s.child.before + s.child.after
    s.child.sendline(new if again is None else again)
    s.child.expect(PROMPT, timeout=15)
    return (output + s.child.before).replace("\r", "")


def test_a_user_changes_their_own_password(s):
    boot(s)
    s.login("alice", "wonderland")
    out = change_password(s, "passwd", "wonderland", "looking-glass")
    assert "passwd: password updated" in out, out
    for secret in ("wonderland", "looking-glass"):
        assert secret not in out, f"{secret!r} was echoed:\n{out}"
    s.child.sendline("exit")
    assert_login_fails(s, "alice", "wonderland")
    s.login("alice", "looking-glass")
    assert promptParts(s)[0] == "alice"


def test_a_password_can_be_changed_twice_in_one_boot(s):
    boot(s)
    s.login("alice", "wonderland")
    change_password(s, "passwd", "wonderland", "looking-glass")
    out = change_password(s, "passwd", "looking-glass", "rabbit-hole")
    assert "passwd: password updated" in out, out
    s.child.sendline("exit")
    s.login("alice", "rabbit-hole")


def test_only_the_users_line_in_shadow_changes(s):
    boot(s)
    s.login("alice", "wonderland")
    change_password(s, "passwd", "wonderland", "looking-glass")
    expected = SHADOW_FILE.replace(
        "alice:%s\n" % passwdChecksum("alice", "wonderland"),
        "alice:%s\n" % passwdChecksum("alice", "looking-glass"))
    assert shadow_after_shutdown(s) == expected


def test_a_wrong_current_password_changes_nothing(s):
    boot(s)
    s.login("alice", "wonderland")
    s.child.sendline("passwd")
    s.child.expect_exact("Current password: ", timeout=10)
    s.child.sendline("not-it")
    idx = s.child.expect_exact(["passwd: password incorrect", "New password: "],
                               timeout=10)
    assert idx == 0, "passwd asked for a new password after a wrong one"
    s.child.expect(PROMPT, timeout=10)
    assert shadow_after_shutdown(s) == SHADOW_FILE


def test_retyping_a_different_password_changes_nothing(s):
    boot(s)
    s.login("alice", "wonderland")
    out = change_password(s, "passwd", "wonderland", "one", "two")
    assert "passwd: the passwords don't match" in out, out
    assert shadow_after_shutdown(s) == SHADOW_FILE


def test_a_user_cannot_change_another_users_password(s):
    boot(s)
    s.login("alice", "wonderland")
    out = s.sh("passwd bob")
    assert out == "passwd: only root can change another user's password", out
    assert shadow_after_shutdown(s) == SHADOW_FILE


def test_root_changes_its_own_password_without_the_current_one(s):
    boot(s)
    s.login("root", "toor")
    out = change_password(s, "passwd", None, "rooted")
    assert "passwd: password updated" in out, out
    s.child.sendline("exit")
    assert_login_fails(s, "root", "toor")
    s.login("root", "rooted")


def test_root_changes_another_users_password_without_theirs(s):
    boot(s)
    s.login("root", "toor")
    out = change_password(s, "passwd bob", None, "fixer")
    assert "passwd: password updated" in out, out
    expected = SHADOW_FILE.replace(
        "bob:%s" % passwdChecksum("bob", "builder"),
        "bob:%s\n" % passwdChecksum("bob", "fixer"))
    assert shadow_after_shutdown(s) == expected


def test_root_can_give_a_password_to_a_user_with_no_shadow_line(s):
    boot(s)
    s.login("root", "toor")
    change_password(s, "passwd carol", None, "carolcarol")
    s.child.sendline("exit")
    s.login("carol", "carolcarol")
    assert promptParts(s)[0] == "carol"


def test_root_cannot_name_a_user_who_does_not_exist(s):
    boot(s)
    s.login("root", "toor")
    assert s.sh("passwd nobody") == "passwd: no such user"
    assert shadow_after_shutdown(s) == SHADOW_FILE


def test_a_stale_lock_is_cleared_at_boot(s):
    if s.child.isalive():
        s.child.terminate(force=True)
    subprocess.run(["mmd", "-i", s.img_copy + "@@2097152",
                    "::/secure/shadow.lock"],
                   env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)
    boot(s)
    s.login("alice", "wonderland")
    out = change_password(s, "passwd", "wonderland", "looking-glass")
    assert "passwd: password updated" in out, out


def system_spawn(s, args):
    """Run dirtest's system mode and return its result line."""
    return s.sh("dirtest system " + args).splitlines()[-1]


def test_a_system_command_must_be_a_bare_name(s):
    s.login()
    einval = "system spawn 5 status 0"
    for name in ("../bin/login", "/system/bin/login", "commands/passwd", ".."):
        assert system_spawn(s, name) == einval, name


def test_only_programs_in_system_commands_can_be_run_as_system(s):
    s.login()
    for name in ("login", "getty", "logger", "nosuchprogram"):
        assert system_spawn(s, name) == "system spawn 8 status 0", name


def test_a_users_set_is_unchanged_after_a_system_command(s):
    boot(s)
    s.login("alice", "wonderland")
    before = s.sh("dirtest caps")
    s.sh("passwd bob")
    assert s.sh("dirtest caps") == before


def test_a_replaced_passwd_cannot_change_another_users_password(s):
    if s.child.isalive():
        s.child.terminate(force=True)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    image = s.img_copy + "@@2097152"
    subprocess.run(["mdeltree", "-i", image, "::/userspace/usr/bin/passwd"],
                   env=env, check=True)
    subprocess.run(["mcopy", "-s", "-i", image, "::/userspace/usr/bin/dirtest",
                    os.path.join(s.tmp, "dirtest")], env=env, check=True)
    subprocess.run(["mcopy", "-s", "-i", image, os.path.join(s.tmp, "dirtest"),
                    "::/userspace/usr/bin/passwd"], env=env, check=True)
    boot(s)
    s.login("alice", "wonderland")
    out = s.sh("passwd system passwd bob")
    assert out.splitlines() == [
        "passwd: only root can change another user's password",
        "system spawn 0 status 1"], out
    assert shadow_after_shutdown(s) == SHADOW_FILE
