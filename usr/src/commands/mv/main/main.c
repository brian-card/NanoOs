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


/// @file main.c
///
/// @brief Implementation of the standard POSIX `mv` command.

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/// @brief Rename a file or directory, reporting any failure.
///
/// @param source The path to rename.
/// @param destination The new path.
///
/// @return Returns 0 on success, 1 on failure.
static int moveFile(const char *source, const char *destination) {
  if (rename(source, destination) != 0) {
    fprintf(stderr, "ERROR: Could not rename \"%s\" to \"%s\": %s\n",
      source, destination, strerror(errno));
    return 1;
  }

  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <source> <destination>\n", argv[0]);
    fprintf(stderr, "       %s <source>... <directory>\n", argv[0]);
    return 1;
  }

  const char *target = argv[argc - 1];
  struct stat targetStat;
  bool targetIsDirectory
    = (lstat(target, &targetStat) == 0) && S_ISDIR(targetStat.st_mode);
  if (!targetIsDirectory) {
    if (argc > 3) {
      fprintf(stderr, "ERROR: \"%s\" is not a directory\n", target);
      return 1;
    }
    return moveFile(argv[1], target);
  }

  int returnValue = 0;
  size_t targetLength = strlen(target);
  bool needsSeparator
    = (targetLength > 0) && (target[targetLength - 1] != '/');
  for (int ii = 1; ii < argc - 1; ii++) {
    const char *baseName = strrchr(argv[ii], '/');
    baseName = (baseName == NULL) ? argv[ii] : baseName + 1;

    char *destination = (char*) malloc(targetLength + strlen(baseName) + 2);
    if (destination == NULL) {
      fprintf(stderr, "ERROR: Out of memory\n");
      return 1;
    }
    strcpy(destination, target);
    if (needsSeparator) {
      strcat(destination, "/");
    }
    strcat(destination, baseName);

    returnValue |= moveFile(argv[ii], destination);
    free(destination);
  }

  return returnValue;
}
