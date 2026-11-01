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
#include "user/NanoOsSignal.h"
#include "user/NanoOsWait.h"

size_t mockUartDrain(char *out, size_t max);
int removeProcessIpcCapability(ProcessDescriptor *processDescriptor,
  ProcessId destinationPid, int64_t signature, uint32_t messageType);

/// @var grandchildStatus
///
/// @brief What a "spawner" child's spawn of its own child returned, or -1
/// before it has tried.
static volatile int grandchildStatus;

/// @fn static void* printingChild(void *args)
///
/// @brief Stand in for a spawned program, chosen by argv[0]:  "idle" runs
/// until killed, "exit" exits, "spawner" spawns a printing child in the
/// foreground, waits for it, prints a line of its own, and idles, and anything
/// else prints a line and exits.
///
/// @param args The ExecArgs of the spawn.
///
/// @return This function always returns NULL.
static void* printingChild(void *args) {
  ExecArgs *execArgs = (ExecArgs*) args;
  if (strcmp(execArgs->argv[0], "exit") == 0) {
    return NULL;
  } else if (strcmp(execArgs->argv[0], "spawner") == 0) {
    posix_spawnattr_t attr = { .flags = POSIX_SPAWN_FOREGROUND };
    pid_t pid = 0;
    char *argv[] = { "print", NULL };
    int status = nanoOsSpawn(&pid, "/fgtest", NULL, &attr, argv, NULL);
    if ((status == 0) && (schedulerWaitpid(pid, NULL, 0) != pid)) {
      status = -2;
    }
    nanoOsFputs("spawner-output\n", nanoOsStdout);
    grandchildStatus = status;
  }
  if ((strcmp(execArgs->argv[0], "idle") == 0)
    || (strcmp(execArgs->argv[0], "spawner") == 0)
  ) {
    while (1) {
      processYield();
    }
  }
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

/// @fn static bool parentOwnsPort(void)
///
/// @brief Determine whether the running process owns console port 0.  The
/// harness's test process has no file descriptors, so it can't find out by
/// printing.
///
/// @return Returns true if the process owns the port, false if not.
static bool parentOwnsPort(void) {
  return getOwnedConsolePort() == 0;
}

/// @fn static pid_t spawnChild(char *name, short flags)
///
/// @brief Spawn a printingChild in a given mode.
///
/// @param name The mode, passed as argv[0].
/// @param flags The POSIX_SPAWN_* flags to spawn with.
///
/// @return Returns the child's process ID, or 0 on failure.
static pid_t spawnChild(char *name, short flags) {
  posix_spawnattr_t attr = { .flags = flags };
  pid_t pid = 0;
  char *argv[] = { name, NULL };
  if (nanoOsSpawn(&pid, "/fgtest", NULL, &attr, argv, NULL) != 0) {
    return 0;
  }
  return pid;
}

/// @fn static pid_t spawnIdleChild(void)
///
/// @brief Spawn a child that runs until killed, lending it the console port.
///
/// @return Returns the child's process ID, or 0 on failure.
static pid_t spawnIdleChild(void) {
  return spawnChild("idle", POSIX_SPAWN_FOREGROUND);
}

/// @fn static bool canSpawn(ProcessDescriptor *processDescriptor)
///
/// @brief Determine whether a process is allowed to spawn.
///
/// @param processDescriptor A pointer to the process's ProcessDescriptor.
///
/// @return Returns true if the process can send SCHEDULER_SPAWN.
static bool canSpawn(ProcessDescriptor *processDescriptor) {
  return findIpcCapability(processDescriptor->ipcCapabilities,
    processDescriptor->numIpcCapabilities, schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_SPAWN) != NULL;
}

/// @fn static bool yieldUntilSet(volatile int *value)
///
/// @brief Yield until a value is no longer -1, giving up after a while.
///
/// @param value A pointer to the value to watch.
///
/// @return Returns true if the value changed.
static bool yieldUntilSet(volatile int *value) {
  for (int ii = 0; (ii < 2000) && (*value == -1); ii++) {
    processYield();
  }
  return *value != -1;
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

NANO_OS_KERNEL_TEST_WITH(spawn_foreground,
  background_child_prints_to_its_parents_port, spawnForegroundPreBoot
) {
  char output[256];

  NANO_OS_ASSERT_TRUE(spawnAndCollectOutput(0, output, sizeof(output)));

  NANO_OS_ASSERT_NOT_NULL(strstr(output, "lent-port-output"));
  NANO_OS_ASSERT_NULL(strstr(output, "non-owning"));
  NANO_OS_ASSERT_TRUE(parentOwnsPort());
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
  NANO_OS_ASSERT_NULL(strstr(output, "lent-port-output"));
  NANO_OS_ASSERT_NULL(strstr(output, "non-owning"));
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

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, port_returns_when_the_child_exits,
  spawnForegroundPreBoot
) {
  char output[256];
  NANO_OS_ASSERT_TRUE(
    spawnAndCollectOutput(POSIX_SPAWN_FOREGROUND, output, sizeof(output)));

  NANO_OS_ASSERT_TRUE(parentOwnsPort());
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, port_returns_when_the_child_is_killed,
  spawnForegroundPreBoot
) {
  pid_t pid = spawnIdleChild();
  NANO_OS_ASSERT_TRUE(pid > 0);
  NANO_OS_ASSERT_FALSE(parentOwnsPort());

  NANO_OS_ASSERT_EQ_INT(0, schedulerKillProcess(pid));
  int status = 0;
  NANO_OS_ASSERT_EQ_INT(pid, schedulerWaitpid(pid, &status, 0));

  NANO_OS_ASSERT_EQ_INT(SIGKILL, WTERMSIG(status));
  NANO_OS_ASSERT_TRUE(parentOwnsPort());
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, port_returns_after_an_interrupt,
  spawnForegroundPreBoot
) {
  pid_t pid = spawnIdleChild();
  NANO_OS_ASSERT_TRUE(pid > 0);

  NANO_OS_ASSERT_EQ_INT(0, schedulerSendSignal(pid, SIGINT));
  int status = 0;
  ProcessId exited = 0;
  for (int ii = 0; (ii < 2000) && (exited == 0); ii++) {
    processYield();
    exited = schedulerWaitpid(pid, &status, WNOHANG);
  }
  NANO_OS_ASSERT_EQ_INT(pid, exited);

  NANO_OS_ASSERT_EQ_INT(SIGINT, WTERMSIG(status));
  NANO_OS_ASSERT_TRUE(parentOwnsPort());
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, allowed_child_spawns_and_gets_port_back,
  spawnForegroundPreBoot
) {
  grandchildStatus = -1;
  drainUart();
  pid_t pid
    = spawnChild("spawner", POSIX_SPAWN_FOREGROUND | POSIX_SPAWN_ALLOW_SPAWN);
  NANO_OS_ASSERT_TRUE(pid > 0);
  NANO_OS_ASSERT_TRUE(canSpawn(&allProcesses[pid - 1]));

  NANO_OS_ASSERT_TRUE(yieldUntilSet(&grandchildStatus));
  NANO_OS_ASSERT_EQ_INT(0, grandchildStatus);
  char output[256];
  collectUart(output, sizeof(output));
  // The port went back to the child that lent it, not to the shell.
  NANO_OS_ASSERT_NOT_NULL(strstr(output, "lent-port-output"));
  NANO_OS_ASSERT_NOT_NULL(strstr(output, "spawner-output"));
  NANO_OS_ASSERT_NULL(strstr(output, "non-owning"));

  NANO_OS_ASSERT_EQ_INT(0, schedulerKillProcess(pid));
  NANO_OS_ASSERT_EQ_INT(pid, schedulerWaitpid(pid, NULL, 0));
  NANO_OS_ASSERT_TRUE(parentOwnsPort());
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, child_cannot_spawn_unless_allowed,
  spawnForegroundPreBoot
) {
  grandchildStatus = -1;
  pid_t pid = spawnChild("spawner", POSIX_SPAWN_FOREGROUND);
  NANO_OS_ASSERT_TRUE(pid > 0);
  NANO_OS_ASSERT_FALSE(canSpawn(&allProcesses[pid - 1]));

  NANO_OS_ASSERT_TRUE(yieldUntilSet(&grandchildStatus));
  NANO_OS_ASSERT_EQ_INT(ENOMEM, grandchildStatus);
  NANO_OS_ASSERT_EQ_INT(0, schedulerKillProcess(pid));
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, allowance_ends_with_the_process,
  spawnForegroundPreBoot
) {
  pid_t allowed = spawnChild("exit", POSIX_SPAWN_ALLOW_SPAWN);
  NANO_OS_ASSERT_TRUE(allowed > 0);
  NANO_OS_ASSERT_EQ_INT(allowed, schedulerWaitpid(allowed, NULL, 0));

  pid_t pid = 0;
  for (int ii = 0; (ii < 2 * (int) numProcesses) && (pid != allowed); ii++) {
    pid = spawnChild("exit", 0);
    NANO_OS_ASSERT_TRUE(pid > 0);
    NANO_OS_ASSERT_FALSE(canSpawn(&allProcesses[pid - 1]));
    NANO_OS_ASSERT_EQ_INT(pid, schedulerWaitpid(pid, NULL, 0));
  }
  NANO_OS_ASSERT_EQ_INT(allowed, pid);
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, allowance_is_not_shared_with_others,
  spawnForegroundPreBoot
) {
  pid_t allowed = spawnChild("idle", POSIX_SPAWN_ALLOW_SPAWN);
  pid_t other = spawnChild("idle", 0);
  NANO_OS_ASSERT_TRUE((allowed > 0) && (other > 0));

  NANO_OS_ASSERT_TRUE(canSpawn(&allProcesses[allowed - 1]));
  NANO_OS_ASSERT_FALSE(canSpawn(&allProcesses[other - 1]));
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, removal_is_not_shared_with_others,
  spawnForegroundPreBoot
) {
  pid_t first = spawnChild("idle", 0);
  pid_t second = spawnChild("idle", 0);
  NANO_OS_ASSERT_TRUE((first > 0) && (second > 0));
  ProcessDescriptor *firstProcess = &allProcesses[first - 1];

  NANO_OS_ASSERT_EQ_INT(0, removeProcessIpcCapability(firstProcess,
    schedulerPid, SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_WAIT_PID));

  NANO_OS_ASSERT_NULL(findIpcCapability(firstProcess->ipcCapabilities,
    firstProcess->numIpcCapabilities, schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_WAIT_PID));
  NANO_OS_ASSERT_NOT_NULL(findIpcCapability(
    allProcesses[second - 1].ipcCapabilities,
    allProcesses[second - 1].numIpcCapabilities, schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_WAIT_PID));
}

NANO_OS_KERNEL_TEST_WITH(spawn_foreground, only_foreground_children_can_kill,
  spawnForegroundPreBoot
) {
  pid_t foreground = spawnIdleChild();
  pid_t background = spawnChild("idle", 0);
  NANO_OS_ASSERT_TRUE((foreground > 0) && (background > 0));

  NANO_OS_ASSERT_NOT_NULL(findIpcCapability(
    allProcesses[foreground - 1].ipcCapabilities,
    allProcesses[foreground - 1].numIpcCapabilities, schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_KILL_PROCESS));
  NANO_OS_ASSERT_NULL(findIpcCapability(
    allProcesses[background - 1].ipcCapabilities,
    allProcesses[background - 1].numIpcCapabilities, schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE, SCHEDULER_KILL_PROCESS));
}

NANO_OS_KERNEL_TEST(spawn_foreground, port_buffer_returns_to_the_output_owner) {
  memset(&lendState, 0, sizeof(lendState));
  lendState.numConsolePorts = 1;
  ConsolePort *consolePort = &lendState.consolePorts[0];
  consolePort->consoleBuffer = &lendState.consoleBuffers[0];
  consolePort->shell = getRunningPid();
  consolePort->outputOwner = consolePid;
  consolePort->consoleBuffer->owner = consolePid;

  NANO_OS_ASSERT_EQ_INT(0,
    releaseConsoleBuffer(&lendState, consolePort->consoleBuffer));

  NANO_OS_ASSERT_EQ_INT(consolePid, consolePort->consoleBuffer->owner);
}
