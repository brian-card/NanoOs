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

void usage(const char *argv0) {
  const char *programName = strrchr(argv0, '/');
  if (programName != NULL) {
    programName++;
  } else {
    programName = argv0;
  }

  fprintf(stderr, "Usage: %s <shutdown type>\n", programName);
  fprintf(stderr, "\n");
  fprintf(stderr, "Available shutdown types:\n");
  fprintf(stderr, "-r --reboot    Reboot the system\n");
  fprintf(stderr, "-h --halt      Halt and power down the system\n");
  fprintf(stderr, "\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
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
    usage(argv[0]);
    return 1;
  }

  return returnValue;
}

