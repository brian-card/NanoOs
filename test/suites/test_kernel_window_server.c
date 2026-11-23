///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_window_server.c
///
/// @brief             Tests of the scheduler's window server support:
///                    registering the window server, connecting clients to
///                    it with IPC grants both ways, and WIN_MSG_CLIENT_ENDED
///                    on every way a client ends.
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

#include "kernel/GraphicsProtocol.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "kernel/WindowProtocol.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsSignal.h"
#include "user/NanoOsSpawn.h"

int addProcessIpcCapability(ProcessDescriptor *processDescriptor,
  ProcessId destinationPid, int64_t signature, uint32_t messageType);

/// @def SERVER_PATH
///
/// @brief The program the tests register as the graphics client, which can
/// become the window server.
#define SERVER_PATH "/system/commands/windowserver"

/// @def SERVER_NAME
///
/// @brief The name a system spawn of SERVER_PATH uses.
#define SERVER_NAME "windowserver"

/// @def APP_PATH
///
/// @brief A user program that connects to the window server.
#define APP_PATH "/usr/bin/app"

/// @def OTHER_PATH
///
/// @brief A user program for a client to exec.
#define OTHER_PATH "/usr/bin/other"

/// @def MAX_YIELDS
///
/// @brief How many times a test yields waiting for something to happen.
#define MAX_YIELDS 1000

/// @def MAX_ENDED
///
/// @brief How many WIN_MSG_CLIENT_ENDED messages the window server records.
#define MAX_ENDED 4

/// @def NUM_SLOTS
///
/// @brief How many programs a test can follow the results of.
#define NUM_SLOTS 3

/// @def NO_RESULT
///
/// @brief The result a program hasn't reported yet.
#define NO_RESULT (-100)

static volatile int endedCount;
static volatile ProcessId endedPids[MAX_ENDED];
static volatile int registerResults[NUM_SLOTS];
static volatile int connectResults[NUM_SLOTS];
static volatile int secondConnectResults[NUM_SLOTS];
static volatile ProcessId connectPids[NUM_SLOTS];
static volatile size_t capabilityCounts[NUM_SLOTS];
static volatile bool go;
static volatile int execStatus;

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

/// @fn static int windowServerCommand(SchedulerCommand command,
///   ProcessId *pid)
///
/// @brief Send the scheduler SCHEDULER_REGISTER_WINDOW_SERVER or
/// SCHEDULER_CONNECT_WINDOW_SERVER and wait for its answer.
///
/// @param command The command.
/// @param pid Set to the window server's ProcessId the answer carries.
///
/// @return Returns the answer's errorNumber, or -1 if the send failed.
static int windowServerCommand(SchedulerCommand command, ProcessId *pid) {
  SchedulerWindowServerArgs args = { .errorNumber = -1, .pid = 0 };
  ProcessMessage *processMessage = initSendProcessMessageToPid(schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | command, &args, sizeof(args), true);
  if (processMessage == NULL) {
    return -1;
  }
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);
  *pid = args.pid;
  return args.errorNumber;
}

/// @fn static void serve(void)
///
/// @brief Act as the window server:  record the PID of every
/// WIN_MSG_CLIENT_ENDED and finish every message.
///
/// @return This function never returns.
static void serve(void) {
  while (1) {
    ProcessMessage *processMessage = processMessageQueueWait(NULL);
    if (processMessage == NULL) {
      continue;
    }
    if (processMessageType(processMessage)
      == (WIN_COMMAND_SIGNATURE | WIN_MSG_CLIENT_ENDED)
    ) {
      if (endedCount < MAX_ENDED) {
        endedPids[endedCount]
          = (ProcessId) (uintptr_t) processMessageData(processMessage);
      }
      endedCount++;
    }
    processMessageSetDone(processMessage);
    if (processMessageWaiting(processMessage) == false) {
      processMessageRelease(processMessage);
    }
  }
}

/// @fn static void* program(void *args)
///
/// @brief Stand in for a spawned program, chosen by argv[0], reporting into
/// the slot argv[1] names:  "server" registers and serves; "connect"
/// connects and runs until killed; "twice" connects twice; "exit" connects
/// and exits; "corrupt" connects and clobbers its coroutine's guard so the
/// scheduler removes it; "exec" connects and execs OTHER_PATH; "idle" only
/// runs until killed.  The connecting modes wait for go first.
///
/// @param args The ExecArgs of the spawn.
///
/// @return This function returns NULL for "exit" and never returns otherwise.
static void* program(void *args) {
  ExecArgs *execArgs = (ExecArgs*) args;
  const char *mode = execArgs->argv[0];
  int slot = execArgs->argv[1][0] - '0';
  ProcessId pid = 0;

  if (strcmp(mode, "server") == 0) {
    registerResults[slot]
      = windowServerCommand(SCHEDULER_REGISTER_WINDOW_SERVER, &pid);
    serve();
  } else if (strcmp(mode, "idle") != 0) {
    while (!go) {
      processYield();
    }
    int result = windowServerCommand(SCHEDULER_CONNECT_WINDOW_SERVER, &pid);
    connectPids[slot] = pid;
    capabilityCounts[slot] = getRunningProcess()->numIpcCapabilities;
    connectResults[slot] = result;
    if (strcmp(mode, "twice") == 0) {
      secondConnectResults[slot]
        = windowServerCommand(SCHEDULER_CONNECT_WINDOW_SERVER, &pid);
    } else if (strcmp(mode, "exit") == 0) {
      return NULL;
    } else if (strcmp(mode, "corrupt") == 0) {
      getRunningProcess()->mainThread->guard1 = 0;
    } else if (strcmp(mode, "exec") == 0) {
      char *argv[] = { "idle", "0", NULL };
      execStatus = schedulerExecve(OTHER_PATH, argv, NULL);
    }
  }
  while (1) {
    processYield();
  }
  return NULL;
}

/// @fn static void windowServerPreBoot(void)
///
/// @brief Run spawned programs as program, and register the first user slot
/// as the display driver of SERVER_PATH.
///
/// @return This function returns no value.
static void windowServerPreBoot(void) {
  halMockSetExecCommand(program);
  halMockSetGraphics(0, SERVER_PATH);
}

/// @fn static void resetResults(void)
///
/// @brief Forget every program's results.
///
/// @return This function returns no value.
static void resetResults(void) {
  endedCount = 0;
  go = false;
  for (int ii = 0; ii < NUM_SLOTS; ii++) {
    registerResults[ii] = NO_RESULT;
    connectResults[ii] = NO_RESULT;
    secondConnectResults[ii] = NO_RESULT;
    connectPids[ii] = 0;
    capabilityCounts[ii] = 0;
  }
}

/// @fn static ProcessDescriptor* startDriver(void)
///
/// @brief Start fakeDriver in the slot windowServerPreBoot registered.
///
/// @return Returns a pointer to the driver's ProcessDescriptor, or NULL on
/// failure.
static ProcessDescriptor* startDriver(void) {
  resetResults();
  ProcessId pid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
  processQueueRemove(halMockFreeQueue(), processDescriptor);
  if (processCreate(processDescriptor, fakeDriver, NULL) != processSuccess) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  processDescriptor->userId = SYSTEM_USER_ID;
  processDescriptor->readyQueue
    = halMockReadyQueue(PRIVILEGE_LEVEL_EXECUTIVE);
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return processDescriptor;
}

/// @fn static ProcessDescriptor* spawn(const char *path, char *mode,
///   char *slot, bool system)
///
/// @brief Spawn program as a program.
///
/// @param path The program's name in /system/commands/ for a system spawn,
///   or its path under /userspace/ otherwise.
/// @param mode The program's mode, passed as argv[0].
/// @param slot The results slot, "0" to "2", passed as argv[1].
/// @param system Whether to spawn with POSIX_SPAWN_SYSTEM.
///
/// @return Returns a pointer to the new process's ProcessDescriptor, or NULL
/// if the spawn failed.
static ProcessDescriptor* spawn(const char *path, char *mode, char *slot,
  bool system
) {
  posix_spawnattr_t attr = { .flags = system ? POSIX_SPAWN_SYSTEM : 0 };
  pid_t pid = 0;
  char *argv[] = { mode, slot, NULL };
  if (nanoOsSpawn(&pid, path, NULL, &attr, argv, NULL) != 0) {
    return NULL;
  }
  return &allProcesses[pid - 1];
}

/// @fn static void waitForResult(volatile int *result)
///
/// @brief Yield until a program reports a result, or MAX_YIELDS times.
///
/// @param result The result to wait for.
///
/// @return This function returns no value.
static void waitForResult(volatile int *result) {
  for (int ii = 0; (ii < MAX_YIELDS) && (*result == NO_RESULT); ii++) {
    processYield();
  }
}

/// @fn static void waitForEnded(int count)
///
/// @brief Yield until the window server has recorded a number of
/// WIN_MSG_CLIENT_ENDED messages, or MAX_YIELDS times.
///
/// @param count The number to wait for.
///
/// @return This function returns no value.
static void waitForEnded(int count) {
  for (int ii = 0; (ii < MAX_YIELDS) && (endedCount < count); ii++) {
    processYield();
  }
}

/// @fn static ProcessDescriptor* startServer(void)
///
/// @brief Start the fake display driver and a window server that registered.
///
/// @return Returns a pointer to the window server's ProcessDescriptor, or
/// NULL on failure.
static ProcessDescriptor* startServer(void) {
  if (startDriver() == NULL) {
    return NULL;
  }
  ProcessDescriptor *server = spawn(SERVER_NAME, "server", "0", true);
  if (server == NULL) {
    return NULL;
  }
  waitForResult(&registerResults[0]);
  return (registerResults[0] == 0) ? server : NULL;
}

/// @fn static uint32_t grantedTypes(ProcessDescriptor *from, ProcessId to)
///
/// @brief Get the window message types one process may send another.
///
/// @param from The sending process.
/// @param to The ProcessId of the receiving process.
///
/// @return Returns the mask of WIN_COMMAND_SIGNATURE types allowed.
static uint32_t grantedTypes(ProcessDescriptor *from, ProcessId to) {
  for (size_t ii = 0; ii < from->numIpcCapabilities; ii++) {
    if ((from->ipcCapabilities[ii].destinationPid == to)
      && (from->ipcCapabilities[ii].signature == WIN_COMMAND_SIGNATURE)
    ) {
      return from->ipcCapabilities[ii].messageTypes;
    }
  }
  return 0;
}

/// @fn static int killPid(ProcessId pid)
///
/// @brief Kill a process with SIGKILL.
///
/// @param pid The process to kill.
///
/// @return Returns the kill's returnValue.
static int killPid(ProcessId pid) {
  SchedulerKillProcessArgs args = {
    .pid = pid, .signal = SIGKILL, .returnValue = 0, .errorNumber = 0,
  };
  ProcessMessage *processMessage = initSendProcessMessageToPid(schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_KILL_PROCESS,
    &args, sizeof(args), true);
  if (processMessage != NULL) {
    processMessageWaitForDone(processMessage, NULL);
    processMessageRelease(processMessage);
  }
  return args.returnValue;
}

/// @fn static ProcessDescriptor* connectClient(const char *mode, int slot)
///
/// @brief Spawn a user program that connects to the window server, and wait
/// for its answer.
///
/// @param mode The program's mode.
/// @param slot The results slot.
///
/// @return Returns a pointer to the program's ProcessDescriptor, or NULL if
/// the spawn failed.
static ProcessDescriptor* connectClient(const char *mode, int slot) {
  static char *slotNames[NUM_SLOTS] = { "0", "1", "2" };
  ProcessDescriptor *client
    = spawn(APP_PATH, (char*) mode, slotNames[slot], false);
  if (client == NULL) {
    return NULL;
  }
  go = true;
  waitForResult(&connectResults[slot]);
  return client;
}

NANO_OS_KERNEL_TEST_WITH(window_server, the_graphics_client_registers_and_a_second_is_busy,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessDescriptor *second = spawn(SERVER_NAME, "server", "1", true);
  NANO_OS_ASSERT_NOT_NULL(second);
  waitForResult(&registerResults[1]);

  NANO_OS_ASSERT_EQ_INT(0, registerResults[0]);
  NANO_OS_ASSERT_TRUE(second->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(EBUSY, registerResults[1]);
  NANO_OS_ASSERT_EQ_INT(0, killPid(second->processId));
  NANO_OS_ASSERT_EQ_INT(0, killPid(server->processId));
}

NANO_OS_KERNEL_TEST_WITH(window_server, a_process_that_isnt_the_graphics_client_cant_register,
  windowServerPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver());
  ProcessId pid = 0;

  NANO_OS_ASSERT_EQ_INT(EPERM,
    windowServerCommand(SCHEDULER_REGISTER_WINDOW_SERVER, &pid));
  NANO_OS_ASSERT_EQ_INT(0, pid);
}

NANO_OS_KERNEL_TEST_WITH(window_server, a_connecting_client_is_granted_both_ways,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessDescriptor *client = connectClient("connect", 0);
  NANO_OS_ASSERT_NOT_NULL(client);

  NANO_OS_ASSERT_EQ_INT(0, connectResults[0]);
  NANO_OS_ASSERT_EQ_INT(server->processId, connectPids[0]);
  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_USER, client->privilegeLevel);
  NANO_OS_ASSERT_TRUE(client->windowClient);
  NANO_OS_ASSERT_EQ_INT(WIN_CLIENT_MESSAGE_MASK,
    grantedTypes(client, server->processId));
  NANO_OS_ASSERT_EQ_INT(WIN_SERVER_MESSAGE_MASK,
    grantedTypes(server, client->processId));
  NANO_OS_ASSERT_EQ_INT(0, killPid(client->processId));
  NANO_OS_ASSERT_EQ_INT(0, killPid(server->processId));
}

NANO_OS_KERNEL_TEST_WITH(window_server, connecting_again_grants_nothing_new,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessDescriptor *client = connectClient("twice", 0);
  NANO_OS_ASSERT_NOT_NULL(client);
  waitForResult(&secondConnectResults[0]);

  NANO_OS_ASSERT_EQ_INT(0, connectResults[0]);
  NANO_OS_ASSERT_EQ_INT(0, secondConnectResults[0]);
  NANO_OS_ASSERT_EQ_INT(server->processId, connectPids[0]);
  NANO_OS_ASSERT_EQ_INT((int) capabilityCounts[0],
    (int) client->numIpcCapabilities);
  NANO_OS_ASSERT_EQ_INT(0, killPid(client->processId));
  NANO_OS_ASSERT_EQ_INT(0, killPid(server->processId));
}

NANO_OS_KERNEL_TEST_WITH(window_server, connecting_with_no_window_server_is_not_found,
  windowServerPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(startDriver());
  ProcessDescriptor *client = connectClient("connect", 0);
  NANO_OS_ASSERT_NOT_NULL(client);

  NANO_OS_ASSERT_EQ_INT(ENOENT, connectResults[0]);
  NANO_OS_ASSERT_FALSE(client->windowClient);
  NANO_OS_ASSERT_EQ_INT(0, killPid(client->processId));
}

NANO_OS_KERNEL_TEST_WITH(window_server, another_users_process_cant_connect,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessDescriptor *client = spawn(APP_PATH, "connect", "0", false);
  NANO_OS_ASSERT_NOT_NULL(client);
  client->userId = (UserId) (server->userId + 1);
  go = true;
  waitForResult(&connectResults[0]);

  NANO_OS_ASSERT_EQ_INT(EPERM, connectResults[0]);
  NANO_OS_ASSERT_FALSE(client->windowClient);
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(client, server->processId));
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(server, client->processId));
  NANO_OS_ASSERT_EQ_INT(0, killPid(client->processId));
  NANO_OS_ASSERT_EQ_INT(0, killPid(server->processId));
}

/// @def assertEndedOnce
///
/// @brief Check that the window server was told once that a client ended,
/// and that the client no longer holds the window grants.  A macro so that a
/// failure reports the test's own line.
#define assertEndedOnce(server, client, pid) \
  do { \
    waitForEnded(1); \
    NANO_OS_ASSERT_EQ_INT(1, endedCount); \
    NANO_OS_ASSERT_EQ_INT((pid), endedPids[0]); \
    NANO_OS_ASSERT_FALSE((client)->windowClient); \
    NANO_OS_ASSERT_EQ_INT(0, grantedTypes((server), (pid))); \
  } while (0)

NANO_OS_KERNEL_TEST_WITH(window_server, an_exiting_client_is_reported_once,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessDescriptor *client = connectClient("exit", 0);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;
  NANO_OS_ASSERT_EQ_INT(0, connectResults[0]);

  assertEndedOnce(server, client, pid);
  NANO_OS_ASSERT_EQ_INT(0, killPid(server->processId));
}

NANO_OS_KERNEL_TEST_WITH(window_server, a_killed_client_is_reported_once,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessDescriptor *client = connectClient("connect", 0);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;

  NANO_OS_ASSERT_EQ_INT(0, killPid(pid));
  assertEndedOnce(server, client, pid);
  NANO_OS_ASSERT_EQ_INT(0, killPid(server->processId));
}

NANO_OS_KERNEL_TEST_WITH(window_server, a_removed_client_is_reported_once,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessDescriptor *client = connectClient("corrupt", 0);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;

  assertEndedOnce(server, client, pid);
  NANO_OS_ASSERT_EQ_INT(0, killPid(server->processId));
}

NANO_OS_KERNEL_TEST_WITH(window_server, a_client_execing_another_program_is_reported,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessDescriptor *client = spawn(APP_PATH, "exec", "0", false);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;
  NANO_OS_ASSERT_EQ_INT(0, addProcessIpcCapability(client, schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_EXECVE));
  execStatus = 1;
  go = true;

  waitForEnded(1);
  NANO_OS_ASSERT_EQ_INT(1, execStatus);
  assertEndedOnce(server, client, pid);
  NANO_OS_ASSERT_EQ_INT(0, connectResults[0]);
  NANO_OS_ASSERT_EQ_INT(0, killPid(pid));
  NANO_OS_ASSERT_EQ_INT(0, killPid(server->processId));
}

NANO_OS_KERNEL_TEST_WITH(window_server, a_process_that_never_connected_ends_unreported,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessDescriptor *idle = spawn(APP_PATH, "idle", "0", false);
  NANO_OS_ASSERT_NOT_NULL(idle);

  NANO_OS_ASSERT_EQ_INT(0, killPid(idle->processId));
  waitForEnded(1);
  NANO_OS_ASSERT_EQ_INT(0, endedCount);
  NANO_OS_ASSERT_EQ_INT(0, killPid(server->processId));
}

NANO_OS_KERNEL_TEST_WITH(window_server, the_window_server_ending_revokes_every_client,
  windowServerPreBoot
) {
  ProcessDescriptor *server = startServer();
  NANO_OS_ASSERT_NOT_NULL(server);
  ProcessId serverPid = server->processId;
  ProcessDescriptor *first = connectClient("connect", 0);
  NANO_OS_ASSERT_NOT_NULL(first);
  ProcessDescriptor *second = connectClient("connect", 1);
  NANO_OS_ASSERT_NOT_NULL(second);
  NANO_OS_ASSERT_TRUE(first->windowClient && second->windowClient);

  NANO_OS_ASSERT_EQ_INT(0, killPid(serverPid));
  for (int ii = 0; (ii < MAX_YIELDS) && first->windowClient; ii++) {
    processYield();
  }

  NANO_OS_ASSERT_FALSE(first->windowClient);
  NANO_OS_ASSERT_FALSE(second->windowClient);
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(first, serverPid));
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(second, serverPid));

  registerResults[0] = NO_RESULT;
  ProcessDescriptor *next = spawn(SERVER_NAME, "server", "0", true);
  NANO_OS_ASSERT_NOT_NULL(next);
  waitForResult(&registerResults[0]);
  NANO_OS_ASSERT_EQ_INT(0, registerResults[0]);
  NANO_OS_ASSERT_EQ_INT(0, killPid(first->processId));
  NANO_OS_ASSERT_EQ_INT(0, killPid(second->processId));
  NANO_OS_ASSERT_EQ_INT(0, killPid(next->processId));
}

NANO_OS_TEST(window_server, the_message_masks_hold_exactly_their_types) {
  NANO_OS_ASSERT_EQ_INT(0x00007FFF, WIN_CLIENT_MESSAGE_MASK);
  NANO_OS_ASSERT_EQ_INT(0x03000000, WIN_SERVER_MESSAGE_MASK);
  NANO_OS_ASSERT_EQ_INT(14, WIN_MSG_END_SESSION);
  NANO_OS_ASSERT_EQ_INT(24, WIN_MSG_EVENTS);
  NANO_OS_ASSERT_EQ_INT(25, WIN_MSG_DISCONNECTED);
  NANO_OS_ASSERT_EQ_INT(31, WIN_MSG_CLIENT_ENDED);
  NANO_OS_ASSERT_EQ_INT(32, WIN_NUM_MESSAGE_TYPES);
}
