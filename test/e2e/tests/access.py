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

"""End-to-end tests of the directory capabilities the filesystem enforces."""

DENIED = "Permission denied"


def test_a_shell_cannot_touch_what_its_set_does_not_name(s):
    s.login()
    assert s.sh("mkdir /newdir") == \
        'ERROR: Could not create directory "/newdir": ' + DENIED
    assert DENIED in s.sh("touch /newfile")
    assert DENIED in s.sh("cat /var/log")
    assert DENIED in s.sh("rm /var/log")
    assert DENIED in s.sh("ll /")


def test_a_shell_cannot_remove_a_directory_its_set_names(s):
    s.login()
    assert s.sh("rmdir /etc") == \
        'ERROR: Could not remove directory "/etc": ' + DENIED


def test_a_case_variant_of_a_granted_directory_is_refused(s):
    s.login()
    assert s.sh("mkdir /ETC/newdir") == \
        'ERROR: Could not create directory "/ETC/newdir": ' + DENIED


def test_dots_cannot_leave_a_granted_directory(s):
    s.login()
    assert s.sh("mkdir /etc/../newdir") == \
        'ERROR: Could not create directory "/etc/../newdir": ' + DENIED


def test_commands_still_work_inside_the_set(s):
    s.login()
    assert s.sh("mkdir /tmp/inside") == ""
    assert s.sh("touch /tmp/inside/f") == ""
    assert s.sh("mv /tmp/inside/f /tmp/inside/g") == ""
    assert s.sh("rm /tmp/inside/g") == ""
    assert s.sh("rmdir /tmp/inside") == ""


def test_the_driver_refuses_to_create_without_the_create_bit(s):
    # EACCES is 4 and ENOENT is 8 in src/user/NanoOsErrno.h.
    s.login()
    out = s.sh("dirtest access")
    assert "create without c -> -1, errno=4" in out, out
    assert "stat refused create -> -1, errno=8" in out, out
    assert "append to existing -> 0, errno=0" in out, out


def test_path_commands_are_refused_on_a_narrowed_directory(s):
    s.login()
    out = s.sh("dirtest access")
    assert "remove without d -> -1, errno=4" in out, out
    assert "mkdir without c -> -1, errno=4" in out, out
    assert "rmdir without d -> -1, errno=4" in out, out


def test_the_driver_refuses_to_replace_without_the_delete_bit(s):
    s.login()
    out = s.sh("dirtest access")
    assert "replace without d -> -1, errno=4" in out, out
    assert "stat source -> 0, errno=0" in out, out
    assert "/tmp/acc/to/y holds original" in out, out


def test_a_rename_to_a_new_name_needs_no_delete_on_the_destination(s):
    s.login()
    out = s.sh("dirtest access")
    assert "rename to new name -> 0, errno=0" in out, out
    assert "stat renamed -> 0, errno=0" in out, out


def install_hello(s, directory):
    assert s.sh("mkdir " + directory) == ""
    assert s.sh("cp /usr/bin/helloworld/main.overlay " + directory) == ""


def test_a_program_in_a_directory_without_x_is_refused(s):
    s.login()
    install_hello(s, "/root/hi")
    assert s.sh("/root/hi") == "/root/hi: Permission denied"
    assert s.sh("echo $?") == "126"
    assert s.sh("echo still-here") == "still-here"


def test_a_program_in_a_directory_with_x_runs(s):
    s.login()
    install_hello(s, "/usr/bin/hi")
    assert s.sh("hi") == "Hello, world!"
    assert s.sh("/usr/bin/hi") == "Hello, world!"


def test_a_refused_program_in_a_pipeline_leaves_the_shell_running(s):
    s.login()
    install_hello(s, "/root/hi")
    assert DENIED in s.sh("/root/hi | cat")
    assert s.sh("echo still-here") == "still-here"


def test_a_directory_without_x_does_not_end_a_path_search(s):
    s.login()
    install_hello(s, "/root/echo")
    assert s.sh("export PATH=/root:/usr/bin") == ""
    assert s.sh("echo found") == "found"


def test_a_missing_command_is_not_found_with_a_path_without_x(s):
    s.login()
    assert s.sh("export PATH=/root:/usr/bin") == ""
    assert s.sh("nosuch") == "nosuch: command not found"
    assert s.sh("echo $?") == "127"


def test_a_program_only_in_a_path_directory_without_x_is_refused(s):
    s.login()
    install_hello(s, "/root/hi")
    assert s.sh("export PATH=/usr/bin:/root") == ""
    assert s.sh("hi") == "hi: Permission denied"
    assert s.sh("echo $?") == "126"
