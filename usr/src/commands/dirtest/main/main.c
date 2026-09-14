////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////


// Doxygen marker
/// @file main.c
///
/// @brief Test-fixture command for the e2e regression suite (see
/// test/e2e/run.py's test_dirent_* tests): exercises opendir/readdir/closedir
/// against the real FAT32 image every e2e test runs against.  Not a
/// general-purpose utility -- follows the same pattern as the looseLoop and
/// tightLoop commands, which likewise exist only to give e2e tests something
/// to invoke.

#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

static void listDir(const char *path) {
  printf("opendir(%s):\n", path);
  DIR *dirp = opendir(path);
  if (dirp == NULL) {
    printf("  opendir failed\n");
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dirp)) != NULL) {
    printf("  d_name=\"%s\" d_type=%d d_ino=%llu d_reclen=%u\n",
      entry->d_name, (int) entry->d_type,
      (unsigned long long) entry->d_ino, (unsigned int) entry->d_reclen);
  }

  closedir(dirp);
}

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;

  // Fixed scenario the e2e tests assert against: test/e2e/mkimage.sh always
  // builds a root directory containing exactly /etc and /usr (each in turn
  // containing known files/subdirectories), so this needs no arguments.
  listDir("/");
  listDir("/etc");
  listDir("/usr");

  errno = 0;
  DIR *missing = opendir("/nonexistent");
  printf("opendir(/nonexistent) -> %s, errno=%d\n",
    (missing == NULL) ? "NULL" : "non-NULL", errno);

  errno = 0;
  DIR *notADir = opendir("/etc/hostname");
  printf("opendir(/etc/hostname) -> %s, errno=%d, strerror=\"%s\"\n",
    (notADir == NULL) ? "NULL" : "non-NULL", errno, strerror(errno));

  // readdir must not disturb errno when it returns NULL because the
  // directory was simply exhausted (not a real error).
  DIR *etc = opendir("/etc");
  while (readdir(etc) != NULL) {
    // Drain the directory.
  }
  errno = 0;
  struct dirent *pastEnd = readdir(etc);
  printf("readdir past end of /etc -> %s, errno=%d\n",
    (pastEnd == NULL) ? "NULL" : "non-NULL", errno);
  closedir(etc);

  // closedir on a NULL DIR must fail and set errno.
  errno = 0;
  int closeResult = closedir(NULL);
  printf("closedir(NULL) -> %d, errno=%d\n", closeResult, errno);

  return 0;
}
