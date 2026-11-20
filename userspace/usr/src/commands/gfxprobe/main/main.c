///////////////////////////////////////////////////////////////////////////////
///
/// @file              main.c
///
/// @brief             A test command that exercises the kernel's graphics
///                    support from a graphics client:  finding the display
///                    driver by name and sending it a message.
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

#include <stdio.h>
#include <string.h>
#include "ExecutiveProcesses.h"
#include "../../../src/kernel/GraphicsProtocol.h"

/// @fn static int findDriver(ProcessId *pid)
///
/// @brief Look up the display driver and print what was found.
///
/// @param pid Where to store the driver's ProcessId.
///
/// @return Returns 0 if the driver was found, nonzero otherwise.
static int findDriver(ProcessId *pid) {
  int status = findProcessByName(GFX_DRIVER_PROCESS_NAME, pid);
  if (status != 0) {
    printf("gfxprobe: no graphics driver (%d)\n", status);
    return 1;
  }
  printf("gfxprobe: graphics driver is pid %d\n", (int) *pid);
  return 0;
}

/// @fn static int sendToDriver(void)
///
/// @brief Send the display driver a GFX_MSG_GET_ERROR, wait for it to be
/// done, and print whether the send was allowed.
///
/// @return Returns 0 if the send succeeded, nonzero otherwise.
static int sendToDriver(void) {
  ProcessId pid = 0;
  if (findDriver(&pid) != 0) {
    return 1;
  }

  MESSAGE_ARGS_ALLOCATE(GfxMsgGetError, args);
  if (args == NULL) {
    printf("gfxprobe: no storage\n");
    return 1;
  }
  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    MESSAGE_ARGS_FREE(args);
    printf("gfxprobe: no message\n");
    return 1;
  }

  processMessageInit(message, GFX_COMMAND_SIGNATURE | GFX_MSG_GET_ERROR,
    args, sizeof(*args), /* waiting= */ true);
  int status = sendProcessMessageToPid(pid, message);
  if (status == processSuccess) {
    processMessageWaitForDone(message, NULL);
  }
  processMessageRelease(message);
  MESSAGE_ARGS_FREE(args);

  if (status != processSuccess) {
    printf("gfxprobe: send refused\n");
    return 1;
  }
  printf("gfxprobe: send done\n");
  return 0;
}

int main(int argc, char **argv) {
  if ((argc == 2) && (strcmp(argv[1], "--help") == 0)) {
    printf("Usage: gfxprobe find|send\n");
    return 0;
  } else if ((argc == 2) && (strcmp(argv[1], "find") == 0)) {
    ProcessId pid = 0;
    return findDriver(&pid);
  } else if ((argc == 2) && (strcmp(argv[1], "send") == 0)) {
    return sendToDriver();
  }

  fprintf(stderr, "Usage: gfxprobe find|send\n");
  return 2;
}
