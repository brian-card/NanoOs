///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_logger.c
///
/// @brief             Tests for the logger's reporting of log messages that
///                    are discarded because they were raised from inside the
///                    send to the logger process.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Hal.h"
#include "kernel/Logger.h"
#include "kernel/MemoryManager.h"
#include "HalMock.h"

// logDroppedReentrantLog only prints the address it's given, so these never
// get dereferenced and can be literals that make the expected output exact.
#define TEST_FILE_NAME_ADDRESS ((const char*) 0x12345678)
#define TEST_NULL_FILE_NAME    ((const char*) 0)

NANO_OS_KERNEL_TEST(logger, dropped_reentrant_log_reports_line_and_address) {
  char drained[256];

  // Discard anything the kernel wrote while starting up.  Kernel logs format
  // immediately on this platform, so there is more than one bufferful of it.
  while (mockUartDrain(drained, sizeof(drained)) == sizeof(drained)) {
    // keep draining
  }

  logDroppedReentrantLog(721, TEST_FILE_NAME_ADDRESS);

  size_t length = mockUartDrain(drained, sizeof(drained));
  NANO_OS_ASSERT_TRUE(length < sizeof(drained));
  drained[length] = '\0';
  // printString promotes the trailing newline to a CRLF.
  NANO_OS_ASSERT_STR_EQ(
    "logMessage: Discarding log raised while logging from line 721 of the "
    "file at 0x12345678.\r\n",
    drained);
}

NANO_OS_KERNEL_TEST(logger, dropped_reentrant_log_reports_a_null_file_name) {
  char drained[256];
  while (mockUartDrain(drained, sizeof(drained)) == sizeof(drained)) {
    // keep draining
  }

  logDroppedReentrantLog(1, TEST_NULL_FILE_NAME);

  size_t length = mockUartDrain(drained, sizeof(drained));
  NANO_OS_ASSERT_TRUE(length < sizeof(drained));
  drained[length] = '\0';
  NANO_OS_ASSERT_STR_EQ(
    "logMessage: Discarding log raised while logging from line 1 of the "
    "file at 0x0.\r\n",
    drained);
}

// A test-owned area, so this exercises the bound without depending on where
// the platform would have put one.  Strings have to be reported absent or
// logMessage formats immediately and never reaches the stash at all.
NANO_OS_KERNEL_TEST(logger, static_logs_never_exceed_their_backing_memory) {
  static struct {
    StaticLogs staticLogs;
    unsigned char canary;
  } area;
  memset(&area, 0, sizeof(area));
  area.canary = 0xA5;

  size_t capacity = sizeof(area.staticLogs.logEntries)
    / sizeof(area.staticLogs.logEntries[0]);

  halMockSetStaticLogs(&area.staticLogs);
  halMockSetStringsPresent(false);
  for (int ii = 0; ii < 64; ii++) {
    logError("flooding the static log area (%d)\n", ii);
  }
  halMockSetStringsPresent(true);
  halMockSetStaticLogs(NULL);

  NANO_OS_ASSERT_EQ_INT((long long) capacity,
    (long long) area.staticLogs.numEntries);
  NANO_OS_ASSERT_EQ_INT(0xA5, area.canary);
}

// The stash is only valid until a logger process drains it and the heap takes
// the memory back.  Nothing here starts one, so no area gets published and
// logMessage formats immediately instead.
NANO_OS_KERNEL_TEST(logger, no_logger_process_means_no_published_stash) {
  StaticLogs *staticLogs = NULL;
  NANO_OS_ASSERT_EQ_INT(0, HAL->memory->staticLogs(&staticLogs));
  NANO_OS_ASSERT_NULL(staticLogs);
  NANO_OS_ASSERT_EQ_INT(0, (long long) loggerPid);
  NANO_OS_ASSERT_TRUE(HAL->memory->stringsPresent == true);
}

// Logging must not write into memory the allocator has handed out.
NANO_OS_KERNEL_TEST(logger, logging_does_not_corrupt_a_heap_block) {
  void *block = malloc(20000);
  NANO_OS_ASSERT_NOT_NULL(block);
  memset(block, 0x5A, 20000);

  for (int ii = 0; ii < 64; ii++) {
    logError("clobber check (%d)\n", ii);
  }

  size_t corrupted = 0;
  for (size_t ii = 0; ii < 20000; ii++) {
    if (((unsigned char*) block)[ii] != 0x5A) {
      corrupted++;
    }
  }
  free(block);
  NANO_OS_ASSERT_EQ_INT(0, (long long) corrupted);
}
