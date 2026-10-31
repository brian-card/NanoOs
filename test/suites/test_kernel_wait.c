///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              10.31.2026
///
/// @file              test_kernel_wait.c
///
/// @brief             Kernel tests for waiting on child processes with
///                    SCHEDULER_WAIT_PID.
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

#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsSignal.h"
#include "user/NanoOsWait.h"

/// @var exitStatuses
///
/// @brief Exit statuses for exitingProcess.  The coroutine's first resume
/// argument has to be a pointer, so these are passed by address.
static const uint8_t exitStatuses[] = { 0, 3, 7, 42 };

/// @var waiterResult
///
/// @brief The value schedulerWaitpid returned in waitingProcess.
static volatile ProcessId waiterResult;

/// @fn static void* exitingProcess(void *args)
///
/// @brief Yield a few times, then exit with the status passed in.
///
/// @param args A pointer to the exit status, cast to a void*.
///
/// @return This function always returns NULL.
static void* exitingProcess(void *args) {
  for (int ii = 0; ii < 3; ii++) {
    processYield();
  }
  getRunningProcess()->exitStatus = *((const uint8_t*) args);
  getRunningProcess()->exitSignal = 0;
  return NULL;
}

/// @fn static void* slowExitingProcess(void *args)
///
/// @brief Yield many times, then exit with the status passed in.
///
/// @param args A pointer to the exit status, cast to a void*.
///
/// @return This function always returns NULL.
static void* slowExitingProcess(void *args) {
  for (int ii = 0; ii < 30; ii++) {
    processYield();
  }
  return exitingProcess(args);
}

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

/// @fn static void* waitingProcess(void *args)
///
/// @brief Wait for any child, then idle.  The process's own message is taken
/// first so that the wait has to use one from the shared pool.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* waitingProcess(void *args) {
  (void) args;
  getAvailableMessage();
  waiterResult = schedulerWaitpid((ProcessId) -1, NULL, 0);
  return idleProcess(NULL);
}

/// @fn static ProcessQueue* freeQueue(void)
///
/// @brief Get the scheduler's queue of free process slots.
///
/// @return Returns a pointer to the free queue.
static ProcessQueue* freeQueue(void) {
  return halMockFreeQueue();
}

/// @fn static int freePoolMessages(void)
///
/// @brief Count the messages in the shared pool that aren't in use.
///
/// @return Returns the number of free messages.
static int freePoolMessages(void) {
  extern ProcessMessage *messages;
  int count = 0;
  for (int ii = 0; ii < NANO_OS_NUM_MESSAGES; ii++) {
    count += (processMessageInUse(&messages[ii]) == false);
  }
  return count;
}

/// @fn static bool inFreeQueue(ProcessDescriptor *processDescriptor)
///
/// @brief Determine whether a process slot is in the free queue.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to look for.
///
/// @return Returns true if the slot is in the free queue, false if not.
static bool inFreeQueue(ProcessDescriptor *processDescriptor) {
  ProcessQueue *processQueue = freeQueue();
  ProcessDescriptor **slot = processQueue->head;
  for (uint8_t ii = 0; ii < processQueue->numElements; ii++) {
    if (*slot == processDescriptor) {
      return true;
    }
    slot++;
    if (slot == processQueue->end) {
      slot = processQueue->processes;
    }
  }
  return false;
}

/// @fn static ProcessDescriptor* startChild(ProcessId parentPid,
///   void* (*body)(void*), void *args)
///
/// @brief Run a function in a free process slot as the child of a process.
///
/// @param parentPid The ID of the child's parent.
/// @param body The function for the child to run.
/// @param args The argument to pass to body.
///
/// @return Returns a pointer to the child's ProcessDescriptor, or NULL if it
/// couldn't be started.
static ProcessDescriptor* startChild(ProcessId parentPid,
  void* (*body)(void*), void *args
) {
  ProcessDescriptor *child = processQueuePop(freeQueue());
  if ((child == NULL)
    || (processCreate(child, body, args) != processSuccess)
  ) {
    return NULL;
  }
  threadSetContext(child->mainThread, child);
  child->privilegeLevel = PRIVILEGE_LEVEL_USER;
  child->userId = getRunningUid();
  child->parentPid = parentPid;
  child->zombie = false;
  child->waitMessage = NULL;
  processQueuePush(child->readyQueue, child);
  return child;
}

/// @fn static bool yieldUntil(volatile bool *condition)
///
/// @brief Yield until a flag becomes true, giving up after a while.
///
/// @param condition A pointer to the flag to check.
///
/// @return Returns the final value of the flag.
static bool yieldUntil(volatile bool *condition) {
  for (int ii = 0; (ii < 1000) && !*condition; ii++) {
    processYield();
  }
  return *condition;
}

NANO_OS_KERNEL_TEST(wait, child_that_exits_first_stays_a_zombie_until_reaped) {
  ProcessDescriptor *child
    = startChild(getRunningPid(), exitingProcess, (void*) &exitStatuses[3]);
  NANO_OS_ASSERT_NOT_NULL(child);
  NANO_OS_ASSERT_TRUE(yieldUntil(&child->zombie));
  NANO_OS_ASSERT_FALSE(inFreeQueue(child));

  int status = -1;
  NANO_OS_ASSERT_EQ_INT(child->processId,
    schedulerWaitpid(child->processId, &status, 0));

  NANO_OS_ASSERT_TRUE(WIFEXITED(status));
  NANO_OS_ASSERT_EQ_INT(42, WEXITSTATUS(status));
  NANO_OS_ASSERT_FALSE(child->zombie);
  NANO_OS_ASSERT_TRUE(inFreeQueue(child));
}

NANO_OS_KERNEL_TEST(wait, waiting_parent_gets_the_status_when_the_child_exits) {
  ProcessDescriptor *child
    = startChild(getRunningPid(), exitingProcess, (void*) &exitStatuses[2]);
  NANO_OS_ASSERT_NOT_NULL(child);

  int status = -1;
  NANO_OS_ASSERT_EQ_INT(child->processId,
    schedulerWaitpid((ProcessId) -1, &status, 0));

  NANO_OS_ASSERT_TRUE(WIFEXITED(status));
  NANO_OS_ASSERT_EQ_INT(7, WEXITSTATUS(status));
  NANO_OS_ASSERT_FALSE(child->zombie);
  NANO_OS_ASSERT_TRUE(inFreeQueue(child));
}

NANO_OS_KERNEL_TEST(wait, waiting_for_one_child_leaves_another_a_zombie) {
  ProcessDescriptor *first
    = startChild(getRunningPid(), exitingProcess, (void*) &exitStatuses[1]);
  NANO_OS_ASSERT_NOT_NULL(first);
  ProcessDescriptor *third = startChild(
    getRunningPid(), slowExitingProcess, (void*) &exitStatuses[2]);
  NANO_OS_ASSERT_NOT_NULL(third);

  int status = -1;
  NANO_OS_ASSERT_EQ_INT(third->processId,
    schedulerWaitpid(third->processId, &status, 0));
  NANO_OS_ASSERT_EQ_INT(7, WEXITSTATUS(status));
  NANO_OS_ASSERT_TRUE(first->zombie);
  NANO_OS_ASSERT_EQ_INT(first->processId,
    schedulerWaitpid((ProcessId) -1, &status, 0));
  NANO_OS_ASSERT_EQ_INT(3, WEXITSTATUS(status));
}

NANO_OS_KERNEL_TEST(wait, killed_child_reports_the_signal) {
  getRunningProcess()->userId = ROOT_USER_ID;
  ProcessDescriptor *child
    = startChild(getRunningPid(), idleProcess, NULL);
  NANO_OS_ASSERT_NOT_NULL(child);
  SchedulerKillProcessArgs schedulerKillProcessArgs = {
    .pid = child->processId,
    .signal = SIGTERM,
    .returnValue = 0,
    .errorNumber = 0,
  };
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_KILL_PROCESS,
    &schedulerKillProcessArgs, sizeof(schedulerKillProcessArgs), true);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

  int status = -1;
  NANO_OS_ASSERT_EQ_INT(child->processId,
    schedulerWaitpid(child->processId, &status, 0));

  NANO_OS_ASSERT_TRUE(WIFSIGNALED(status));
  NANO_OS_ASSERT_EQ_INT(SIGTERM, WTERMSIG(status));
}

NANO_OS_KERNEL_TEST(wait, wnohang_returns_zero_while_the_child_runs) {
  ProcessDescriptor *child
    = startChild(getRunningPid(), exitingProcess, (void*) &exitStatuses[1]);
  NANO_OS_ASSERT_NOT_NULL(child);

  NANO_OS_ASSERT_EQ_INT(0,
    schedulerWaitpid(child->processId, NULL, WNOHANG));
  NANO_OS_ASSERT_EQ_INT(child->processId,
    schedulerWaitpid(child->processId, NULL, 0));
}

NANO_OS_KERNEL_TEST(wait, waiting_without_a_child_fails_with_echild) {
  ProcessDescriptor *other = startChild(0, idleProcess, NULL);
  NANO_OS_ASSERT_NOT_NULL(other);

  errno = 0;
  NANO_OS_ASSERT_EQ_INT((ProcessId) -1,
    schedulerWaitpid(other->processId, NULL, 0));
  NANO_OS_ASSERT_EQ_INT(ECHILD, errno);
  errno = 0;
  NANO_OS_ASSERT_EQ_INT((ProcessId) -1,
    schedulerWaitpid((ProcessId) -1, NULL, WNOHANG));
  NANO_OS_ASSERT_EQ_INT(ECHILD, errno);
}

NANO_OS_KERNEL_TEST(wait, exiting_parent_frees_its_zombie_children) {
  ProcessDescriptor *parent
    = startChild(0, slowExitingProcess, (void*) &exitStatuses[0]);
  NANO_OS_ASSERT_NOT_NULL(parent);
  ProcessDescriptor *child
    = startChild(parent->processId, exitingProcess, (void*) &exitStatuses[0]);
  NANO_OS_ASSERT_NOT_NULL(child);
  NANO_OS_ASSERT_TRUE(yieldUntil(&child->zombie));
  volatile bool bothFree = false;
  for (int ii = 0; (ii < 1000) && !bothFree; ii++) {
    processYield();
    bothFree = inFreeQueue(parent) && inFreeQueue(child);
  }

  NANO_OS_ASSERT_TRUE(bothFree);
  NANO_OS_ASSERT_FALSE(child->zombie);
  NANO_OS_ASSERT_EQ_INT(0, child->parentPid);
}

NANO_OS_KERNEL_TEST(wait, parent_killed_while_waiting_orphans_its_child) {
  getRunningProcess()->userId = ROOT_USER_ID;
  waiterResult = 0;
  int freeMessages = freePoolMessages();
  ProcessDescriptor *parent = startChild(0, waitingProcess, NULL);
  NANO_OS_ASSERT_NOT_NULL(parent);
  ProcessDescriptor *child
    = startChild(parent->processId, idleProcess, NULL);
  NANO_OS_ASSERT_NOT_NULL(child);
  volatile bool waiting = false;
  for (int ii = 0; (ii < 1000) && !waiting; ii++) {
    processYield();
    waiting = (parent->waitMessage != NULL);
  }
  NANO_OS_ASSERT_TRUE(waiting);
  NANO_OS_ASSERT_EQ_INT(freeMessages - 1, freePoolMessages());

  NANO_OS_ASSERT_EQ_INT(0, schedulerKillProcess(parent->processId));
  volatile bool parentFree = false;
  for (int ii = 0; (ii < 1000) && !parentFree; ii++) {
    processYield();
    parentFree = inFreeQueue(parent);
  }
  NANO_OS_ASSERT_TRUE(parentFree);
  NANO_OS_ASSERT_NULL(parent->waitMessage);
  NANO_OS_ASSERT_EQ_INT(freeMessages, freePoolMessages());
  NANO_OS_ASSERT_EQ_INT(0, child->parentPid);
  NANO_OS_ASSERT_EQ_INT(0, waiterResult);

  NANO_OS_ASSERT_EQ_INT(0, schedulerKillProcess(child->processId));
  volatile bool childFree = false;
  for (int ii = 0; (ii < 1000) && !childFree; ii++) {
    processYield();
    childFree = inFreeQueue(child);
  }
  NANO_OS_ASSERT_TRUE(childFree);
  NANO_OS_ASSERT_FALSE(child->zombie);
}
