///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              10.31.2026
///
/// @file              test_kernel_spawn_foreground.c
///
/// @brief             Kernel tests for lending a console port to a process
///                    spawned with POSIX_SPAWN_FOREGROUND.
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

#include "kernel/Console.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsSpawn.h"
#include "user/NanoOsStdio.h"
#include "user/NanoOsErrno.h"

size_t mockUartDrain(char *out, size_t max);

/// @fn static void* printingChild(void *args)
///
/// @brief Stand in for a spawned program:  print a line and exit.
///
/// @param args The ExecArgs of the spawn.  Unused.
///
/// @return This function always returns NULL.
static void* printingChild(void *args) {
  (void) args;
  nanoOsFputs("lent-port-output\n", nanoOsStdout);
  return NULL;
}

/// @fn static void spawnForegroundPreBoot(void)
///
/// @brief Make spawned processes run printingChild instead of an overlay.
///
/// @return This function returns no value.
static void spawnForegroundPreBoot(void) {
  halMockSetExecCommand(printingChild);
}

/// @fn static void drainUart(void)
///
/// @brief Discard everything already written to the console UART.
///
/// @return This function returns no value.
static void drainUart(void) {
  char scratch[256];
  while (mockUartDrain(scratch, sizeof(scratch)) == sizeof(scratch)) {
  }
}

/// @fn static void collectUart(char *output, size_t size)
///
/// @brief Give the console time to write, then collect what reached the
/// console UART.
///
/// @param output The buffer to collect the UART output in.
/// @param size The size of the output buffer.
///
/// @return This function returns no value.
static void collectUart(char *output, size_t size) {
  memset(output, 0, size);
  size_t length = 0;
  for (int ii = 0; ii < 100; ii++) {
    processYield();
    length += mockUartDrain(&output[length], size - 1 - length);
  }
}

/// @fn static bool spawnAndWait(short flags)
///
/// @brief Spawn printingChild with a set of spawn flags and wait for it.
///
/// @param flags The POSIX_SPAWN_* flags to spawn with.
///
/// @return Returns true if the child was spawned and waited for, false if not.
static bool spawnAndWait(short flags) {
  posix_spawnattr_t attr = { .flags = flags };
  pid_t pid = 0;
  char *argv[] = { "fgtest", NULL };
  return (nanoOsSpawn(&pid, "/fgtest", NULL, &attr, argv, NULL) == 0)
    && (schedulerWaitpid(pid, NULL, 0) == pid);
}

/// @fn static bool spawnAndCollectOutput(short flags, char *output,
///   size_t size)
///
/// @brief Spawn printingChild with a set of spawn flags, wait for it, and
/// collect what reached the console UART.  The harness runs tests in the
/// shell slot, so the caller owns console port 0.
///
/// @param flags The POSIX_SPAWN_* flags to spawn with.
/// @param output The buffer to collect the UART output in.
/// @param size The size of the output buffer.
///
/// @return Returns true if the child was spawned and waited for, false if not.
static bool spawnAndCollectOutput(short flags, char *output, size_t size) {
  drainUart();
  if (spawnAndWait(flags) == false) {
    return false;
  }
  collectUart(output, size);
  return true;
}

/// @var spawnerDone
///
/// @brief Whether nonOwnerSpawner has spawned and waited for its child.
static volatile bool spawnerDone;

/// @var spawnStatus
///
/// @brief What nanoOsSpawn returned in userSpawner.
static volatile int spawnStatus;

/// @fn static void* userSpawner(void *args)
///
/// @brief Try to spawn printingChild from a user-level process, which isn't
/// allowed to spawn, then idle.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* userSpawner(void *args) {
  (void) args;
  pid_t pid = 0;
  char *argv[] = { "fgtest", NULL };
  spawnStatus = nanoOsSpawn(&pid, "/fgtest", NULL, NULL, argv, NULL);
  spawnerDone = true;
  while (1) {
    processYield();
  }
  return NULL;
}

/// @fn static ProcessDescriptor* startProcess(void* (*body)(void*),
///   bool canSpawn)
///
/// @brief Run a function in a free process slot that owns no console port.
/// Free slots are user level; one that can spawn gets the running process's
/// supervisor-level IPC capabilities, which the harness leaves in place.
///
/// @param body The function for the process to run.
/// @param canSpawn Whether the process should be allowed to spawn.
///
/// @return Returns a pointer to the process's ProcessDescriptor, or NULL if
/// it couldn't be started.
static ProcessDescriptor* startProcess(void* (*body)(void*), bool canSpawn) {
  ProcessDescriptor *processDescriptor = processQueuePop(halMockFreeQueue());
  if ((processDescriptor == NULL)
    || (processCreate(processDescriptor, body, NULL) != processSuccess)
  ) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  if (canSpawn) {
    processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
    processDescriptor->ipcCapabilities = getRunningProcess()->ipcCapabilities;
    processDescriptor->numIpcCapabilities
      = getRunningProcess()->numIpcCapabilities;
  }
  processDescriptor->userId = getRunningUid();
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return processDescriptor;
}

/// @fn static void* nonOwnerSpawner(void *args)
///
/// @brief Spawn printingChild with POSIX_SPAWN_FOREGROUND from a process that
/// owns no console port, wait for it, then idle.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* nonOwnerSpawner(void *args) {
  (void) args;
  spawnerDone = spawnAndWait(POSIX_SPAWN_FOREGROUND);
  while (1) {
    processYield();
  }
  return NULL;
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, child_prints_to_the_lent_port,
  spawnForegroundPreBoot
) {
  char output[256];

  NANO_OS_ASSERT_TRUE(
    spawnAndCollectOutput(POSIX_SPAWN_FOREGROUND, output, sizeof(output)));

  NANO_OS_ASSERT_NOT_NULL(strstr(output, "lent-port-output"));
  NANO_OS_ASSERT_NULL(strstr(output, "non-owning"));
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, child_without_the_flag_gets_no_port,
  spawnForegroundPreBoot
) {
  char output[256];

  NANO_OS_ASSERT_TRUE(spawnAndCollectOutput(0, output, sizeof(output)));

  NANO_OS_ASSERT_NOT_NULL(strstr(output, "non-owning"));
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, caller_without_a_port_lends_nothing,
  spawnForegroundPreBoot
) {
  spawnerDone = false;
  drainUart();
  NANO_OS_ASSERT_NOT_NULL(startProcess(nonOwnerSpawner, true));
  for (int ii = 0; (ii < 1000) && !spawnerDone; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_TRUE(spawnerDone);

  char output[256];
  collectUart(output, sizeof(output));
  NANO_OS_ASSERT_NOT_NULL(strstr(output, "non-owning"));
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, refused_spawn_reports_failure,
  spawnForegroundPreBoot
) {
  spawnerDone = false;
  spawnStatus = 0;
  NANO_OS_ASSERT_NOT_NULL(startProcess(userSpawner, false));
  for (int ii = 0; (ii < 1000) && !spawnerDone; ii++) {
    processYield();
  }

  NANO_OS_ASSERT_TRUE(spawnerDone);
  NANO_OS_ASSERT_EQ_INT(ENOMEM, spawnStatus);
}

/// @var lendState
///
/// @brief Console state for calling consoleLendPortCommandHandler directly.
static ConsoleState lendState;

NANO_OS_KERNEL_TEST(spawn_foreground, lend_from_a_non_scheduler_is_ignored) {
  memset(&lendState, 0, sizeof(lendState));
  lendState.numConsolePorts = 1;
  ConsolePort *consolePort = &lendState.consolePorts[0];
  consolePort->consoleBuffer = &lendState.consoleBuffers[0];
  consolePort->inputOwner = getRunningPid();
  consolePort->outputOwner = getRunningPid();
  ConsoleLendPortArgs consoleLendPortArgs = {
    .lender = getRunningPid(),
    .borrower = consolePid,
  };
  ProcessMessage *processMessage = getAvailableMessage();
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  processMessageInit(processMessage,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_LEND_PORT,
    &consoleLendPortArgs, sizeof(consoleLendPortArgs), true);
  // A message only records its sender when it's sent.
  NANO_OS_ASSERT_EQ_INT(processSuccess,
    processMessageQueuePush(getRunningProcess(), processMessage));
  NANO_OS_ASSERT_EQ_PTR(processMessage, processMessageQueuePop());

  consoleLendPortCommandHandler(&lendState, processMessage);

  NANO_OS_ASSERT_TRUE(processMessageDone(processMessage));
  NANO_OS_ASSERT_EQ_INT(getRunningPid(), consolePort->inputOwner);
  NANO_OS_ASSERT_EQ_INT(getRunningPid(), consolePort->outputOwner);
  processMessageRelease(processMessage);
}
