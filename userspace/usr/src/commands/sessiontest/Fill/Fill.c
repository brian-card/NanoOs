///////////////////////////////////////////////////////////////////////////////
///
/// @file              Fill.c
///
/// @brief             sessiontest's fill mode:  it opens sessions until there
///                    are no more ports.
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
#include <string.h>
#include <unistd.h>

#include "ConsoleOutput.h"
#include "UserMessages.h"
#include "UserProcesses.h"

/// @fn static void waitForEnd(uint8_t port)
///
/// @brief Wait for a session to end, taking the output its shells send in the
/// meantime.
///
/// @param port The session's console port.
///
/// @return This function returns no value.
static void waitForEnd(uint8_t port) {
  while (1) {
    ProcessMessage *message = processMessageQueueWait(NULL);
    if (processMessageType(message)
      == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_SESSION_ENDED)
    ) {
      uint8_t ended = (uint8_t) (uintptr_t) processMessageData(message);
      processMessageRelease(message);
      if (ended == port) {
        return;
      }
    } else {
      consoleOutputDone(message);
    }
  }
}

/// @fn void* fillSessions(void *args)
///
/// @brief Open sessions until one fails, then close one and open another in
/// its place, and close them all.
///
/// @param args A pointer to the int that receives the exit status:  0 if at
///   least one session opened, 1 otherwise.
///
/// @return Always returns the provided pointer.
void* fillSessions(void *args) {
  int *status = (int*) args;
  *status = 1;
  uint8_t ports[CONSOLE_NUM_PORTS];
  int numPorts = 0;
  uint8_t shell = 0;
  while (numPorts < CONSOLE_NUM_PORTS) {
    if (windowServerOpenSession(24, 80, getpid(), &ports[numPorts], &shell)
      != 0
    ) {
      printf("sessiontest: open failed: %s\n", strerror(errno));
      break;
    }
    printf("sessiontest: opened port %d\n", ports[numPorts]);
    numPorts++;
  }
  if (numPorts == 0) {
    return args;
  }

  windowServerCloseSession(ports[0]);
  waitForEnd(ports[0]);
  printf("sessiontest: ended port %d\n", ports[0]);
  if (windowServerOpenSession(24, 80, getpid(), &ports[0], &shell) == 0) {
    printf("sessiontest: opened port %d\n", ports[0]);
  } else {
    printf("sessiontest: open failed: %s\n", strerror(errno));
  }

  for (int ii = 0; ii < numPorts; ii++) {
    windowServerCloseSession(ports[ii]);
    waitForEnd(ports[ii]);
  }
  printf("sessiontest: ended %d sessions\n", numPorts);
  *status = 0;
  return args;
}

