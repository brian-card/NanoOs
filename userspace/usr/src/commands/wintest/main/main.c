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
/// @brief Test-fixture command for the e2e regression suite:  connects to
/// the window server and prints what it hears.  Not a general-purpose
/// utility, like termtest.

#include <stdio.h>
#include <string.h>
#include "NanoOsUtils.h"
#include "../../../src/kernel/Window.h"

/// @fn static void printUsage(FILE *stream)
///
/// @brief Print the usage summary for wintest.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: wintest hello|wait|hold\n"
    "\n"
    "Connect to the window server.\n"
    "  hello  Print its greeting, then disconnect.\n"
    "  wait   Print its greeting, then wait until the window server goes\n"
    "         away.\n"
    "  hold   Wait until the window server goes away, printing nothing.\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

/// @fn static const char* roleName(uint8_t role)
///
/// @brief Get the name wintest prints for a WinRole.
///
/// @param role The role.
///
/// @return Returns the name.
static const char* roleName(uint8_t role) {
  if (role == WIN_ROLE_DESKTOP) {
    return "desktop";
  } else if (role == WIN_ROLE_TASKBAR) {
    return "taskbar";
  }
  return "none";
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  } else if ((argc != 2)
    || ((strcmp(argv[1], "hello") != 0) && (strcmp(argv[1], "wait") != 0)
      && (strcmp(argv[1], "hold") != 0))
  ) {
    printUsage(stderr);
    return 1;
  }

  WinSession session;
  WinError status = winConnect(&session);
  if (status == WIN_ERR_NOT_FOUND) {
    fputs("wintest: no window server\n", stderr);
    return 1;
  } else if (status != WIN_OK) {
    fprintf(stderr, "wintest: error %d\n", (int) status);
    return 1;
  }

  bool quiet = (strcmp(argv[1], "hold") == 0);
  const WinHelloArgs *hello = &session.hello;
  if (!quiet) {
    printf("wintest: screen %ux%u work %d,%d,%d,%d cell %ux%u role %s\n",
      (unsigned int) hello->screenWidth, (unsigned int) hello->screenHeight,
      hello->workArea.x, hello->workArea.y, hello->workArea.w,
      hello->workArea.h, (unsigned int) hello->cellWidth,
      (unsigned int) hello->cellHeight, roleName(hello->role));
  }
  if (strcmp(argv[1], "hello") == 0) {
    winDisconnect(&session);
    return 0;
  }

  while (1) {
    ProcessMessage *message = processMessageQueueWait(NULL);
    if (message == NULL) {
      continue;
    } else if (winIsDisconnected(message)) {
      processMessageRelease(message);
      if (!quiet) {
        puts("wintest: disconnected");
      }
      return 0;
    }
    processMessageSetDone(message);
    if (processMessageWaiting(message) == false) {
      processMessageRelease(message);
    }
  }
}
