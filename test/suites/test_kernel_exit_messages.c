///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_exit_messages.c
///
/// @brief             Kernel tests for processCompleteMessagesTo, which
///                    completes the messages sent to an exiting process.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Processes.h"
#include "kernel/Scheduler.h"

static volatile ProcessMessage *poppedMessage;

/// @fn static void* idleProcess(void *args)
///
/// @brief Yield forever without ever reading a message.
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

/// @fn static void* abandoningProcess(void *args)
///
/// @brief Pop one message and then yield forever without completing it.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* abandoningProcess(void *args) {
  (void) args;
  poppedMessage = processMessageQueueWait(NULL);
  while (1) {
    processYield();
  }

  return NULL;
}

/// @fn static ProcessDescriptor* startProcess(
///   ProcessId pid, void* (*body)(void*))
///
/// @brief Run a function as the process in a user process slot.
///
/// @param pid The ProcessId of the user process slot to use.
/// @param body The function to run.
///
/// @return Returns a pointer to the process's ProcessDescriptor, or NULL if it
/// couldn't be started.
static ProcessDescriptor* startProcess(ProcessId pid, void* (*body)(void*)) {
  ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
  if (processCreate(processDescriptor, body, NULL) != processSuccess) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return processDescriptor;
}

/// @fn static ProcessId firstUserPid(void)
///
/// @brief Get the ProcessId of the first user process slot.
///
/// @return Returns the ProcessId.
static ProcessId firstUserPid(void) {
  return schedulerGetFirstShellPid() + schedulerGetNumShells();
}

/// @fn static ProcessMessage* pushTo(
///   ProcessDescriptor *processDescriptor, bool waiting)
///
/// @brief Push a message from this process onto another process's queue.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to push to.
/// @param waiting Whether this process will wait on the message.
///
/// @return Returns the pushed message, or NULL on failure.
static ProcessMessage* pushTo(
  ProcessDescriptor *processDescriptor, bool waiting
) {
  ProcessMessage *processMessage = getAvailableMessage();
  if (processMessage == NULL) {
    return NULL;
  }
  processMessageInit(processMessage, 0, NULL, 0, waiting);
  if (processMessageQueuePush(processDescriptor, processMessage)
    != processSuccess
  ) {
    processMessageRelease(processMessage);
    return NULL;
  }

  return processMessage;
}

NANO_OS_KERNEL_TEST(exit_messages, queued_messages_are_dequeued_and_done) {
  ProcessDescriptor *victim = startProcess(firstUserPid(), idleProcess);
  NANO_OS_ASSERT_NOT_NULL(victim);
  ProcessMessage *embedded = pushTo(victim, true);
  NANO_OS_ASSERT_EQ_PTR(&getRunningProcess()->message, embedded);
  ProcessMessage *pooled = pushTo(victim, true);
  NANO_OS_ASSERT_NOT_NULL(pooled);
  NANO_OS_ASSERT_TRUE(pooled != embedded);
  NANO_OS_ASSERT_EQ_PTR(embedded, victim->mainThread->messageQueue.head);

  processCompleteMessagesTo(victim);

  NANO_OS_ASSERT_TRUE(processMessageDone(embedded));
  NANO_OS_ASSERT_TRUE(processMessageDone(pooled));
  NANO_OS_ASSERT_NOT_NULL(processMessageInUse(embedded));
  NANO_OS_ASSERT_NOT_NULL(processMessageInUse(pooled));
  NANO_OS_ASSERT_NULL(victim->mainThread->messageQueue.head);
  processMessageRelease(embedded);
  processMessageRelease(pooled);
}

NANO_OS_KERNEL_TEST(exit_messages, abandoned_popped_message_is_done) {
  poppedMessage = NULL;
  ProcessDescriptor *victim = startProcess(firstUserPid(), abandoningProcess);
  NANO_OS_ASSERT_NOT_NULL(victim);
  ProcessMessage *sent = pushTo(victim, true);
  NANO_OS_ASSERT_NOT_NULL(sent);
  for (int ii = 0; (ii < 100) && (poppedMessage == NULL); ii++) {
    processYield();
  }
  NANO_OS_ASSERT_EQ_PTR(sent, (ProcessMessage*) poppedMessage);
  NANO_OS_ASSERT_FALSE(processMessageDone(sent));

  processCompleteMessagesTo(victim);

  NANO_OS_ASSERT_TRUE(processMessageDone(sent));
  processMessageRelease(sent);
}

NANO_OS_KERNEL_TEST(exit_messages, unwaited_message_is_released) {
  ProcessDescriptor *victim = startProcess(firstUserPid(), idleProcess);
  NANO_OS_ASSERT_NOT_NULL(victim);
  ProcessMessage *sent = pushTo(victim, false);
  NANO_OS_ASSERT_NOT_NULL(sent);

  processCompleteMessagesTo(victim);

  NANO_OS_ASSERT_NULL(processMessageInUse(sent));
  NANO_OS_ASSERT_NULL(victim->mainThread->messageQueue.head);
}

NANO_OS_KERNEL_TEST(exit_messages, message_to_another_process_is_untouched) {
  ProcessDescriptor *victim = startProcess(firstUserPid(), idleProcess);
  NANO_OS_ASSERT_NOT_NULL(victim);
  ProcessDescriptor *other = startProcess(firstUserPid() + 1, idleProcess);
  NANO_OS_ASSERT_NOT_NULL(other);
  ProcessMessage *sent = pushTo(other, true);
  NANO_OS_ASSERT_NOT_NULL(sent);

  processCompleteMessagesTo(victim);

  NANO_OS_ASSERT_FALSE(processMessageDone(sent));
  NANO_OS_ASSERT_EQ_PTR(sent, other->mainThread->messageQueue.head);
  comessageQueueRemove(other->mainThread, sent);
  processMessageRelease(sent);
}

NANO_OS_KERNEL_TEST(exit_messages, reinitialized_message_is_untouched) {
  ProcessDescriptor *victim = startProcess(firstUserPid(), idleProcess);
  NANO_OS_ASSERT_NOT_NULL(victim);
  ProcessMessage *sent = pushTo(victim, true);
  NANO_OS_ASSERT_NOT_NULL(sent);
  comessageQueueRemove(victim->mainThread, sent);
  processMessageInit(sent, 0, NULL, 0, true);

  processCompleteMessagesTo(victim);

  NANO_OS_ASSERT_FALSE(processMessageDone(sent));
  processMessageRelease(sent);
}
