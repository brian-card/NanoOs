///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_scheduler.c
///
/// @brief             Kernel tests for scheduler-owned services: getpid, the
///                    process table, running-process count.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "kernel/MemoryManager.h"

// nanoOsGetpid() lives in user/NanoOsUnistd.h, which drags in the NanoOs
// sys/types.h (pid_t == uint8_t) and collides with the system one already
// pulled in by <string.h>.  Forward-declare it instead.
unsigned char nanoOsGetpid(void);

NANO_OS_KERNEL_TEST(sched, getpid_matches_running_pid) {
  unsigned int pid = nanoOsGetpid();
  NANO_OS_ASSERT_EQ_INT((long long) getRunningPid(), (long long) pid);
  NANO_OS_ASSERT_TRUE(pid >= SCHEDULER_STATE->firstShellPid);
}

NANO_OS_KERNEL_TEST(sched, process_info_lists_the_core_processes) {
  ProcessInfo *info = schedulerGetProcessInfo();
  NANO_OS_ASSERT_NOT_NULL(info);
  NANO_OS_ASSERT_TRUE(info->numProcesses >= 3);

  const char *schedulerName = NULL;
  const char *consoleName   = NULL;
  const char *memmgrName     = NULL;
  bool sawSelf = false;
  unsigned int selfPid = nanoOsGetpid();

  for (uint8_t ii = 0; ii < info->numProcesses; ii++) {
    ProcessInfoElement *e = &info->processes[ii];
    if (e->pid == SCHEDULER_STATE->schedulerPid)     schedulerName = e->name;
    if (e->pid == SCHEDULER_STATE->consolePid)       consoleName   = e->name;
    if (e->pid == SCHEDULER_STATE->memoryManagerPid) memmgrName     = e->name;
    if ((unsigned int) e->pid == selfPid)                            sawSelf       = true;
  }

  NANO_OS_ASSERT_NOT_NULL(schedulerName);
  NANO_OS_ASSERT_NOT_NULL(consoleName);
  NANO_OS_ASSERT_NOT_NULL(memmgrName);
  NANO_OS_ASSERT_STR_EQ("console", consoleName);
  NANO_OS_ASSERT_STR_EQ("memory manager", memmgrName);
  NANO_OS_ASSERT_TRUE(sawSelf);

  free(info);
}

NANO_OS_KERNEL_TEST(sched, num_running_processes_is_sane) {
  SchedulerGetNumRunningProcessesArgs args;
  memset(&args, 0, sizeof(args));

  ProcessMessage *msg = initSendProcessMessageToPid(
    SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_GET_NUM_RUNNING_PROCESSES,
    &args, sizeof(args), true);
  NANO_OS_ASSERT_NOT_NULL(msg);
  NANO_OS_ASSERT_EQ_INT(0, processMessageWaitForDone(msg, NULL));
  processMessageRelease(msg);

  // scheduler + console + memory manager + this driver, at minimum.
  NANO_OS_ASSERT_TRUE(args.returnValue >= 4);
  NANO_OS_ASSERT_TRUE(args.returnValue <= NANO_OS_NUM_PROCESSES);
}
