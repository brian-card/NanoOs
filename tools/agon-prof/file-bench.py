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

"""Count emulator instructions for file-heavy workloads on the Agon.

Boots a prebuilt image in the emulator test layer's private emulator copy,
with fab-agon-emulator's profiler (branch nanoos-profiler) counting
instructions in one window per workload.  Each window opens after a command
has been typed and closes when the prompt returns, so typing is not counted.
An empty Return is measured the same way as a control.

Usage:  file-bench.py <image> <firmware> <work dir> [boots] [repetitions]

<image> and <firmware> come from
  ./builddev AgonLight2 nanoemu <image> contiguous "" <firmware>
where <firmware> starts as a copy of the emulator's mos_platform.bin.  The
image is copied for each boot, so it is never modified.
"""

import json
import os
import shutil
import statistics
import subprocess
import sys
import time

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "test", "emulator"))
import run as emulator_layer  # noqa: E402

PARTITION = "@@2097152"
UP = "\x1b[A"


def workload_files(work):
    """Write the files the workloads use and return their image paths.

    @param work The directory to write them in.

    @return A list of (local path, image path) pairs.
    """
    script = os.path.join(work, "s50.sh")
    with open(script, "w") as f:
        f.write("".join("export X=%02d\n" % n for n in range(1, 51)))
    history = os.path.join(work, "mush_history")
    with open(history, "w") as f:
        f.write("".join("export H=%02d\n" % n for n in range(1, 51)))
    text = os.path.join(work, "f4k.txt")
    with open(text, "w") as f:
        f.write(("a" * 63 + "\n") * 64)
    return [(script, "::/userspace/root/s50.sh"),
            (history, "::/userspace/root/.mush_history"),
            (text, "::/userspace/root/f4k.txt")]


class BenchEmulator(emulator_layer.Emulator):
    """An emulator test layer boot of a prebuilt image."""

    def __init__(self, tmp, image, firmware, files):
        """Copy the image and firmware in, add the files, and boot.

        @param tmp The directory for this boot.
        @param image The prebuilt SD card image.
        @param firmware The firmware builddev wrote for that image.
        @param files The (local path, image path) pairs to copy in.
        """
        real_run = subprocess.run

        def prebuilt(args, **kwargs):
            if args[0] != "./builddev":
                return real_run(args, **kwargs)
            shutil.copy(image, args[3])
            shutil.copy(firmware, args[6])
            env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
            for local, path in files:
                real_run(["mcopy", "-o", "-i", args[3] + PARTITION, local,
                          path], check=True, env=env)
            return subprocess.CompletedProcess(args, 0)

        subprocess.run = prebuilt
        try:
            super().__init__(tmp)
        finally:
            subprocess.run = real_run


class Profiler:
    """Open and close windows of fab-agon-emulator's profiler."""

    def __init__(self, path):
        self.path = path

    def start(self):
        if os.path.exists(self.path):
            os.remove(self.path)
        open(self.path + ".on", "w").close()
        time.sleep(0.005)

    def stop(self):
        """Close the window and return its instruction count."""
        os.remove(self.path + ".on")
        deadline = time.time() + 10
        while not os.path.exists(self.path):
            if time.time() > deadline:
                raise RuntimeError("the profiler never closed its window")
            time.sleep(0.01)
        time.sleep(0.05)
        with open(self.path) as f:
            return int(f.read().split()[1])


def measure(e, profiler, workload):
    """Count the instructions a workload takes, from its first key in the
    window until the prompt returns, and check that it did what it should.

    @param e The BenchEmulator.
    @param profiler The Profiler.
    @param workload A (name, typed, keys, after, check) tuple:  what to type
      before the window opens, what to send at once inside it, a function to
      call with the emulator after that or None, and a function that takes
      the output and returns True if the workload ran correctly.

    @return The instruction count.
    """
    name, typed, keys, after, check = workload
    e.send(typed)
    time.sleep(0.2)
    profiler.start()
    e.child.send(keys)
    if after is not None:
        after(e)
    e.child.expect(emulator_layer.PROMPT, timeout=60)
    count = profiler.stop()
    output = emulator_layer.LOG_LINE.sub("", e.child.before.replace("\r", ""))
    if not check(output):
        raise RuntimeError("%s did not run as expected:\n%s" % (name, output))
    return count


def edit(e):
    e.child.expect("4096", timeout=30)
    e.child.send("1s/a/b/\rw\rq\r")


WORKLOADS = [
    ("control", "", "\r", None, lambda out: out.strip() == ""),
    ("script", "mush /root/s50.sh", "\r", None,
     lambda out: out.strip() == "mush /root/s50.sh"),
    ("history", "", UP * 10 + "\r", None, lambda out: "export H=" in out),
    ("cat", "cat /root/f4k.txt", "\r", None,
     lambda out: out.count("a" * 63) == 64),
    ("cp", "cp /root/f4k.txt /root/f4k.cp", "\r", None,
     lambda out: out.strip() == "cp /root/f4k.txt /root/f4k.cp"),
    ("ed", "ed /root/f4k.txt", "\r", edit, lambda out: "4096" in out),
]


def main():
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    image, firmware, work = (os.path.abspath(a) for a in sys.argv[1:4])
    boots = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    reps = int(sys.argv[5]) if len(sys.argv) > 5 else 5
    os.makedirs(work, exist_ok=True)
    files = workload_files(work)
    profile = os.path.join(work, "prof")
    os.environ["NANOOS_PROFILE"] = profile
    profiler = Profiler(profile)
    samples = {name: [] for name in ["boot"] + [w[0] for w in WORKLOADS]}
    raw = open(os.path.join(work, "samples.jsonl"), "w")

    for boot in range(boots):
        tmp = os.path.join(work, "boot%d" % boot)
        shutil.rmtree(tmp, ignore_errors=True)
        profiler.start()
        e = BenchEmulator(tmp, image, firmware, files)
        try:
            e.child.expect("login: ", timeout=120)
            samples["boot"].append(profiler.stop())
            raw.write(json.dumps({"boot": boot, "workload": "boot",
                                  "rep": 0, "instructions":
                                  samples["boot"][-1]}) + "\n")
            e.send("root\r")
            e.child.expect("password: ", timeout=10)
            e.send("rootroot\r")
            e.child.expect(emulator_layer.PROMPT, timeout=20)
            for rep in range(reps):
                for workload in WORKLOADS:
                    name = workload[0]
                    count = measure(e, profiler, workload)
                    samples[name].append(count)
                    raw.write(json.dumps({"boot": boot, "workload": name,
                                          "rep": rep,
                                          "instructions": count}) + "\n")
                    raw.flush()
        finally:
            e.close()

    control = statistics.median(samples["control"])
    print("%-8s %5s %12s %12s %12s %14s" % (
        "workload", "n", "min", "median", "max", "median-control"))
    for name, values in samples.items():
        median = statistics.median(values)
        net = median if name in ("boot", "control") else median - control
        print("%-8s %5d %12d %12d %12d %14d" % (
            name, len(values), min(values), median, max(values), net))
    return 0


if __name__ == "__main__":
    sys.exit(main())
