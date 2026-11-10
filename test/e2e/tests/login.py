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

"""End-to-end tests of login and the passwd file."""

import os
import re
import time

import pexpect

from harness import PROMPT, promptParts, REPO_ROOT


def test_login_root(s):
    s.login("root", "rootroot")


def test_login_rejects_bad_password(s):
    s.wait_login_prompt()
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("wrongpassword")
    idx = s.child.expect([PROMPT, "login: ", pexpect.TIMEOUT], timeout=10)
    assert idx != 0, "bad password produced a shell prompt"


def failedLoginDelay(s, user, password):
    """Return the seconds between "Login failed!" and the next login prompt."""
    s.wait_login_prompt()
    s.child.sendline(user)
    s.child.expect("password: ", timeout=10)
    s.child.sendline(password)
    s.child.expect("Login failed!", timeout=10)
    start = time.monotonic()
    s.child.expect("login: ", timeout=15)
    return time.monotonic() - start


def test_login_sleeps_after_a_bad_password(s):
    delay = failedLoginDelay(s, "root", "wrongpassword")
    assert 2.9 <= delay < 4.5, f"failed login took {delay:.3f} s"


def test_login_sleeps_after_a_bad_username(s):
    delay = failedLoginDelay(s, "nosuchuser", "rootroot")
    assert 2.9 <= delay < 4.5, f"failed login took {delay:.3f} s"


def passwdChecksum(user, password):
    """Return the password field login accepts for user and password."""
    return str(sum(map(ord, user + password)))


PASSWD_FILE = (
    "bad:x:x:0:Bad UID:/root:/usr/bin/mush\n"
    "root:x:0:0:Root User:/root:/usr/bin/mush\n"
    "big:x:7:7:%s:/root:/usr/bin/mush\n"
    "alice:x:5:5:Alice:/root:/usr/bin/mush\n"
) % ("g" * 100)

SHADOW_FILE = "root:%s\nalice:%s\n" % (
    passwdChecksum("root", "toor"), passwdChecksum("alice", "wonderland"))


def restart_with_users(s, passwd=PASSWD_FILE, shadow=SHADOW_FILE):
    """Boot again with a passwd file and, if given, a shadow file."""
    if shadow is not None:
        s.write_file("/secure/shadow", shadow)
    s.restart_with_file("/etc/passwd", passwd)


def test_login_uses_the_passwd_file_when_there_is_one(s):
    restart_with_users(s)
    s.login("alice", "wonderland")
    user, _ = promptParts(s)
    assert user == "alice", f"prompt user is {user!r}"
    out = s.sh("ps")
    assert re.search(r"^\d+  alice ps$", out, re.M), out
    assert s.sh("echo back") == "back"
    user, _ = promptParts(s)
    assert user == "alice", f"prompt user after a command is {user!r}"


def test_passwd_line_with_a_bad_uid_matches_no_user(s):
    restart_with_users(s)
    s.restart_with_config_sys("CONSOLE=UART1 /userspace/usr/bin/ps\n")
    s.child.expect(r"\d+  (\S+) ps\r*\n", timeout=15)
    assert s.child.match.group(1) == "unknown", s.child.match.group(1)


def test_passwd_file_replaces_the_built_in_users(s):
    restart_with_users(s)
    for user, password in (("root", "rootroot"), ("user1", "user1user1")):
        s.wait_login_prompt()
        s.child.sendline(user)
        s.child.expect("password: ", timeout=10)
        s.child.sendline(password)
        s.child.expect("Login failed!", timeout=10)
    s.login("root", "toor")


def test_shipped_passwd_file_matches_the_built_in_users(s):
    with open(os.path.join(REPO_ROOT, "etc", "passwd")) as passwd, \
            open(os.path.join(REPO_ROOT, "etc", "secure", "shadow")) as shadow:
        restart_with_users(s, passwd.read(), shadow.read())
    for user in ("root", "user1", "user2"):
        s.login(user, user * 2)
        assert promptParts(s)[0] == user
        s.child.sendline("exit")


def assert_login_fails(s, user, password):
    s.wait_login_prompt()
    s.child.sendline(user)
    s.child.expect("password: ", timeout=10)
    s.child.sendline(password)
    s.child.expect("Login failed!", timeout=10)


def test_a_checksum_in_the_passwd_file_does_not_authenticate(s):
    field = passwdChecksum("alice", "editedpassword")
    restart_with_users(
        s, "alice:%s:5:5:Alice:/root:/usr/bin/mush\n" % field)
    assert_login_fails(s, "alice", "editedpassword")
    s.login("alice", "wonderland")


def test_a_user_with_no_shadow_line_cannot_log_in(s):
    restart_with_users(s, "carol:x:6:6:Carol:/root:/usr/bin/mush\n")
    assert_login_fails(s, "carol", "carolcarol")


def test_no_one_can_log_in_without_a_shadow_file(s):
    restart_with_users(s, shadow=None)
    assert_login_fails(s, "root", "rootroot")


def test_passwd_entry_too_long_for_the_buffer_is_an_error(s):
    restart_with_users(s)
    s.wait_login_prompt()
    s.child.sendline("big")
    s.child.expect("getpwnam_r returned status (\\d+)", timeout=10)
    assert s.child.match.group(1) == "16", s.child.match.group(1)
