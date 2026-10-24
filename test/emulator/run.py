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
# Log lines the SD card driver and the HAL's boot trace can print in the
# middle of other output.
LOG_LINE = re.compile(r"\[\d+\.\d+ [^\]\r\n]*\][^\n]*\n"
                      r"|\[boot\] [^\n]*\n")
# fsck.vfat output that is expected on every image:  mformat's label mismatch,
# and the FSInfo free count, which the driver never updates.
FSCK_KNOWN = re.compile(
    r"^(fsck\.fat \d|Volume label .* different\.|"
    r"\s+Auto-copying volume label|Free cluster summary wrong|"
    r"\s+Auto-correcting\.|Leaving filesystem unchanged\.|"
    r"\S+: \d+ files, \d+/\d+ clusters|$)")
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


def ll_names(e, path):
    """List a directory's entries the way ll names them.

    @param e The Emulator.
    @param path The directory to list.

    @return The sorted names, without "./" and "../".
    """
    names = [line.split()[-1] for line in e.sh("ll " + path).splitlines()]
    return sorted(name for name in names if name not in ("./", "../"))


def ps_entries(e):
    """Run ps and parse its process lines.

    @param e The Emulator.

    @return A list of (pid, user, name) tuples.
    """
    lines = e.sh("ps").splitlines()
    assert lines[0].startswith("- Dynamic memory left: "), lines
    assert lines[-1].startswith("- Dynamic memory left: "), lines
    entries = []
    for line in lines[1:-1]:
        m = re.fullmatch(r"(\d+)  (\S+) (.+)", line)
        assert m, line
        entries.append((int(m.group(1)), m.group(2), m.group(3)))
    return entries


def test_helloworld_prints_its_greeting(e):
    assert e.sh("helloworld") == "Hello, world!"


def test_help_describes_every_command(e):
    lines = e.sh("help").splitlines()
    assert lines[-2:] == ["", "Run a command with --help to see its usage."]
    described = [line.split(":")[0] for line in lines[:-2]]
    installed = [name.rstrip("/") for name in ll_names(e, "/usr/bin")]
    assert described == sorted(installed + ["cd", "exit", "pwd"],
                               key=str.lower), (described, installed)


def test_cd_and_pwd_track_the_working_directory(e):
    assert e.sh("cd /usr/bin") == ""
    assert e.sh("pwd") == "/usr/bin"
    assert e.sh("cd ../../etc") == ""
    assert e.sh("pwd") == "/etc"
    assert e.sh("cd") == ""
    assert e.sh("pwd") == "/root"


def edit(e, keys, output):
    """Type keys at the shell prompt and check the edited command's output.

    @param e The Emulator, at a prompt.
    @param keys The keys to type, ending with Enter.
    @param output The output the edited command line must print.
    """
    e.send(keys)
    e.child.expect(PROMPT, timeout=20)
    text = LOG_LINE.sub("", e.child.before.replace("\r", ""))
    assert text.partition("\n")[2].strip("\n") == output, repr(text)


def test_line_editor_moves_edits_and_recalls(e):
    left, up, delete = "\x1b[D", "\x1b[A", "\x1b[3~"
    edit(e, "echo ac" + left + "b\r", "abc")
    edit(e, "xecho abxc" + left + left + delete + "\x1b[H" + delete
         + "\x1b[F" + "d\r", "abcd")
    assert e.sh("pwd") == "/root"
    edit(e, up + "\r", "/root")


def test_grep_finds_lines_in_a_file_and_a_pipe(e):
    assert e.sh("grep CONSOLE /etc/config.sys") == \
        "CONSOLE=UART1 /usr/bin/getty\nCONSOLE=/usr/lib/vdp.drv /usr/bin/getty"
    assert e.sh("echo needle-in-haystack | grep needle") == \
        "needle-in-haystack"
    assert e.sh("echo one-two-three | grep zzz") == ""


def test_cat_reports_a_missing_file(e):
    assert e.sh("cat /etc/missing") == \
        'ERROR: Could not open file "/etc/missing": No such entry found'


def test_mkdir_touch_mv_rm_and_rmdir_manage_files(e):
    assert e.sh("mkdir /work") == ""
    assert e.sh("touch /work/first") == ""
    assert ll_names(e, "/work") == ["first"]
    assert e.sh("mv /work/first /work/second-with-a-long-name") == ""
    assert ll_names(e, "/work") == ["second-with-a-long-name"]
    assert e.sh("rmdir /work") == \
        'ERROR: Could not remove directory "/work": Directory not empty'
    assert e.sh("rm /work/second-with-a-long-name") == ""
    assert e.sh("rm /work/missing") == \
        'ERROR: Could not remove "/work/missing": No such entry found'
    assert e.sh("rmdir /work") == ""
    assert "work/" not in ll_names(e, "/")


def test_cp_copies_text_files(e):
    config = e.sh("cat /etc/config.sys")
    assert e.sh("cp /etc/config.sys /etc/config.copy") == ""
    assert e.sh("cat /etc/config.copy") == config
    assert e.sh("mkdir /copies") == ""
    assert e.sh("cp /etc/hostname /etc/issue /copies") == ""
    assert ll_names(e, "/copies") == ["hostname", "issue"]
    assert e.sh("cat /copies/hostname") == HOSTNAME
    assert e.sh("cp /etc/missing /copies") == \
        'ERROR: Could not open file "/etc/missing": No such entry found'


def test_cp_copies_a_command_that_then_runs(e):
    # An overlay starts with a NULL pointer, so a copy that treats its buffer
    # as a string writes nothing.
    assert e.sh("mkdir /usr/bin/hello2") == ""
    assert e.sh("cp /usr/bin/helloworld/main.overlay /usr/bin/hello2") == ""
    original = e.sh("ll /usr/bin/helloworld").splitlines()[-1].split()
    copied = e.sh("ll /usr/bin/hello2").splitlines()[-1].split()
    assert copied[3] == original[3], (original, copied)
    assert e.sh("hello2") == "Hello, world!"


def test_mv_moves_a_command_into_its_directory(e):
    assert e.sh("cd") == ""
    assert e.sh("mkdir tmp") == ""
    assert e.sh("cp /usr/bin/helloworld/main.overlay tmp/main.overlay") == ""
    assert e.sh("mkdir /usr/bin/hello3") == ""
    assert e.sh("mv tmp/main.overlay /usr/bin/hello3") == ""
    assert e.sh("hello3") == "Hello, world!"
    assert e.sh("mv /usr/bin/hello3/main.overlay /root/tmp") == ""
    assert ll_names(e, "/root/tmp") == ["main.overlay"]
    assert e.sh("mv /root/tmp/main.overlay /usr/bin/hello3") == ""
    assert ll_names(e, "/root/tmp") == []
    assert e.sh("hello3") == "Hello, world!"


def test_directories_grow_past_their_first_cluster(e):
    # A 1 KiB cluster holds 32 entries, and each of these names takes three.
    names = ["file-with-long-name-%02d" % n for n in range(16)]
    assert e.sh("mkdir /many") == ""
    for name in names:
        assert e.sh("touch /many/" + name) == ""
    assert e.sh("cp /etc/config.sys /many/config.sys") == ""
    assert ll_names(e, "/many") == sorted(names + ["config.sys"])
    assert e.sh("cat /many/config.sys") == e.sh("cat /etc/config.sys")


def test_ed_writes_a_new_file(e):
    e.send("ed\r")
    for line in ("a", "one", "two", "three", "."):
        time.sleep(0.2)
        e.send(line + "\r")
    time.sleep(0.2)
    e.send("w /etc/ed.txt\r")
    e.child.expect(r"14\r*\n", timeout=20)
    e.send("q\r")
    e.child.expect(PROMPT, timeout=20)
    assert e.sh("cat /etc/ed.txt") == "one\ntwo\nthree"


def test_dirtest_reports_entry_types(e):
    out = e.sh("dirtest")
    assert 'd_name="hostname" d_type=8' in out, out
    assert 'd_name="usr" d_type=4' in out, out


def test_dump_memory_allocations_lists_the_heap(e):
    out = e.sh("dumpMemoryAllocations").splitlines()
    assert out[0] == "Outstanding allocations:", out
    split = out.index("Available memory blocks:")
    owned = [re.fullmatch(r"  0x[0-9a-f]+: \d+ bytes owned by \d+", line)
             for line in out[1:split]]
    free = [re.fullmatch(r"  0x[0-9a-f]+: \d+ bytes available", line)
            for line in out[split + 1:]]
    assert owned and all(owned), out
    assert free and all(free), out


def test_kill_terminates_a_background_process(e):
    assert e.sh("looseLoop &") == ""
    pids = [pid for pid, _user, name in ps_entries(e) if name == "looseLoop"]
    assert len(pids) == 1, pids
    assert e.sh("kill %d" % pids[0]) == \
        "Termination of process %d successful." % pids[0]
    assert "looseLoop" not in [name for _pid, _user, name in ps_entries(e)]


def test_ctrl_c_kills_foreground_commands(e):
    before = ps_entries(e)
    for command in ("looseLoop", "tightLoop", "cat"):
        e.send(command + "\r")
        time.sleep(1)
        e.child.send("\x03")
        e.child.expect(PROMPT, timeout=10)
        assert "overflowed" not in e.child.before, e.child.before
        assert ps_entries(e) == before, command
    assert e.sh("echo alive") == "alive"


def test_commands_that_cannot_load_leave_the_shell_running(e):
    before = ps_entries(e)
    assert e.sh("mkdir /usr/bin/corrupt") == ""
    assert e.sh("cp /etc/config.sys /usr/bin/corrupt/main.overlay") == ""
    for command in ("corrupt", "corrupt &"):
        assert e.sh(command) == "corrupt: cannot execute"
        assert ps_entries(e) == before, command
    assert e.sh("looseLoop &") == ""
    pids = [pid for pid, _user, name in ps_entries(e) if name == "looseLoop"]
    assert len(pids) == 1, pids
    assert e.sh("kill %d" % pids[0]) == \
        "Termination of process %d successful." % pids[0]


def test_every_command_prints_its_usage_for_help_options(e):
    # Earlier tests leave copied and corrupt commands in /usr/bin.
    built = sorted(os.listdir(os.path.join(REPO_ROOT, "usr", "bin")))
    installed = [n.rstrip("/") for n in ll_names(e, "/usr/bin")]
    assert set(built) <= set(installed), (built, installed)
    for name in built:
        out = e.sh(name + " --help")
        assert out.startswith("Usage: " + name), out
        assert "Options:" in out.splitlines(), out
    assert e.sh("echo still-here") == "still-here"


def test_soft_reset_boots_again(e):
    e.send("shutdown -r\r")
    e.child.expect(r"NanoOs [^\r\n]* %s tty0" % HOSTNAME, timeout=30)
    e.login()
    assert e.sh("echo after-reset") == "after-reset"
    assert e.sh("hello2") == "Hello, world!"


def test_no_scheduler_queue_errors(e):
    assert "Could not push process" not in e.output(), \
        "the scheduler reported a queue overflow"


def test_no_traps_were_reported(e):
    assert "TRAP" not in e.output(), "the HAL reported a trap"


def test_filesystem_is_consistent_after_shutdown(e):
    e.send("shutdown -h\r")
    time.sleep(3)
    partition = os.path.join(e.tmp, "partition.img")
    subprocess.run(["dd", "if=" + os.path.join(e.tmp, "NanoOs.img"),
                    "of=" + partition, "bs=1M", "skip=2", "status=none"],
                   check=True)
    out = subprocess.run(["/sbin/fsck.vfat", "-n", partition],
                         capture_output=True, text=True)
    problems = [line for line in (out.stdout + out.stderr).splitlines()
                if not FSCK_KNOWN.match(line)]
    assert problems == [], problems


TESTS = [test_boots_to_a_login_prompt,
         test_echo_roundtrips_its_argument,
         test_ll_lists_the_root_directory,
         test_config_sys_is_the_agon_one,
         test_vdp_driver_and_its_getty_are_running,
         test_helloworld_prints_its_greeting,
         test_help_describes_every_command,
         test_cd_and_pwd_track_the_working_directory,
         test_line_editor_moves_edits_and_recalls,
         test_grep_finds_lines_in_a_file_and_a_pipe,
         test_cat_reports_a_missing_file,
         test_mkdir_touch_mv_rm_and_rmdir_manage_files,
         test_cp_copies_text_files,
         test_cp_copies_a_command_that_then_runs,
         test_mv_moves_a_command_into_its_directory,
         test_directories_grow_past_their_first_cluster,
         test_ed_writes_a_new_file,
         test_dirtest_reports_entry_types,
         test_dump_memory_allocations_lists_the_heap,
         test_kill_terminates_a_background_process,
         test_ctrl_c_kills_foreground_commands,
         test_commands_that_cannot_load_leave_the_shell_running,
         test_every_command_prints_its_usage_for_help_options,
         test_soft_reset_boots_again,
         test_no_scheduler_queue_errors,
         test_no_traps_were_reported,
         test_filesystem_is_consistent_after_shutdown]


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
