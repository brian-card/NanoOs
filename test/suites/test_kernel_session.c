///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_session.c
///
/// @brief             Tests of console sessions:  shell slots that aren't
///                    the ones reserved at boot, console ports that come and
///                    go, with separate input and output servicers, and who
///                    may open a session.
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

#include "NanoOsTest.h"
#include "HalMock.h"

#include <string.h>
#include <unistd.h>

#include "kernel/Console.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsSpawn.h"
#include "user/NanoOsWindowServer.h"

int addProcessIpcCapability(ProcessDescriptor *processDescriptor,
  ProcessId destinationPid, int64_t signature, uint32_t messageType);
int schedulerSetPortShell(uint8_t consolePort, ProcessId shell);

/// @def MAX_YIELDS
///
/// @brief How many times a test yields waiting for something to happen.
#define MAX_YIELDS 1000

/// @def SHELL_PATH
///
/// @brief The program a shell slot runs as the user's shell.
#define SHELL_PATH "/usr/bin/report"

/// @def SHELL_FULL_PATH
///
/// @brief SHELL_PATH as the scheduler records it.
#define SHELL_FULL_PATH "/userspace/usr/bin/report"

static volatile bool reported;
static const char *volatile recordedShellPath;
static volatile ProcessId stdoutPid;
static volatile ProcessId stderrPid;
static volatile ProcessId outputPid;
static volatile int execErrno = -1;

/// @def NO_RESULT
///
/// @brief The result a program hasn't reported yet.
#define NO_RESULT (-100)

/// @var openErrnos
///
/// @brief The errno of a session open by:  an executive process that isn't
/// the window server, a user-level process, and the window server with a
/// display of another user.
static volatile int openErrnos[3] = { NO_RESULT, NO_RESULT, NO_RESULT };

/// @var serverReady
///
/// @brief Whether the window server has registered.
static volatile bool serverReady;

/// @var displayPid
///
/// @brief The process the window server names as the display, or 0 until it
/// should try.
static volatile ProcessId displayPid;

/// @fn static int tryOpen(ProcessId display)
///
/// @brief Try to open a session.
///
/// @param display The display to name.
///
/// @return Returns the errno of the failure, or 0 if the session opened.
static int tryOpen(ProcessId display) {
  uint8_t port = 0;
  uint8_t shell = 0;
  errno = 0;
  if (nanoOsWindowServerOpenSession(24, 80, display, &port, &shell) == 0) {
    return 0;
  }
  return errno;
}

/// @fn static void* program(void *args)
///
/// @brief Stand in for a spawned program, chosen by argv[0]:  "login" makes
/// its slot a shell slot, which may exec, and execs SHELL_PATH as login does,
/// recording errno if the exec fails; "exit" makes its slot a shell slot
/// whose output goes to outputPid, with SHELL_PATH as the user's shell, and
/// exits so that the slot's shell is started again; "report", the shell,
/// records its slot's shell path and where its stdout and stderr go;
/// "server" registers as the window server and tries to open a session on
/// displayPid once it's set; "intruder", an executive process, gives itself
/// the grant to open a session and tries; "user" tries; "display" runs as
/// another user; "quit" exits.
///
/// @param args The ExecArgs of the spawn.
///
/// @return This function returns NULL for "exit" and "quit" and never returns
/// otherwise.
static void* program(void *args) {
  ExecArgs *execArgs = (ExecArgs*) args;
  const char *mode = execArgs->argv[0];
  ProcessDescriptor *self = getRunningProcess();

  if (strcmp(mode, "login") == 0) {
    self->shellSlot = SHELL_SLOT_CONSOLE;
    addProcessIpcCapability(self, schedulerPid, SCHEDULER_COMMAND_SIGNATURE,
      SCHEDULER_EXECVE);
    char *argv[] = { "report", NULL };
    schedulerExecve(SHELL_PATH, argv, NULL);
    execErrno = errno;
    reported = true;
  } else if (strcmp(mode, "exit") == 0) {
    self->shellSlot = SHELL_SLOT_CONSOLE;
    self->shellOutputPid = outputPid;
    self->shellPath = (char*) SHELL_FULL_PATH;
    self->restartFunction = restartOverlayShell;
    return NULL;
  } else if (strcmp(mode, "report") == 0) {
    recordedShellPath = self->shellPath;
    stdoutPid = self->fileDescriptors[1]->outputChannel.pid;
    stderrPid = self->fileDescriptors[2]->outputChannel.pid;
    reported = true;
  } else if (strcmp(mode, "server") == 0) {
    serverReady = (nanoOsWindowServerRegister() == 0);
    while (displayPid == 0) {
      processYield();
    }
    openErrnos[2] = tryOpen(displayPid);
  } else if (strcmp(mode, "intruder") == 0) {
    addProcessIpcCapability(self, schedulerPid, SCHEDULER_COMMAND_SIGNATURE,
      SCHEDULER_OPEN_SESSION);
    openErrnos[0] = tryOpen(self->processId);
  } else if (strcmp(mode, "user") == 0) {
    openErrnos[1] = tryOpen(self->processId);
  } else if (strcmp(mode, "display") == 0) {
    self->userId = ROOT_USER_ID + 1;
    reported = true;
  } else if (strcmp(mode, "quit") == 0) {
    return NULL;
  }
  while (1) {
    processYield();
  }
  return NULL;
}

/// @fn static void sessionPreBoot(void)
///
/// @brief Run spawned programs as program.
///
/// @return This function returns no value.
static void sessionPreBoot(void) {
  halMockSetExecCommand(program);
}

/// @fn static ProcessDescriptor* spawnProgram(char *mode)
///
/// @brief Spawn program in a user slot and wait for it to report.
///
/// @param mode The program's mode, passed as argv[0].
///
/// @return Returns a pointer to the slot's ProcessDescriptor, or NULL if the
/// spawn failed or the program never reported.
static ProcessDescriptor* spawnProgram(char *mode) {
  reported = false;
  recordedShellPath = NULL;
  stdoutPid = PROCESS_ID_NOT_SET;
  stderrPid = PROCESS_ID_NOT_SET;
  pid_t pid = 0;
  char *argv[] = { mode, NULL };
  if (nanoOsSpawn(&pid, "/usr/bin/program", NULL, NULL, argv, NULL) != 0) {
    return NULL;
  }
  for (int ii = 0; (ii < MAX_YIELDS) && !reported; ii++) {
    processYield();
  }
  return reported ? &allProcesses[pid - 1] : NULL;
}

NANO_OS_KERNEL_TEST_WITH(session,
  a_shell_slot_outside_the_boot_range_records_its_login_shell,
  sessionPreBoot
) {
  ProcessDescriptor *slot = spawnProgram("login");
  NANO_OS_ASSERT_EQ_INT(-1, execErrno);
  NANO_OS_ASSERT_NOT_NULL(slot);
  NANO_OS_ASSERT_TRUE(slot->processId
    >= schedulerGetFirstShellPid() + schedulerGetNumShells());
  NANO_OS_ASSERT_NOT_NULL(recordedShellPath);
  NANO_OS_ASSERT_STR_EQ(SHELL_FULL_PATH, recordedShellPath);
}

NANO_OS_KERNEL_TEST_WITH(session,
  a_shell_slot_outside_the_boot_range_has_its_output_directed,
  sessionPreBoot
) {
  outputPid = getRunningPid();
  ProcessDescriptor *slot = spawnProgram("exit");
  NANO_OS_ASSERT_NOT_NULL(slot);
  NANO_OS_ASSERT_TRUE(slot->processId
    >= schedulerGetFirstShellPid() + schedulerGetNumShells());
  NANO_OS_ASSERT_EQ_INT(outputPid, stdoutPid);
  NANO_OS_ASSERT_EQ_INT(outputPid, stderrPid);
}

/// @var portState
///
/// @brief Console state for calling the port handlers directly.
static ConsoleState portState;

/// @var portArgs
///
/// @brief The arguments of each CONSOLE_ADD_FORWARDING_PORT a test sends.
static ConsoleAddForwardingPortArgs portArgs;

/// @var readyArgs
///
/// @brief The arguments of each CONSOLE_INPUT_READY a test sends.
static ConsoleInputReadyArgs readyArgs[CONSOLE_NUM_PORTS];

/// @fn static void portFixture(void)
///
/// @brief Set up a ConsoleState with no ports in use and no buffer owned.
///
/// @return This function returns no value.
static void portFixture(void) {
  memset(&portState, 0, sizeof(portState));
  memset(readyArgs, 0, sizeof(readyArgs));
  for (int ii = 0; ii < CONSOLE_NUM_BUFFERS; ii++) {
    portState.consoleBuffers[ii].owner = PROCESS_ID_NOT_SET;
  }
}

/// @fn static ProcessMessage* deliver(int64_t type, void *args, size_t size,
///   ProcessId from, void (*handler)(ConsoleState*, ProcessMessage*))
///
/// @brief Push a console command onto this process's own queue, pop it, make
/// it come from another process, and hand it to a handler as the console
/// would.  Pushing directly skips the IPC capability check a send would make.
///
/// @param type The ConsoleCommand.
/// @param args The command's arguments.
/// @param size The size of the arguments.
/// @param from The ProcessId the command comes from.
/// @param handler The handler to call.
///
/// @return Returns the message, which the caller must release, or NULL on
/// failure.
static ProcessMessage* deliver(int64_t type, void *args, size_t size,
  ProcessId from, void (*handler)(ConsoleState*, ProcessMessage*)
) {
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    return NULL;
  }
  processMessageInit(sent, CONSOLE_COMMAND_SIGNATURE | type, args, size,
    /* waiting= */ true);
  if (processMessageQueuePush(getRunningProcess(), sent) != processSuccess) {
    processMessageRelease(sent);
    return NULL;
  }
  ProcessMessage *received = processMessageQueuePop();
  if (received != sent) {
    processMessageRelease(sent);
    return NULL;
  }
  msg_from(received).coro = allProcesses[from - 1].mainThread;
  handler(&portState, received);
  return sent;
}

/// @fn static int addSessionPort(ProcessId inputPid, ProcessId outputPid)
///
/// @brief Have the scheduler add a forwarding port with separate input and
/// output servicers.
///
/// @param inputPid The input servicer.
/// @param outputPid The output servicer.
///
/// @return Returns the port's index, or -1 on failure.
static int addSessionPort(ProcessId inputPid, ProcessId outputPid) {
  portArgs.forwardingPid = inputPid;
  portArgs.outputPid = outputPid;
  portArgs.portId = 0xff;
  portArgs.returnValue = 1;
  ProcessMessage *sent = deliver(CONSOLE_ADD_FORWARDING_PORT, &portArgs,
    sizeof(portArgs), schedulerPid, consoleAddForwardingPort);
  if (sent == NULL) {
    return -1;
  }
  processMessageRelease(sent);
  return (portArgs.returnValue == 0) ? portArgs.portId : -1;
}

/// @fn static bool removePort(uint8_t portId, ProcessId from)
///
/// @brief Send CONSOLE_REMOVE_PORT for a port.
///
/// @param portId The index of the port.
/// @param from The ProcessId the command comes from.
///
/// @return Returns true if the handler marked the command done.
static bool removePort(uint8_t portId, ProcessId from) {
  ProcessMessage *sent = deliver(CONSOLE_REMOVE_PORT,
    (void*) (uintptr_t) portId, 0, from, consoleRemovePortCommandHandler);
  if (sent == NULL) {
    return false;
  }
  bool done = processMessageDone(sent);
  processMessageRelease(sent);
  return done;
}

/// @fn static int numPorts(void)
///
/// @brief Ask the handler for the number of ports in use.
///
/// @return Returns the number, or -1 on failure.
static int numPorts(void) {
  ConsoleGetNumPortsArgs args = { .numPorts = -1 };
  ProcessMessage *sent = deliver(CONSOLE_GET_NUM_PORTS, &args, sizeof(args),
    getRunningPid(), consoleGetNumPortsCommandHandler);
  if (sent == NULL) {
    return -1;
  }
  processMessageRelease(sent);
  return args.numPorts;
}

/// @fn static ProcessMessage* sendByte(uint8_t portId, ProcessId from,
///   char byte)
///
/// @brief Send a port one byte of input.
///
/// @param portId The index of the port.
/// @param from The ProcessId the input comes from.
/// @param byte The byte.
///
/// @return Returns the message, which the caller must release, or NULL on
/// failure.
static ProcessMessage* sendByte(uint8_t portId, ProcessId from, char byte) {
  readyArgs[portId].portId = portId;
  readyArgs[portId].numBytes = 1;
  readyArgs[portId].bytes[0] = (uint8_t) byte;
  return deliver(CONSOLE_INPUT_READY, &readyArgs[portId],
    sizeof(readyArgs[portId]), from, consoleInputReadyCommandHandler);
}

/// @fn static int readPort(uint8_t portId)
///
/// @brief Read a byte of a port's input the way the console does.
///
/// @param portId The index of the port.
///
/// @return Returns the byte, or -1 if there was none.
static int readPort(uint8_t portId) {
  ConsolePort *consolePort = &portState.consolePorts[portId];
  return consolePort->readByte(consolePort);
}

/// @def FIRST_SERVICER
///
/// @brief The first of the ProcessIds the port tests use as servicers.
#define FIRST_SERVICER 6

NANO_OS_KERNEL_TEST(session, a_removed_port_index_is_reused) {
  portFixture();
  NANO_OS_ASSERT_EQ_INT(0, addSessionPort(FIRST_SERVICER, FIRST_SERVICER));
  NANO_OS_ASSERT_EQ_INT(1,
    addSessionPort(FIRST_SERVICER + 1, FIRST_SERVICER + 1));
  NANO_OS_ASSERT_EQ_INT(2,
    addSessionPort(FIRST_SERVICER + 2, FIRST_SERVICER + 2));
  NANO_OS_ASSERT_EQ_INT(3, numPorts());

  NANO_OS_ASSERT_TRUE(removePort(1, schedulerPid));
  NANO_OS_ASSERT_EQ_INT(2, numPorts());
  NANO_OS_ASSERT_EQ_INT(1,
    addSessionPort(FIRST_SERVICER + 3, FIRST_SERVICER + 3));
  NANO_OS_ASSERT_EQ_INT(3, numPorts());

  const char bytes[] = { 'a', 'd', 'c' };
  const ProcessId servicers[] = {
    FIRST_SERVICER, FIRST_SERVICER + 3, FIRST_SERVICER + 2 };
  for (uint8_t ii = 0; ii < 3; ii++) {
    ProcessMessage *sent = sendByte(ii, servicers[ii], bytes[ii]);
    NANO_OS_ASSERT_NOT_NULL(sent);
    NANO_OS_ASSERT_EQ_INT(bytes[ii], readPort(ii));
    NANO_OS_ASSERT_TRUE(processMessageDone(sent));
    processMessageRelease(sent);
  }
}

NANO_OS_KERNEL_TEST(session, removing_a_port_finishes_its_held_input) {
  portFixture();
  NANO_OS_ASSERT_EQ_INT(0, addSessionPort(FIRST_SERVICER, FIRST_SERVICER));
  ProcessMessage *sent = sendByte(0, FIRST_SERVICER, 'x');
  NANO_OS_ASSERT_NOT_NULL(sent);
  NANO_OS_ASSERT_EQ_PTR(sent, portState.consolePorts[0].pendingInput);
  NANO_OS_ASSERT_FALSE(processMessageDone(sent));

  NANO_OS_ASSERT_TRUE(removePort(0, schedulerPid));
  NANO_OS_ASSERT_TRUE(processMessageDone(sent));
  NANO_OS_ASSERT_NULL(portState.consolePorts[0].pendingInput);
  NANO_OS_ASSERT_EQ_INT(0, numPorts());
  processMessageRelease(sent);

  ProcessMessage *again = sendByte(0, FIRST_SERVICER, 'y');
  NANO_OS_ASSERT_NOT_NULL(again);
  NANO_OS_ASSERT_TRUE(processMessageDone(again));
  NANO_OS_ASSERT_NULL(portState.consolePorts[0].pendingInput);
  processMessageRelease(again);
}

NANO_OS_KERNEL_TEST(session, only_the_scheduler_removes_a_port) {
  portFixture();
  NANO_OS_ASSERT_EQ_INT(0, addSessionPort(FIRST_SERVICER, FIRST_SERVICER));
  NANO_OS_ASSERT_TRUE(removePort(0, FIRST_SERVICER));
  NANO_OS_ASSERT_TRUE(removePort(0, consolePid));
  NANO_OS_ASSERT_EQ_INT(1, numPorts());

  ProcessMessage *sent = sendByte(0, FIRST_SERVICER, 'z');
  NANO_OS_ASSERT_NOT_NULL(sent);
  NANO_OS_ASSERT_EQ_INT('z', readPort(0));
  processMessageRelease(sent);
}

/// @var inputPid
///
/// @brief The input servicer of the split port the launch hook adds.
static ProcessId inputPid;

/// @var splitPortId
///
/// @brief The index of the split port the launch hook adds, or -1.
static volatile int splitPortId = -1;

/// @var forwardedTo
///
/// @brief For each servicer, the text the console forwarded it.
static char forwardedTo[2][16];

/// @var refusedErrnos
///
/// @brief The errno of each send the output servicer tries, or 0 if the send
/// went through:  CONSOLE_INPUT_READY, then CONSOLE_SET_WINSIZE.
static volatile int refusedErrnos[2];

/// @var refusalsTried
///
/// @brief Whether the output servicer has tried its sends.
static volatile bool refusalsTried;

/// @var inputSent
///
/// @brief Whether the input servicer has sent its byte.
static volatile bool inputSent;

/// @var queryWinsize
///
/// @brief Whether the input servicer reads the split port's size once the
/// output servicer has tried its sends, instead of sending the port input.
static volatile bool queryWinsize;

/// @var portRows
///
/// @brief The rows of the split port, as its input servicer reads them, or -1.
static volatile int portRows = -1;

/// @var portColumns
///
/// @brief The columns of the split port, as its input servicer reads them, or
/// -1.
static volatile int portColumns = -1;

/// @fn static void recordForward(int servicer, ProcessMessage *message)
///
/// @brief Record the text of a CONSOLE_FORWARD_STRING and finish it.
///
/// @param servicer 0 for the input servicer, 1 for the output servicer.
/// @param message The message.
///
/// @return This function returns no value.
static void recordForward(int servicer, ProcessMessage *message) {
  if (processMessageType(message)
    == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_FORWARD_STRING)
  ) {
    size_t used = strlen(forwardedTo[servicer]);
    strncat(forwardedTo[servicer], (const char*) processMessageData(message),
      sizeof(forwardedTo[servicer]) - used - 1);
  }
  processMessageSetDone(message);
  if (processMessageWaiting(message) == false) {
    processMessageRelease(message);
  }
}

/// @fn static int trySend(ConsoleCommand command, void *args, size_t size)
///
/// @brief Send the console a command and wait for it.
///
/// @param command The command.
/// @param args Its arguments.
/// @param size Their size.
///
/// @return Returns 0 if the send went through, errno if it didn't.
static int trySend(ConsoleCommand command, void *args, size_t size) {
  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return ENOMEM;
  }
  processMessageInit(message, CONSOLE_COMMAND_SIGNATURE | command, args,
    size, /* waiting= */ true);
  errno = 0;
  if (sendProcessMessageToPid(consolePid, message) != processSuccess) {
    int sendErrno = errno;
    processMessageRelease(message);
    return sendErrno;
  }
  processMessageWaitForDone(message, NULL);
  processMessageRelease(message);
  return 0;
}

/// @fn static void* inputServicer(void *args)
///
/// @brief Send the split port an "a", or read the port's size if
/// queryWinsize is set, then record what the console forwards.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* inputServicer(void *args) {
  (void) args;
  if (queryWinsize) {
    while (refusalsTried == false) {
      processYield();
    }
    static ConsoleWinsizeArgs winsizeArgs;
    if (trySend(CONSOLE_GET_WINSIZE, &winsizeArgs, sizeof(winsizeArgs)) == 0) {
      portRows = winsizeArgs.rows;
      portColumns = winsizeArgs.columns;
    }
  } else {
    static ConsoleInputReadyArgs inputArgs;
    inputArgs.portId = (uint8_t) splitPortId;
    inputArgs.numBytes = 1;
    inputArgs.bytes[0] = 'a';
    ProcessMessage *message = getAvailableMessage();
    if (message != NULL) {
      processMessageInit(message,
        CONSOLE_COMMAND_SIGNATURE | CONSOLE_INPUT_READY, &inputArgs,
        sizeof(inputArgs), /* waiting= */ false);
      if (sendProcessMessageToPid(consolePid, message) != processSuccess) {
        processMessageRelease(message);
      }
    }
  }
  inputSent = true;
  while (1) {
    recordForward(0, processMessageQueueWait(NULL));
  }
  return NULL;
}

/// @fn static void* outputServicer(void *args)
///
/// @brief Try to send the split port input and a window size, then record
/// what the console forwards.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* outputServicer(void *args) {
  (void) args;
  static ConsoleInputReadyArgs inputArgs;
  static ConsoleWinsizeArgs winsizeArgs;
  inputArgs.portId = (uint8_t) splitPortId;
  inputArgs.numBytes = 1;
  inputArgs.bytes[0] = 'z';
  winsizeArgs.rows = 3;
  winsizeArgs.columns = 4;
  refusedErrnos[0]
    = trySend(CONSOLE_INPUT_READY, &inputArgs, sizeof(inputArgs));
  refusedErrnos[1]
    = trySend(CONSOLE_SET_WINSIZE, &winsizeArgs, sizeof(winsizeArgs));
  refusalsTried = true;
  while (1) {
    recordForward(1, processMessageQueueWait(NULL));
  }
  return NULL;
}

/// @var stallOutput
///
/// @brief Whether the split port's servicers are typingInputServicer and
/// stalledOutputServicer.
static volatile bool stallOutput;

/// @var outputStalled
///
/// @brief Whether stalledOutputServicer leaves what the console sends it
/// unread.
static volatile bool outputStalled = true;

/// @var typed
///
/// @brief The byte typingInputServicer sends the split port next, or '\0'.
static volatile char typed;

/// @fn static void* typingInputServicer(void *args)
///
/// @brief Send the split port each byte put in typed, once the console has
/// read the last.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* typingInputServicer(void *args) {
  (void) args;
  static ConsoleInputReadyArgs inputArgs;
  ProcessMessage *message = NULL;
  while (1) {
    if ((typed == '\0')
      || ((message != NULL) && (processMessageDone(message) == false))
    ) {
      processYield();
      continue;
    }
    if (message != NULL) {
      processMessageRelease(message);
    }
    message = getAvailableMessage();
    if (message == NULL) {
      continue;
    }
    inputArgs.portId = (uint8_t) splitPortId;
    inputArgs.numBytes = 1;
    inputArgs.bytes[0] = (uint8_t) typed;
    processMessageInit(message, CONSOLE_COMMAND_SIGNATURE | CONSOLE_INPUT_READY,
      &inputArgs, sizeof(inputArgs), /* waiting= */ true);
    if (sendProcessMessageToPid(consolePid, message) != processSuccess) {
      processMessageRelease(message);
      message = NULL;
    }
    typed = '\0';
  }
  return NULL;
}

/// @fn static void* stalledOutputServicer(void *args)
///
/// @brief Leave what the console sends unread while outputStalled is set,
/// as a display that has stopped would, moving the virtual clock on 10 ms
/// each time it yields so that timed waits can end, then record it.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* stalledOutputServicer(void *args) {
  (void) args;
  while (outputStalled) {
    mockClockAdvanceNs(10000000ULL);
    processYield();
  }
  while (1) {
    recordForward(1, processMessageQueueWait(NULL));
  }
  return NULL;
}

/// @fn static int startServicer(ProcessDescriptor *processDescriptor)
///
/// @brief Run a split port's servicer in its slot when the slot's boot
/// process exits, as a restartFunction, so that the slot keeps the grants
/// the scheduler gave it.
///
/// @param processDescriptor A pointer to the slot's ProcessDescriptor.
///
/// @return Returns 0 on success, -ENOMEM on failure.
static int startServicer(ProcessDescriptor *processDescriptor) {
  void* (*body)(void*) = outputServicer;
  if (stallOutput) {
    body = stalledOutputServicer;
  }
  if (processDescriptor->processId == inputPid) {
    body = inputServicer;
    if (stallOutput) {
      body = typingInputServicer;
    }
  }
  if (processCreate(processDescriptor, body, NULL) != processSuccess) {
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->restartFunction = NULL;
  return 0;
}

/// @fn static int addSplitPort(void)
///
/// @brief Have the scheduler add a port whose input servicer is the first
/// user slot and whose output servicer is the second, and start the
/// servicers in those slots.
///
/// @return Returns 0 on success, -errno on failure.
static int addSplitPort(void) {
  inputPid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  outputPid = inputPid + 1;
  allProcesses[inputPid - 1].restartFunction = startServicer;
  allProcesses[outputPid - 1].restartFunction = startServicer;
  splitPortId = schedulerAddForwardingPort(inputPid, outputPid, 0, 0);
  return (splitPortId >= 0) ? 0 : splitPortId;
}

/// @fn static void splitPreBoot(void)
///
/// @brief Add the split port at launch, with an input servicer that sends it
/// input.
///
/// @return This function returns no value.
static void splitPreBoot(void) {
  halMockSetLaunchHook(addSplitPort);
}

/// @fn static void splitQueryPreBoot(void)
///
/// @brief Add the split port at launch, with an input servicer that reads its
/// size.
///
/// @return This function returns no value.
static void splitQueryPreBoot(void) {
  queryWinsize = true;
  halMockSetLaunchHook(addSplitPort);
}

NANO_OS_KERNEL_TEST_WITH(session,
  a_split_port_echoes_to_its_output_servicer_only, splitPreBoot
) {
  NANO_OS_ASSERT_TRUE(splitPortId >= 0);
  for (int ii = 0; (ii < MAX_YIELDS)
    && ((inputSent == false) || (forwardedTo[1][0] == '\0')); ii++
  ) {
    processYield();
  }
  for (int ii = 0; ii < 100; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_STR_EQ("a", forwardedTo[1]);
  NANO_OS_ASSERT_STR_EQ("", forwardedTo[0]);
}

NANO_OS_KERNEL_TEST_WITH(session,
  the_output_servicer_cannot_send_a_split_port_input, splitQueryPreBoot
) {
  NANO_OS_ASSERT_TRUE(splitPortId >= 0);
  for (int ii = 0; (ii < MAX_YIELDS) && (portRows < 0); ii++) {
    processYield();
  }
  NANO_OS_ASSERT_TRUE(refusalsTried);
  NANO_OS_ASSERT_EQ_INT(EPERM, refusedErrnos[0]);
  NANO_OS_ASSERT_EQ_INT(EPERM, refusedErrnos[1]);
  NANO_OS_ASSERT_STR_EQ("", forwardedTo[1]);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_DEFAULT_ROWS, portRows);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_DEFAULT_COLUMNS, portColumns);
}

/// @fn static void stallPreBoot(void)
///
/// @brief Add the split port at launch, with an input servicer that types
/// what a test asks and an output servicer that has stopped reading.
///
/// @return This function returns no value.
static void stallPreBoot(void) {
  stallOutput = true;
  halMockSetLaunchHook(addSplitPort);
}

/// @fn static int numPortsBy(int seconds)
///
/// @brief Ask the console how many ports are in use, waiting for its answer
/// for a number of seconds.
///
/// @param seconds How long to wait.
///
/// @return Returns the number, or -1 if the console didn't answer in time.
static int numPortsBy(int seconds) {
  static ConsoleGetNumPortsArgs args;
  args.numPorts = -1;
  addProcessIpcCapability(getRunningProcess(), consolePid,
    CONSOLE_COMMAND_SIGNATURE, CONSOLE_GET_NUM_PORTS);
  ProcessMessage *message = initSendProcessMessageToPid(consolePid,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_GET_NUM_PORTS, &args, sizeof(args),
    true);
  if (message == NULL) {
    return -1;
  }
  struct timespec deadline;
  timespec_get(&deadline, TIME_UTC);
  deadline.tv_sec += seconds;
  if (processMessageWaitForDone(message, &deadline) != processSuccess) {
    return -1;
  }
  processMessageRelease(message);
  return args.numPorts;
}

/// @fn static void typeAndSettle(char byte)
///
/// @brief Have the split port's input servicer type a byte, and yield until
/// it has been sent and the console has had time to handle it.
///
/// @param byte The byte.
///
/// @return This function returns no value.
static void typeAndSettle(char byte) {
  typed = byte;
  for (int ii = 0; (ii < MAX_YIELDS) && (typed != '\0'); ii++) {
    processYield();
  }
  for (int ii = 0; ii < 100; ii++) {
    processYield();
  }
}

NANO_OS_KERNEL_TEST_WITH(session,
  a_display_that_stops_reading_doesnt_stop_the_console, stallPreBoot
) {
  NANO_OS_ASSERT_TRUE(splitPortId >= 0);
  typeAndSettle('a');
  NANO_OS_ASSERT_TRUE(numPortsBy(5) > 0);
  typeAndSettle('c');
  outputStalled = false;
  for (int ii = 0; (ii < MAX_YIELDS) && (forwardedTo[1][0] == '\0'); ii++) {
    processYield();
  }
  typeAndSettle('b');
  for (int ii = 0; (ii < MAX_YIELDS) && (strlen(forwardedTo[1]) < 2); ii++) {
    processYield();
  }
  NANO_OS_ASSERT_STR_EQ("ab", forwardedTo[1]);
}

/// @var driverPid
///
/// @brief The slot of the driver of the port driverPreBoot adds.
static volatile ProcessId driverPid = PROCESS_ID_NOT_SET;

/// @var driverPortId
///
/// @brief The port driverPreBoot adds, or -1.
static volatile int driverPortId = -1;

/// @var echoHeld
///
/// @brief Whether stalledDriver has the console's echo and hasn't finished it.
static volatile bool echoHeld;

/// @var driverStalled
///
/// @brief Whether stalledDriver keeps the console's echo unfinished.
static volatile bool driverStalled = true;

/// @fn static void* stalledDriver(void *args)
///
/// @brief Stand in for a device console's driver that types a byte and then
/// is slow to draw its echo:  it keeps the console's first string unfinished
/// while driverStalled is set, then records it and every one after.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* stalledDriver(void *args) {
  (void) args;
  static ConsoleInputReadyArgs inputArgs;
  inputArgs.portId = (uint8_t) driverPortId;
  inputArgs.numBytes = 1;
  inputArgs.bytes[0] = 'x';
  ProcessMessage *typed = getAvailableMessage();
  if (typed != NULL) {
    processMessageInit(typed, CONSOLE_COMMAND_SIGNATURE | CONSOLE_INPUT_READY,
      &inputArgs, sizeof(inputArgs), /* waiting= */ true);
    if (sendProcessMessageToPid(consolePid, typed) != processSuccess) {
      processMessageRelease(typed);
    }
  }
  ProcessMessage *echo = processMessageQueueWait(NULL);
  echoHeld = true;
  while (driverStalled) {
    processYield();
  }
  recordForward(0, echo);
  while (1) {
    recordForward(0, processMessageQueueWait(NULL));
  }
  return NULL;
}

/// @fn static int startDriver(ProcessDescriptor *processDescriptor)
///
/// @brief Run stalledDriver in its slot when the slot's boot process exits,
/// as a restartFunction, so that the slot keeps the grants the scheduler
/// gave it.
///
/// @param processDescriptor A pointer to the slot's ProcessDescriptor.
///
/// @return Returns 0 on success, -ENOMEM on failure.
static int startDriver(ProcessDescriptor *processDescriptor) {
  if (processCreate(processDescriptor, stalledDriver, NULL) != processSuccess) {
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->restartFunction = NULL;
  return 0;
}

/// @fn static int addDriverPort(void)
///
/// @brief Have the scheduler add a device console's port whose driver, which
/// services both its input and its output, is the first user slot.
///
/// @return Returns 0 on success, -errno on failure.
static int addDriverPort(void) {
  driverPid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  allProcesses[driverPid - 1].restartFunction = startDriver;
  driverPortId = schedulerAddForwardingPort(driverPid, PROCESS_ID_NOT_SET,
    0, 0);
  return (driverPortId >= 0) ? 0 : driverPortId;
}

/// @fn static void driverPreBoot(void)
///
/// @brief Add a device console's port at launch, with stalledDriver as its
/// driver, and run spawned programs as program.
///
/// @return This function returns no value.
static void driverPreBoot(void) {
  halMockSetExecCommand(program);
  halMockSetLaunchHook(addDriverPort);
}

NANO_OS_KERNEL_TEST_WITH(session,
  an_exit_while_a_driver_is_slow_to_draw_an_echo_doesnt_hang, driverPreBoot
) {
  NANO_OS_ASSERT_TRUE(driverPortId >= 0);
  for (int ii = 0; (ii < MAX_YIELDS) && !echoHeld; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_TRUE(echoHeld);
  // A hang here never comes back to this process, so it ends the test.
  alarm(10);
  mockClockSetStepNs(100000ULL);
  pid_t pid = 0;
  char *argv[] = { "quit", NULL };
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawn(&pid, "/usr/bin/program", NULL, NULL, argv, NULL));
  for (int ii = 0; (ii < MAX_YIELDS) && processRunning(&allProcesses[pid - 1]);
    ii++
  ) {
    processYield();
  }
  NANO_OS_ASSERT_FALSE(processRunning(&allProcesses[pid - 1]));
  NANO_OS_ASSERT_TRUE(numPortsBy(5) > 0);
  alarm(0);
  driverStalled = false;
  for (int ii = 0; (ii < MAX_YIELDS) && (forwardedTo[0][0] == '\0'); ii++) {
    processYield();
  }
  NANO_OS_ASSERT_STR_EQ("x", forwardedTo[0]);
}

/// @def SERVER_PATH
///
/// @brief The program the session tests register as the graphics client,
/// which can become the window server.
#define SERVER_PATH "/system/commands/sessionserver"

/// @fn static void* fakeDriver(void *args)
///
/// @brief Stand in for the display driver, finishing every message.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* fakeDriver(void *args) {
  (void) args;
  while (1) {
    ProcessMessage *processMessage = processMessageQueueWait(NULL);
    if (processMessage == NULL) {
      continue;
    }
    processMessageSetDone(processMessage);
    if (processMessageWaiting(processMessage) == false) {
      processMessageRelease(processMessage);
    }
  }
  return NULL;
}

/// @fn static void serverPreBoot(void)
///
/// @brief Run spawned programs as program, with the first user slot as the
/// display driver of SERVER_PATH.
///
/// @return This function returns no value.
static void serverPreBoot(void) {
  halMockSetExecCommand(program);
  halMockSetGraphics(0, SERVER_PATH);
}

/// @fn static bool startServer(void)
///
/// @brief Start the fake display driver and a window server that registered,
/// started from a login like nook's, so that the session's display is the
/// only thing a test's open can be refused for.
///
/// @return Returns true on success.
static bool startServer(void) {
  getRunningProcess()->shellPath = (char*) SHELL_FULL_PATH;
  ProcessId pid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *driver = &allProcesses[pid - 1];
  processQueueRemove(halMockFreeQueue(), driver);
  if (processCreate(driver, fakeDriver, NULL) != processSuccess) {
    return false;
  }
  threadSetContext(driver->mainThread, driver);
  driver->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  driver->userId = SYSTEM_USER_ID;
  driver->readyQueue = halMockReadyQueue(PRIVILEGE_LEVEL_EXECUTIVE);
  processQueuePush(driver->readyQueue, driver);

  posix_spawnattr_t attr = { .flags = POSIX_SPAWN_SYSTEM };
  pid_t serverPid = 0;
  char *argv[] = { "server", NULL };
  if (nanoOsSpawn(&serverPid, "sessionserver", NULL, &attr, argv, NULL)
    != 0
  ) {
    return false;
  }
  for (int ii = 0; (ii < MAX_YIELDS) && (serverReady == false); ii++) {
    processYield();
  }
  return serverReady;
}

/// @fn static int numConsolePorts(void)
///
/// @brief Ask the console how many ports are in use.
///
/// @return Returns the number, or -1 on failure.
static int numConsolePorts(void) {
  addProcessIpcCapability(getRunningProcess(), consolePid,
    CONSOLE_COMMAND_SIGNATURE, CONSOLE_GET_NUM_PORTS);
  ConsoleGetNumPortsArgs args = { .numPorts = -1 };
  ProcessMessage *message = initSendProcessMessageToPid(consolePid,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_GET_NUM_PORTS, &args, sizeof(args),
    true);
  if (message == NULL) {
    return -1;
  }
  processMessageWaitForDone(message, NULL);
  processMessageRelease(message);
  return args.numPorts;
}

/// @fn static void waitForOpen(int which)
///
/// @brief Yield until a program reports a session open's result, or
/// MAX_YIELDS times.
///
/// @param which The index in openErrnos.
///
/// @return This function returns no value.
static void waitForOpen(int which) {
  for (int ii = 0; (ii < MAX_YIELDS) && (openErrnos[which] == NO_RESULT);
    ii++
  ) {
    processYield();
  }
}

NANO_OS_KERNEL_TEST_WITH(session, only_the_window_server_opens_a_session,
  serverPreBoot
) {
  NANO_OS_ASSERT_TRUE(startServer());
  int portsBefore = numConsolePorts();
  NANO_OS_ASSERT_TRUE(portsBefore > 0);

  posix_spawnattr_t attr = { .flags = POSIX_SPAWN_SYSTEM };
  pid_t pid = 0;
  char *intruderArgv[] = { "intruder", NULL };
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawn(&pid, "intruder", NULL, &attr, intruderArgv, NULL));
  waitForOpen(0);
  char *userArgv[] = { "user", NULL };
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawn(&pid, "/usr/bin/user", NULL, NULL, userArgv, NULL));
  waitForOpen(1);

  NANO_OS_ASSERT_EQ_INT(EPERM, openErrnos[0]);
  NANO_OS_ASSERT_EQ_INT(EPERM, openErrnos[1]);
  NANO_OS_ASSERT_EQ_INT(portsBefore, numConsolePorts());
  for (ProcessId ii = 1; ii <= numProcesses; ii++) {
    NANO_OS_ASSERT_TRUE(allProcesses[ii - 1].shellSlot != SHELL_SLOT_SESSION);
  }
}

NANO_OS_KERNEL_TEST_WITH(session, a_display_of_another_user_is_refused,
  serverPreBoot
) {
  NANO_OS_ASSERT_TRUE(startServer());
  int portsBefore = numConsolePorts();
  ProcessDescriptor *display = spawnProgram("display");
  NANO_OS_ASSERT_NOT_NULL(display);
  displayPid = display->processId;
  waitForOpen(2);
  NANO_OS_ASSERT_EQ_INT(EINVAL, openErrnos[2]);
  NANO_OS_ASSERT_EQ_INT(portsBefore, numConsolePorts());
}

/// @var lastSlotStatus
///
/// @brief What schedulerSetPortShell returned for the last process slot.
static volatile int lastSlotStatus = -1;

/// @fn static int setLastSlotAsShell(void)
///
/// @brief Make the last process slot the shell of console port 0, then the
/// first shell slot again, as the scheduler does when a session takes the
/// last free slot.
///
/// @return Returns 0.
static int setLastSlotAsShell(void) {
  lastSlotStatus = schedulerSetPortShell(0, numProcesses);
  schedulerSetPortShell(0, schedulerGetFirstShellPid());
  return 0;
}

/// @fn static void lastSlotPreBoot(void)
///
/// @brief Set the last slot as a port's shell at launch.
///
/// @return This function returns no value.
static void lastSlotPreBoot(void) {
  halMockSetLaunchHook(setLastSlotAsShell);
}

NANO_OS_KERNEL_TEST_WITH(session, the_last_process_slot_can_be_a_ports_shell,
  lastSlotPreBoot
) {
  NANO_OS_ASSERT_EQ_INT(processSuccess, lastSlotStatus);
}
