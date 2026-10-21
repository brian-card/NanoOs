///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_kill.c
///
/// @brief             Kernel tests for which processes SCHEDULER_KILL_PROCESS
///                    is allowed to kill.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include "kernel/Console.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"

/// @fn static void* idleProcess(void *args)
///
/// @brief Yield forever.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* idleProcess(void *args) {
  (void) args;
  while (1) {
    processYield();
  }

  return NULL;
}

/// @fn static ProcessDescriptor* startIdleProcess(
///   PrivilegeLevel privilegeLevel)
///
/// @brief Run idleProcess in the first user process slot at a privilege level,
/// owned by this process's user.
///
/// @param privilegeLevel The privilege level to give the process.
///
/// @return Returns a pointer to the process's ProcessDescriptor, or NULL if it
/// couldn't be started.
static ProcessDescriptor* startIdleProcess(PrivilegeLevel privilegeLevel) {
  ProcessId pid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
  if (processCreate(processDescriptor, idleProcess, NULL) != processSuccess) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->privilegeLevel = privilegeLevel;
  processDescriptor->userId = getRunningUid();
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return processDescriptor;
}

/// @fn static SchedulerKillProcessArgs killPid(ProcessId pid)
///
/// @brief Ask the scheduler to kill a process and wait for its answer.
///
/// @param pid The ProcessId of the process to kill.
///
/// @return Returns the SchedulerKillProcessArgs as the scheduler left them.
static SchedulerKillProcessArgs killPid(ProcessId pid) {
  SchedulerKillProcessArgs schedulerKillProcessArgs = {
    .pid = pid,
    .returnValue = 0,
    .errorNumber = 0,
  };
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_KILL_PROCESS,
    &schedulerKillProcessArgs, sizeof(schedulerKillProcessArgs), true);
  if (processMessage != NULL) {
    processMessageWaitForDone(processMessage, NULL);
    processMessageRelease(processMessage);
  }
  return schedulerKillProcessArgs;
}

NANO_OS_KERNEL_TEST(kill, executive_process_in_a_user_slot_survives) {
  getRunningProcess()->userId = ROOT_USER_ID;
  ProcessDescriptor *target = startIdleProcess(PRIVILEGE_LEVEL_EXECUTIVE);
  NANO_OS_ASSERT_NOT_NULL(target);

  SchedulerKillProcessArgs result = killPid(target->processId);

  NANO_OS_ASSERT_EQ_INT(1, result.returnValue);
  NANO_OS_ASSERT_EQ_INT(EACCES, result.errorNumber);
  NANO_OS_ASSERT_TRUE(processRunning(target));
}

NANO_OS_KERNEL_TEST(kill, kernel_process_survives_root) {
  getRunningProcess()->userId = ROOT_USER_ID;

  SchedulerKillProcessArgs result = killPid(consolePid);

  NANO_OS_ASSERT_EQ_INT(1, result.returnValue);
  NANO_OS_ASSERT_EQ_INT(EACCES, result.errorNumber);
  NANO_OS_ASSERT_TRUE(processRunning(&allProcesses[consolePid - 1]));
}

NANO_OS_KERNEL_TEST(kill, user_process_is_killed) {
  getRunningProcess()->userId = ROOT_USER_ID;
  ProcessDescriptor *target = startIdleProcess(PRIVILEGE_LEVEL_USER);
  NANO_OS_ASSERT_NOT_NULL(target);

  SchedulerKillProcessArgs result = killPid(target->processId);

  NANO_OS_ASSERT_EQ_INT(0, result.returnValue);
  NANO_OS_ASSERT_FALSE(processRunning(target));
}

NANO_OS_KERNEL_TEST(kill, pid_zero_is_invalid) {
  getRunningProcess()->userId = ROOT_USER_ID;

  SchedulerKillProcessArgs result = killPid(0);

  NANO_OS_ASSERT_EQ_INT(1, result.returnValue);
  NANO_OS_ASSERT_EQ_INT(EINVAL, result.errorNumber);
}
