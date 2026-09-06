///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_memory.c
///
/// @brief             Kernel tests for the memory manager, exercised through
///                    the NanoOs libc malloc/free/calloc/realloc that route
///                    IPC to the memory-manager process.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/NanoOs.h"
#include "kernel/MemoryManager.h"
#include "kernel/Processes.h"

#include "HalMock.h"

NANO_OS_KERNEL_TEST(mm, malloc_returns_usable_distinct_blocks) {
  void *a = malloc(64);
  void *b = malloc(64);
  NANO_OS_ASSERT_NOT_NULL(a);
  NANO_OS_ASSERT_NOT_NULL(b);
  NANO_OS_ASSERT_NE_INT((intptr_t) a, (intptr_t) b);

  // Whole block must be writable.
  memset(a, 0xAB, 64);
  memset(b, 0xCD, 64);
  NANO_OS_ASSERT_EQ_INT(0xAB, ((unsigned char*) a)[0]);
  NANO_OS_ASSERT_EQ_INT(0xAB, ((unsigned char*) a)[63]);
  NANO_OS_ASSERT_EQ_INT(0xCD, ((unsigned char*) b)[63]);

  free(a);
  free(b);
}

NANO_OS_KERNEL_TEST(mm, calloc_zeroes_memory) {
  size_t n = 128;
  unsigned char *p = (unsigned char*) calloc(1, n);
  NANO_OS_ASSERT_NOT_NULL(p);
  int nonZero = 0;
  for (size_t ii = 0; ii < n; ii++) {
    if (p[ii] != 0) {
      nonZero++;
    }
  }
  NANO_OS_ASSERT_EQ_INT(0, nonZero);
  free(p);
}

NANO_OS_KERNEL_TEST(mm, realloc_preserves_contents_and_grows) {
  size_t oldSize = 32;
  size_t newSize = 96;
  unsigned char *p = (unsigned char*) malloc(oldSize);
  NANO_OS_ASSERT_NOT_NULL(p);
  for (size_t ii = 0; ii < oldSize; ii++) {
    p[ii] = (unsigned char) (ii + 1);
  }

  unsigned char *q = (unsigned char*) realloc(p, newSize);
  NANO_OS_ASSERT_NOT_NULL(q);
  for (size_t ii = 0; ii < oldSize; ii++) {
    NANO_OS_ASSERT_EQ_INT((unsigned char) (ii + 1), q[ii]);
  }
  // New tail must be writable.
  memset(q + oldSize, 0x5A, newSize - oldSize);
  NANO_OS_ASSERT_EQ_INT(0x5A, q[newSize - 1]);
  free(q);
}

NANO_OS_KERNEL_TEST(mm, free_null_is_a_noop) {
  free(NULL); // must not crash / corrupt
  void *p = malloc(16);
  NANO_OS_ASSERT_NOT_NULL(p);
  free(p);
}

NANO_OS_KERNEL_TEST(mm, realloc_null_behaves_like_malloc) {
  void *p = realloc(NULL, 48);
  NANO_OS_ASSERT_NOT_NULL(p);
  memset(p, 0, 48);
  free(p);
}

NANO_OS_KERNEL_TEST(mm, oversized_malloc_fails_cleanly) {
  // Far larger than the simulated ~64 KB heap - must return NULL, not crash.
  void *p = malloc((size_t) 16 * 1024 * 1024);
  NANO_OS_ASSERT_NULL(p);
}

NANO_OS_KERNEL_TEST(mm, free_memory_accounting_moves_with_allocations) {
  size_t before = getFreeMemory();
  NANO_OS_ASSERT_TRUE(before > 0);

  void *p = malloc(4096);
  NANO_OS_ASSERT_NOT_NULL(p);
  size_t during = getFreeMemory();
  NANO_OS_ASSERT_TRUE(during < before);

  free(p);
  size_t after = getFreeMemory();
  // Freeing should recover essentially all of it (allow allocator overhead).
  NANO_OS_ASSERT_TRUE(after >= during);
  NANO_OS_ASSERT_TRUE(after + 512 >= before);
}

// -------------------------------------------------------------------------
// dumpMemoryAllocations: drives the real MEMORY_MANAGER_DUMP_MEMORY_
// ALLOCATIONS IPC round trip (the libc-side helper, not a hand-built
// message) and checks the raw printString/printHex/printInt output the
// command handler writes to the console UART.
// -------------------------------------------------------------------------

NANO_OS_KERNEL_TEST(mm, dump_memory_allocations_reports_known_allocation) {
  // Discard any console chatter already sitting in the tx buffer so the
  // dump output below starts clean.
  char scratch[1024];
  while (mockUartDrain(scratch, sizeof(scratch)) == sizeof(scratch)) {
    // keep draining
  }

  // A fresh boot's heap is one big free block, so this carves out exactly
  // 64 bytes rather than handing back an oversized remainder.
  void *p = malloc(64);
  NANO_OS_ASSERT_NOT_NULL(p);
  ProcessId owner = getRunningPid();

  NANO_OS_ASSERT_EQ_INT(0, dumpMemoryAllocations());

  char out[4096];
  memset(out, 0, sizeof(out));
  size_t got = mockUartDrain(out, sizeof(out) - 1);
  NANO_OS_ASSERT_TRUE(got > 0);

  char *outstanding = strstr(out, "Outstanding allocations:\r\n");
  NANO_OS_ASSERT_NOT_NULL(outstanding);
  char *available = strstr(out, "Available memory blocks:\r\n");
  NANO_OS_ASSERT_NOT_NULL(available);
  NANO_OS_ASSERT_TRUE(available > outstanding);

  // The allocation must be reported, with its address, size, and owning
  // PID rendered exactly as printString/printHex/printInt would produce
  // them, and it must fall within the "Outstanding allocations" section.
  char expectedLine[128];
  snprintf(expectedLine, sizeof(expectedLine),
    "  0x%llx: 64 bytes owned by %d\r\n",
    (unsigned long long) (uintptr_t) p, (int) owner);
  char *allocationLine = strstr(outstanding, expectedLine);
  NANO_OS_ASSERT_NOT_NULL(allocationLine);
  NANO_OS_ASSERT_TRUE(allocationLine < available);

  free(p);
}
