////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file
///
/// @brief Test-fixture command for the e2e regression suite (see
/// test/e2e/run.py's test_dirent_* tests): exercises opendir/readdir/closedir
/// against the real FAT32 image every e2e test runs against.  Not a
/// general-purpose utility -- follows the same pattern as the looseLoop and
/// tightLoop commands, which likewise exist only to give e2e tests something
/// to invoke.

#include <stdio.h>
#include <dirent.h>
#include <stdint.h>

static void listDir(const char *path) {
  printf("opendir(%s):\n", path);
  DIR *dirp = opendir(path);
  if (dirp == NULL) {
    printf("  opendir failed\n");
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dirp)) != NULL) {
    printf("  d_name=\"%s\" d_type=%d d_ino=%lu d_reclen=%u\n",
      entry->d_name, (int) entry->d_type,
      (unsigned long) entry->d_ino, (unsigned int) entry->d_reclen);
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
  printf("opendir(/nonexistent) -> %s\n",
    (opendir("/nonexistent") == NULL) ? "NULL" : "non-NULL");
  printf("opendir(/etc/hostname) -> %s\n",
    (opendir("/etc/hostname") == NULL) ? "NULL" : "non-NULL");

  return 0;
}
