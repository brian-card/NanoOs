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

// Doxygen file marker
/// @file

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Link.h"

/// @fn void usage(const char *argv0)
///
/// @brief Print a usage statement for this program.
///
/// @param argv0 The argv[0] pointer passed in on the command line, i.e. the
///   full path to this program.
///
/// @return This function returns no value.
void usage(const char *argv0) {
  const char *programName = argv0;
  const char *lastSlashAt = strrchr(programName, '/');
  if (lastSlashAt != NULL) {
    programName = lastSlashAt + 1;
  }

  fprintf(stderr, "Usage: %s <link file>\n", programName);
  fprintf(stderr, "\n");
  fprintf(stderr, "Show the contents of the target of a link file.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Arguments:\n");
  fprintf(stderr, "- link file: The path to the link file where the target\n");
  fprintf(stderr, "             is stored.\n");
  fprintf(stderr, "\n");

  return;
}

/// @fn int main(int argc, char **argv)
///
/// @brief Main entry point for the program.  Validates the arguments and
/// creates the link on the filesystem.
///
/// @param argc The number of provided command line arguments.
/// @param argv The array of command line arguments.
///
/// @return Returns 0 on success, 1 on failure.
int main(int argc, char **argv) {
  if (argc != 2) {
    usage(argv[0]);
    return 1;
  }

  FILE *file = lopen(argv[1], "r");
  if (file == NULL) {
    fprintf(stderr, "Opening link failed.\n");
    return 1;
  }
  
  char buffer[96];
  while (fgets(buffer, sizeof(buffer), file)) {
    fputs(buffer, stdout);
  }
  
  fclose(file); file = NULL;

  return 0;
}

