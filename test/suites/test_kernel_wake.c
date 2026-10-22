///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_wake.c
///
/// @brief             Kernel tests for signalCallback, which moves a process
///                    that a condition signals onto its ready queue.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Processes.h"
#include "kernel/Scheduler.h"

void signalCallback(void *stateData, Cocondition *cocondition);

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
