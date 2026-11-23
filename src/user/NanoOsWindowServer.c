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

///////////////////////////////////////////////////////////////////////////////
///
/// @file              NanoOsWindowServer.c
///
/// @brief             Kernel-side implementation of the calls a process uses
///                    to register as the window server or connect to it.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsWindowServer.h"
#include "NanoOsErrno.h"
#include "../kernel/NanoOsTypes.h"
#include "../kernel/Processes.h"
#include "../kernel/Scheduler.h"

// Must come last
#include "NanoOsStdio.h"

/// @fn static int nanoOsWindowServerCommand(SchedulerCommand command,
///   uint8_t *pid)
///
/// @brief Send the scheduler SCHEDULER_REGISTER_WINDOW_SERVER or
/// SCHEDULER_CONNECT_WINDOW_SERVER and wait for its answer.
///
/// @param command The command.
/// @param pid Where to store the window server's ProcessId, or NULL.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
static int nanoOsWindowServerCommand(SchedulerCommand command, uint8_t *pid) {
  MESSAGE_ARGS_ALLOCATE(SchedulerWindowServerArgs, schedulerWindowServerArgs);
  if (schedulerWindowServerArgs == NULL) {
    errno = ENOMEM;
    return -1;
  }
  schedulerWindowServerArgs->errorNumber = 0;
  schedulerWindowServerArgs->pid = 0;

  ProcessMessage *processMessage = initSendProcessMessageToPid(schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | command,
    schedulerWindowServerArgs, sizeof(*schedulerWindowServerArgs), true);
  if (processMessage == NULL) {
    MESSAGE_ARGS_FREE(schedulerWindowServerArgs);
    errno = ENOMEM;
    return -1;
  }
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

  int errorNumber = schedulerWindowServerArgs->errorNumber;
  if (pid != NULL) {
    *pid = schedulerWindowServerArgs->pid;
  }
  MESSAGE_ARGS_FREE(schedulerWindowServerArgs);
  if (errorNumber != 0) {
    errno = errorNumber;
    return -1;
  }
  return 0;
}

/// @fn int nanoOsWindowServerRegister(void)
///
/// @brief Make the running process the window server.  Only the program
/// config.sys's GRAPHICS line names may be one, and only one at a time.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
int nanoOsWindowServerRegister(void) {
  return nanoOsWindowServerCommand(SCHEDULER_REGISTER_WINDOW_SERVER, NULL);
}

/// @fn int nanoOsWindowServerConnect(uint8_t *pid)
///
/// @brief Connect the running process to the window server, which grants
/// both of them the IPC capabilities of the window protocol.
///
/// @param pid Where to store the window server's ProcessId.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
int nanoOsWindowServerConnect(uint8_t *pid) {
  return nanoOsWindowServerCommand(SCHEDULER_CONNECT_WINDOW_SERVER, pid);
}
