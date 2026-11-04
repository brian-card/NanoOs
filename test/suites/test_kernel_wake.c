///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_wake.c
///
/// @brief             Kernel tests for signalCallback and unlockCallback, which
///                    move a process that a condition signals or a mutex
///                    unlock wakes onto its ready queue, and for
///                    schedulerQueueWaitingProcess, which queues a process
///                    that has yielded to wait.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Processes.h"
#include "kernel/Scheduler.h"

void signalCallback(void *stateData, Cocondition *cocondition);
void unlockCallback(void *stateData, Comutex *comutex);

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

/// @fn static ProcessDescriptor* startIdleProcess(void)
///
/// @brief Run idleProcess in the first user process slot, on its ready queue.
///
/// @return Returns a pointer to the process's ProcessDescriptor, or NULL if it
/// couldn't be started.
static ProcessDescriptor* startIdleProcess(void) {
  ProcessId pid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
  if (processCreate(processDescriptor, idleProcess, NULL) != processSuccess) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return processDescriptor;
}

/// @fn static int countInQueue(ProcessQueue *processQueue,
///   ProcessDescriptor *processDescriptor)
///
/// @brief Count the entries of a process in a ProcessQueue.
///
/// @param processQueue A pointer to the ProcessQueue to search.
/// @param processDescriptor A pointer to the ProcessDescriptor to count.
///
/// @return Returns the number of entries.
static int countInQueue(ProcessQueue *processQueue,
  ProcessDescriptor *processDescriptor
) {
  int count = 0;
  ProcessDescriptor **slot = processQueue->head;
  for (uint8_t ii = 0; ii < processQueue->numElements; ii++) {
    count += (*slot == processDescriptor);
    slot++;
    if (slot == processQueue->end) {
      slot = processQueue->processes;
    }
  }
  return count;
}

/// @fn static void signalOnly(ProcessDescriptor *processDescriptor)
///
/// @brief Run signalCallback for a condition whose only signalled waiter is a
/// process.
///
/// @param processDescriptor A pointer to the waiting process.
///
/// @return This function returns no value.
static void signalOnly(ProcessDescriptor *processDescriptor) {
  Cocondition cocondition;
  memset(&cocondition, 0, sizeof(cocondition));
  cocondition.numWaiters = 1;
  cocondition.numSignals = 1;
  cocondition.head = processDescriptor->mainThread;
  cocondition.tail = processDescriptor->mainThread;
  Coroutine *nextToSignal = processDescriptor->mainThread->nextToSignal;
  processDescriptor->mainThread->nextToSignal = NULL;
  signalCallback(NULL, &cocondition);
  processDescriptor->mainThread->nextToSignal = nextToSignal;
}

NANO_OS_KERNEL_TEST(wake, popped_process_is_not_queued_again) {
  ProcessDescriptor *target = startIdleProcess();
  NANO_OS_ASSERT_NOT_NULL(target);
  NANO_OS_ASSERT_EQ_INT(0,
    processQueueRemove(target->readyQueue, target));

  signalOnly(target);

  NANO_OS_ASSERT_EQ_INT(0, countInQueue(target->readyQueue, target));
  NANO_OS_ASSERT_NULL(target->processQueue);
  processQueuePush(target->readyQueue, target);
}

NANO_OS_KERNEL_TEST(wake, queued_process_stays_queued_once) {
  ProcessDescriptor *target = startIdleProcess();
  NANO_OS_ASSERT_NOT_NULL(target);

  signalOnly(target);

  NANO_OS_ASSERT_EQ_INT(1, countInQueue(target->readyQueue, target));
  NANO_OS_ASSERT_EQ_PTR(target->readyQueue, target->processQueue);
}

/// @fn static ProcessDescriptor* startPoppedWaiter(CoroutineState state)
///
/// @brief Start an idle process and leave it as the scheduler does once it
/// has yielded to wait:  in a waiting state and in no queue.
///
/// @param state The CoroutineState to leave it in.
///
/// @return Returns a pointer to the process's ProcessDescriptor, or NULL if it
/// couldn't be started.
static ProcessDescriptor* startPoppedWaiter(CoroutineState state) {
  ProcessDescriptor *target = startIdleProcess();
  if ((target == NULL)
    || (processQueueRemove(target->readyQueue, target) != 0)
  ) {
    return NULL;
  }
  target->mainThread->state = state;
  return target;
}

/// @fn static void readyAgain(ProcessDescriptor *target)
///
/// @brief Put a process left by startPoppedWaiter back as startIdleProcess
/// started it, so that the kernel can run it after the test.
///
/// @param target A pointer to the process's ProcessDescriptor.
///
/// @return This function returns no value.
static void readyAgain(ProcessDescriptor *target) {
  target->mainThread->blockingCocondition = NULL;
  target->mainThread->blockingComutex = NULL;
  target->mainThread->state = COROUTINE_STATE_NOT_RUNNING;
  if (target->processQueue != NULL) {
    processQueueRemove(target->processQueue, target);
  }
  processQueuePush(target->readyQueue, target);
}

NANO_OS_KERNEL_TEST(wake, waiter_signalled_while_popped_is_queued_ready) {
  ProcessDescriptor *target = startPoppedWaiter(COROUTINE_STATE_WAIT);
  NANO_OS_ASSERT_NOT_NULL(target);
  Cocondition cocondition;
  memset(&cocondition, 0, sizeof(cocondition));
  cocondition.numWaiters = 1;
  cocondition.head = target->mainThread;
  cocondition.tail = target->mainThread;
  target->mainThread->nextToSignal = NULL;
  target->mainThread->blockingCocondition = &cocondition;

  cocondition.numSignals = 1;
  signalCallback(NULL, &cocondition);
  NANO_OS_ASSERT_NULL(target->processQueue);

  NANO_OS_ASSERT_TRUE(schedulerQueueWaitingProcess(target));
  NANO_OS_ASSERT_EQ_PTR(target->readyQueue, target->processQueue);
  NANO_OS_ASSERT_EQ_INT(1, countInQueue(target->readyQueue, target));
  readyAgain(target);
}

NANO_OS_KERNEL_TEST(wake, later_waiter_is_queued_ready_only_when_signalled) {
  ProcessDescriptor *target = startPoppedWaiter(COROUTINE_STATE_WAIT);
  NANO_OS_ASSERT_NOT_NULL(target);
  Coroutine first;
  memset(&first, 0, sizeof(first));
  first.nextToSignal = target->mainThread;
  Cocondition cocondition;
  memset(&cocondition, 0, sizeof(cocondition));
  cocondition.numWaiters = 2;
  cocondition.head = &first;
  cocondition.tail = target->mainThread;
  target->mainThread->nextToSignal = NULL;
  target->mainThread->blockingCocondition = &cocondition;

  cocondition.numSignals = 1;
  NANO_OS_ASSERT_TRUE(schedulerQueueWaitingProcess(target));
  NANO_OS_ASSERT_NOT_NULL(target->processQueue);
  NANO_OS_ASSERT_TRUE(target->processQueue != target->readyQueue);
  processQueueRemove(target->processQueue, target);

  cocondition.numSignals = 2;
  NANO_OS_ASSERT_TRUE(schedulerQueueWaitingProcess(target));
  NANO_OS_ASSERT_EQ_PTR(target->readyQueue, target->processQueue);
  readyAgain(target);
}

NANO_OS_KERNEL_TEST(wake, unsignalled_waiters_are_queued_to_wait) {
  ProcessDescriptor *target = startPoppedWaiter(COROUTINE_STATE_WAIT);
  NANO_OS_ASSERT_NOT_NULL(target);
  Cocondition cocondition;
  memset(&cocondition, 0, sizeof(cocondition));
  cocondition.numWaiters = 1;
  cocondition.head = target->mainThread;
  cocondition.tail = target->mainThread;
  target->mainThread->nextToSignal = NULL;
  target->mainThread->blockingCocondition = &cocondition;

  NANO_OS_ASSERT_TRUE(schedulerQueueWaitingProcess(target));
  ProcessQueue *waitingQueue = target->processQueue;
  NANO_OS_ASSERT_NOT_NULL(waitingQueue);
  NANO_OS_ASSERT_TRUE(waitingQueue != target->readyQueue);
  NANO_OS_ASSERT_EQ_INT(1, countInQueue(waitingQueue, target));
  processQueueRemove(waitingQueue, target);

  target->mainThread->state = COROUTINE_STATE_TIMEDWAIT;
  NANO_OS_ASSERT_TRUE(schedulerQueueWaitingProcess(target));
  NANO_OS_ASSERT_NOT_NULL(target->processQueue);
  NANO_OS_ASSERT_TRUE(target->processQueue != target->readyQueue);
  NANO_OS_ASSERT_TRUE(target->processQueue != waitingQueue);
  readyAgain(target);
}

NANO_OS_KERNEL_TEST(wake, lock_waiter_unlocked_while_popped_is_queued_ready) {
  ProcessDescriptor *target = startPoppedWaiter(COROUTINE_STATE_WAIT);
  NANO_OS_ASSERT_NOT_NULL(target);
  Comutex comutex;
  memset(&comutex, 0, sizeof(comutex));
  comutex.head = target->mainThread;
  comutex.tail = target->mainThread;
  target->mainThread->blockingComutex = &comutex;

  unlockCallback(NULL, &comutex);
  NANO_OS_ASSERT_NULL(target->processQueue);

  NANO_OS_ASSERT_TRUE(schedulerQueueWaitingProcess(target));
  NANO_OS_ASSERT_EQ_PTR(target->readyQueue, target->processQueue);
  NANO_OS_ASSERT_EQ_INT(1, countInQueue(target->readyQueue, target));
  readyAgain(target);
}

NANO_OS_KERNEL_TEST(wake, lock_waiter_is_queued_to_wait_while_held) {
  ProcessDescriptor *target = startPoppedWaiter(COROUTINE_STATE_WAIT);
  NANO_OS_ASSERT_NOT_NULL(target);
  Coroutine holder;
  memset(&holder, 0, sizeof(holder));
  Comutex comutex;
  memset(&comutex, 0, sizeof(comutex));
  atomic_store(&comutex.coroutine, &holder);
  comutex.head = target->mainThread;
  comutex.tail = target->mainThread;
  target->mainThread->blockingComutex = &comutex;

  NANO_OS_ASSERT_TRUE(schedulerQueueWaitingProcess(target));
  NANO_OS_ASSERT_NOT_NULL(target->processQueue);
  NANO_OS_ASSERT_TRUE(target->processQueue != target->readyQueue);
  readyAgain(target);
}

NANO_OS_KERNEL_TEST(wake, running_process_is_not_queued_as_a_waiter) {
  ProcessDescriptor *target = startPoppedWaiter(COROUTINE_STATE_RUNNING);
  NANO_OS_ASSERT_NOT_NULL(target);

  NANO_OS_ASSERT_FALSE(schedulerQueueWaitingProcess(target));
  NANO_OS_ASSERT_NULL(target->processQueue);
  readyAgain(target);
}
