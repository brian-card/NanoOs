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


VERBOSE = False


# Heap, in bytes, left at the shell prompt in the small-memory tests:  a little
# over 2.5 KB, as on the SAMD21 boards.
SMALL_MEMORY_HEAP_RANGE = (2560, 2816)


SMALL_MEMORY_HEAP = 2688


# Simulated memory size, in bytes, booted once to find the size that leaves
# SMALL_MEMORY_HEAP.  The heap left at a given size depends on the host
# compiler's stack frame sizes, so it has to be measured on each host.
SMALL_MEMORY_PROBE_SIZE = 75000


_small_memory_sizes = {}


class SkipTest(Exception):
    """Raised by a test that doesn't apply to the configuration it runs in."""


class Session:
    def __init__(self, sim_bin, image, env=None):
        self.sim_bin = sim_bin
        self.env = env
        self.tmp = tempfile.mkdtemp(prefix="nanoe2e-")
        img_copy = os.path.join(self.tmp, "disk.img")
        subprocess.run(["cp", "--sparse=always", image, img_copy], check=True)
        self.img_copy = img_copy
        self.vdp = None
        self.spawn([img_copy])

    def spawn(self, args):
        # codec_errors="replace": a misbehaving simulator can emit bytes that
        # aren't valid UTF-8, and a UnicodeDecodeError out of expect() aborts
        # the test with a decoding traceback instead of reporting what the
        # simulator actually did.
        self.child = pexpect.spawn(self.sim_bin, args, encoding="utf-8",
                                   codec_errors="replace", timeout=15,
                                   env=dict(os.environ, **(self.env or {})))
        if VERBOSE:
            self.child.logfile_read = sys.stdout

    def restart_with_memory(self, memory_size):
        if self.child.isalive():
            self.child.terminate(force=True)
        self.spawn([self.img_copy, str(memory_size)])
        self.child.expect(r"Using (\d+) bytes of dynamic memory", timeout=15)
        return int(self.child.match.group(1))

    def write_file(self, path, text):
        """Stop the simulator and write a file into this session's image."""
        if self.child.isalive():
            self.child.terminate(force=True)
        subprocess.run(["mcopy", "-o", "-i", self.img_copy + "@@2097152", "-",
                        "::" + image_path(path)], input=text.encode(),
                       env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)

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
        self.vdp = FakeVdp(os.path.join(self.tmp, "vdp.sock"), **kwargs)
        self.env = dict(self.env or {}, NANO_OS_SIM_UART0=self.vdp.path)
        self.restart_with_config_sys(config_sys)
        return self.vdp

    def wait_login_prompt(self):
        self.child.expect("login: ", timeout=15)

    def login(self, user="root", password="rootroot"):
        self.wait_login_prompt()
        self.child.sendline(user)
        self.child.expect("password: ", timeout=10)
        self.child.sendline(password)
        self.child.expect(PROMPT, timeout=10)

    def sh(self, command, timeout=10):
        self.child.sendline(command)
        self.child.expect_exact(command, timeout=timeout)
        self.child.expect(PROMPT, timeout=timeout)
        return self.child.before.replace("\r", "").strip("\n")

    def close(self):
        c = self.child
        try:
            if c.isalive():
                c.sendline("shutdown -h")
                c.expect(pexpect.EOF, timeout=8)
        except Exception:
            pass
        if c.isalive():
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
