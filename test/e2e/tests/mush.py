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

"""End-to-end tests of mush:  its line editor, scripts, pipes and jobs."""

import os
import re
import subprocess
import time

import pexpect

from fakevdp import (KEYCODE_DELETE, KEYCODE_DOWN, KEYCODE_END, KEYCODE_HOME,
                     KEYCODE_LEFT, KEYCODE_RIGHT, KEYCODE_UP)
from harness import (heap_at_prompt, help_text, PROMPT, PROMPT_PARTS,
                     promptParts, ps_entries, vdp_login)


UP, DOWN, RIGHT, LEFT = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"
HOME, END, DELETE = "\x1b[H", "\x1b[F", "\x1b[3~"
MUSH_USAGE = "Usage: mush [script [argument ...]]"


def edit(s, keys, output):
    """Type keys at the shell prompt and wait for the command's output.

    @param s The Session, at a prompt.
    @param keys The keys to type, ending with Enter.
    @param output The output the edited command line must print.
    """
    s.child.send(keys)
    s.child.expect(r"\r\n" + re.escape(output) + r"\r*\n" + PROMPT, timeout=10)


def test_line_editor_left_and_right_move_the_cursor(s):
    s.login()
    edit(s, "echo ac" + LEFT + "b\r", "abc")
    edit(s, "echo ad" + LEFT + LEFT + RIGHT + "bc\r", "abcd")
    edit(s, "echo a" + RIGHT + RIGHT + "b\r", "ab")


def test_line_editor_home_and_end_jump_to_the_ends(s):
    s.login()
    edit(s, "cho a" + HOME + "e\r", "a")
    edit(s, "echo b" + HOME + END + "c\r", "bc")
    for home, end in (("\x1bOH", "\x1bOF"), ("\x1b[1~", "\x1b[4~"),
                      ("\x1b[7~", "\x1b[8~")):
        edit(s, "cho d" + home + "e" + end + "e\r", "de")


def test_line_editor_delete_and_backspace_remove_mid_line(s):
    s.login()
    edit(s, "echo abxc" + LEFT + LEFT + DELETE + "\r", "abc")
    edit(s, "echo abxc" + LEFT + "\x7f\r", "abc")
    edit(s, "echo abxc" + LEFT + "\b\r", "abc")
    edit(s, "echo ab" + DELETE + "c\r", "abc")
    edit(s, "xecho abc" + HOME + "\x7f" + DELETE + "\r", "abc")


def test_line_editor_up_and_down_walk_the_history(s):
    s.login()
    edit(s, UP + DOWN + "echo first\r", "first")
    edit(s, UP + "\r", "first")
    edit(s, "echo second\r", "second")
    edit(s, "pwd\r", "/root")
    edit(s, UP + UP + UP + "\r", "first")
    edit(s, UP + UP + UP + DOWN + "\r", "/root")
    edit(s, UP + DOWN + "\r" + "echo after-empty\r", "after-empty")
    edit(s, "echo typed" + UP + "\x7f" * 6 + "-edited\r", "after-edited")
    edit(s, UP * 8 + "\r", "first")
    assert s.sh("cat .mush_history") == "\n".join([
        "echo first", "echo second", "pwd", "echo first", "pwd",
        "echo after-empty", "echo after-edited", "echo first",
        "cat .mush_history"])


def test_line_editor_history_survives_logging_out(s):
    s.login()
    edit(s, "echo remembered\r", "remembered")
    s.child.sendline("exit")
    s.login()
    edit(s, UP + UP + "\r", "remembered")


def test_line_editor_ctrl_c_abandons_the_line(s):
    s.login()
    edit(s, "echo older\r", "older")
    edit(s, "echo kept\r", "kept")
    for keys, shown in (("echo dropped" + UP, "echo kept"),
                        ("echo typed", "echo typed")):
        s.child.send(keys)
        s.child.expect_exact(shown, timeout=10)
        s.child.send("\x03")
        s.child.expect_exact("^C", timeout=10)
        s.child.expect(PROMPT, timeout=10)
        assert s.child.before.replace("\r", "") == "\n", repr(s.child.before)
    edit(s, UP + "\r", "kept")
    edit(s, UP + UP + UP + "\r", "older")
    edit(s, UP * 4 + DOWN + "\r", "kept")
    assert s.sh("cat .mush_history") == "\n".join([
        "echo older", "echo kept", "echo older", "echo kept",
        "cat .mush_history"])


def test_line_editor_takes_an_arrow_key_arriving_in_pieces(s):
    s.login()
    s.child.delaybeforesend = None
    s.child.send("echo ac\x1b")
    time.sleep(0.05)
    s.child.send("[")
    time.sleep(0.05)
    s.child.send("D")
    edit(s, "b\r", "abc")


def test_line_editor_drops_a_lone_escape(s):
    s.login()
    s.child.send("\x1b")
    time.sleep(0.1)
    edit(s, "echo after-escape\r", "after-escape")


def test_line_editor_keeps_lines_typed_ahead(s):
    s.login()
    s.child.delaybeforesend = None
    s.child.send("echo one\recho two\r")
    s.child.expect(r"\none\r*\n" + PROMPT, timeout=10)
    s.child.expect(r"\ntwo\r*\n" + PROMPT, timeout=10)
    assert s.sh("echo after") == "after"


def test_line_editor_leaves_other_programs_input_cooked(s):
    s.login()
    s.child.sendline("cat")
    s.child.expect(r"cat\r*\n", timeout=10)
    s.child.send("abx\x7fc\r")
    s.child.expect(r"\x08c\r*\nabc\r*\n", timeout=10)
    s.child.send("\x03")
    s.child.expect(PROMPT, timeout=10)
    edit(s, "echo ab" + LEFT + "x\r", "axb")


def test_vdp_line_editor_redraws_the_line(s):
    vdp = s.restart_with_vdp()
    vdp_login(vdp)
    vdp.type("a-much-longer-line\r")
    vdp.expect(r"\r\na-much-longer-line: command not found\r\n" + PROMPT)
    vdp.clear()
    vdp.type("echo ac")
    vdp.press(KEYCODE_LEFT)
    vdp.type("b")
    vdp.press(KEYCODE_HOME)
    vdp.press(KEYCODE_DELETE)
    vdp.type("X")
    vdp.press(KEYCODE_END)
    vdp.type("d")
    vdp.expect(r"^Xcho abcd$")
    vdp.press(KEYCODE_UP)
    vdp.expect(r"^a-much-longer-line$")
    vdp.press(KEYCODE_DOWN)
    vdp.expect(r"^ {18}$")
    vdp.type("Xcho abcd")
    vdp.press(KEYCODE_LEFT)
    vdp.press(KEYCODE_LEFT)
    vdp.type("\b")
    vdp.press(KEYCODE_RIGHT)
    vdp.type("\r")
    vdp.expect(r"^Xcho acd {10}\r\nXcho: command not found\r\n" + PROMPT)
    vdp.clear()
    vdp.press(KEYCODE_UP)
    vdp.press(KEYCODE_HOME)
    vdp.press(KEYCODE_DELETE)
    vdp.type("e\r")
    vdp.expect(r"^echo acd\r\nacd\r\n" + PROMPT)
    assert vdp.errors == [], f"unexpected VDU commands: {vdp.errors}"


def test_prompt_reports_the_user_and_working_directory(s):
    # A missing or empty PWD in the shell's environment has to fail here rather
    # than being absorbed by the prompt pattern.
    s.login()
    user, pwd = promptParts(s)
    assert user == "root", f"prompt user is {user!r}"
    assert pwd.startswith("/"), f"prompt directory is not absolute: {pwd!r}"
    assert "unknown" not in pwd, f"shell could not read PWD: {pwd!r}"
    assert pwd == s.sh("pwd").splitlines()[-1].strip(), \
        f"prompt directory {pwd!r} disagrees with pwd builtin"


def test_cd_changes_the_working_directory(s):
    # Each step checks the user as well as the directory: setenv reallocates the
    # environment block, so anything the shell cached a getenv pointer to shows
    # up here as a mangled prompt.
    s.login()
    home = promptParts(s)[1]

    for command, expected in (("cd /usr/bin", "/usr/bin"),
                              ("cd ..", "/usr"),
                              ("cd -", "/usr/bin"),
                              ("cd", home)):
        s.child.sendline(command)
        s.child.expect(PROMPT_PARTS, timeout=10)
        user, pwd = s.child.match.group(1), s.child.match.group(2)
        assert pwd == expected, f"{command!r} left the prompt at {pwd!r}"
        assert user == "root", f"{command!r} mangled the prompt user to {user!r}"

    assert "/usr/bin" not in s.sh("pwd"), \
        "bare cd did not return to the home directory"
    assert "alive" in s.sh("echo alive")


PROFILE = "/root/.mush_profile"


def test_login_runs_the_profile_once(s):
    # Every foreground command restarts the shell, so a profile that ran again
    # would undo the cd made after it.  The export after echo shows the profile
    # resumed after the command replaced the shell.
    s.restart_with_file(PROFILE, "# Set up.\n\n  cd /usr\necho from-profile\n"
                        "export OLDPWD=/tmp\n")
    s.login()
    out = s.child.before.replace("\r", "")
    assert out.split("Login successful!\n", 1)[1].strip("\n") \
        == "from-profile", out
    assert promptParts(s) == ("root", "/usr")
    assert s.sh("echo restarted") == "restarted"
    assert s.sh("cd -") == ""
    assert promptParts(s) == ("root", "/tmp")
    assert s.sh("cd /etc") == ""
    assert s.sh("echo restarted") == "restarted"
    assert help_text(s, "mush --help").splitlines()[0] == MUSH_USAGE
    assert promptParts(s) == ("root", "/etc")


def test_profile_skips_a_line_that_is_too_long(s):
    long_line = "export LONG=" + "x" * 100
    s.restart_with_file(PROFILE, "\n".join([
        "echo one", long_line, "export NAME", "cd /usr", ""]))
    s.login()
    out = s.child.before.replace("\r", "")
    assert out.split("Login successful!\n", 1)[1].strip("\n").splitlines() == [
        "one",
        PROFILE + ": line 2: too long",
        "Usage: export NAME=value"], out
    assert promptParts(s) == ("root", "/usr")


def write_files(s, files):
    """Write files into the session's image, boot again, and log in as root.

    @param s The Session.
    @param files A dict of absolute path to file contents.
    """
    if s.child.isalive():
        s.child.terminate(force=True)
    for path, text in files.items():
        subprocess.run(["mmd", "-i", s.img_copy + "@@2097152",
                        "::" + os.path.dirname(path)],
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"),
                       capture_output=True)
        subprocess.run(["mcopy", "-o", "-i", s.img_copy + "@@2097152", "-",
                        "::" + path], input=text.encode(),
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)
    s.spawn([s.img_copy])
    s.login()


GREET = "echo hello\ncd /etc\nexport OLDPWD=/tmp\necho world\npwd\n"


def test_script_runs_by_name_path_and_mush(s):
    # Each echo replaces the shell, so every later line runs in a restarted
    # shell that has to find its place in the script again.
    write_files(s, {"/root/greet": GREET})
    home = promptParts(s)[1]
    assert s.sh("cd /usr") == ""
    for command in ("/root/greet", "../root/greet", "mush /root/greet"):
        assert s.sh(command) == "hello\nworld\n/etc", command
        assert promptParts(s) == ("root", "/usr"), command
    assert s.sh("cd") == ""
    assert s.sh("./greet") == "hello\nworld\n/etc"
    assert s.sh("export PATH=/usr/bin:" + home) == ""
    assert s.sh("greet") == "hello\nworld\n/etc"
    assert promptParts(s) == ("root", home)
    assert s.sh("cd -") == ""
    assert promptParts(s) == ("root", "/usr"), "the script's export leaked"


def test_script_runs_scripts_inside_it(s):
    write_files(s, {
        "/root/outer": "echo outer-start\n./inner\nmush inner\necho outer-end\n",
        "/root/inner": "echo inner\nexit\necho not-reached\n",
        "/root/again": "echo level\n./again\necho unwound\n",
    })
    assert s.sh("./outer") == "outer-start\ninner\ninner\nouter-end"
    assert s.sh("./again") == "\n".join(
        ["level"] * 9
        + ["/root/again: Too many scripts running inside each other"]
        + ["unwound"] * 9)
    assert s.sh("echo alive") == "alive"


def test_script_refuses_the_background_and_missing_files(s):
    write_files(s, {"/root/greet": GREET})
    for command in ("./greet &", "echo a | ./greet"):
        assert s.sh(command) == \
            "/root/greet: A script can't run in the background or in a pipeline"
    assert s.sh("mush missing") == "missing: No such entry found"
    assert s.sh("./missing") == "./missing: command not found"
    assert promptParts(s)[1] == "/root"
    assert s.sh("echo alive") == "alive"


ARGS = """echo 0=$0 1=$1 2=${2} 3=$3 9=$9
echo [$*] [${@}]
shift
echo 0=$0 1=$1 2=$2
./inner x$1
mush inner
echo back 1=$1
shift
shift
echo $? 1=$1
shift
echo $? 1=$1 [$*]
"""


def test_script_sees_its_positional_parameters(s):
    # Each script's arguments are its own:  inner's don't replace args', and
    # the ones shift drops stay dropped across the shell restarting.
    write_files(s, {"/root/args": ARGS,
                    "/root/inner": "echo inner 0=$0 1=$1 2=$2\n",
                    "/root/nine": "echo $9 ${9} $10\nshift\necho $9\necho $@\n"})
    assert s.sh('./args a "b c" d') == "\n".join([
        "0=./args 1=a 2=b c 3=d 9=",
        "[a b c d] [a b c d]",
        "0=./args 1=b c 2=d",
        "inner 0=./inner 1=xb 2=c",
        "inner 0=inner 1= 2=",
        "back 1=b c",
        "0 1=",
        "1 1= []"])
    assert s.sh("mush /root/inner p q") == "inner 0=/root/inner 1=p 2=q"
    assert s.sh("./nine 1 2 3 4 5 6 7 8 nine ten") == \
        "nine nine 10\nten\n2 3 4 5 6 7 8 nine ten"
    assert s.sh("echo [$0] [$1] [${9}] [$*] [${@}]") == "[] [] [] [] []"
    assert s.sh("shift") == ""
    assert s.sh("echo $?") == "1"


def test_profile_adds_a_directory_to_the_path(s):
    write_files(s, {PROFILE: "export PATH=${PATH}:${HOME}/bin\n",
                    "/root/bin/hello": "echo hello from bin\n"})
    assert s.sh("echo $PATH") == "/usr/bin:/root/bin"
    assert s.sh("hello") == "hello from bin"
    assert s.sh("echo $?") == "0"


def test_variables_expand_in_command_lines(s):
    s.login()
    home = promptParts(s)[1]
    assert s.sh("echo $HOME ${USER} x${USER}y $UNSET- \\$HOME ${HOME $") \
        == home + " root xrooty - $HOME ${HOME $"
    assert s.sh("export GREETING=hi") == ""
    assert s.sh("echo ${GREETING}there") == "hithere"
    assert s.sh("$UNSET") == ""
    assert s.sh("cd ${HOME}/../etc") == ""
    assert promptParts(s) == ("root", "/etc")


def test_exit_status_reports_the_last_command(s):
    s.login()
    for command, status in (("echo ok", "0"), ("cat /missing", "1"),
                            ("nosuch", "127"), ("export", "1"),
                            ("pwd", "0")):
        s.sh(command)
        assert s.sh("echo $?") == status, command
    help_text(s, "mush --help")
    assert s.sh("echo ${?}") == "0"
    s.child.sendline("tightLoop")
    time.sleep(0.5)
    s.child.sendcontrol("c")
    s.child.expect(PROMPT, timeout=10)
    assert s.sh("echo $?") == "130"


def test_script_sees_the_exit_status_of_its_commands(s):
    write_files(s, {"/root/status": "cat /missing\necho status=$?\n"
                                    "nosuch\necho status=$?\n"})
    assert s.sh("./status").splitlines()[1:] == [
        "status=1", "nosuch: command not found", "status=127"]


def test_export_sets_a_variable_later_commands_inherit(s):
    s.login()
    for command in ("export", "export =x", "export HOME =/tmp", "export HOME"):
        assert s.sh(command) == "Usage: export NAME=value", command
    assert s.sh("export HOME=/tmp  ") == ""
    assert s.sh("echo restarted") == "restarted"
    assert s.sh("cd") == ""
    assert promptParts(s) == ("root", "/tmp")


def test_pipe_into_a_missing_command_leaves_the_shell_reading_the_console(s):
    s.login()
    assert s.sh("echo a | nosuch") == "nosuch: command not found"
    assert s.sh("echo alive") == "alive"


def test_pipe_between_commands(s):
    s.login()
    assert "needle-in-haystack" in s.sh("echo needle-in-haystack | grep needle")
    neg = s.sh("echo one-two-three | grep zzz")
    assert "one-two-three" not in neg, neg


def test_pipe_two_stage_is_the_working_max(s):
    # A single pipe (2 commands) is the deepest pipeline NanoOs currently
    # handles.  Confirm it works and the shell stays alive afterwards.
    s.login()
    assert "keep" in s.sh("echo keep-this-line | grep keep")
    assert "still-here" in s.sh("echo still-here")


def test_pipe_three_stage_does_not_crash_the_os(s):
    # Regression test for a SIGSEGV/reboot on 3+-stage pipelines: the
    # process that is simultaneously a pipe reader (of an already-exited
    # upstream writer) and a pipe writer (to a still-live downstream
    # reader) could exit with a stale message still linked into its own
    # coroutine message queue.  closeProcessFileDescriptors() pushes a
    # stack-local ProcessMessage as a best-effort "you're unblocked" ping
    # to a waiting process and gives up after a bounded timeout; if the
    # receiver never drained it in time, the queue was left holding a
    # dangling pointer into the now-returned stack frame, and crashed
    # whatever later walked that queue - including the queue's own
    # destruction when the receiving process itself exited.  Fixed by
    # unlinking the message (msg_q_remove()/comessageQueueRemove()) before
    # giving up on it.  Confirmed via gdb backtraces on
    # sim/bin/nano-os-sim core dumps and printString-level tracing in the
    # real AgonLight2 emulator before the fix (msg_destroy() crashed on a
    # dangling msg->msg_sync while the AgonLight2 target instead
    # free-ran off the deep end into an eventual watchdog-style reboot).
    s.login()
    s.child.sendline("echo a | grep a | grep a")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=12)
    if idx == 1:
        sig = s.child.signalstatus
        raise AssertionError(
            f"simulator died on a 3-command pipeline "
            f"(signal {sig}{' = SIGSEGV' if sig == 11 else ''})")
    if idx == 2:
        raise AssertionError("3-command pipeline hung the shell")
    # Reached a prompt: the pipeline should have actually matched and
    # printed its output, and the shell must still be usable afterwards.
    assert "a" in s.child.before, s.child.before
    assert "alive" in s.sh("echo alive"), "shell unresponsive after the pipeline"


def test_pipe_four_stage_does_not_crash_the_os(s):
    # One stage deeper than the bug above ever needed to reproduce -
    # confirms the fix isn't specific to exactly 3 stages.
    s.login()
    s.child.sendline("echo a | grep a | grep a | grep a")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=12)
    if idx == 1:
        sig = s.child.signalstatus
        raise AssertionError(
            f"simulator died on a 4-command pipeline "
            f"(signal {sig}{' = SIGSEGV' if sig == 11 else ''})")
    if idx == 2:
        raise AssertionError("4-command pipeline hung the shell")
    assert "a" in s.child.before, s.child.before
    assert "alive" in s.sh("echo alive"), "shell unresponsive after the pipeline"


def test_background_jobs_fail_gracefully_past_the_slot_limit(s):
    # Contrast with the pipe path: launching more background jobs than there
    # are free process slots must report an error and leave the shell alive.
    s.login()
    saw_error = False
    for _ in range(6):
        s.child.sendline("looseLoop &")
        idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=8)
        assert idx == 0, f"background spawn killed/hung the shell (expect idx {idx})"
        if "out of process slots" in s.child.before.lower():
            saw_error = True
    assert saw_error, "never hit the process-slot limit"
    # Shell still works, and ps still works.
    assert "looseLoop" in s.sh("ps")


def test_background_output_reaches_the_uart_console(s):
    # The typed line is echoed, so only the expanded output reads bg-output.
    s.login()
    assert s.sh("export X=put") == ""
    s.child.sendline("echo bg-out$X &")
    s.child.expect_exact("echo bg-out$X &", timeout=10)
    s.child.expect(r"bg-output\r*\n", timeout=10)
    assert "non-owning" not in s.child.before
    assert s.sh("echo alive") == "alive"


def test_logout_silences_background_jobs(s):
    s.login()
    s.child.sendline("looseLoop tick &")
    s.child.expect_exact("looseLoop tick &", timeout=10)
    s.child.expect(r"\btick\r*\n", timeout=10)
    s.child.sendline("exit")
    s.wait_login_prompt()
    i = s.child.expect([r"\btick\r*\n|non-owning", pexpect.TIMEOUT], timeout=4)
    assert i == 1, "a background job printed after its user logged out"
    s.child.sendline("root")
    s.child.expect("password: ", timeout=10)
    s.child.sendline("rootroot")
    s.child.expect(PROMPT, timeout=10)
    assert "looseLoop" in s.sh("ps")


def test_commands_that_cannot_load_leave_the_shell_running(s):
    s.login()
    before = ps_entries(s)
    assert s.sh("mkdir /usr/bin/corrupt") == ""
    assert s.sh("cp /etc/catfile /usr/bin/corrupt/main.overlay") == ""
    assert s.sh("mkdir /usr/bin/empty") == ""
    assert s.sh("touch /usr/bin/empty/main.overlay") == ""
    for command in ("corrupt", "empty", "corrupt &", "empty &"):
        name = command.split()[0]
        assert s.sh(command) == name + ": cannot execute"
        assert ps_entries(s) == before, command
    for _ in range(3):
        assert s.sh("looseLoop &") == ""
    assert [name for _pid, _user, name in ps_entries(s)].count("looseLoop") \
        == 3


def test_failed_background_launches_release_their_slots(s):
    s.login()
    heaps = []
    for _ in range(8):
        assert s.sh("no_such_command_here &") == \
            "no_such_command_here: command not found"
        heaps.append(heap_at_prompt(s))
    # The file cache makes the heap alternate, but it must not trend down.
    assert min(heaps[4:]) >= min(heaps[:4]), heaps
    for _ in range(3):
        assert s.sh("looseLoop &") == ""
    assert [name for _pid, _user, name in ps_entries(s)].count("looseLoop") \
        == 3


def test_mush_help_leaves_the_user_logged_in(s):
    s.login()
    assert s.sh("cd /etc") == ""
    before = ps_entries(s)
    user, pwd = promptParts(s)
    heaps = []
    for _ in range(8):
        assert help_text(s, "mush --help").splitlines()[0] == MUSH_USAGE
        assert promptParts(s) == (user, pwd)
        assert s.sh("pwd") == "/etc"
        assert ps_entries(s) == before
        heaps.append(heap_at_prompt(s))
    # The file cache makes the heap alternate, but it must not trend down.
    assert min(heaps[4:]) >= min(heaps[:4]), heaps


def test_typed_ahead_lines_both_run_in_order(s):
    # Both lines arrive in one read.  The second must neither be edited into
    # the console buffer while the shell is still reading the first out of it
    # nor be dropped because the shell wasn't waiting for it yet.  Whether both
    # lines land in one read depends on timing, so try a few.
    s.login()
    for first, second in (("abc", "def"), ("ghi", "jkl"), ("mno", "pqr")):
        s.child.send(f"echo {first}\recho {second}\r")
        s.child.expect(r"\n%s\r*\n" % first, timeout=10)
        # The typed-ahead line was echoed before the first one ran, so its
        # output follows the shell's next prompt on the same line.
        s.child.expect(r"%s\r*\n" % second, timeout=10)
        s.child.expect(PROMPT, timeout=10)
    assert s.sh("echo ok") == "ok"


def test_unknown_command_errors(s):
    s.login()
    assert s.sh("no_such_command_here arg") == \
        "no_such_command_here: command not found"
    assert s.sh("echo still-here") == "still-here"


def test_unknown_background_command_errors(s):
    s.login()
    assert s.sh("no_such_command_here &") == \
        "no_such_command_here: command not found"
    assert s.sh("echo still-here") == "still-here"
