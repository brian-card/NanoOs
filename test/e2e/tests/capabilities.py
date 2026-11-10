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

"""End-to-end tests of the capabilities each login is given."""

import os
import subprocess

from harness import REPO_ROOT

DENIED = "Permission denied"
READ_SHADOW = "cat /secure/shadow"


def repo_file(*parts):
    with open(os.path.join(REPO_ROOT, *parts)) as source:
        return source.read()


def boot_with(s, files=None, directories=()):
    """Boot again with files and directories added to the image.

    @param s The Session.
    @param files A dict of absolute path to file contents.
    @param directories Absolute paths of directories to create.
    """
    if s.child.isalive():
        s.child.terminate(force=True)
    for directory in directories:
        subprocess.run(["mmd", "-D", "s", "-i", s.img_copy + "@@2097152",
                        "::/userspace" + directory],
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))
    last = None
    for path, text in (files or {}).items():
        s.write_file(path, text)
        last = path
    s.spawn([s.img_copy])


HOMES = ("/home", "/home/user1", "/home/user2")

# A supervisor file narrower than the compiled base set, which is everything
# under /userspace.
NARROW_BASE = (
    "supervisor rwcdlsx /usr/\n"
    "supervisor rwcdls /etc/\n"
    "supervisor rwcdlsx /home/\n"
    "supervisor rwcdls /root/\n"
    "supervisor rwcdls /tmp/\n"
)


def caps_of(s):
    """Return the running login's set as a dict of name to mask."""
    return {line.split()[2]: line.split()[1]
            for line in s.sh("dirtest caps").splitlines()
            if line.startswith("cap ")}


def test_a_plain_user_can_write_only_in_home_and_tmp(s):
    boot_with(s, directories=HOMES)
    s.login("user1", "user1user1")
    assert s.sh("touch /home/user1/mine") == ""
    assert s.sh("touch /tmp/mine") == ""
    for path in ("/etc/mine", "/usr/mine", "/usr/bin/mine",
                 "/home/user2/mine", "/root/mine"):
        assert DENIED in s.sh("touch " + path), path


def test_a_plain_user_has_exactly_the_default_set(s):
    boot_with(s, directories=HOMES)
    s.login("user1", "user1user1")
    assert caps_of(s) == {
        "/usr/bin/": "71", "/usr/lib/": "41", "/etc/": "31",
        "/home/user1/": "3f", "/tmp/": "3f",
    }


def test_root_has_exactly_the_default_set(s):
    s.login()
    assert caps_of(s) == {
        "/usr/": "7f", "/usr/bin/": "71", "/usr/lib/": "41", "/etc/": "3f",
        "/root/": "3f", "/tmp/": "3f",
    }


def test_no_login_can_read_the_protected_trees_or_list_the_root(s):
    boot_with(s, directories=HOMES)
    for user, password in (("root", "rootroot"), ("user1", "user1user1")):
        s.login(user, password)
        assert DENIED in s.sh(READ_SHADOW), user
        assert DENIED in s.sh("cat /system/data/capabilities"), user
        assert DENIED in s.sh("ll /"), user
        s.child.sendline("exit")


def test_root_can_edit_etc_and_add_a_program(s):
    s.login()
    assert s.sh("touch /etc/added") == ""
    assert s.sh("mkdir /usr/bin/hi") == ""
    assert s.sh("cp /usr/bin/helloworld/main.overlay /usr/bin/hi") == ""
    assert s.sh("hi") == "Hello, world!"


def install_hello_in_home(s):
    assert s.sh("mkdir /home/user1/bin") == ""
    assert s.sh("mkdir /home/user1/bin/hi") == ""
    assert s.sh("cp /usr/bin/helloworld/main.overlay /home/user1/bin/hi") \
        == ""


def test_a_plain_user_cannot_run_their_own_programs(s):
    boot_with(s, directories=HOMES)
    s.login("user1", "user1user1")
    install_hello_in_home(s)
    assert s.sh("/home/user1/bin/hi") == "/home/user1/bin/hi: " + DENIED


def test_a_user_with_x_on_their_bin_can_run_their_own_programs(s):
    boot_with(s, {"/etc/capabilities": repo_file("etc", "capabilities")
                  + "user1 rwcdlsx ~/bin/\n"}, HOMES)
    s.login("user1", "user1user1")
    install_hello_in_home(s)
    assert s.sh("/home/user1/bin/hi") == "Hello, world!"


def test_the_shipped_files_give_the_default_sets_and_menu(s):
    boot_with(s, {"/etc/capabilities": repo_file("etc", "capabilities"),
                  "/system/data/capabilities":
                  repo_file("etc", "system", "capabilities")},
              HOMES + ("/menu",))
    s.login()
    assert caps_of(s) == {
        "/usr/": "7f", "/usr/bin/": "71", "/usr/lib/": "41", "/etc/": "3f",
        "/root/": "3f", "/tmp/": "3f", "/menu/": "33",
    }


def test_root_can_grant_a_new_top_level_directory(s):
    if s.child.isalive():
        s.child.terminate(force=True)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    image = s.img_copy + "@@2097152"
    subprocess.run(["mmd", "-i", image, "::/userspace/apps"], env=env,
                   check=True)
    subprocess.run(["mcopy", "-s", "-i", image,
                    os.path.join(REPO_ROOT, "usr", "bin", "helloworld"),
                    "::/userspace/apps/hi"], env=env, check=True)
    boot_with(s, {"/etc/capabilities": repo_file("etc", "capabilities")
                  + "* rxls /apps/\n"}, HOMES)
    s.login("user1", "user1user1")
    assert caps_of(s)["/apps/"] == "71"
    assert s.sh("/apps/hi") == "Hello, world!"
    assert DENIED in s.sh("touch /apps/mine")
    assert DENIED in s.sh("ll /")


def test_etc_capabilities_cannot_reach_the_other_namespaces(s):
    boot_with(s, {"/etc/capabilities": repo_file("etc", "capabilities")
                  + "* rwcdlsx /secure/\n* rwcdlsx /system/\n"}, HOMES)
    s.login("user1", "user1user1")
    caps = caps_of(s)
    assert caps["/secure/"] == "7f" and caps["/system/"] == "7f", caps
    for command in (READ_SHADOW, "cat /system/data/capabilities",
                    "/system/bin/login"):
        out = s.sh(command)
        assert "No such" in out or "not found" in out, (command, out)
    assert "root:" not in s.sh(READ_SHADOW)


def test_a_line_for_another_user_gives_this_user_nothing(s):
    boot_with(s, {"/etc/capabilities": "user2 rwcdls /etc/\n"
                  + repo_file("etc", "capabilities")}, HOMES)
    s.login("user1", "user1user1")
    assert DENIED in s.sh("touch /etc/mine")


def test_a_users_own_line_adds_to_the_star_lines(s):
    boot_with(s, {"/etc/capabilities": repo_file("etc", "capabilities")
                  + "user1 w /etc/\n"}, HOMES)
    s.login("user1", "user1user1")
    assert caps_of(s)["/etc/"] == "33"


def test_a_subdirectory_can_keep_bits_its_parent_loses(s):
    boot_with(s, {"/etc/capabilities": "* r /etc/\n* rwcds /etc/sub/\n"
                  "* rx /usr/\n* home /home/\n"}, HOMES)
    s.login("user1", "user1user1")
    assert caps_of(s) == {"/etc/sub/": "2f", "/etc/": "01", "/usr/": "41"}


def test_a_supervisor_line_in_etc_capabilities_widens_nothing(s):
    boot_with(s, {"/etc/capabilities": repo_file("etc", "capabilities")
                  + "supervisor rwcdlsx /etc/\n"}, HOMES)
    s.login("user1", "user1user1")
    assert "supervisor line" in s.child.before
    assert DENIED in s.sh("touch /etc/mine")


def test_a_line_beyond_the_base_set_is_logged_and_skipped(s):
    boot_with(s, {"/system/data/capabilities": NARROW_BASE,
                  "/etc/capabilities": repo_file("etc", "capabilities")
                  + "user1 rwcdls /opt/\n"},
              HOMES + ("/opt",))
    s.login("user1", "user1user1")
    assert "not in the supervisor base set" in s.child.before
    assert "/opt/" not in caps_of(s)
    assert DENIED in s.sh("touch /opt/mine")
    assert s.sh("touch /home/user1/mine") == ""


def test_a_malformed_line_is_logged_and_skipped(s):
    boot_with(s, {"/etc/capabilities": repo_file("etc", "capabilities")
                  + "user1 rwz /etc/\n* rw\nuser1 r x /etc/\n"}, HOMES)
    s.login("user1", "user1user1")
    assert "invalid permissions" in s.child.before
    assert "malformed line" in s.child.before
    assert caps_of(s)["/etc/"] == "31"


def boot_with_home(s, home):
    """Boot with a user, alice, whose home directory is home."""
    boot_with(s, {
        "/etc/passwd": "alice:x:5:5:Alice:%s:/usr/bin/mush\n" % home,
        "/secure/shadow": "alice:%d\n" % sum(map(ord, "alicewonderland")),
    }, HOMES)
    s.login("alice", "wonderland")


def assert_no_home_capability(s):
    assert "home directory not allowed" in s.child.before
    caps = caps_of(s)
    assert caps == {"/usr/bin/": "71", "/usr/lib/": "41", "/etc/": "31",
                    "/tmp/": "3f"}, caps


def test_a_home_of_etc_gets_no_home_capability(s):
    boot_with_home(s, "/etc")
    assert_no_home_capability(s)
    assert DENIED in s.sh("touch /etc/mine")


def test_a_home_of_secure_gets_no_home_capability(s):
    boot_with_home(s, "/secure")
    assert_no_home_capability(s)


def test_a_home_of_exactly_home_gets_no_home_capability(s):
    boot_with_home(s, "/home")
    assert_no_home_capability(s)
    assert DENIED in s.sh("touch /home/user2/mine")


def test_a_home_with_dots_is_checked_after_collapsing(s):
    boot_with_home(s, "/home/../etc")
    assert_no_home_capability(s)


def test_roots_home_passes_the_home_check(s):
    s.login()
    assert s.sh("touch /root/mine") == ""


def test_the_supervisor_file_narrows_every_login(s):
    boot_with(s, {"/system/data/capabilities": "# base\n"
                  "supervisor rwcdlsx /usr/\nsupervisor rwcdls /etc/\n"
                  "supervisor rwcdls /root/\n"})
    s.login()
    caps = caps_of(s)
    assert "/tmp/" not in caps, caps
    assert DENIED in s.sh("touch /tmp/mine")
    assert s.sh("touch /root/mine") == ""


def test_the_supervisor_file_can_widen_what_a_user_may_be_given(s):
    boot_with(s, {
        "/system/data/capabilities":
        NARROW_BASE + "supervisor rwcdls /opt/\n",
        "/etc/capabilities":
        repo_file("etc", "capabilities") + "user1 rwcdls /opt/\n",
    }, HOMES + ("/opt",))
    s.login("user1", "user1user1")
    assert s.sh("touch /opt/mine") == ""


def test_a_supervisor_file_with_no_lines_keeps_the_compiled_set(s):
    boot_with(s, {"/system/data/capabilities": "# nothing here\n"})
    s.login()
    assert caps_of(s)["/usr/"] == "7f"


def test_a_user_logging_in_after_another_starts_from_the_base_set(s):
    boot_with(s, directories=HOMES)
    s.login("user1", "user1user1")
    s.child.sendline("exit")
    s.login()
    assert caps_of(s)["/usr/"] == "7f"
    assert s.sh("touch /etc/mine") == ""


def test_a_supervisor_file_of_the_same_size_is_not_the_compiled_set(s):
    boot_with(s, {"/system/data/capabilities":
                  repo_file("etc", "system", "capabilities")
                  .replace("rwcdlsx", "rwclsx")})
    s.login()
    caps = caps_of(s)
    assert "/usr/" not in caps and "/etc/" not in caps, caps
    assert caps["/usr/bin/"] == "71", caps


def test_a_custom_supervisor_file_still_lets_login_read_the_shadow_file(s):
    boot_with(s, {
        "/system/data/capabilities": repo_file("etc", "system", "capabilities")
        + "supervisor rwcdls /opt/\n",
        "/etc/passwd": repo_file("etc", "passwd"),
        "/secure/shadow": repo_file("etc", "secure", "shadow"),
    })
    s.login()
    assert s.sh("echo logged-in") == "logged-in"
    assert DENIED in s.sh(READ_SHADOW)


def test_lines_for_others_in_the_supervisor_file_are_ignored(s):
    boot_with(s, {"/system/data/capabilities":
                  NARROW_BASE + "root rwcdls /opt/\n",
                  "/etc/capabilities": repo_file("etc", "capabilities")
                  + "root rwcdls /opt/\n"}, ("/opt",))
    s.login()
    assert "/opt/" not in caps_of(s)


def test_a_normal_grant_of_a_protected_name_drops_the_full_path_entry(s):
    boot_with(s, {"/system/data/capabilities":
                  repo_file("etc", "system", "capabilities")
                  + "supervisor r /secure/\n",
                  "/etc/capabilities": repo_file("etc", "capabilities")
                  + "root r /secure/\n"})
    s.login()
    held = [line for line in s.sh("dirtest caps").splitlines()
            if line.endswith(" /secure/")]
    assert held == ["cap 01 /secure/"], held


def test_a_refused_entry_is_not_left_held(s):
    boot_with(s, {"/system/data/capabilities": NARROW_BASE,
                  "/etc/capabilities": repo_file("etc", "capabilities")
                  + "user1 x /etc/\n"}, HOMES)
    s.login("user1", "user1user1")
    assert "not in the supervisor base set" in s.child.before
    assert "/etc/" not in caps_of(s)


def test_a_shadow_line_must_match_the_whole_name(s):
    boot_with(s, {
        "/etc/passwd": "ali:x:5:5:Ali:/root:/usr/bin/mush\n"
        "root:x:0:0:Root:/root:/usr/bin/mush\n",
        "/secure/shadow": "alix%d\nroot:1356\n"
        % sum(map(ord, "aliwonderland")),
    })
    s.wait_login_prompt()
    s.child.sendline("ali")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("wonderland")
    s.child.expect("Login failed!", timeout=10)
