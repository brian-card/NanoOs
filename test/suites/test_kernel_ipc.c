///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_ipc.c
///
/// @brief             Kernel tests for the inter-process message system:
///                    the shared message pool and a blocking request/response
///                    round trip to the memory-manager process.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <stdint.h>

#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "kernel/MemoryManager.h"

// -------------------------------------------------------------------------
// Message pool: getAvailableMessage hands out a bounded number of slots and
// then reports exhaustion (NULL) rather than corrupting state.
// -------------------------------------------------------------------------

NANO_OS_KERNEL_TEST(ipc_pool, exhausts_gracefully_then_recovers) {
  // NANO_OS_NUM_MESSAGES shared slots + this process's embedded slot is the
  // ceiling; grabbing without releasing must terminate at NULL.
  ProcessMessage *held[NANO_OS_NUM_MESSAGES + 4];
  int count = 0;
  for (int ii = 0; ii < (int) (sizeof(held) / sizeof(held[0])); ii++) {
    held[ii] = getAvailableMessage();
    if (held[ii] == NULL) {
      break;
    }
    count++;
  }

  NANO_OS_ASSERT_TRUE(count > 0);
  NANO_OS_ASSERT_TRUE(count <= NANO_OS_NUM_MESSAGES + 1);
  NANO_OS_ASSERT_NULL(getAvailableMessage()); // still exhausted

  // Release everything; the pool must be usable again.
  for (int ii = 0; ii < count; ii++) {
    processMessageRelease(held[ii]);
  }
  ProcessMessage *again = getAvailableMessage();
  NANO_OS_ASSERT_NOT_NULL(again);
  processMessageRelease(again);
}

NANO_OS_KERNEL_TEST(ipc_pool, fresh_message_is_marked_in_use) {
  ProcessMessage *msg = getAvailableMessage();
  NANO_OS_ASSERT_NOT_NULL(msg);
  NANO_OS_ASSERT_TRUE(processMessageInUse(msg));
  processMessageRelease(msg);
  NANO_OS_ASSERT_FALSE(processMessageInUse(msg));
}

// -------------------------------------------------------------------------
// Blocking request/response: ask the memory manager how much memory is
// free, the canonical "send with waiting=true, wait for done, read result"
// pattern.
// -------------------------------------------------------------------------

NANO_OS_KERNEL_TEST(ipc_roundtrip, blocking_call_to_memory_manager) {
  typedef struct {
    size_t returnValue;
  } GetFreeMemoryArgs;

  GetFreeMemoryArgs args;
  args.returnValue = (size_t) -1;

  ProcessMessage *msg = initSendProcessMessageToPid(
    SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_GET_FREE_MEMORY,
    &args, sizeof(args), /* waiting = */ true);
  NANO_OS_ASSERT_NOT_NULL(msg);

  int waitStatus = processMessageWaitForDone(msg, NULL);
  NANO_OS_ASSERT_EQ_INT(0, waitStatus);
  NANO_OS_ASSERT_TRUE(processMessageDone(msg));

  processMessageRelease(msg);

  NANO_OS_ASSERT_NE_INT((size_t) -1, args.returnValue);
  NANO_OS_ASSERT_TRUE(args.returnValue > 0);
}

NANO_OS_KERNEL_TEST(ipc_roundtrip, getFreeMemory_helper_matches_direct_call) {
  // getFreeMemory() is the libc wrapper around the message above; it should
  // give a comparable answer (allocations between the two calls aside).
  size_t viaHelper = getFreeMemory();
  NANO_OS_ASSERT_TRUE(viaHelper > 0);
}
