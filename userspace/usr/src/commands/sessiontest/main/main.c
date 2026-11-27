///////////////////////////////////////////////////////////////////////////////
///
/// @file              main.c
///
/// @brief             The end-to-end tests' bridge to a console session:  a
///                    window server that opens a session and bridges it to
///                    its own console port, as sysnook will to a terminal
///                    window.  The Bridge and Fill overlays do the work.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NanoOsUtils.h"
#include "UserMessages.h"
#include "UserProcesses.h"
#include "SessionTest.h"

/// @fn static void printUsage(FILE *stream)
///
/// @brief Print sessiontest's usage.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: sessiontest <uart> [rows columns]\n"
    "       sessiontest fill\n"
    "\n"
    "Register as the window server and open a console session.  With a UART,\n"
    "bridge the session to this console port until it ends:  what's typed\n"
    "here is the session's input, and its output is written to the UART.\n"
    "With fill, open sessions until one fails, close one and open another,\n"
    "then close them all.\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  } else if ((argc != 2) && (argc != 4)) {
    printUsage(stderr);
    return 1;
  }

  if (windowServerRegister() != 0) {
    fprintf(stderr, "sessiontest: could not register: %s\n",
      strerror(errno));
    return 1;
  }

  if (strcmp(argv[1], "fill") == 0) {
    int status = 1;
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Fill", "fillSessions",
      &status);
    return status;
  }

  BridgeArgs bridgeArgs = {
    .uart = (int32_t) strtol(argv[1], NULL, 10),
    .rows = 24,
    .columns = 80,
    .status = 1,
  };

  if (argc == 4) {
    bridgeArgs.rows = (uint8_t) strtol(argv[2], NULL, 10);
    bridgeArgs.columns = (uint8_t) strtol(argv[3], NULL, 10);
  }

  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Bridge", "bridgeSession",
    &bridgeArgs);

  return bridgeArgs.status;
}
