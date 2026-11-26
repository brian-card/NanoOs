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

"""The session and helpers the end-to-end tests share."""

import datetime
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import pexpect

from fakevdp import FakeVdp


E2E_DIR = os.path.dirname(os.path.abspath(__file__))


REPO_ROOT = os.path.dirname(os.path.dirname(E2E_DIR))


HOSTNAME = "nanoe2e"


PROMPT = r"mush [^\r\n]*@%s:[^\r\n]+[#$] " % HOSTNAME


# Same prompt, capturing the user and the working directory it reports.
PROMPT_PARTS = r"mush ([^\r\n@]+)@%s:([^\r\n]+)[#$] " % HOSTNAME


def image_path(path):
    """Get where the kernel stores a path on the image.

    @param path The path as a process names it, or a system tree path.

    @return The path from the real root of the image.
    """
    if path.startswith(("/system/", "/secure/")):
        return path
    return "/userspace" + path


# A getty on UART1 and another on a VDP console served by vdp.drv, which owns
# UART0.
VDP_CONFIG_SYS = (
    "DEVICE=/system/drivers/vdp.drv\n"
    "HARDWARE=/system/drivers/vdp.drv UART0\n"
    "CONSOLE=UART1 /system/bin/getty\n"
    "CONSOLE=/system/drivers/vdp.drv /system/bin/getty\n")


# The config.sys a bridged Session boots with:  sessiontest, on the GRAPHICS
# line with loopback.drv standing in for the display driver, bridges a session
# to UART1.
BRIDGE_CONFIG_SYS = (
    "DEVICE=/system/drivers/loopback.drv 1\n"
    "GRAPHICS=/system/drivers/loopback.drv /system/commands/sessiontest\n")


# The environment variable that makes a Session bridged:  its logins run the
# shell in a console session that sessiontest bridges to UART1.
BRIDGE_VARIABLE = "NANO_OS_E2E_BRIDGE"


# The GRAPHICS line a Session whose logins run the shell in term boots with.
TERM_NOOK_LINE = "GRAPHICS=/system/drivers/vdp.drv /system/commands/sysnook\n"


# A line of the kernel's log.
TERM_LOG_LINE = re.compile(r"\[\d+\.\d+ [^\]]*\][^\n]*\n?")


# The least seconds between keys typed into term, once the last key has made
# the session print something other than its echo.
TERM_KEY_SECONDS = 0.07


# The most seconds to wait for a key typed into term to make the session print
# something, as a key that echoes nothing doesn't.
TERM_REACTION_SECONDS = 0.5


# The palette index of the desktop sysnook paints.
TERM_DESKTOP = 3


# The environment variable that makes a Session's logins run the shell in
# term:  Nook is started on the FakeVdp's console, and term on UART1, where it
# writes what the session prints.
TERM_VARIABLE = "NANO_OS_E2E_TERM"


VERBOSE = False


class TermChild:
    """The UART1 child of a Session whose logins run the shell in term:  what
    a test sends is typed on the FakeVdp's keyboard, which the window server
    sends term's session, and what it expects is read from UART1, where term
    writes what the session prints."""

    def __init__(self, child, vdp):
        """@param child The pexpect child on UART1.
        @param vdp The FakeVdp whose keyboard types into term."""
        object.__setattr__(self, "child", child)
        object.__setattr__(self, "vdp", vdp)

    def send(self, text):
        """Type text, one key at a time, each once the last has been echoed or
        has made the session print something, so that keys never pile up in
        vdp.drv's queue, which drops what doesn't fit."""
        for ch in text:
            self.vdp.key(ch)
            self._await_reaction("\n" if ch == "\r" else ch)
        return len(text)

    def _await_reaction(self, echo):
        """Wait until echo is printed, or until anything is printed and
        TERM_KEY_SECONDS have passed, or for TERM_REACTION_SECONDS, keeping
        what is read for the next expect.

        @param echo The text the key typed echoes.
        """
        child = self.child
        start = time.time()
        seen = ""
        while True:
            elapsed = time.time() - start
            if (echo in seen) or (seen and elapsed >= TERM_KEY_SECONDS):
                return
            if elapsed >= TERM_REACTION_SECONDS:
                return
            try:
                data = child.read_nonblocking(4096, timeout=0.02)
            except pexpect.TIMEOUT:
                continue
            except pexpect.EOF:
                return
            seen += data
            # expect searches both of these, so what is read ahead goes in
            # both.
            child.buffer = child.buffer + data
            child._before.seek(0, io.SEEK_END)
            child._before.write(data)

    def sendline(self, text=""):
        """Type a line and Return."""
        return self.send(text + "\r")

    def sendcontrol(self, ch):
        """Type Ctrl and a letter."""
        self.vdp.ctrl(ch.lower())
        return 1

    def sendintr(self):
        """Type Ctrl-C."""
        self.vdp.ctrl("c")

    def read_nonblocking(self, size=1, timeout=-1):
        """Read what send read ahead for expect, if any is left, or else read
        from the child."""
        child = self.child
        pending = child.buffer
        if not pending:
            return child.read_nonblocking(size, timeout)
        child.buffer = ""
        child._before = child.buffer_type()
        return pending

    def __getattr__(self, name):
        return getattr(self.child, name)

    def __setattr__(self, name, value):
        setattr(self.child, name, value)


# Heap, in bytes, left at the shell prompt in the small-memory tests:  a little
# over 2.5 KB, as on the SAMD21 boards.
SMALL_MEMORY_HEAP_RANGE = (2560, 2816)


SMALL_MEMORY_HEAP = 2688


# Simulated memory size, in bytes, booted once to find the size that leaves
# SMALL_MEMORY_HEAP.  The heap left at a given size depends on the host
# compiler's stack frame sizes, so it has to be measured on each host.
SMALL_MEMORY_PROBE_SIZE = 150000


_small_memory_sizes = {}


class SkipTest(Exception):
    """Raised by a test that doesn't apply to the configuration it runs in."""


# The prefix of every report the checked simulator's message-argument checker
# prints.
MESSAGE_ARGS_MARKER = "MESSAGE_ARGS: "

# The prefix of every Session's temporary directory.
SESSION_DIR_PREFIX = "nanoe2e-"

# Every Session not yet closed in this process.
_open_sessions = set()


def kill_open_sessions():
    """Kill the simulator of every Session not yet closed in this process."""
    for s in list(_open_sessions):
        s.kill()


class Transcript:
    """Everything a session's simulators print, copied to stdout with -v."""

    def __init__(self):
        self.parts = []

    def write(self, text):
        self.parts.append(text)
        if VERBOSE:
            sys.stdout.write(text)

    def flush(self):
        if VERBOSE:
            sys.stdout.flush()

    def getvalue(self):
        return "".join(self.parts)


def message_args_reports(s):
    """Return the message-argument checker's reports from a session's output,
    including output no expect has read yet, as in a test that only watches a
    FakeVdp.

    @param s The Session.

    @return The lines that hold a report, in order.
    """
    if s.sim_bin.endswith("-checked") and s.child.isalive():
        # Bounded, since a simulator may never stop printing.
        deadline = time.time() + 2
        try:
            while time.time() < deadline:
                s.child.read_nonblocking(4096, timeout=0.2)
        except (pexpect.TIMEOUT, pexpect.EOF):
            pass
    return [line.strip() for line in s.transcript.getvalue().splitlines()
            if MESSAGE_ARGS_MARKER in line]


class Session:
    def __init__(self, sim_bin, image, env=None):
        self.sim_bin = sim_bin
        self.env = env
        self.bridged = (env or {}).get(BRIDGE_VARIABLE) == "1"
        self.termed = (env or {}).get(TERM_VARIABLE) == "1"
        self.transcript = Transcript()
        self.tmp = tempfile.mkdtemp(prefix=SESSION_DIR_PREFIX)
        img_copy = os.path.join(self.tmp, "disk.img")
        subprocess.run(["cp", "--sparse=always", image, img_copy], check=True)
        self.img_copy = img_copy
        self.vdp = None
        self.child = None
        _open_sessions.add(self)
        if self.bridged:
            self._copy_in("/system/data/config.sys", BRIDGE_CONFIG_SYS)
        if self.termed:
            self._boot_with_vdp(VDP_CONFIG_SYS + TERM_NOOK_LINE)
        else:
            self.spawn([img_copy])

    def spawn(self, args):
        # codec_errors="replace": a misbehaving simulator can emit bytes that
        # aren't valid UTF-8, and a UnicodeDecodeError out of expect() aborts
        # the test with a decoding traceback instead of reporting what the
        # simulator actually did.
        self.child = pexpect.spawn(self.sim_bin, args, encoding="utf-8",
                                   codec_errors="replace", timeout=15,
                                   env=dict(os.environ, **(self.env or {})))
        self.child.logfile_read = self.transcript

    def _refuse_reboot(self):
        """Skip a test that boots a configuration of its own when it would
        replace the bridge's."""
        if self.bridged or self.termed:
            raise SkipTest("boots a configuration of its own")

    def restart_with_memory(self, memory_size):
        self._refuse_reboot()
        if self.child.isalive():
            self.child.terminate(force=True)
        self.spawn([self.img_copy, str(memory_size)])
        self.child.expect(r"Using (\d+) bytes of dynamic memory", timeout=15)
        return int(self.child.match.group(1))

    def write_file(self, path, text):
        """Stop the simulator and write a file, text or bytes, into this
        session's image."""
        self._refuse_reboot()
        if self.child.isalive():
            self.child.terminate(force=True)
        self._copy_in(path, text)

    def _copy_in(self, path, text):
        """Write a file, text or bytes, into this session's image, which no
        simulator is using."""
        data = text if isinstance(text, bytes) else text.encode()
        subprocess.run(["mcopy", "-o", "-i", self.img_copy + "@@2097152", "-",
                        "::" + image_path(path)], input=data,
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)

    def copy_program(self, source, target):
        """Copy a program on this session's image to another path on it,
        which no simulator is using.  The copy comes from the image, not the
        tree, whose programs another target's build may have replaced.

        @param source The program's directory, as a process names it.
        @param target Where to copy it, as a process names it.
        """
        image = self.img_copy + "@@2097152"
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        staging = tempfile.mkdtemp(dir=self.tmp)
        subprocess.run(["mcopy", "-s", "-i", image, "::" + image_path(source),
                        staging], env=env, check=True)
        subprocess.run(["mcopy", "-s", "-i", image,
                        os.path.join(staging, os.path.basename(source)),
                        "::" + image_path(target)], env=env, check=True)

    def restart_with_file(self, path, text):
        """Write a file into this session's image and boot again."""
        self.write_file(path, text)
        self.spawn([self.img_copy])

    def restart_with_config_sys(self, text):
        """Write /system/data/config.sys into this session's image and boot again."""
        self.restart_with_file("/system/data/config.sys", text)

    def restart_with_vdp(self, config_sys=VDP_CONFIG_SYS, **kwargs):
        """Boot again with vdp.drv serving a console on a FakeVdp.

        @param config_sys The /system/data/config.sys to boot with.
        @param kwargs Arguments for FakeVdp.

        @return The FakeVdp.
        """
        self._refuse_reboot()
        return self._boot_with_vdp(config_sys, **kwargs)

    def _boot_with_vdp(self, config_sys, **kwargs):
        """Boot with vdp.drv serving a console on a new FakeVdp.

        @param config_sys The /system/data/config.sys to boot with.
        @param kwargs Arguments for FakeVdp.

        @return The FakeVdp.
        """
        if self.vdp is not None:
            self.vdp.close()
        self.vdp = FakeVdp(os.path.join(self.tmp, "vdp.sock"), **kwargs)
        # Like the Agon's, UART0 drops what its receive ring has no room for.
        self.env = dict(self.env or {}, NANO_OS_SIM_UART0=self.vdp.path,
                        NANO_OS_SIM_UART0_DROP="1")
        if self.child is not None and self.child.isalive():
            self.child.terminate(force=True)
        self._copy_in("/system/data/config.sys", config_sys)
        self.spawn([self.img_copy])
        return self.vdp

    def wait_login_prompt(self):
        self.child.expect("login: ", timeout=15)

    def login(self, user="root", password="rootroot"):
        self.wait_login_prompt()
        self.child.sendline(user)
        self.child.expect("password: ", timeout=10)
        self.child.sendline(password)
        self.child.expect(PROMPT, timeout=10)
        if self.bridged:
            self.child.sendline("dirtest system sessiontest 1")
            self.child.expect(r"sessiontest: opened port \d+ shell \d+",
                              timeout=15)
            self.child.expect(PROMPT, timeout=15)
            # The history holds only the command that started the bridge, which
            # a console's fresh login wouldn't have.
            self.sh("rm $HOME/.mush_history")
        elif self.termed:
            self._start_term(user, password)

    def _start_term(self, user, password):
        """Log in on the FakeVdp's console, start Nook there, start term on
        UART1, and drive term's session from then on.

        @param user The user name.
        @param password The password.
        """
        vdp = self.vdp
        vdp.expect("login: ", timeout=30)
        vdp.type(user + "\r")
        vdp.expect("password: ")
        vdp.type(password + "\r")
        vdp.expect(PROMPT, timeout=15)
        acquired = len(vdp.context_events())
        vdp.type("nook\r")
        vdp.wait_context_events(acquired + 1, timeout=30)
        # sysnook registers as the window server once it has painted the
        # desktop.
        deadline = time.time() + 30
        while vdp.pixel(639, 479) != TERM_DESKTOP:
            assert time.time() < deadline, "sysnook never painted the desktop"
            time.sleep(0.1)
        time.sleep(1)
        self.child.sendline("term --tee --plain --size=80x24 --at=0,0")
        self.child.expect_exact("term --tee", timeout=10)
        self.child.expect(PROMPT, timeout=30)
        self.child = TermChild(self.child, vdp)
        # The history holds the commands that started Nook and term, which a
        # console's fresh login wouldn't have.
        self.sh("rm $HOME/.mush_history")

    def sh(self, command, timeout=10):
        self.child.sendline(command)
        self.child.expect_exact(command, timeout=timeout)
        self.child.expect(PROMPT, timeout=timeout)
        output = self.child.before.replace("\r", "")
        if self.termed:
            # The kernel's log shares UART1 with what term writes there, and
            # isn't the session's output.
            output = TERM_LOG_LINE.sub("", output)
        return output.strip("\n")

    def close(self):
        c = self.child
        try:
            if c.isalive():
                c.sendline("shutdown -h")
                # Only root may shut down, so another user's shell refuses.
                c.expect([pexpect.EOF, "Could not shut down"], timeout=8)
        except Exception:
            pass
        self.kill()

    def kill(self):
        """Kill the simulator without shutting it down and remove its files."""
        _open_sessions.discard(self)
        c = self.child
        if c is not None and c.isalive():
            c.terminate(force=True)
        if self.vdp is not None:
            self.vdp.close()
        shutil.rmtree(self.tmp, ignore_errors=True)


def vdp_login(vdp):
    """Log in as root on a FakeVdp's console.

    @param vdp The FakeVdp.
    """
    vdp.expect("login: ")
    vdp.type("root\r")
    vdp.expect("password: ")
    vdp.type("rootroot\r")
    vdp.expect(PROMPT)


def promptParts(s):
    """Return (user, directory) from the next prompt the shell emits."""
    s.child.sendline("")
    s.child.expect(PROMPT_PARTS, timeout=10)
    return s.child.match.group(1), s.child.match.group(2)


CATFILE_CONTENTS = "\n".join("line-%02d" % n for n in range(1, 41))


def ll_names(s, path):
    out = s.sh("ll " + path)
    names = [m.group("name") for m in (LS_LINE_RE.match(line)
             for line in out.splitlines()) if m is not None]
    return sorted(n for n in names if n not in ("./", "../"))


def settled(sample, attempts=6):
    """Take a measurement of the heap until two in a row agree.  The first
    after other commands can count the scheduler's copy of the overlay path
    the shell last ran, which has another length than the measuring
    command's own.

    @param sample A function that takes the measurement.
    @param attempts The most measurements to take after the first.

    @return The last measurement.
    """
    last = sample()
    for _attempt in range(attempts):
        now = sample()
        if now == last:
            break
        last = now
    return last


def heap_at_prompt(s):
    m = re.match(r"- Dynamic memory left: (\d+)$", s.sh("ps").splitlines()[0])
    assert m, s.child.before
    return int(m.group(1))


def small_memory_key(sim_bin, env):
    return (sim_bin, tuple(sorted((env or {}).items())))


def small_memory_size(s):
    # The heap grows byte for byte with the memory size, so one probe boot per
    # simulator and configuration finds the size.
    key = small_memory_key(s.sim_bin, s.env)
    if key not in _small_memory_sizes:
        s.restart_with_memory(SMALL_MEMORY_PROBE_SIZE)
        s.login()
        _small_memory_sizes[key] = \
            SMALL_MEMORY_PROBE_SIZE + SMALL_MEMORY_HEAP - heap_at_prompt(s)
    return _small_memory_sizes[key]


# fsck.vfat output that is expected on every image:  mformat's label mismatch,
# and the FSInfo free count, which the driver never updates.
_FSCK_KNOWN = re.compile(
    r"^(fsck\.fat \d|Volume label .* different\.|"
    r"\s+Auto-copying volume label|Free cluster summary wrong|"
    r"\s+Auto-correcting\.|Leaving filesystem unchanged\.|"
    r"\S+: \d+ files, \d+/\d+ clusters|$)")


def fsck_problems(s):
    s.child.sendline("shutdown -h")
    s.child.expect(pexpect.EOF, timeout=10)
    partition = os.path.join(s.tmp, "partition.img")
    subprocess.run(["dd", "if=" + s.img_copy, "of=" + partition, "bs=64K",
                    "skip=32", "conv=sparse", "status=none"], check=True)
    out = subprocess.run(["/sbin/fsck.vfat", "-n", partition],
                         capture_output=True, text=True)
    return [line for line in (out.stdout + out.stderr).splitlines()
            if not _FSCK_KNOWN.match(line)]


def assert_created_now(s, command, listed_name):
    # The sim's clock is the host's, and its local time is UTC-8 with DST on.
    before = datetime.datetime.now(datetime.UTC) - datetime.timedelta(hours=7)
    assert s.sh(command) == ""
    after = datetime.datetime.now(datetime.UTC) - datetime.timedelta(hours=7)
    out = s.sh("ll /etc")
    m = re.search(r"(\w{3}) (\d+)-(\d+)-(\d+)\s+(\d+):(\d+):(\d+) "
                  + re.escape(listed_name) + "$", out, re.M)
    assert m, out
    shown = datetime.datetime(*(int(g) for g in m.groups()[1:]),
                               tzinfo=datetime.UTC)
    assert before - datetime.timedelta(seconds=2) <= shown <= after, \
        (before, shown, after)
    assert m.group(1) == shown.strftime("%a"), out


def console_pattern(pattern):
    """Let a pattern's newlines match the carriage returns the console adds.

    @param pattern A regular expression whose newlines are literal or escaped.

    @return The pattern with each newline allowing carriage returns before it.
    """
    return pattern.replace("\\\n", "\n").replace("\n", "\r*\n")


def ed_session(s, steps):
    """Type ed's input one line at a time and match the whole transcript.

    @param s The Session to type into.
    @param steps (line, output) pairs, starting with the command that runs ed.
      An output is the exact text the line produces, or a compiled pattern
      for it.

    @return The match of the transcript against every step's line and output.
    """
    transcript = ""
    expected = ""
    # Typing with no delay makes every run read input that ed hasn't asked for.
    s.child.delaybeforesend = None
    for line, output in steps:
        if isinstance(output, str):
            output = re.escape(output)
        else:
            output = output.pattern
        step = re.escape(line + "\n") + output
        expected += step
        s.child.sendline(line)
        # Like a terminal, the console echoes the next typed-ahead line as soon
        # as ed takes this one, which can be before ed prints its output.
        if output:
            s.child.expect(console_pattern(step), timeout=30)
            transcript += s.child.before + s.child.after
    s.child.expect(PROMPT, timeout=30)
    transcript = (transcript + s.child.before).replace("\r", "")
    m = re.fullmatch(expected, transcript)
    assert m, repr(transcript[-300:])
    return m


def ed_input(lines):
    """Make ed steps for lines of text typed in input mode.

    @param lines The lines of text.

    @return A (line, "") step for each line, since input mode prints nothing.
    """
    return [(line, "") for line in lines]


# More lines than a table of them in memory could hold at SMALL_MEMORY_HEAP.
OVERSIZED_LINE_COUNT = 300


def write_oversized_file(s):
    """Write /etc/bigfile with OVERSIZED_LINE_COUNT lines, using ed.

    @param s The Session, logged in.

    @return The file's lines.
    """
    big_lines = ["big-%03d" % n for n in range(1, OVERSIZED_LINE_COUNT + 1)]
    ed_session(s, [("ed", ""), ("a", "")] + ed_input(big_lines)
               + [(".", ""), ("w /etc/bigfile", "%d\n" % (8 * len(big_lines))),
                  ("q", "")])
    return big_lines


LS_LINE_RE = re.compile(
    r'^[dl-][rwx-]{9}\s+\S+\s+\S+\s+\d+\s+(?P<wday>\w{3})\s+'
    r'(?P<year>\d+)-(?P<month>\d+)-(?P<day>\d+)\s+\d+:\d+:\d+\s+(?P<name>\S+)$')


def ps_entries(s):
    """Run ps and parse its process lines.

    @param s The Session.

    @return A list of (pid, user, name) tuples.
    """
    lines = s.sh("ps").splitlines()
    assert lines[0].startswith("- Dynamic memory left: "), lines
    assert lines[-1].startswith("- Dynamic memory left: "), lines
    entries = []
    for line in lines[1:-1]:
        m = re.fullmatch(r"(\d+)  (\S+) (.+)", line)
        assert m, line
        entries.append((int(m.group(1)), m.group(2), m.group(3)))
    return entries


def assert_ctrl_c_kills(s, command):
    """Run a foreground command, interrupt it, and check nothing is left over.

    @param s The Session.
    @param command The command line to run and interrupt.
    """
    s.login()
    before = ps_entries(s)
    s.child.sendline(command)
    time.sleep(0.5)
    s.child.sendcontrol("c")
    idx = s.child.expect([PROMPT, pexpect.EOF, pexpect.TIMEOUT], timeout=10)
    assert idx == 0, "Ctrl-C did not return the shell to a prompt"
    assert "overflowed" not in s.child.before, s.child.before
    assert ps_entries(s) == before
    for _ in range(3):
        assert s.sh("looseLoop &") == ""
    assert [name for _pid, _user, name in ps_entries(s)].count("looseLoop") \
        == 3
    assert s.sh("echo alive") == "alive"


def help_text(s, command):
    """Run a command and return its output, which must be a usage summary.

    @param s The Session.
    @param command The command line, which asks for help.

    @return The output.
    """
    name = command.split()[0]
    out = s.sh(command)
    lines = out.splitlines()
    assert re.fullmatch(r"Usage: %s( .*)?" % re.escape(name), lines[0]), out
    assert "Options:" in lines, out
    return out
