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

/// @fn static int nanoOsWindowServerSession(SchedulerCommand command,
///   SchedulerOpenSessionArgs *args)
///
/// @brief Send the scheduler SCHEDULER_OPEN_SESSION or SCHEDULER_CLOSE_SESSION
/// and wait for its answer.
///
/// @param command The command.
/// @param args The command's arguments, which the answer fills in.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
static int nanoOsWindowServerSession(SchedulerCommand command,
  SchedulerOpenSessionArgs *args
) {
  MESSAGE_ARGS_ALLOCATE(SchedulerOpenSessionArgs, sessionArgs);

  if (sessionArgs == NULL) {
    errno = ENOMEM;
    return -1;
  }

  *sessionArgs = *args;
  sessionArgs->status = EIO;

  ProcessMessage *processMessage = initSendProcessMessageToPid(schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | command, sessionArgs, sizeof(*sessionArgs),
    true);
  if (processMessage == NULL) {
    MESSAGE_ARGS_FREE(sessionArgs);
    errno = EPERM;
    return -1;
  }

  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

  *args = *sessionArgs;
  MESSAGE_ARGS_FREE(sessionArgs);

  if (args->status != 0) {
    errno = args->status;
    return -1;
  }

  return 0;
}

int nanoOsWindowServerOpenSession(uint8_t rows, uint8_t columns,
  uint8_t display, uint8_t *port, uint8_t *shell
) {
  SchedulerOpenSessionArgs args = {
    .status = 0,
    .rows = rows,
    .columns = columns,
    .display = display,
    .port = 0,
    .shell = 0,
  };
  int returnValue = nanoOsWindowServerSession(SCHEDULER_OPEN_SESSION, &args);

  if (returnValue == 0) {
    *port = args.port;
    *shell = args.shell;
  }

  return returnValue;
}

int nanoOsWindowServerCloseSession(uint8_t port) {
  SchedulerOpenSessionArgs args = {
    .status = 0,
    .rows = 0,
    .columns = 0,
    .display = 0,
    .port = port,
    .shell = 0,
  };

  return nanoOsWindowServerSession(SCHEDULER_CLOSE_SESSION, &args);
}
