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

"""End-to-end tests of FAT32 name and path lookup."""

from harness import CATFILE_CONTENTS, fsck_problems, ll_names


def test_long_names_match_on_every_fragment(s):
    # The two 27-character names share their last two 13-character LFN
    # fragments, and the 13-character name fills one fragment with no
    # terminator.
    s.login()
    missing = 'ERROR: Could not open file "%s": No such entry found'
    assert s.sh("mv /etc/catfile /etc/aaaaaaaaaaaaa-same-tail.txt") == ""
    assert s.sh("touch /etc/abcdefghijklm") == ""
    assert s.sh("cat /etc/bbbbbbbbbbbbb-same-tail.txt") == \
        missing % "/etc/bbbbbbbbbbbbb-same-tail.txt"
    assert s.sh("cat /etc/aaaaaaaaaaaaa-same-tail.txt") == CATFILE_CONTENTS
    assert s.sh("cat /etc/abcdefghijklmn") == missing % "/etc/abcdefghijklmn"
    assert s.sh("cat /etc/abcdefghijkl") == missing % "/etc/abcdefghijkl"
    assert s.sh("cat /etc/abcdefghijklm") == ""
    assert ll_names(s, "/etc") == \
        ["aaaaaaaaaaaaa-same-tail.txt", "abcdefghijklm", "hostname", "issue"]


def test_path_components_match_whole_names(s):
    # Each directory in a path is looked up as a slice of the path, so a
    # component that is only a prefix of a directory's name must not match it.
    s.login()
    missing = 'ERROR: Could not open file "%s": No such entry found'
    assert s.sh("mkdir /etc/abcdefgh-long-dir") == ""
    assert s.sh("touch /etc/abcdefgh-long-dir/f") == ""
    assert s.sh("cat /etc/abcdefgh-long-dir/f") == ""
    assert s.sh("cat /etc/ABCDEFGH-Long-Dir/f") == \
        missing % "/etc/ABCDEFGH-Long-Dir/f"
    assert s.sh("cat /etc/abcdefgh-long/f") == missing % "/etc/abcdefgh-long/f"
    assert s.sh("cat /etc/abcdefgh-long-dirx/f") == \
        missing % "/etc/abcdefgh-long-dirx/f"


def test_relative_paths_resolve_against_the_working_directory(s):
    s.login()
    assert s.sh("cd /etc") == ""
    assert s.sh("cat catfile") == CATFILE_CONTENTS
    assert ll_names(s, ".") == ["catfile", "hostname", "issue"]
    assert s.sh("mkdir newdir") == ""
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue", "newdir/"]
    assert s.sh("mv catfile newdir/moved") == ""
    assert ll_names(s, "/etc/newdir") == ["moved"]
    assert s.sh("cd newdir") == ""
    assert s.sh("cat ../newdir/./moved") == CATFILE_CONTENTS
    assert s.sh("rm moved") == ""
    assert s.sh("cd ..") == ""
    assert s.sh("rmdir newdir") == ""
    assert ll_names(s, "/etc") == ["hostname", "issue"]
    assert fsck_problems(s) == []


def test_directories_grow_past_their_first_cluster(s):
    # A 1 KiB cluster holds 32 entries, and each of these names takes three.
    s.login()
    names = ["file-with-long-name-%02d" % n for n in range(40)]
    assert s.sh("mkdir /etc/many") == ""
    for name in names:
        assert s.sh("touch /etc/many/" + name) == ""
    assert s.sh("cp /etc/catfile /etc/many/catfile") == ""
    assert ll_names(s, "/etc/many") == sorted(names + ["catfile"])
    assert s.sh("cat /etc/many/catfile") == CATFILE_CONTENTS
    assert s.sh("rm /etc/many/" + names[0]) == ""
    assert ll_names(s, "/etc/many") == sorted(names[1:] + ["catfile"])
    assert fsck_problems(s) == []


def test_names_match_only_in_their_exact_case(s):
    s.login()
    missing = 'ERROR: Could not open file "%s": No such entry found'
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS
    assert s.sh("cat /etc/CatFile") == missing % "/etc/CatFile"
    assert s.sh("cat /ETC/catfile") == missing % "/ETC/catfile"
    assert s.sh("mkdir /etc/Mixed-Case-Dir") == ""
    assert s.sh("touch /etc/Mixed-Case-Dir/f") == ""
    assert s.sh("cat /etc/mixed-case-dir/f") == \
        missing % "/etc/mixed-case-dir/f"
    assert s.sh("cat /etc/Mixed-Case-Dir/f") == ""


def test_short_aliases_of_long_names_do_not_resolve(s):
    # NanoOs gives "a-long-file-name.txt" the 8.3 alias "A-LONG~1.TXT".
    s.login()
    assert s.sh("cp /etc/catfile /etc/a-long-file-name.txt") == ""
    assert s.sh("cat /etc/A-LONG~1.TXT") == \
        'ERROR: Could not open file "/etc/A-LONG~1.TXT": No such entry found'
    assert s.sh("cat /etc/a-long~1.txt") == \
        'ERROR: Could not open file "/etc/a-long~1.txt": No such entry found'
    assert s.sh("touch /etc/A-LONG~1.TXT") == \
        'ERROR: Could not create "/etc/A-LONG~1.TXT": File exists'
    assert s.sh("cat /etc/a-long-file-name.txt") == CATFILE_CONTENTS


def test_names_that_differ_only_in_case_cannot_be_created(s):
    # Other systems treat these as the same file, so both can't exist.
    s.login()
    assert s.sh("touch /etc/CATFILE") == \
        'ERROR: Could not create "/etc/CATFILE": File exists'
    assert s.sh("cp /etc/issue /etc/CatFile") == \
        'ERROR: Could not create "/etc/CatFile": File exists'
    assert s.sh("mkdir /etc/Issue") == \
        'ERROR: Could not create directory "/etc/Issue": File exists'
    assert s.sh("mv /etc/issue /etc/HostName") == \
        'ERROR: Could not rename "/etc/issue" to "/etc/HostName": File exists'
    assert s.sh("cat /etc/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc") == ["catfile", "hostname", "issue"]
    assert fsck_problems(s) == []


def test_absolute_paths_are_collapsed_before_lookup(s):
    s.login()
    assert s.sh("cat /../etc/catfile") == CATFILE_CONTENTS
    assert s.sh("cat //etc//catfile") == CATFILE_CONTENTS
    assert s.sh("cat /etc/./catfile") == CATFILE_CONTENTS
    assert s.sh("cat /usr/bin/../../etc/catfile") == CATFILE_CONTENTS
    assert ll_names(s, "/etc/") == ["catfile", "hostname", "issue"]
