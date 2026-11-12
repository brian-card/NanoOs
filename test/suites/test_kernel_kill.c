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
#include "kernel/MemoryManager.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsSignal.h"
#include "user/NanoOsUnistd.h"

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

/// @fn static ProcessDescriptor* startIdleProcessInSlot(unsigned int slot,
///   PrivilegeLevel privilegeLevel)
///
/// @brief Run idleProcess in one of the user process slots at a privilege
/// level, owned by this process's user.
///
/// @param slot The index of the user process slot, counting from 0.
/// @param privilegeLevel The privilege level to give the process.
///
/// @return Returns a pointer to the process's ProcessDescriptor, or NULL if it
/// couldn't be started.
static ProcessDescriptor* startIdleProcessInSlot(unsigned int slot,
  PrivilegeLevel privilegeLevel
) {
  ProcessId pid = schedulerGetFirstShellPid() + schedulerGetNumShells() + slot;
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
  return startIdleProcessInSlot(0, privilegeLevel);
}

/// @fn static SchedulerKillProcessArgs killPid(ProcessId pid, int signal)
///
/// @brief Ask the scheduler to kill a process and wait for its answer.
///
/// @param pid The ProcessId of the process to kill.
/// @param signal The signal to kill the process with.
///
/// @return Returns the SchedulerKillProcessArgs as the scheduler left them.
static SchedulerKillProcessArgs killPid(ProcessId pid, int signal) {
  SchedulerKillProcessArgs schedulerKillProcessArgs = {
    .pid = pid,
    .signal = signal,
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

  SchedulerKillProcessArgs result = killPid(target->processId, SIGKILL);

  NANO_OS_ASSERT_EQ_INT(1, result.returnValue);
  NANO_OS_ASSERT_EQ_INT(EACCES, result.errorNumber);
  NANO_OS_ASSERT_TRUE(processRunning(target));
}

NANO_OS_KERNEL_TEST(kill, kernel_process_survives_root) {
  getRunningProcess()->userId = ROOT_USER_ID;

  SchedulerKillProcessArgs result = killPid(consolePid, SIGKILL);

  NANO_OS_ASSERT_EQ_INT(1, result.returnValue);
  NANO_OS_ASSERT_EQ_INT(EACCES, result.errorNumber);
  NANO_OS_ASSERT_TRUE(processRunning(&allProcesses[consolePid - 1]));
}

NANO_OS_KERNEL_TEST(kill, user_process_is_killed) {
  getRunningProcess()->userId = ROOT_USER_ID;
  ProcessDescriptor *target = startIdleProcess(PRIVILEGE_LEVEL_USER);
  NANO_OS_ASSERT_NOT_NULL(target);

  SchedulerKillProcessArgs result = killPid(target->processId, SIGKILL);

  NANO_OS_ASSERT_EQ_INT(0, result.returnValue);
  NANO_OS_ASSERT_FALSE(processRunning(target));
}

NANO_OS_KERNEL_TEST(kill, killed_process_exit_status_names_the_signal) {
  getRunningProcess()->userId = ROOT_USER_ID;
  ProcessDescriptor *target = startIdleProcess(PRIVILEGE_LEVEL_USER);
  NANO_OS_ASSERT_NOT_NULL(target);

  SchedulerKillProcessArgs result = killPid(target->processId, SIGTERM);

  NANO_OS_ASSERT_EQ_INT(0, result.returnValue);
  NANO_OS_ASSERT_EQ_INT(128 + SIGTERM, target->exitStatus);
}

NANO_OS_KERNEL_TEST(kill, pid_zero_is_invalid) {
  getRunningProcess()->userId = ROOT_USER_ID;

  SchedulerKillProcessArgs result = killPid(0, SIGKILL);

  NANO_OS_ASSERT_EQ_INT(1, result.returnValue);
  NANO_OS_ASSERT_EQ_INT(EINVAL, result.errorNumber);
}

/// @fn static void giveFileDescriptors(ProcessDescriptor *processDescriptor,
///   FileDescriptor *first, FileDescriptor *second)
///
/// @brief Replace a process's file descriptor table with one that holds the
/// given descriptors.
///
/// @param processDescriptor A pointer to the process's ProcessDescriptor.
/// @param first The descriptor for the table's first entry.
/// @param second The descriptor for the table's second entry, or NULL for a
///   one-entry table.
///
/// @return This function returns no value.
static void giveFileDescriptors(ProcessDescriptor *processDescriptor,
  FileDescriptor *first, FileDescriptor *second
) {
  uint8_t numFileDescriptors = (second == NULL) ? 1 : 2;
  FileDescriptor **fileDescriptors = (FileDescriptor**) malloc(
    numFileDescriptors * sizeof(FileDescriptor*));
  NANO_OS_ASSERT_NOT_NULL(fileDescriptors);
  fileDescriptors[0] = first;
  if (second != NULL) {
    fileDescriptors[1] = second;
  }
  processDescriptor->fileDescriptors = fileDescriptors;
  processDescriptor->numFileDescriptors = numFileDescriptors;
}

NANO_OS_KERNEL_TEST(kill, killed_pipe_writer_leaves_no_link_to_the_read_end) {
  // A shell that dies holding both ends of its last pipe frees the read end
  // first, so its write end mustn't still point there.
  getRunningProcess()->userId = ROOT_USER_ID;
  ProcessDescriptor *self = getRunningProcess();
  int fds[2] = {-1, -1};
  NANO_OS_ASSERT_EQ_INT(0, nanoOsPipe(fds));
  FileDescriptor *readEnd = self->fileDescriptors[fds[0]];
  FileDescriptor *writeEnd = self->fileDescriptors[fds[1]];
  self->fileDescriptors[fds[0]] = NULL;
  self->fileDescriptors[fds[1]] = NULL;

  ProcessDescriptor *writer = startIdleProcessInSlot(0, PRIVILEGE_LEVEL_USER);
  NANO_OS_ASSERT_NOT_NULL(writer);
  ProcessDescriptor *shell = startIdleProcessInSlot(1, PRIVILEGE_LEVEL_USER);
  NANO_OS_ASSERT_NOT_NULL(shell);
  giveFileDescriptors(writer, writeEnd, NULL);
  giveFileDescriptors(shell, readEnd, writeEnd);
  writeEnd->refCount = 2;

  NANO_OS_ASSERT_EQ_INT(0, killPid(writer->processId, SIGKILL).returnValue);
  NANO_OS_ASSERT_EQ_INT(1, writeEnd->refCount);
  NANO_OS_ASSERT_NULL(readEnd->pipeEnd);
  NANO_OS_ASSERT_NULL(writeEnd->pipeEnd);

  NANO_OS_ASSERT_EQ_INT(0, killPid(shell->processId, SIGKILL).returnValue);
  NANO_OS_ASSERT_FALSE(processRunning(shell));
}
