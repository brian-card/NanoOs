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

#include "kernel/Logger.h"
#include "HalMock.h"

// logDroppedReentrantLog only prints the address it's given, so these never
// get dereferenced and can be literals that make the expected output exact.
#define TEST_FILE_NAME_ADDRESS ((const char*) 0x12345678)
#define TEST_NULL_FILE_NAME    ((const char*) 0)

NANO_OS_KERNEL_TEST(logger, dropped_reentrant_log_reports_line_and_address) {
  char drained[256];

  // Discard anything the kernel wrote while starting up.
  mockUartDrain(drained, sizeof(drained));

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
  mockUartDrain(drained, sizeof(drained));

  logDroppedReentrantLog(1, TEST_NULL_FILE_NAME);

  size_t length = mockUartDrain(drained, sizeof(drained));
  NANO_OS_ASSERT_TRUE(length < sizeof(drained));
  drained[length] = '\0';
  NANO_OS_ASSERT_STR_EQ(
    "logMessage: Discarding log raised while logging from line 1 of the "
    "file at 0x0.\r\n",
    drained);
}
