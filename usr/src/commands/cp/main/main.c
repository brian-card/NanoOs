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
/// @brief Implementation of the standard POSIX `cp` command.  Only regular
/// files are supported; there is no recursive copy.

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "NanoOsUtils.h"

/// @brief Copy the contents of a regular file to a new or existing file.
///
/// @param source The path of the file to copy.
/// @param destination The path of the file to create or overwrite.
///
/// @return Returns 0 on success, 1 on failure.
static int copyFile(const char *source, const char *destination) {
  struct stat sourceStat;
  if (lstat(source, &sourceStat) != 0) {
    fprintf(stderr, "ERROR: Could not open file \"%s\": %s\n",
      source, strerror(errno));
    return 1;
  }
  if (S_ISDIR(sourceStat.st_mode)) {
    fprintf(stderr, "ERROR: \"%s\" is a directory\n", source);
    return 1;
  }

  // Opening the destination for writing would truncate the source.
  struct stat destinationStat;
  if ((lstat(destination, &destinationStat) == 0)
    && (destinationStat.st_dev == sourceStat.st_dev)
    && (destinationStat.st_ino == sourceStat.st_ino)
  ) {
    fprintf(stderr, "ERROR: \"%s\" and \"%s\" are the same file\n",
      source, destination);
    return 1;
  }

  FILE *inputFile = fopen(source, "r");
  if (inputFile == NULL) {
    fprintf(stderr, "ERROR: Could not open file \"%s\": %s\n",
      source, strerror(errno));
    return 1;
  }
  FILE *outputFile = fopen(destination, "w");
  if (outputFile == NULL) {
    fprintf(stderr, "ERROR: Could not create \"%s\": %s\n",
      destination, strerror(errno));
    fclose(inputFile);
    return 1;
  }

  int returnValue = 0;
  char buffer[96];
  size_t bytesRead = fread(buffer, 1, sizeof(buffer), inputFile);
  while (bytesRead > 0) {
    if (fwrite(buffer, 1, bytesRead, outputFile) != bytesRead) {
      fprintf(stderr, "ERROR: Could not write \"%s\": %s\n",
        destination, strerror(errno));
      returnValue = 1;
      break;
    }
    bytesRead = fread(buffer, 1, sizeof(buffer), inputFile);
  }

  fclose(inputFile);
  if ((fclose(outputFile) != 0) && (returnValue == 0)) {
    fprintf(stderr, "ERROR: Could not write \"%s\": %s\n",
      destination, strerror(errno));
    returnValue = 1;
  }

  return returnValue;
}

/// @brief Print the usage summary for cp.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: cp <source> <destination>\n"
    "       cp <source>... <directory>\n"
    "\n"
    "Copy a regular file to a new file, or over an existing one.  When the last\n"
    "argument is a directory, copy each source into it under the source's own\n"
    "name.  Directories can't be copied, and a file can't be copied onto itself.\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }
  if (argc < 3) {
    printUsage(stderr);
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
    return copyFile(argv[1], target);
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

    returnValue |= copyFile(argv[ii], destination);
    free(destination);
  }

  return returnValue;
}
