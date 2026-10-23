#!/usr/bin/env python3
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
################################################################################


"""End-to-end tests of a real Agon Light 2 build in fab-agon-emulator.

Needs pexpect, socat, the ez80 toolchain (AGDEV_HOME, default ~/agondev), and
fab-agon-emulator (NANO_OS_AGON_EMULATOR, default ~/src/fab-agon-emulator).
Reports every test skipped, not failed, when any of them is missing.  Emits
TAP on stdout.

    test/emulator/run.py [-v] [name-substring-filter]

Builds an SD card image with builddev, then boots it once in a private copy
of the emulator, without a window, and drives the UART1 console through a
pseudo-terminal.  The tests run in order against that one boot.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import tty

EMULATOR_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(EMULATOR_DIR))
HOSTNAME = "nanoemu"
PROMPT = r"mush [^\r\n]*@%s:[^\r\n]+[#$] " % HOSTNAME
# Log lines the SD card driver can print in the middle of other output.
LOG_LINE = re.compile(r"\[\d+\.\d+ [^\]\r\n]*\][^\n]*\n")
VERBOSE = False


def missing_requirement():
    """Find the first thing these tests need that this host lacks.

    @return A description of what's missing, or None if nothing is.
    """
    try:
        import pexpect  # noqa: F401
    except ImportError:
        return "python3 'pexpect' module not available"
    if shutil.which("socat") is None:
        return "socat not installed"
    agdev = os.environ.get("AGDEV_HOME", os.path.expanduser("~/agondev"))
    if not os.path.exists(os.path.join(agdev, "bin", "ez80-none-elf-clang")):
        return f"ez80 toolchain not found in {agdev} (set AGDEV_HOME)"
    emulator = os.environ.get("NANO_OS_AGON_EMULATOR",
                              os.path.expanduser("~/src/fab-agon-emulator"))
    for name in ("fab-agon-emulator", "firmware/mos_platform.bin",
                 "firmware/vdp_platform.so"):
        if not os.path.exists(os.path.join(emulator, name)):
            return (f"{name} not found in {emulator} "
                    "(set NANO_OS_AGON_EMULATOR)")
    return None


class Emulator:
    """One boot of a NanoOs SD card image in a private emulator copy."""

    def __init__(self, tmp):
        """Build the image and boot it.

        @param tmp The directory to keep the emulator copy and image in.
        """
        import pexpect.fdpexpect
        agdev = os.environ.get("AGDEV_HOME", os.path.expanduser("~/agondev"))
        source = os.environ.get("NANO_OS_AGON_EMULATOR",
                                os.path.expanduser("~/src/fab-agon-emulator"))
        self.tmp = tmp
        os.makedirs(os.path.join(tmp, "firmware"))
        shutil.copy(os.path.join(source, "fab-agon-emulator"), tmp)
        for name in ("mos_platform.bin", "vdp_platform.so"):
            shutil.copy(os.path.join(source, "firmware", name),
                        os.path.join(tmp, "firmware"))
        image = os.path.join(tmp, "NanoOs.img")
        print("# building the Agon Light 2 image ...", flush=True)
        subprocess.run(
            ["./builddev", "AgonLight2", HOSTNAME, image, "contiguous", "",
             os.path.join(tmp, "firmware", "mos_platform.bin")],
            cwd=REPO_ROOT, check=True, env=dict(os.environ, AGDEV_HOME=agdev),
            stdout=(None if VERBOSE else subprocess.DEVNULL),
            stderr=(None if VERBOSE else subprocess.DEVNULL))

        emu_tty = os.path.join(tmp, "ttyEmu")
        host_tty = os.path.join(tmp, "ttyHost")
        self.socat = subprocess.Popen(
            ["socat", f"pty,raw,echo=0,link={emu_tty}",
             f"pty,raw,echo=0,link={host_tty}"],
            stderr=subprocess.DEVNULL)
        deadline = time.time() + 5
        while not os.path.exists(host_tty):
            if time.time() > deadline:
                raise RuntimeError("socat never created its ptys")
            time.sleep(0.05)
        self.fd = os.open(host_tty, os.O_RDWR | os.O_NOCTTY)
        tty.setraw(self.fd)
        self.log = open(os.path.join(tmp, "emulator.log"), "w")
        self.process = subprocess.Popen(
            ["./fab-agon-emulator", "--sdcard-img", image,
             "--uart1-device", emu_tty, "--uart1-baud", "1152000"],
            cwd=tmp, stdout=self.log, stderr=subprocess.STDOUT,
            env=dict(os.environ, SDL_VIDEODRIVER="dummy",
                     SDL_AUDIODRIVER="dummy"))
        self.child = pexpect.fdpexpect.fdspawn(
            self.fd, encoding="utf-8", codec_errors="replace", timeout=20)
        self.child.delaybeforesend = None
        self.transcript = []
        self.child.logfile_read = _Collector(self.transcript)

    def output(self):
        """Return everything UART1 has printed so far."""
        return "".join(self.transcript)

    def login(self, user="root", password="rootroot"):
        self.child.expect("login: ", timeout=20)
        self.send(user + "\r")
        self.child.expect("password: ", timeout=10)
        self.send(password + "\r")
        self.child.expect(PROMPT, timeout=10)

    def send(self, text):
        """Type text at the console, one character at a time.

        @param text The characters to type.
        """
        for ch in text:
            self.child.send(ch)
            time.sleep(0.005)

    def sh(self, command, timeout=20):
        """Run a shell command and return its output.

        @param command The command line.
        @param timeout Seconds to wait for the prompt to come back.

        @return The output, without carriage returns or log lines.
        """
        self.send(command + "\r")
        self.child.expect(PROMPT, timeout=timeout)
        text = LOG_LINE.sub("", self.child.before.replace("\r", ""))
        echoed, _, output = text.partition("\n")
        assert echoed.endswith(command), f"{command!r} echoed as {echoed!r}"
        return output.strip("\n")

    def close(self):
        for process in (self.process, self.socat):
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
        os.close(self.fd)
        self.log.close()


class _Collector:
    """A pexpect logfile that keeps what it's given."""

    def __init__(self, chunks):
        self.chunks = chunks

    def write(self, data):
        self.chunks.append(data)
        if VERBOSE:
            sys.stdout.write(data)

    def flush(self):
        pass


# --- tests: each takes the Emulator, asserts, raises on failure ----------

def test_boots_to_a_login_prompt(e):
    e.child.expect(r"NanoOs [^\r\n]* %s tty0" % HOSTNAME, timeout=20)
    e.login()


def test_echo_roundtrips_its_argument(e):
    assert e.sh("echo hello-agon") == "hello-agon"


def test_ll_lists_the_root_directory(e):
    names = [line.split()[-1] for line in e.sh("ll /").splitlines()]
    for name in ("usr/", "bin/", "etc/"):
        assert name in names, f"{name} missing from {names}"


def test_config_sys_is_the_agon_one(e):
    expected = open(os.path.join(REPO_ROOT, "etc", "AgonLight2",
                                 "config.sys")).read().strip("\n")
    assert e.sh("cat /etc/config.sys") == expected


def test_vdp_driver_and_its_getty_are_running(e):
    names = [" ".join(line.split()[2:]) for line in e.sh("ps").splitlines()
             if re.match(r"\s*\d+\s", line)]
    assert "vdp.drv" in names, f"vdp.drv isn't running: {names}"
    assert "getty" in names, f"no getty on the VDP console: {names}"


def test_soft_reset_boots_again(e):
    e.send("shutdown -r\r")
    e.child.expect(r"NanoOs [^\r\n]* %s tty0" % HOSTNAME, timeout=30)
    e.login()
    assert e.sh("echo after-reset") == "after-reset"


def test_no_scheduler_queue_errors(e):
    assert "Could not push process" not in e.output(), \
        "the scheduler reported a queue overflow"


TESTS = [test_boots_to_a_login_prompt,
         test_echo_roundtrips_its_argument,
         test_ll_lists_the_root_directory,
         test_config_sys_is_the_agon_one,
         test_vdp_driver_and_its_getty_are_running,
         test_soft_reset_boots_again,
         test_no_scheduler_queue_errors]


def main(argv):
    global VERBOSE
    args = argv[1:]
    if "-v" in args:
        VERBOSE = True
        args.remove("-v")
    flt = args[0] if args else None
    tests = [t for t in TESTS if not flt or re.search(flt, t.__name__)]

    print("TAP version 13")
    missing = missing_requirement()
    if missing:
        print(f"1..0 # SKIP {missing}")
        return 0

    print(f"1..{len(tests)}", flush=True)
    failures = 0
    tmp = tempfile.mkdtemp(prefix="nanoemu-")
    e = None
    try:
        e = Emulator(tmp)
        for i, t in enumerate(tests, 1):
            try:
                t(e)
                print(f"ok {i} - {t.__name__}", flush=True)
            except Exception as ex:  # noqa: BLE001
                failures += 1
                msg = str(ex).splitlines()[0] if str(ex) else type(ex).__name__
                print(f"not ok {i} - {t.__name__}")
                print(f"#   {type(ex).__name__}: {msg}")
                tail = LOG_LINE.sub("", e.output().replace("\r", ""))[-300:]
                for line in tail.splitlines():
                    print(f"#   | {line}")
                sys.stdout.flush()
    finally:
        if e is not None:
            e.close()
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"# {len(tests)} test(s), {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
