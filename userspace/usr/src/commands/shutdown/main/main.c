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
/// @brief Entrypoint into the looseLoop program.

// Standard C includes
#include <stdio.h>
#include <string.h>

// NanoOs includes
#include "NanoOsHardware.h"
#include "NanoOsUtils.h"

/// @brief Print the usage summary for shutdown.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: shutdown <-r | --reboot | -h | --halt>\n"
    "\n"
    "Shut the system down.\n"
    "\n"
    "Options:\n"
    "  -r, --reboot  Reboot the system.\n"
    "  -h, --halt    Halt the system and power it off.\n"
    "  --help        Show this help and exit.\n",
    stream);
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }
  if (argc < 2) {
    printUsage(stderr);
    return 1;
  }

  const char *shutdownType = argv[1];

  int returnValue = 0;
  if ((strcmp(shutdownType, "-r") == 0)
    || (strcmp(shutdownType, "--reboot") == 0)
  ) {
    nanoOsShutdown(NANO_OS_SHUTDOWN_RESET);
  } else if ((strcmp(shutdownType, "-h") == 0)
    || (strcmp(shutdownType, "--halt") == 0)
  ) {
    nanoOsShutdown(NANO_OS_SHUTDOWN_OFF);
  } else {
    fprintf(stderr, "Error! Unknown shutdown type: \"%s\"\n", shutdownType);
    printUsage(stderr);
    return 1;
  }

  return returnValue;
}

