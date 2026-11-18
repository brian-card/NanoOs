///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_message_args.c
///
/// @brief             Tests of the checked message-argument storage in
///                    MessageArgs.c:  every detector, the arena, buffer
///                    sharing, adoption, and the kernel hooks.
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

#include <string.h>

#include "kernel/Console.h"
#include "kernel/MemoryManager.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsSignal.h"

#ifndef NANO_OS_MESSAGE_ARGS_CHECKED
#error "This suite tests the checked expansions."
#endif

/// @def POISON
///
/// @brief The byte the checker fills freed storage with.
#define POISON 0xa5

/// @def MAX_YIELDS
///
/// @brief The most times a test yields while waiting for another process.
#define MAX_YIELDS 1000

/// @fn static uint16_t errors(int kind)
///
/// @brief Get the checker's count of one kind of error.
///
/// @param kind The MESSAGE_ARGS_ERROR_* value.
///
/// @return Returns the count.
static uint16_t errors(int kind) {
  return messageArgsCounts()->errors[kind];
}

/// @fn static void assertOnlyError(int kind, uint16_t count)
///
/// @brief Assert that the checker counted exactly `count` errors of one kind
/// and none of any other, then clear the counts so the harness's own check
/// passes.
///
/// @param kind The MESSAGE_ARGS_ERROR_* value expected.
/// @param count The number expected.
///
/// @return This function returns no value.
static void assertOnlyError(int kind, uint16_t count) {
  NANO_OS_ASSERT_EQ_INT(count, errors(kind));
  NANO_OS_ASSERT_EQ_INT(count, messageArgsErrorTotal());
  messageArgsResetCounts();
}

/// @fn static ProcessMessage* claimMessage(void *data, bool waiting)
///
/// @brief Claim a message carrying data and announce it to the send hook as
/// sent to the console, without pushing it anywhere.
///
/// @param data The payload.
/// @param waiting Whether the message is marked as waited on.
///
/// @return Returns the message.
static ProcessMessage* claimMessage(void *data, bool waiting) {
  ProcessMessage *processMessage = getAvailableMessage();
  if (processMessage != NULL) {
    processMessageInit(processMessage, 0, data, 16, waiting);
    MESSAGE_ARGS_SENDING(processMessage, consolePid);
  }
  return processMessage;
}

typedef struct TestArgs {
  uint32_t value;
  uint8_t bytes[20];
} TestArgs;

NANO_OS_KERNEL_TEST(message_args, allocate_is_zeroed_after_reuse) {
  MESSAGE_ARGS_ALLOCATE(TestArgs, first);
  NANO_OS_ASSERT_NOT_NULL(first);
  memset(first, 0x5a, sizeof(*first));
  MESSAGE_ARGS_FREE(first);

  MESSAGE_ARGS_ALLOCATE(TestArgs, second);
  NANO_OS_ASSERT_EQ_PTR(first, second);
  NANO_OS_ASSERT_EQ_INT(0, second->value);
  for (size_t ii = 0; ii < sizeof(second->bytes); ii++) {
    NANO_OS_ASSERT_EQ_INT(0, second->bytes[ii]);
  }
  NANO_OS_ASSERT_EQ_INT(1, messageArgsCounts()->live);
  MESSAGE_ARGS_FREE(second);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsCounts()->live);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST(message_args, freed_storage_is_poisoned) {
  MESSAGE_ARGS_ALLOCATE(TestArgs, args);
  NANO_OS_ASSERT_NOT_NULL(args);
  volatile uint8_t *bytes = (volatile uint8_t*) args;
  MESSAGE_ARGS_FREE(args);
  for (size_t ii = 0; ii < sizeof(TestArgs); ii++) {
    NANO_OS_ASSERT_EQ_INT(POISON, bytes[ii]);
  }
}

NANO_OS_KERNEL_TEST(message_args, double_free_is_counted) {
  MESSAGE_ARGS_ALLOCATE(TestArgs, args);
  MESSAGE_ARGS_FREE(args);
  MESSAGE_ARGS_FREE(args);
  assertOnlyError(MESSAGE_ARGS_ERROR_DOUBLE_FREE, 1);
}

NANO_OS_KERNEL_TEST(message_args, free_of_a_foreign_pointer_is_counted) {
  TestArgs onStack;
  MESSAGE_ARGS_DESTROY(&onStack);
  assertOnlyError(MESSAGE_ARGS_ERROR_BAD_POINTER, 1);
}

NANO_OS_KERNEL_TEST(message_args, write_after_free_is_counted) {
  MESSAGE_ARGS_ALLOCATE(TestArgs, args);
  volatile uint8_t *bytes = (volatile uint8_t*) args;
  MESSAGE_ARGS_FREE(args);
  bytes[3] = 0;

  MESSAGE_ARGS_ALLOCATE(TestArgs, again);
  NANO_OS_ASSERT_EQ_PTR(args, again);
  MESSAGE_ARGS_FREE(again);
  assertOnlyError(MESSAGE_ARGS_ERROR_WRITE_AFTER_FREE, 1);
}

NANO_OS_KERNEL_TEST(message_args, free_while_in_flight_is_counted) {
  MESSAGE_ARGS_ALLOCATE(TestArgs, args);
  ProcessMessage *processMessage = claimMessage(args, true);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  MESSAGE_ARGS_FREE(args);
  processMessageRelease(processMessage);
  assertOnlyError(MESSAGE_ARGS_ERROR_FREE_IN_FLIGHT, 1);
}

NANO_OS_KERNEL_TEST(message_args, free_after_done_is_clean) {
  MESSAGE_ARGS_ALLOCATE(TestArgs, args);
  ProcessMessage *processMessage = claimMessage(args, true);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  processMessageSetDone(processMessage);
  MESSAGE_ARGS_FREE(args);
  processMessageRelease(processMessage);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST(message_args, round_trip_sent_without_waiting_is_counted) {
  MESSAGE_ARGS_ALLOCATE(TestArgs, args);
  ProcessMessage *processMessage = claimMessage(args, false);
  processMessageSetDone(processMessage);
  processMessageRelease(processMessage);
  MESSAGE_ARGS_FREE(args);
  assertOnlyError(MESSAGE_ARGS_ERROR_FIRE_AND_FORGET, 1);
}

NANO_OS_KERNEL_TEST(message_args, one_way_storage_may_be_sent_without_waiting) {
  TestArgs *args = MESSAGE_ARGS_CREATE(TestArgs, MESSAGE_ARGS_ONE_WAY);
  ProcessMessage *processMessage = claimMessage(args, false);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
  processMessageSetDone(processMessage);
  processMessageRelease(processMessage);
  // Only the recipient may free it, and the recipient here is a stand-in.
  MESSAGE_ARGS_DESTROY(args);
  assertOnlyError(MESSAGE_ARGS_ERROR_WRONG_RECIPIENT, 1);
}

NANO_OS_KERNEL_TEST(message_args, a_real_send_without_waiting_is_counted) {
  MESSAGE_ARGS_ALLOCATE(MemoryManagerGetFreeMemoryArgs, args);
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_GET_FREE_MEMORY,
    args, sizeof(*args), false);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  for (int ii = 0; (ii < MAX_YIELDS) && (args->bytesFree == 0); ii++) {
    processYield();
  }
  NANO_OS_ASSERT_TRUE(args->bytesFree > 0);
  MESSAGE_ARGS_FREE(args);
  assertOnlyError(MESSAGE_ARGS_ERROR_FIRE_AND_FORGET, 1);
}

NANO_OS_KERNEL_TEST(message_args, a_real_round_trip_is_clean) {
  MESSAGE_ARGS_ALLOCATE(MemoryManagerGetFreeMemoryArgs, args);
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_GET_FREE_MEMORY,
    args, sizeof(*args), true);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);
  NANO_OS_ASSERT_TRUE(args->bytesFree > 0);
  MESSAGE_ARGS_FREE(args);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST(message_args, a_real_send_freed_before_done_is_counted) {
  MESSAGE_ARGS_ALLOCATE(MemoryManagerGetFreeMemoryArgs, args);
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_GET_FREE_MEMORY,
    args, sizeof(*args), true);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  MESSAGE_ARGS_FREE(args);
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);
  NANO_OS_ASSERT_EQ_INT(1, errors(MESSAGE_ARGS_ERROR_FREE_IN_FLIGHT));

  // The memory manager wrote its answer into the freed storage.
  MESSAGE_ARGS_ALLOCATE(MemoryManagerGetFreeMemoryArgs, again);
  NANO_OS_ASSERT_EQ_PTR(args, again);
  MESSAGE_ARGS_FREE(again);
  NANO_OS_ASSERT_EQ_INT(1, errors(MESSAGE_ARGS_ERROR_WRITE_AFTER_FREE));
  NANO_OS_ASSERT_EQ_INT(2, messageArgsErrorTotal());
  messageArgsResetCounts();
}

NANO_OS_KERNEL_TEST(message_args, one_way_freed_by_its_sender_is_counted) {
  TestArgs *args = MESSAGE_ARGS_CREATE(TestArgs, MESSAGE_ARGS_ONE_WAY);
  ProcessMessage *processMessage = claimMessage(args, false);
  processMessageSetDone(processMessage);
  processMessageRelease(processMessage);
  MESSAGE_ARGS_DESTROY(args);
  assertOnlyError(MESSAGE_ARGS_ERROR_WRONG_RECIPIENT, 1);
}

NANO_OS_KERNEL_TEST(message_args, unsent_one_way_freed_by_its_owner_is_clean) {
  TestArgs *args = MESSAGE_ARGS_CREATE(TestArgs, MESSAGE_ARGS_ONE_WAY);
  NANO_OS_ASSERT_NOT_NULL(args);
  MESSAGE_ARGS_DESTROY(args);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST(message_args, created_storage_sent_to_the_memory_manager_is_counted) {
  TestArgs *args = MESSAGE_ARGS_CREATE(TestArgs, MESSAGE_ARGS_ROUND_TRIP);
  ProcessMessage *processMessage = getAvailableMessage();
  processMessageInit(processMessage, 0, args, sizeof(*args), true);
  MESSAGE_ARGS_SENDING(processMessage, memoryManagerPid);
  processMessageSetDone(processMessage);
  processMessageRelease(processMessage);
  MESSAGE_ARGS_DESTROY(args);
  assertOnlyError(MESSAGE_ARGS_ERROR_MEMORY_MANAGER, 1);
}

NANO_OS_KERNEL_TEST(message_args, a_failed_send_is_forgotten) {
  TestArgs *args = MESSAGE_ARGS_CREATE(TestArgs, MESSAGE_ARGS_ONE_WAY);
  ProcessMessage *processMessage = claimMessage(args, false);
  MESSAGE_ARGS_SEND_FAILED(processMessage);
  processMessageRelease(processMessage);
  MESSAGE_ARGS_DESTROY(args);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST(message_args, in_buffer_is_a_copy) {
  char path[] = "/etc/passwd";
  char *shared = (char*) MESSAGE_BUFFER_SHARE(path, sizeof(path),
    MESSAGE_BUFFER_IN);
  NANO_OS_ASSERT_NOT_NULL(shared);
  NANO_OS_ASSERT_TRUE(shared != path);
  NANO_OS_ASSERT_STR_EQ(path, shared);
  MESSAGE_BUFFER_UNSHARE(shared, path, MESSAGE_BUFFER_IN);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
  NANO_OS_ASSERT_EQ_INT(0, messageArgsCounts()->live);
}

NANO_OS_KERNEL_TEST(message_args, in_buffer_written_by_the_recipient_is_counted) {
  char path[] = "/etc/passwd";
  char *shared = (char*) MESSAGE_BUFFER_SHARE(path, sizeof(path),
    MESSAGE_BUFFER_IN);
  shared[1] = 'X';
  MESSAGE_BUFFER_UNSHARE(shared, path, MESSAGE_BUFFER_IN);
  NANO_OS_ASSERT_STR_EQ("/etc/passwd", path);
  assertOnlyError(MESSAGE_ARGS_ERROR_IN_MODIFIED, 1);
}

NANO_OS_KERNEL_TEST(message_args, out_buffer_is_copied_back_at_unshare) {
  char result[8] = "xxxxxxx";
  char *shared = (char*) MESSAGE_BUFFER_SHARE(result, sizeof(result),
    MESSAGE_BUFFER_OUT);
  NANO_OS_ASSERT_NOT_NULL(shared);
  NANO_OS_ASSERT_EQ_INT(0, shared[0]);
  strcpy(shared, "abc");
  NANO_OS_ASSERT_STR_EQ("xxxxxxx", result);
  MESSAGE_BUFFER_UNSHARE(shared, result, MESSAGE_BUFFER_OUT);
  NANO_OS_ASSERT_STR_EQ("abc", result);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST(message_args, a_retained_buffer_reads_poison) {
  char secret[] = "secret";
  volatile char *shared = (volatile char*) MESSAGE_BUFFER_SHARE(secret,
    sizeof(secret), MESSAGE_BUFFER_IN);
  MESSAGE_BUFFER_UNSHARE(shared, secret, MESSAGE_BUFFER_IN);
  for (size_t ii = 0; ii < sizeof(secret); ii++) {
    NANO_OS_ASSERT_EQ_INT(POISON, (uint8_t) shared[ii]);
  }
}

NANO_OS_KERNEL_TEST(message_args, unshare_with_the_wrong_original_is_counted) {
  char path[] = "/a";
  char other[] = "/b";
  void *shared = MESSAGE_BUFFER_SHARE(path, sizeof(path), MESSAGE_BUFFER_IN);
  MESSAGE_BUFFER_UNSHARE(shared, other, MESSAGE_BUFFER_IN);
  MESSAGE_BUFFER_UNSHARE(shared, path, MESSAGE_BUFFER_IN);
  assertOnlyError(MESSAGE_ARGS_ERROR_BAD_POINTER, 1);
}

NANO_OS_KERNEL_TEST(message_args, sharing_null_yields_null) {
  NANO_OS_ASSERT_NULL(MESSAGE_BUFFER_SHARE(NULL, 4, MESSAGE_BUFFER_IN));
  MESSAGE_BUFFER_UNSHARE(NULL, NULL, MESSAGE_BUFFER_IN);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

/// @var _adoptedPid
///
/// @brief The process testAssign was last asked to assign memory to.
static ProcessId _adoptedPid = 0;

/// @var _adoptedMemory
///
/// @brief The memory testAllocate hands out.
static uint8_t _adoptedMemory[64];

/// @fn static void* testAllocate(size_t size)
///
/// @brief Stand in for schedMalloc in adoption tests.
///
/// @param size The number of bytes needed.
///
/// @return Returns _adoptedMemory, or NULL if it's too small.
static void* testAllocate(size_t size) {
  return (size <= sizeof(_adoptedMemory)) ? _adoptedMemory : NULL;
}

/// @fn static int testAssign(void *ptr, ProcessId pid)
///
/// @brief Stand in for assignMemory in adoption tests.
///
/// @param ptr The memory being assigned.
/// @param pid The process it is assigned to.
///
/// @return Returns 0.
static int testAssign(void *ptr, ProcessId pid) {
  (void) ptr;
  _adoptedPid = pid;
  return 0;
}

/// @fn static void reversingCopy(void *destination, const void *source,
///   size_t size)
///
/// @brief A copy function for adoption tests that stores a block reversed, so
/// the test can tell it ran and was given the block's size.
///
/// @param destination The new block.
/// @param source The arena block.
/// @param size The size of the block.
///
/// @return This function returns no value.
static void reversingCopy(void *destination, const void *source,
  size_t size
) {
  const uint8_t *from = (const uint8_t*) source;
  uint8_t *to = (uint8_t*) destination;
  for (size_t ii = 0; ii < size; ii++) {
    to[ii] = from[size - 1 - ii];
  }
}

NANO_OS_KERNEL_TEST(message_args, adopt_moves_the_block_and_frees_the_arena) {
  uint8_t *block = (uint8_t*) MESSAGE_ARGS_CREATE_BYTES(24,
    MESSAGE_ARGS_ROUND_TRIP);
  for (int ii = 0; ii < 24; ii++) {
    block[ii] = (uint8_t) ii;
  }
  void *ptr = block;
  NANO_OS_ASSERT_EQ_INT(0, messageArgsAdoptWith(&ptr, 9, NULL,
    testAllocate, testAssign));
  NANO_OS_ASSERT_EQ_PTR(_adoptedMemory, ptr);
  NANO_OS_ASSERT_EQ_INT(9, _adoptedPid);
  for (int ii = 0; ii < 24; ii++) {
    NANO_OS_ASSERT_EQ_INT(ii, _adoptedMemory[ii]);
  }
  NANO_OS_ASSERT_EQ_INT(POISON, block[0]);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsCounts()->live);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST(message_args, adopt_rebuilds_with_the_copy_function) {
  uint8_t *block = (uint8_t*) MESSAGE_ARGS_CREATE_BYTES(4,
    MESSAGE_ARGS_ROUND_TRIP);
  memcpy(block, "abcd", 4);
  void *ptr = block;
  NANO_OS_ASSERT_EQ_INT(0, messageArgsAdoptWith(&ptr, 9, reversingCopy,
    testAllocate, testAssign));
  NANO_OS_ASSERT_EQ_INT(0, memcmp(_adoptedMemory, "dcba", 4));
}

NANO_OS_KERNEL_TEST(message_args, a_sender_free_after_adopt_is_counted) {
  void *block = MESSAGE_ARGS_CREATE_BYTES(16, MESSAGE_ARGS_ROUND_TRIP);
  void *ptr = block;
  messageArgsAdoptWith(&ptr, 9, NULL, testAllocate, testAssign);
  MESSAGE_ARGS_DESTROY(block);
  assertOnlyError(MESSAGE_ARGS_ERROR_FREE_AFTER_ADOPT, 1);
}

NANO_OS_KERNEL_TEST(message_args, adopting_a_foreign_pointer_only_assigns_it) {
  uint8_t onStack[8];
  void *ptr = onStack;
  NANO_OS_ASSERT_EQ_INT(0, messageArgsAdoptWith(&ptr, 4, NULL,
    testAllocate, testAssign));
  NANO_OS_ASSERT_EQ_PTR(onStack, ptr);
  NANO_OS_ASSERT_EQ_INT(4, _adoptedPid);
}

NANO_OS_KERNEL_TEST(message_args, a_full_arena_returns_null_and_recovers) {
  void *blocks[64];
  int count = 0;
  for (; count < 64; count++) {
    blocks[count] = MESSAGE_ARGS_CREATE_BYTES(4096, MESSAGE_ARGS_ROUND_TRIP);
    if (blocks[count] == NULL) {
      break;
    }
  }
  NANO_OS_ASSERT_TRUE((count > 0) && (count < 64));
  NANO_OS_ASSERT_EQ_INT(1, errors(MESSAGE_ARGS_ERROR_ARENA_FULL));
  for (int ii = 0; ii < count; ii++) {
    MESSAGE_ARGS_DESTROY(blocks[ii]);
  }

  // Freed neighbors coalesce, so the whole run fits in one block again.
  void *big = MESSAGE_ARGS_CREATE_BYTES((size_t) count * 4096,
    MESSAGE_ARGS_ROUND_TRIP);
  NANO_OS_ASSERT_EQ_PTR(blocks[0], big);
  MESSAGE_ARGS_DESTROY(big);
  assertOnlyError(MESSAGE_ARGS_ERROR_ARENA_FULL, 1);
}

/// @var _childArgs
///
/// @brief Storage a child process allocates or frees for the process tests.
static void *_childArgs = NULL;

/// @var _childDone
///
/// @brief Set by a child process when it has done its part.
static volatile bool _childDone = false;

/// @fn static void* leakingChild(void *args)
///
/// @brief A process that allocates argument storage and exits without freeing
/// it.
///
/// @param args Unused.
///
/// @return This function returns NULL.
static void* leakingChild(void *args) {
  (void) args;
  MESSAGE_ARGS_ALLOCATE(TestArgs, leaked);
  _childArgs = leaked;
  return NULL;
}

/// @fn static void* holdingChild(void *args)
///
/// @brief A process that allocates argument storage and then idles until it's
/// killed.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* holdingChild(void *args) {
  (void) args;
  MESSAGE_ARGS_ALLOCATE(TestArgs, held);
  _childArgs = held;
  _childDone = true;
  while (1) {
    processYield();
  }
  return NULL;
}

/// @fn static void* overflowingChild(void *args)
///
/// @brief A process that allocates argument storage, then clobbers its stack's
/// end marker so the scheduler terminates it as overflowed.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* overflowingChild(void *args) {
  (void) args;
  MESSAGE_ARGS_ALLOCATE(TestArgs, held);
  _childArgs = held;
  _childDone = true;
  *coroutineStackEnd(getRunningProcess()->mainThread) = 0;
  while (1) {
    processYield();
  }
  return NULL;
}

/// @fn static void* freeingChild(void *args)
///
/// @brief A process that frees storage another process allocated, then exits.
///
/// @param args Unused.
///
/// @return This function returns NULL.
static void* freeingChild(void *args) {
  (void) args;
  MESSAGE_ARGS_DESTROY(_childArgs);
  _childDone = true;
  return NULL;
}

/// @fn static ProcessDescriptor* startChild(void* (*fn)(void *args))
///
/// @brief Run a function as a user process in the first free user slot,
/// owned by this process's user.
///
/// @param fn The process's function.
///
/// @return Returns the process's ProcessDescriptor, or NULL if it couldn't be
/// started.
static ProcessDescriptor* startChild(void* (*fn)(void *args)) {
  ProcessId pid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
  if (processCreate(processDescriptor, fn, NULL) != processSuccess) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_USER;
  processDescriptor->userId = getRunningUid();
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return processDescriptor;
}

NANO_OS_KERNEL_TEST(message_args, storage_left_at_exit_is_a_leak) {
  _childArgs = NULL;
  ProcessDescriptor *child = startChild(leakingChild);
  NANO_OS_ASSERT_NOT_NULL(child);
  for (int ii = 0;
    (ii < MAX_YIELDS) && (errors(MESSAGE_ARGS_ERROR_LEAK) == 0);
    ii++
  ) {
    processYield();
  }
  NANO_OS_ASSERT_NOT_NULL(_childArgs);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsCounts()->live);
  assertOnlyError(MESSAGE_ARGS_ERROR_LEAK, 1);
}

NANO_OS_KERNEL_TEST(message_args, a_killed_process_storage_is_reclaimed) {
  getRunningProcess()->userId = ROOT_USER_ID;
  _childDone = false;
  ProcessDescriptor *child = startChild(holdingChild);
  NANO_OS_ASSERT_NOT_NULL(child);
  for (int ii = 0; (ii < MAX_YIELDS) && !_childDone; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_TRUE(_childDone);
  NANO_OS_ASSERT_EQ_INT(1, messageArgsCounts()->live);

  MESSAGE_ARGS_ALLOCATE(SchedulerKillProcessArgs, killArgs);
  killArgs->pid = child->processId;
  killArgs->signal = SIGKILL;
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    schedulerPid, SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_KILL_PROCESS,
    killArgs, sizeof(*killArgs), true);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);
  NANO_OS_ASSERT_EQ_INT(0, killArgs->returnValue);
  MESSAGE_ARGS_FREE(killArgs);

  NANO_OS_ASSERT_EQ_INT(1, messageArgsCounts()->reclaimed);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsCounts()->live);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST(message_args, a_stack_overflow_storage_is_reclaimed) {
  _childDone = false;
  ProcessDescriptor *child = startChild(overflowingChild);
  NANO_OS_ASSERT_NOT_NULL(child);
  for (int ii = 0;
    (ii < MAX_YIELDS)
      && ((messageArgsCounts()->reclaimed == 0)
        && (messageArgsErrorTotal() == 0));
    ii++
  ) {
    processYield();
  }
  NANO_OS_ASSERT_TRUE(_childDone);
  NANO_OS_ASSERT_EQ_INT(SIGSEGV, child->exitSignal);
  NANO_OS_ASSERT_EQ_INT(0, errors(MESSAGE_ARGS_ERROR_LEAK));
  NANO_OS_ASSERT_EQ_INT(1, messageArgsCounts()->reclaimed);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsCounts()->live);
  NANO_OS_ASSERT_EQ_INT(0, messageArgsErrorTotal());
}

NANO_OS_KERNEL_TEST(message_args, a_free_by_another_process_is_counted) {
  MESSAGE_ARGS_ALLOCATE(TestArgs, args);
  _childArgs = args;
  _childDone = false;
  NANO_OS_ASSERT_NOT_NULL(startChild(freeingChild));
  for (int ii = 0; (ii < MAX_YIELDS) && !_childDone; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_TRUE(_childDone);
  assertOnlyError(MESSAGE_ARGS_ERROR_WRONG_OWNER, 1);
}

NANO_OS_KERNEL_TEST(message_args, sending_freed_storage_is_counted) {
  MESSAGE_ARGS_ALLOCATE(TestArgs, args);
  MESSAGE_ARGS_FREE(args);
  ProcessMessage *processMessage = claimMessage(args, true);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  processMessageSetDone(processMessage);
  processMessageRelease(processMessage);
  assertOnlyError(MESSAGE_ARGS_ERROR_SEND_AFTER_FREE, 1);
}
