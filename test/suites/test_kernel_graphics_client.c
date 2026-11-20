///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_graphics_client.c
///
/// @brief             Tests of the scheduler's graphics client support:  the
///                    IPC grants between the display driver and the program
///                    config.sys's GRAPHICS line names, and the
///                    GFX_MSG_CLIENT_ENDED notification on every way a client
///                    ends.
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
#include "user/NanoOsSignal.h"
#include "user/NanoOsSpawn.h"

int addProcessIpcCapability(ProcessDescriptor *processDescriptor,
  ProcessId destinationPid, int64_t signature, uint32_t messageType);

/// @def CLIENT_PATH
///
/// @brief The program the tests register as the graphics client.
#define CLIENT_PATH "/system/commands/gfxclient"

/// @def CLIENT_NAME
///
/// @brief The name a system spawn of CLIENT_PATH uses.
#define CLIENT_NAME "gfxclient"

/// @def OTHER_PATH
///
/// @brief A system command that isn't the graphics client.
#define OTHER_PATH "/system/commands/other"

/// @def OTHER_NAME
///
/// @brief The name a system spawn of OTHER_PATH uses.
#define OTHER_NAME "other"

/// @def MAX_YIELDS
///
/// @brief How many times a test yields waiting for something to happen.
#define MAX_YIELDS 1000

/// @def MAX_ENDED
///
/// @brief How many GFX_MSG_CLIENT_ENDED messages the fake driver records.
#define MAX_ENDED 4

static volatile int endedCount;
static volatile ProcessId endedPids[MAX_ENDED];
static volatile int execStatus;

/// @fn static void* fakeDriver(void *args)
///
/// @brief Stand in for the display driver: record the PID of every
/// GFX_MSG_CLIENT_ENDED and finish every message.
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
    if (processMessageType(processMessage)
      == (GFX_COMMAND_SIGNATURE | GFX_MSG_CLIENT_ENDED)
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
  return NULL;
}

/// @fn static void* clientProgram(void *args)
///
/// @brief Stand in for a spawned program, chosen by argv[0]:  "idle" runs
/// until killed, "exit" exits, "overflow" clobbers its stack's end marker so
/// the scheduler terminates it, "corrupt" clobbers its coroutine's guard so
/// the scheduler removes it, and "exec" execs the program argv[1] names.
///
/// @param args The ExecArgs of the spawn.
///
/// @return This function returns NULL for "exit" and never returns otherwise.
static void* clientProgram(void *args) {
  ExecArgs *execArgs = (ExecArgs*) args;
  const char *mode = execArgs->argv[0];
  if (strcmp(mode, "exit") == 0) {
    return NULL;
  } else if (strcmp(mode, "overflow") == 0) {
    *coroutineStackEnd(getRunningProcess()->mainThread) = 0;
  } else if (strcmp(mode, "corrupt") == 0) {
    getRunningProcess()->mainThread->guard1 = 0;
  } else if (strcmp(mode, "exec") == 0) {
    char *argv[] = { "idle", NULL };
    execStatus = schedulerExecveFullPath(execArgs->argv[1], argv, NULL);
  }
  while (1) {
    processYield();
  }
  return NULL;
}

/// @fn static void graphicsPreBoot(void)
///
/// @brief Run spawned programs as clientProgram, and register the first user
/// slot as the display driver of CLIENT_PATH.
///
/// @return This function returns no value.
static void graphicsPreBoot(void) {
  halMockSetExecCommand(clientProgram);
  halMockSetGraphics(0, CLIENT_PATH);
}

/// @fn static ProcessDescriptor* startDriver(void)
///
/// @brief Start fakeDriver in the slot graphicsPreBoot registered.
///
/// @return Returns a pointer to the driver's ProcessDescriptor, or NULL on
/// failure.
static ProcessDescriptor* startDriver(void) {
  endedCount = 0;
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
///   char *argument, bool system)
///
/// @brief Spawn clientProgram as a program.
///
/// @param path The program's name in /system/commands/ for a system spawn,
///   or its path under /userspace/ otherwise.
/// @param mode The clientProgram mode, passed as argv[0].
/// @param argument argv[1], or NULL.
/// @param system Whether to spawn with POSIX_SPAWN_SYSTEM.
///
/// @return Returns a pointer to the new process's ProcessDescriptor, or NULL
/// if the spawn failed.
static ProcessDescriptor* spawn(const char *path, char *mode, char *argument,
  bool system
) {
  posix_spawnattr_t attr = { .flags = system ? POSIX_SPAWN_SYSTEM : 0 };
  pid_t pid = 0;
  char *argv[] = { mode, argument, NULL };
  if (nanoOsSpawn(&pid, path, NULL, &attr, argv, NULL) != 0) {
    return NULL;
  }
  return &allProcesses[pid - 1];
}

/// @fn static uint32_t grantedTypes(ProcessDescriptor *from, ProcessId to)
///
/// @brief Get the graphics message types one process may send another.
///
/// @param from The sending process.
/// @param to The ProcessId of the receiving process.
///
/// @return Returns the mask of GFX_COMMAND_SIGNATURE types allowed.
static uint32_t grantedTypes(ProcessDescriptor *from, ProcessId to) {
  for (size_t ii = 0; ii < from->numIpcCapabilities; ii++) {
    if ((from->ipcCapabilities[ii].destinationPid == to)
      && (from->ipcCapabilities[ii].signature == GFX_COMMAND_SIGNATURE)
    ) {
      return from->ipcCapabilities[ii].messageTypes;
    }
  }
  return 0;
}

/// @fn static void waitForEnded(int count)
///
/// @brief Yield until the fake driver has recorded a number of
/// GFX_MSG_CLIENT_ENDED messages, or MAX_YIELDS times.
///
/// @param count The number to wait for.
///
/// @return This function returns no value.
static void waitForEnded(int count) {
  for (int ii = 0; (ii < MAX_YIELDS) && (endedCount < count); ii++) {
    processYield();
  }
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

NANO_OS_KERNEL_TEST_WITH(graphics_client, the_client_program_is_granted_both_ways,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver();
  NANO_OS_ASSERT_NOT_NULL(driver);
  ProcessDescriptor *client = spawn(CLIENT_NAME, "idle", NULL, true);
  NANO_OS_ASSERT_NOT_NULL(client);

  NANO_OS_ASSERT_TRUE(client->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(GFX_CLIENT_MESSAGE_MASK,
    grantedTypes(client, driver->processId));
  NANO_OS_ASSERT_EQ_INT(GFX_DRIVER_MESSAGE_MASK,
    grantedTypes(driver, client->processId));

  NANO_OS_ASSERT_EQ_INT(0, killPid(client->processId));
}

NANO_OS_KERNEL_TEST_WITH(graphics_client, another_system_command_is_not_granted,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver();
  NANO_OS_ASSERT_NOT_NULL(driver);
  ProcessDescriptor *other = spawn(OTHER_NAME, "idle", NULL, true);
  NANO_OS_ASSERT_NOT_NULL(other);

  NANO_OS_ASSERT_FALSE(other->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(other, driver->processId));
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(driver, other->processId));

  NANO_OS_ASSERT_EQ_INT(0, killPid(other->processId));
  waitForEnded(1);
  NANO_OS_ASSERT_EQ_INT(0, endedCount);
}

NANO_OS_KERNEL_TEST_WITH(graphics_client, a_user_level_spawn_of_the_client_is_not_granted,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver();
  NANO_OS_ASSERT_NOT_NULL(driver);
  ProcessDescriptor *client = spawn(CLIENT_PATH, "idle", NULL, false);
  NANO_OS_ASSERT_NOT_NULL(client);

  NANO_OS_ASSERT_TRUE(client->privilegeLevel != PRIVILEGE_LEVEL_EXECUTIVE);
  NANO_OS_ASSERT_FALSE(client->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(client, driver->processId));
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(driver, client->processId));
  NANO_OS_ASSERT_EQ_INT(0, killPid(client->processId));
}

NANO_OS_KERNEL_TEST_WITH(graphics_client, an_exiting_client_is_reported_once,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver();
  NANO_OS_ASSERT_NOT_NULL(driver);
  ProcessDescriptor *client = spawn(CLIENT_NAME, "exit", NULL, true);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;

  waitForEnded(1);
  NANO_OS_ASSERT_EQ_INT(1, endedCount);
  NANO_OS_ASSERT_EQ_INT(pid, endedPids[0]);
  NANO_OS_ASSERT_FALSE(client->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(driver, pid));
}

NANO_OS_KERNEL_TEST_WITH(graphics_client, a_killed_client_is_reported_once,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver();
  NANO_OS_ASSERT_NOT_NULL(driver);
  ProcessDescriptor *client = spawn(CLIENT_NAME, "idle", NULL, true);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;

  NANO_OS_ASSERT_EQ_INT(0, killPid(pid));
  waitForEnded(1);
  NANO_OS_ASSERT_EQ_INT(1, endedCount);
  NANO_OS_ASSERT_EQ_INT(pid, endedPids[0]);
  NANO_OS_ASSERT_FALSE(client->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(driver, pid));
}

NANO_OS_KERNEL_TEST_WITH(graphics_client, a_client_whose_stack_overflowed_is_reported_once,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver();
  NANO_OS_ASSERT_NOT_NULL(driver);
  ProcessDescriptor *client = spawn(CLIENT_NAME, "overflow", NULL, true);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;

  waitForEnded(1);
  NANO_OS_ASSERT_EQ_INT(1, endedCount);
  NANO_OS_ASSERT_EQ_INT(pid, endedPids[0]);
  NANO_OS_ASSERT_FALSE(client->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(driver, pid));
}

NANO_OS_KERNEL_TEST_WITH(graphics_client, a_removed_client_is_reported_once,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver();
  NANO_OS_ASSERT_NOT_NULL(driver);
  ProcessDescriptor *client = spawn(CLIENT_NAME, "corrupt", NULL, true);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;

  waitForEnded(1);
  NANO_OS_ASSERT_EQ_INT(1, endedCount);
  NANO_OS_ASSERT_EQ_INT(pid, endedPids[0]);
  NANO_OS_ASSERT_FALSE(client->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(driver, pid));
}

NANO_OS_KERNEL_TEST_WITH(graphics_client, a_client_execing_another_program_is_reported,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver();
  NANO_OS_ASSERT_NOT_NULL(driver);
  execStatus = 1;
  ProcessDescriptor *client = spawn(CLIENT_NAME, "idle", NULL, true);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;
  NANO_OS_ASSERT_EQ_INT(0, killPid(pid));
  waitForEnded(1);
  endedCount = 0;

  client = spawn(CLIENT_NAME, "exec", OTHER_PATH, true);
  NANO_OS_ASSERT_NOT_NULL(client);
  pid = client->processId;
  NANO_OS_ASSERT_EQ_INT(0, addProcessIpcCapability(client, schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_EXECVE));

  waitForEnded(1);
  NANO_OS_ASSERT_EQ_INT(1, execStatus);
  NANO_OS_ASSERT_EQ_INT(1, endedCount);
  NANO_OS_ASSERT_EQ_INT(pid, endedPids[0]);
  NANO_OS_ASSERT_FALSE(client->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(0, grantedTypes(driver, pid));
  NANO_OS_ASSERT_EQ_INT(0, killPid(pid));
}

NANO_OS_KERNEL_TEST_WITH(graphics_client, a_client_execing_itself_is_reported_and_granted_again,
  graphicsPreBoot
) {
  ProcessDescriptor *driver = startDriver();
  NANO_OS_ASSERT_NOT_NULL(driver);
  execStatus = 1;
  ProcessDescriptor *client = spawn(CLIENT_NAME, "exec", CLIENT_PATH, true);
  NANO_OS_ASSERT_NOT_NULL(client);
  ProcessId pid = client->processId;
  NANO_OS_ASSERT_EQ_INT(0, addProcessIpcCapability(client, schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_EXECVE));

  waitForEnded(1);
  NANO_OS_ASSERT_EQ_INT(1, execStatus);
  NANO_OS_ASSERT_EQ_INT(1, endedCount);
  NANO_OS_ASSERT_EQ_INT(pid, endedPids[0]);
  NANO_OS_ASSERT_TRUE(client->graphicsClient);
  NANO_OS_ASSERT_EQ_INT(GFX_CLIENT_MESSAGE_MASK,
    grantedTypes(client, driver->processId));
  NANO_OS_ASSERT_EQ_INT(GFX_DRIVER_MESSAGE_MASK,
    grantedTypes(driver, pid));
  NANO_OS_ASSERT_EQ_INT(0, killPid(pid));
}

NANO_OS_TEST(graphics_client, the_message_masks_hold_exactly_their_types) {
  NANO_OS_ASSERT_EQ_INT((((uint32_t) 1) << GFX_MSG_COUNT) - 1,
    GFX_CLIENT_MESSAGE_MASK);
  NANO_OS_ASSERT_EQ_INT((((uint32_t) 1) << GFX_MSG_INPUT_EVENTS)
    | (((uint32_t) 1) << GFX_MSG_DISPLAY_REVOKED), GFX_DRIVER_MESSAGE_MASK);
  NANO_OS_ASSERT_EQ_INT(0,
    GFX_CLIENT_MESSAGE_MASK & (((uint32_t) 1) << GFX_MSG_CLIENT_ENDED));
  NANO_OS_ASSERT_EQ_INT(0,
    GFX_DRIVER_MESSAGE_MASK & (((uint32_t) 1) << GFX_MSG_CLIENT_ENDED));
  NANO_OS_ASSERT_TRUE(GFX_NUM_MESSAGE_TYPES > GFX_MSG_CLIENT_ENDED);
}
