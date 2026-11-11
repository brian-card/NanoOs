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

"""End-to-end tests of caps."""

import os
import subprocess

from harness import help_text, REPO_ROOT
from tests.capabilities import HOMES, boot_with, repo_file

LETTERS = "rwcdlsx"


def mask_of(letters):
    """Return the mask that a string of permission letters stands for."""
    return sum(1 << LETTERS.index(letter) for letter in letters)


def test_a_plain_user_sees_the_set_the_file_grants(s):
    boot_with(s, directories=HOMES)
    s.login("user1", "user1user1")
    assert s.sh("caps").splitlines() == [
        "rwcdls   /home/user1/",
        "rlsx     /usr/bin/",
        "rx       /usr/lib/",
        "rls      /etc/",
        "rwcdls   /tmp/",
    ]


def test_root_sees_the_set_the_file_grants(s):
    s.login()
    assert s.sh("caps").splitlines() == [
        "rlsx     /usr/bin/",
        "rx       /usr/lib/",
        "rwcdls   /root/",
        "rwcdls   /etc/",
        "rwcdls   /tmp/",
        "rwcdlsx  /usr/",
    ]


def test_every_line_matches_the_mask_the_process_holds(s):
    boot_with(s, {"/etc/capabilities": repo_file("etc", "capabilities"),
                  "/system/data/capabilities":
                  repo_file("etc", "system", "capabilities")},
              HOMES + ("/menu",))
    s.login()
    held = [line.split()[1:] for line in s.sh("dirtest caps").splitlines()
            if line.startswith("cap ")]
    shown = [line.split() for line in s.sh("caps").splitlines()]
    assert [[int(mask, 16), name] for mask, name in held] == \
        [[mask_of(letters), name] for letters, name in shown], (held, shown)
    assert ["rwls", "/menu/"] in shown, shown


def test_a_full_path_entry_is_marked(s):
    if s.child.isalive():
        s.child.terminate(force=True)
    subprocess.run(["mcopy", "-s", "-i", s.img_copy + "@@2097152",
                    os.path.join(REPO_ROOT, "usr", "bin", "caps"),
                    "::/system/commands/caps"],
                   env=dict(os.environ, MTOOLS_SKIP_CHECK="1"), check=True)
    s.spawn([s.img_copy])
    s.login()
    assert s.sh("dirtest system caps").splitlines() == [
        "rwcd     /secure/ (full path)",
        "r        /etc/",
        "system spawn 0 status 0",
    ]


def test_an_argument_prints_the_usage(s):
    s.login()
    assert s.sh("caps extra") == help_text(s, "caps --help")
