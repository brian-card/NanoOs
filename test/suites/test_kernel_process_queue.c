////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file test_kernel_process_queue.c
///
/// @brief Kernel tests for the ProcessQueue ring buffer.

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"

#define QUEUE_TEST_MAX_PROCESSES 16

/// @struct QueueTestStorage
///
/// @brief Concrete backing for a ProcessQueue under test.  Layout must match
/// struct ProcessQueue in NanoOsTypes.h, with the trailing flexible array
/// given a real size.
typedef struct QueueTestStorage {
  const char         *name;
  ProcessDescriptor **head;
  ProcessDescriptor **tail;
  ProcessDescriptor **end;
  uint8_t             numElements;
  ProcessDescriptor  *processes[QUEUE_TEST_MAX_PROCESSES];
} QueueTestStorage;

static QueueTestStorage _queueStorage;
static ProcessDescriptor _descriptors[QUEUE_TEST_MAX_PROCESSES + 1];

/// @fn static ProcessQueue* queueTestInit(void)
///
/// @brief Initialize the queue under test and the descriptors pushed onto it.
///
/// @return Returns a pointer to the initialized ProcessQueue.
static ProcessQueue* queueTestInit(void) {
  memset(&_queueStorage, 0, sizeof(_queueStorage));
  memset(_descriptors, 0, sizeof(_descriptors));
  for (int ii = 0; ii <= QUEUE_TEST_MAX_PROCESSES; ii++) {
    _descriptors[ii].processId = (ProcessId) (ii + 1);
  }
  ProcessQueue *queue = (ProcessQueue*) &_queueStorage;
  processQueueInit(queue, "test");
  return queue;
}

/// @fn static void queueTestAdvance(ProcessQueue *queue, size_t count)
///
/// @brief Move an empty queue's head and tail forward by count elements.
///
/// @param queue The empty ProcessQueue to advance.
/// @param count The number of elements to advance by.
///
/// @return This function returns no value.
static void queueTestAdvance(ProcessQueue *queue, size_t count) {
  for (size_t ii = 0; ii < count; ii++) {
    processQueuePush(queue, &_descriptors[0]);
    processQueuePop(queue);
  }
}

NANO_OS_KERNEL_TEST(process_queue, init_spans_num_processes) {
  NANO_OS_ASSERT_TRUE(numProcesses <= QUEUE_TEST_MAX_PROCESSES);
  ProcessQueue *queue = queueTestInit();

  NANO_OS_ASSERT_STR_EQ("test", queue->name);
  NANO_OS_ASSERT_EQ_PTR(queue->processes, queue->head);
  NANO_OS_ASSERT_EQ_PTR(queue->processes, queue->tail);
  NANO_OS_ASSERT_EQ_PTR(queue->processes + numProcesses, queue->end);
  NANO_OS_ASSERT_EQ_INT(0, queue->numElements);
  NANO_OS_ASSERT_NULL(processQueuePop(queue));
}

NANO_OS_KERNEL_TEST(process_queue, push_and_pop_wrap_in_order) {
  ProcessQueue *queue = queueTestInit();
  size_t numSteps = (3 * numProcesses) + 1;

  for (size_t ii = 0; ii < 3; ii++) {
    NANO_OS_ASSERT_EQ_INT(0, processQueuePush(queue, &_descriptors[ii]));
  }
  for (size_t ii = 0; ii < numSteps; ii++) {
    NANO_OS_ASSERT_EQ_PTR(&_descriptors[ii % numProcesses],
      processQueuePop(queue));
    NANO_OS_ASSERT_EQ_INT(0,
      processQueuePush(queue, &_descriptors[(ii + 3) % numProcesses]));
    NANO_OS_ASSERT_TRUE((queue->head >= queue->processes)
      && (queue->head < queue->end));
    NANO_OS_ASSERT_TRUE((queue->tail >= queue->processes)
      && (queue->tail < queue->end));
    NANO_OS_ASSERT_EQ_INT(3, queue->numElements);
  }

  NANO_OS_ASSERT_EQ_PTR(queue->processes + ((numSteps + 3) % numProcesses),
    queue->tail);
  NANO_OS_ASSERT_EQ_PTR(queue->processes + (numSteps % numProcesses),
    queue->head);
}

NANO_OS_KERNEL_TEST(process_queue, full_queue_rejects_a_push) {
  ProcessQueue *queue = queueTestInit();
  queueTestAdvance(queue, 2);

  for (size_t ii = 0; ii < numProcesses; ii++) {
    NANO_OS_ASSERT_EQ_INT(0, processQueuePush(queue, &_descriptors[ii]));
  }
  ProcessDescriptor *extra = &_descriptors[numProcesses];
  NANO_OS_ASSERT_EQ_INT(ENOMEM, processQueuePush(queue, extra));
  NANO_OS_ASSERT_NULL(extra->processQueue);
  NANO_OS_ASSERT_EQ_INT((int) numProcesses, queue->numElements);

  for (size_t ii = 0; ii < numProcesses; ii++) {
    ProcessDescriptor *popped = processQueuePop(queue);
    NANO_OS_ASSERT_EQ_PTR(&_descriptors[ii], popped);
    NANO_OS_ASSERT_NULL(popped->processQueue);
  }
  NANO_OS_ASSERT_NULL(processQueuePop(queue));
}

/// @fn static void queueTestExpect(ProcessQueue *queue,
///   const size_t *expected, size_t numExpected)
///
/// @brief Pop every element from a queue and check it against the expected
/// descriptor indexes, in order.
///
/// @param queue The ProcessQueue to drain.
/// @param expected The expected indexes into _descriptors, head first.
/// @param numExpected The number of elements in expected.
///
/// @return This function returns no value.
static void queueTestExpect(ProcessQueue *queue,
  const size_t *expected, size_t numExpected
) {
  NANO_OS_ASSERT_EQ_INT((int) numExpected, queue->numElements);
  for (size_t ii = 0; ii < numExpected; ii++) {
    NANO_OS_ASSERT_EQ_PTR(&_descriptors[expected[ii]], processQueuePop(queue));
  }
  NANO_OS_ASSERT_NULL(processQueuePop(queue));
}

NANO_OS_KERNEL_TEST(process_queue, remove_across_the_wrap_compacts_in_order) {
  ProcessQueue *queue = queueTestInit();
  queueTestAdvance(queue, numProcesses - 2);

  for (size_t ii = 0; ii < 5; ii++) {
    NANO_OS_ASSERT_EQ_INT(0, processQueuePush(queue, &_descriptors[ii]));
  }
  NANO_OS_ASSERT_EQ_PTR(queue->processes + 3, queue->tail);

  NANO_OS_ASSERT_EQ_INT(0, processQueueRemove(queue, &_descriptors[2]));
  NANO_OS_ASSERT_NULL(_descriptors[2].processQueue);
  NANO_OS_ASSERT_EQ_PTR(queue->processes + numProcesses - 2, queue->head);
  NANO_OS_ASSERT_EQ_PTR(queue->processes + 2, queue->tail);

  size_t expected[] = { 0, 1, 3, 4 };
  queueTestExpect(queue, expected, 4);
}

NANO_OS_KERNEL_TEST(process_queue, remove_head_and_tail_keep_order) {
  ProcessQueue *queue = queueTestInit();
  queueTestAdvance(queue, numProcesses - 1);
  for (size_t ii = 0; ii < 4; ii++) {
    NANO_OS_ASSERT_EQ_INT(0, processQueuePush(queue, &_descriptors[ii]));
  }

  NANO_OS_ASSERT_EQ_INT(0, processQueueRemove(queue, &_descriptors[0]));
  NANO_OS_ASSERT_EQ_INT(0, processQueueRemove(queue, &_descriptors[3]));
  NANO_OS_ASSERT_EQ_PTR(queue->processes + 1, queue->tail);

  NANO_OS_ASSERT_EQ_INT(0, processQueuePush(queue, &_descriptors[5]));
  size_t expected[] = { 1, 2, 5 };
  queueTestExpect(queue, expected, 3);
}

NANO_OS_KERNEL_TEST(process_queue, remove_only_element_empties_the_queue) {
  ProcessQueue *queue = queueTestInit();
  queueTestAdvance(queue, 3);
  NANO_OS_ASSERT_EQ_INT(0, processQueuePush(queue, &_descriptors[7]));

  NANO_OS_ASSERT_EQ_INT(0, processQueueRemove(queue, &_descriptors[7]));
  NANO_OS_ASSERT_EQ_INT(0, queue->numElements);
  NANO_OS_ASSERT_EQ_PTR(queue->head, queue->tail);
  NANO_OS_ASSERT_EQ_INT(EINVAL, processQueueRemove(queue, &_descriptors[7]));
  NANO_OS_ASSERT_NULL(processQueuePop(queue));
}

NANO_OS_KERNEL_TEST(process_queue, remove_of_a_non_member_changes_nothing) {
  ProcessQueue *queue = queueTestInit();
  for (size_t ii = 0; ii < 3; ii++) {
    NANO_OS_ASSERT_EQ_INT(0, processQueuePush(queue, &_descriptors[ii]));
  }
  _descriptors[9].processQueue = queue;

  NANO_OS_ASSERT_EQ_INT(EINVAL, processQueueRemove(queue, &_descriptors[9]));
  NANO_OS_ASSERT_EQ_PTR(queue, _descriptors[9].processQueue);
  size_t expected[] = { 0, 1, 2 };
  queueTestExpect(queue, expected, 3);
}
