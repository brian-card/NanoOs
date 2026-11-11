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
/// @brief Implementation of a `caps` command that prints the directory
/// capabilities of the process running it.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dircap.h>
#include "NanoOsUtils.h"

/// @brief Print the usage summary for caps.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: caps\n"
    "\n"
    "Print the directories this process may use, one per line, with the\n"
    "permissions it has in each in the form /etc/capabilities uses:  r read,\n"
    "w write, c create, d delete, l list, s stat, x exec.\n"
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
  if (argc != 1) {
    printUsage(stderr);
    return 1;
  }

  int size = dircapGet(NULL, 0);
  DirCapabilities *set = NULL;
  if (size > 0) {
    set = (DirCapabilities*) malloc(size);
    if (set == NULL) {
      errno = ENOMEM;
    } else if (dircapGet(set, size) != size) {
      free(set); set = NULL;
    }
  }
  if (set == NULL) {
    fprintf(stderr, "ERROR: Could not get the capabilities: %s\n",
      strerror(errno));
    return 1;
  }

  const char *letters = "rwcdlsx";
  DirCapability *capability = set->capabilities;
  for (uint8_t ii = 0; ii < set->numCapabilities; ii++) {
    char permissions[8];
    uint8_t numLetters = 0;
    for (uint8_t bit = 0; letters[bit] != '\0'; bit++) {
      if ((capability->mask & (1 << bit)) != 0) {
        permissions[numLetters++] = letters[bit];
      }
    }
    permissions[numLetters] = '\0';
    printf("%-7s  %.*s%s\n", permissions,
      (int) capability->len, capability->name,
      ((capability->mask & DIRCAP_FULL_PATH) != 0) ? " (full path)" : "");
    capability = (DirCapability*) &capability->name[capability->len];
  }

  free(set);
  return 0;
}
