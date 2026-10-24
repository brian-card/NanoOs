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
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
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
  area.staticLogs.canary = STATIC_LOGS_CANARY;
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

// The heap may grow into the area before the logger starts, so an overwritten
// canary means the memory belongs to someone else now.
NANO_OS_KERNEL_TEST(logger, static_logs_are_not_written_once_overwritten) {
  static StaticLogs area;
  memset(&area, 0x5A, sizeof(area));
  area.numEntries = 0;
  StaticLogs before = area;

  halMockSetStaticLogs(&area);
  halMockSetStringsPresent(false);
  for (int ii = 0; ii < 4; ii++) {
    logError("written over the static log area (%d)\n", ii);
  }
  halMockSetStringsPresent(true);
  halMockSetStaticLogs(NULL);

  NANO_OS_ASSERT_TRUE(memcmp(&area, &before, sizeof(area)) == 0);
}

// The area isn't reserved after boot, so a restarted logger would replay
// whatever has since been allocated there.
NANO_OS_KERNEL_TEST(logger, restarted_logger_gets_no_static_logs) {
  static StaticLogs area;
  memset(&area, 0, sizeof(area));
  halMockSetStaticLogs(&area);

  StaticLogs *staticLogs = NULL;
  loggerSetRunning(false);
  NANO_OS_ASSERT_EQ_INT(0, HAL->memory->staticLogs(&staticLogs));
  NANO_OS_ASSERT_TRUE(staticLogs == &area);
  loggerSetRunning(true);
  NANO_OS_ASSERT_EQ_INT(0, HAL->memory->staticLogs(&staticLogs));
  NANO_OS_ASSERT_TRUE(staticLogs == &area);

  loggerSetRunning(false);
  NANO_OS_ASSERT_EQ_INT(0, HAL->memory->staticLogs(&staticLogs));
  NANO_OS_ASSERT_NULL(staticLogs);
  loggerSetRunning(true);
  NANO_OS_ASSERT_EQ_INT(0, HAL->memory->staticLogs(&staticLogs));
  NANO_OS_ASSERT_NULL(staticLogs);
  halMockSetStaticLogs(NULL);
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

// Messages logged while the logger process can't receive them are held and
// delivered, in order, once it can.  The logger slot here is a finished
// process, the way it is between the dummy's exit and restartLogger.
NANO_OS_KERNEL_TEST(logger, logs_held_until_logger_runs_arrive_in_order) {
  ProcessId selfPid = getRunningPid();
  ProcessId stoppedPid = 0;
  for (ProcessId pid = schedulerGetFirstShellPid() + schedulerGetNumShells();
    pid <= numProcesses; pid++
  ) {
    if (processFinished(&allProcesses[pid - 1])) {
      stoppedPid = pid;
      break;
    }
  }
  NANO_OS_ASSERT_TRUE(stoppedPid != 0);

  ProcessDescriptor *self = getRunningProcess();
  IpcCapability *savedCapabilities = self->ipcCapabilities;
  size_t savedNumCapabilities = self->numIpcCapabilities;
  static IpcCapability selfCapability;
  selfCapability.destinationPid = selfPid;
  selfCapability.signature = LOGGER_COMMAND_SIGNATURE;
  selfCapability.messageTypes = ((uint32_t) 1) << LOGGER_LOG_MESSAGE;
  self->ipcCapabilities = &selfCapability;
  self->numIpcCapabilities = 1;

  halMockSetStringsPresent(false);
  loggerPid = stoppedPid;
  for (int ii = 0; ii < 3; ii++) {
    logError("held log %d\n", ii);
  }
  loggerPid = selfPid;
  loggerSetRunning(true);
  halMockSetStringsPresent(true);

  int received = 0;
  for (ProcessMessage *processMessage = processMessageQueuePop();
    processMessage != NULL; processMessage = processMessageQueuePop()
  ) {
    LogEntry *logEntry = (LogEntry*) processMessageData(processMessage);
    NANO_OS_ASSERT_EQ_INT(received, (long long) logEntry->args[0]);
    logEntry->inUse = false;
    processMessageRelease(processMessage);
    received++;
  }

  loggerPid = 0;
  self->ipcCapabilities = savedCapabilities;
  self->numIpcCapabilities = savedNumCapabilities;
  NANO_OS_ASSERT_EQ_INT(3, received);
}
