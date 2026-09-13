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
/// @brief Standard Unix `ls` command implementation.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdint.h>

void listDir(const char *path) {
  DIR *dirp = opendir(path);
  if (dirp == NULL) {
    puts(strerror(errno));
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dirp)) != NULL) {
    printf("%s%s\n", entry->d_name, (entry->d_type == DT_DIR) ? "/" : "");
  }

  closedir(dirp);
}

int main(int argc, char **argv) {
  const char *path = getenv("PWD");
  if (argc > 1) {
    path = argv[1];
  }

  listDir(path);

  return 0;
}
