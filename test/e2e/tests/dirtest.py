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

"""End-to-end tests of the dirent API, through dirtest."""

import re


def test_dirent_lists_directories_with_correct_type(s):
    # test/e2e/mkimage.sh always builds /usr containing bin and lib (via
    # mmd), both real subdirectories.  mtools writes lowercase 8.3 names
    # using the ntReserved case bits rather than an LFN, so the
    # case-preserving name comes back lowercase, not the legacy all-uppercase
    # short-name rendering.
    s.login()
    out = s.sh("dirtest")
    for name in ("bin", "lib"):
        assert f'd_name="{name}" d_type=4' in out, out


def test_dirent_cannot_list_the_root(s):
    # EACCES is 4 in src/user/NanoOsErrno.h.
    s.login()
    out = s.sh("dirtest")
    assert "opendir(/):\n  opendir failed, errno=4" in out, out


def test_dirent_lists_files_with_correct_type(s):
    # /etc always contains hostname and issue, both regular files (via
    # mcopy), written lowercase (see above).
    s.login()
    out = s.sh("dirtest")
    for name in ("hostname", "issue"):
        assert f'd_name="{name}" d_type=8' in out, out


def test_dirent_includes_dot_and_dotdot_in_subdirectories(s):
    s.login()
    out = s.sh("dirtest")
    assert 'd_name="."' in out, out
    assert 'd_name=".."' in out, out


def test_dirent_opendir_rejects_missing_path_and_regular_file(s):
    # opendir must fail both for a path that doesn't exist and for a path
    # that names a regular file rather than a directory.
    s.login()
    out = s.sh("dirtest")
    assert "opendir(/etc/nonexistent) -> NULL" in out, out
    assert "opendir(/etc/hostname) -> NULL" in out, out


def test_dirent_sets_errno_correctly(s):
    # ENOENT (8) for a path that doesn't exist, ENOTDIR (25) for a path that
    # names a regular file rather than a directory (see src/user/NanoOsErrno.h
    # for the numbering) -- distinct values, not the same generic failure.
    s.login()
    out = s.sh("dirtest")
    assert "opendir(/etc/nonexistent) -> NULL, errno=8" in out, out
    assert 'opendir(/etc/hostname) -> NULL, errno=25, strerror="Not a directory"' in out, out
    # readdir must leave errno untouched when it returns NULL only because
    # the directory was exhausted -- that's not an error.
    assert "readdir past end of /etc -> NULL, errno=0" in out, out
    # closedir on a NULL DIR must fail (-1) and set errno (EBADF = 13).
    assert "closedir(NULL) -> -1, errno=13" in out, out


def test_dirstat_reports_the_entry(s):
    s.login()
    out = s.sh("dirtest")
    assert "dirstat -> 0, ino_matches=1" in out, out


def test_dirstat_ignores_a_changed_d_ino(s):
    s.login()
    out = s.sh("dirtest")
    assert "dirstat changed d_ino -> 0, ino_matches=1" in out, out


def test_dirstat_after_closedir_is_ebadf(s):
    # EBADF is 13 in src/user/NanoOsErrno.h.
    s.login()
    out = s.sh("dirtest")
    assert "dirstat after closedir -> -1, errno=13" in out, out


def test_dirstat_without_the_capability_is_eacces(s):
    # EACCES is 4 in src/user/NanoOsErrno.h.
    s.login()
    out = s.sh("dirtest")
    assert "dirstat without STAT -> -1, errno=4" in out, out


def test_readdir_returns_a_maximum_length_name_whole(s):
    # A new stream's entry buffer holds an 8.3 name, so readdir has to grow
    # it and fetch the same entry again.
    s.login()
    assert s.sh("dirtest long /tmp") == "created"
    name = "".join(str(ii % 10) for ii in range(255))
    names = s.sh("dirtest list /tmp").splitlines()
    assert name in names, names


def test_interleaved_readdirs_keep_their_own_entries(s):
    s.login()
    etc = s.sh("dirtest list /etc").splitlines()
    usr = s.sh("dirtest list /usr").splitlines()
    out = s.sh("dirtest interleave /etc /usr").splitlines()
    pairs = [re.fullmatch(r'a="([^"]*)" b="([^"]*)"', line) for line in out]
    assert all(pairs), out
    assert [m.group(1) for m in pairs if m.group(1)] == etc, out
    assert [m.group(2) for m in pairs if m.group(2)] == usr, out
    assert set(etc) != set(usr), (etc, usr)
