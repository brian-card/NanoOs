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
/// @brief Implementation of a `ver` command in the style of the MS-DOS one.

#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include "NanoOsUtils.h"

/// @brief Print the usage summary for ver.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: ver\n"
    "\n"
    "Display the operating system version.\n"
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

  struct utsname *utsname = (struct utsname*) malloc(sizeof(struct utsname));
  if ((utsname == NULL) || (uname(utsname) != 0)) {
    fputs("ERROR: Could not get the system version.\n", stderr);
    free(utsname);
    return 1;
  }

  printf("\n%s Version %s\n", utsname->sysname, utsname->release);
  fputs(
    "Copyright (c) 2026 Brian Card\n"
    "Released under the MIT License.\n"
    "\n",
    stdout);

  free(utsname);
  return 0;
}
