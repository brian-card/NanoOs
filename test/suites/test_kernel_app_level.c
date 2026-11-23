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
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////


#include "NanoOsTest.h"
#include "HalMock.h"

#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsHardware.h"
#include "user/NanoOsSpawn.h"

extern IpcCapability baseUserIpcCapabilities[];
extern IpcCapability baseSupervisorIpcCapabilities[];

/// @def NUM_BASE_IPC_CAPABILITIES
///
/// @brief The number of entries in baseUserIpcCapabilities and
/// baseSupervisorIpcCapabilities.
#define NUM_BASE_IPC_CAPABILITIES 4

/// @def APP_USER_ID
///
/// @brief The ordinary user the test processes run as.
#define APP_USER_ID 5

/// @def MAX_YIELDS
///
/// @brief How many times a test yields waiting for something to happen.
#define MAX_YIELDS 1000

/// @var _spawnFlags
///
/// @brief The POSIX_SPAWN_* flags the spawner process spawns with.
static short _spawnFlags = 0;

/// @var _spawnedPid
///
/// @brief The process ID the spawner's spawn gave, 0 if it failed, or -1
/// until it has run.
static volatile int _spawnedPid = -1;

/// @var _shutdownResult
///
/// @brief What the shutdown sender's SCHEDULER_SHUTDOWN returned, or 1 until
/// it has run.
static volatile int _shutdownResult = 1;

/// @var _numPowerModesEntered
///
/// @brief How many times the mock HAL's power mode was entered.
static volatile int _numPowerModesEntered = 0;

/// @fn static void waitForever(void)
///
/// @brief Finish every message sent to the running process until it's
/// killed.
///
/// @return This function never returns.
static void waitForever(void) {
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
}

/// @fn static void* idleProgram(void *args)
///
/// @brief Stand in for a spawned program that runs until killed.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* idleProgram(void *args) {
  (void) args;
  waitForever();
  return NULL;
}

/// @fn static void* spawner(void *args)
///
/// @brief Spawn idleProgram with _spawnFlags the way sysnook spawns an
/// application, and record the new process's ID.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* spawner(void *args) {
  (void) args;
  posix_spawnattr_t attr = { .flags = _spawnFlags };
  pid_t pid = 0;
  char *argv[] = { "app", NULL };
  if (nanoOsSpawn(&pid, "/usr/bin/app", NULL, &attr, argv, NULL) != 0) {
    pid = 0;
  }
  _spawnedPid = pid;
  waitForever();
  return NULL;
}

/// @fn static void* shutdownSender(void *args)
///
/// @brief Ask the scheduler to reset the system and record its answer.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* shutdownSender(void *args) {
  (void) args;
  _shutdownResult = nanoOsHardwareShutdown(NANO_OS_SHUTDOWN_RESET);
  waitForever();
  return NULL;
}

/// @fn static void countPowerMode(void)
///
/// @brief Count an entry into a power mode instead of ending the test.
///
/// @return This function returns no value.
static void countPowerMode(void) {
  _numPowerModesEntered++;
}

/// @fn static ProcessDescriptor* startProcess(void* (*function)(void*),
///   int privilegeLevel, UserId userId, IpcCapability *ipcCapabilities)
///
/// @brief Start a function as a process at a privilege level, as a user,
/// with one of the base IPC capability arrays.
///
/// @param function The function the process runs.
/// @param privilegeLevel The process's privilege level.
/// @param userId The user the process runs as.
/// @param ipcCapabilities The base IPC capability array the process gets.
///
/// @return Returns a pointer to the process's ProcessDescriptor, or NULL on
/// failure.
static ProcessDescriptor* startProcess(void* (*function)(void*),
  int privilegeLevel, UserId userId, IpcCapability *ipcCapabilities
) {
  ProcessDescriptor *processDescriptor
    = processQueuePop(halMockFreeQueue());
  if ((processDescriptor == NULL)
    || (processCreate(processDescriptor, function, NULL) != processSuccess)
  ) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->privilegeLevel = privilegeLevel;
  processDescriptor->userId = userId;
  processDescriptor->ipcCapabilities = ipcCapabilities;
  processDescriptor->numIpcCapabilities = NUM_BASE_IPC_CAPABILITIES;
  processDescriptor->ipcCapabilitiesDynamic = false;
  processDescriptor->readyQueue = halMockReadyQueue(privilegeLevel);
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return processDescriptor;
}

/// @fn static ProcessDescriptor* spawnFromExecutive(short flags)
///
/// @brief Have an executive process of an ordinary user spawn idleProgram.
///
/// @param flags The POSIX_SPAWN_* flags to spawn with.
///
/// @return Returns a pointer to the spawned process's ProcessDescriptor, or
/// NULL on failure.
static ProcessDescriptor* spawnFromExecutive(short flags) {
  _spawnFlags = flags;
  _spawnedPid = -1;
  if (startProcess(spawner, PRIVILEGE_LEVEL_EXECUTIVE, APP_USER_ID,
      baseSupervisorIpcCapabilities) == NULL
  ) {
    return NULL;
  }
  for (int ii = 0; (ii < MAX_YIELDS) && (_spawnedPid == -1); ii++) {
    processYield();
  }
  if (_spawnedPid <= 0) {
    return NULL;
  }
  return &allProcesses[_spawnedPid - 1];
}

/// @fn static void appLevelPreBoot(void)
///
/// @brief Make spawned processes run idleProgram instead of an overlay.
///
/// @return This function returns no value.
static void appLevelPreBoot(void) {
  halMockSetExecCommand(idleProgram);
}

/// @fn static int sendShutdownAs(UserId userId)
///
/// @brief Have a supervisor process of a user send SCHEDULER_SHUTDOWN, with
/// power mode entries counted.
///
/// @param userId The user the sender runs as.
///
/// @return Returns what the scheduler answered, or 1 if it never did.
static int sendShutdownAs(UserId userId) {
  halMockSetPowerHook(countPowerMode);
  _numPowerModesEntered = 0;
  _shutdownResult = 1;
  if (startProcess(shutdownSender, PRIVILEGE_LEVEL_SUPERVISOR, userId,
      baseSupervisorIpcCapabilities) == NULL
  ) {
    return 1;
  }
  for (int ii = 0; (ii < MAX_YIELDS) && (_shutdownResult == 1); ii++) {
    processYield();
  }
  return _shutdownResult;
}

NANO_OS_KERNEL_TEST_WITH(app_level, an_executive_spawn_gives_a_user_level_app,
  appLevelPreBoot
) {
  ProcessDescriptor *app = spawnFromExecutive(0);
  NANO_OS_ASSERT_NOT_NULL(app);

  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_USER, app->privilegeLevel);
  NANO_OS_ASSERT_EQ_INT(APP_USER_ID, app->userId);
  NANO_OS_ASSERT_TRUE(app->ipcCapabilities == baseUserIpcCapabilities);
  NANO_OS_ASSERT_EQ_INT(NUM_BASE_IPC_CAPABILITIES, app->numIpcCapabilities);
  NANO_OS_ASSERT_FALSE(app->ipcCapabilitiesDynamic);
}

NANO_OS_KERNEL_TEST_WITH(app_level, an_allowed_app_gets_only_spawn_added,
  appLevelPreBoot
) {
  ProcessDescriptor *app = spawnFromExecutive(POSIX_SPAWN_ALLOW_SPAWN);
  NANO_OS_ASSERT_NOT_NULL(app);

  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_USER, app->privilegeLevel);
  NANO_OS_ASSERT_EQ_INT(APP_USER_ID, app->userId);
  NANO_OS_ASSERT_EQ_INT(NUM_BASE_IPC_CAPABILITIES, app->numIpcCapabilities);
  for (int ii = 0; ii < NUM_BASE_IPC_CAPABILITIES; ii++) {
    uint32_t expected = baseUserIpcCapabilities[ii].messageTypes;
    if (baseUserIpcCapabilities[ii].destinationPid == schedulerPid) {
      expected |= ((uint32_t) 1) << SCHEDULER_SPAWN;
    }
    NANO_OS_ASSERT_EQ_INT(baseUserIpcCapabilities[ii].destinationPid,
      app->ipcCapabilities[ii].destinationPid);
    NANO_OS_ASSERT_TRUE(baseUserIpcCapabilities[ii].signature
      == app->ipcCapabilities[ii].signature);
    NANO_OS_ASSERT_EQ_INT(expected, app->ipcCapabilities[ii].messageTypes);
  }
}

NANO_OS_KERNEL_TEST_WITH(app_level, shutdown_from_an_ordinary_user_is_refused,
  appLevelPreBoot
) {
  NANO_OS_ASSERT_EQ_INT(-EPERM, sendShutdownAs(APP_USER_ID));
  NANO_OS_ASSERT_EQ_INT(0, _numPowerModesEntered);
}

NANO_OS_KERNEL_TEST_WITH(app_level, shutdown_from_root_enters_the_power_mode,
  appLevelPreBoot
) {
  NANO_OS_ASSERT_EQ_INT(0, sendShutdownAs(ROOT_USER_ID));
  NANO_OS_ASSERT_EQ_INT(1, _numPowerModesEntered);
}
