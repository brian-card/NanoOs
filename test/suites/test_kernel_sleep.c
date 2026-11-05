///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.05.2026
///
/// @file              test_kernel_sleep.c
///
/// @brief             Kernel tests for the POSIX behavior of nanoOsNanosleep
///                    and nanoOsSleep.
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
#include "HalMock.h"

#include <stdbool.h>
#include <stdint.h>

#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsSignal.h"
#include "user/NanoOsTime.h"
#include "user/NanoOsUnistd.h"

/// @def SLEEP_TEST_START_NS
///
/// @brief Virtual time each test starts sleeping at.  A clock that reads zero
/// would make an absolute deadline indistinguishable from a relative interval.
#define SLEEP_TEST_START_NS 1000000000000ULL

/// @def SLEEP_TEST_SLACK_STEPS
///
/// @brief Number of ticker steps a sleep may overrun its interval by.
#define SLEEP_TEST_SLACK_STEPS 10

/// @struct SleepTicker
///
/// @brief State shared between a test body and the process that advances the
/// virtual clock while the body sleeps.
///
/// @param stepNs Nanoseconds to advance the clock by on each turn.
/// @param watchdogNs Virtual time at which a sleep still in progress is cut
///   short so that a broken sleep fails the test instead of hanging it.
/// @param sleeper The process that sleeps.
/// @param signal The signal to send the sleeper, or 0 for none.
/// @param signalAtNs Virtual time at which to send the signal.
/// @param signalResult What schedulerSendSignal returned.
/// @param signalSent Whether the signal has been sent.
/// @param watchdogFired Whether the watchdog cut the sleep short.
typedef struct SleepTicker {
  uint64_t stepNs;
  uint64_t watchdogNs;
  ProcessDescriptor *sleeper;
  int signal;
  uint64_t signalAtNs;
  int signalResult;
  bool signalSent;
  bool watchdogFired;
} SleepTicker;

/// @fn static void* tickerProcess(void *args)
///
/// @brief Advance the virtual clock one step per turn, sending the configured
/// signal and enforcing the watchdog as their times pass.
///
/// @param args A pointer to the test's SleepTicker.
///
/// @return This function never returns.
static void* tickerProcess(void *args) {
  SleepTicker *ticker = (SleepTicker*) args;
  while (1) {
    mockClockAdvanceNs(ticker->stepNs);
    uint64_t now = mockClockNowNs();
    if ((ticker->signal != 0) && (ticker->signalSent == false)
      && (now >= ticker->signalAtNs)
    ) {
      ticker->signalSent = true;
      ticker->signalResult = schedulerSendSignal(
        ticker->sleeper->processId, ticker->signal);
    }
    if ((ticker->watchdogFired == false) && (now >= ticker->watchdogNs)) {
      Comutex *comutex = ticker->sleeper->mainThread->blockingComutex;
      if (comutex != NULL) {
        ticker->watchdogFired = true;
        comutex->timeoutTime = 0;
      }
    }
    processYield();
  }

  return NULL;
}

/// @fn static bool startTicker(SleepTicker *ticker, uint64_t stepNs,
///   uint64_t intervalNs)
///
/// @brief Move the virtual clock to SLEEP_TEST_START_NS and run tickerProcess
/// in the first user process slot for a sleep of a given interval.
///
/// @param ticker The SleepTicker to initialize and hand the process.
/// @param stepNs Nanoseconds to advance the clock by on each turn.
/// @param intervalNs The interval the test body is about to sleep for.
///
/// @return Returns true if the process was started, false if it wasn't.
static bool startTicker(SleepTicker *ticker, uint64_t stepNs,
  uint64_t intervalNs
) {
  mockClockAdvanceNs(SLEEP_TEST_START_NS - mockClockNowNs());

  ticker->stepNs = stepNs;
  ticker->watchdogNs = SLEEP_TEST_START_NS + intervalNs + 1000000000ULL;
  ticker->sleeper = getRunningProcess();
  ticker->signal = 0;
  ticker->signalAtNs = 0;
  ticker->signalResult = -1;
  ticker->signalSent = false;
  ticker->watchdogFired = false;

  ProcessId pid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
  if (processCreate(processDescriptor, tickerProcess, ticker)
    != processSuccess
  ) {
    return false;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->ipcCapabilities = ticker->sleeper->ipcCapabilities;
  processDescriptor->numIpcCapabilities
    = ticker->sleeper->numIpcCapabilities;
  processDescriptor->userId = ticker->sleeper->userId;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return true;
}

/// @fn static void assertSleptFor(uint64_t elapsedNs, uint64_t intervalNs,
///   const SleepTicker *ticker)
///
/// @brief Assert that a sleep lasted at least its interval and overran it by
/// no more than SLEEP_TEST_SLACK_STEPS ticker steps.
///
/// @param elapsedNs The virtual time the sleep took.
/// @param intervalNs The interval that was requested.
/// @param ticker The SleepTicker that advanced the clock.
///
/// @return This function returns no value.
static void assertSleptFor(uint64_t elapsedNs, uint64_t intervalNs,
  const SleepTicker *ticker
) {
  NANO_OS_ASSERT_FALSE(ticker->watchdogFired);
  NANO_OS_ASSERT_TRUE(elapsedNs >= intervalNs);
  NANO_OS_ASSERT_TRUE(
    elapsedNs <= intervalNs + (SLEEP_TEST_SLACK_STEPS * ticker->stepNs));
}

/// @fn static void assertRejected(time_t seconds, long nanoseconds)
///
/// @brief Assert that nanoOsNanosleep fails with EINVAL for an interval
/// without sleeping and without touching the remaining-time output.
///
/// @param seconds The tv_sec of the interval.
/// @param nanoseconds The tv_nsec of the interval.
///
/// @return This function returns no value.
static void assertRejected(time_t seconds, long nanoseconds) {
  SleepTicker ticker;
  NANO_OS_ASSERT_TRUE(startTicker(&ticker, 1000000ULL, 0));

  struct timespec duration = { .tv_sec = seconds, .tv_nsec = nanoseconds };
  struct timespec rem = { .tv_sec = 1234, .tv_nsec = 5678 };
  errno = 0;
  uint64_t start = mockClockNowNs();
  int returnValue = nanoOsNanosleep(&duration, &rem);
  uint64_t elapsed = mockClockNowNs() - start;

  NANO_OS_ASSERT_EQ_INT(-1, returnValue);
  NANO_OS_ASSERT_EQ_INT(EINVAL, errno);
  NANO_OS_ASSERT_EQ_INT(0, elapsed);
  NANO_OS_ASSERT_EQ_INT(1234, rem.tv_sec);
  NANO_OS_ASSERT_EQ_INT(5678, rem.tv_nsec);
}

NANO_OS_KERNEL_TEST(sleep, nanosleep_sleeps_for_the_requested_interval) {
  SleepTicker ticker;
  NANO_OS_ASSERT_TRUE(startTicker(&ticker, 1000000ULL, 2500000000ULL));

  struct timespec duration = { .tv_sec = 2, .tv_nsec = 500000000 };
  struct timespec rem = { .tv_sec = 1234, .tv_nsec = 5678 };
  uint64_t start = mockClockNowNs();
  int returnValue = nanoOsNanosleep(&duration, &rem);
  uint64_t elapsed = mockClockNowNs() - start;

  NANO_OS_ASSERT_EQ_INT(0, returnValue);
  assertSleptFor(elapsed, 2500000000ULL, &ticker);
  NANO_OS_ASSERT_EQ_INT(1234, rem.tv_sec);
  NANO_OS_ASSERT_EQ_INT(5678, rem.tv_nsec);
}

NANO_OS_KERNEL_TEST(sleep, nanosleep_accepts_a_null_rem) {
  SleepTicker ticker;
  NANO_OS_ASSERT_TRUE(startTicker(&ticker, 1000000ULL, 300000000ULL));

  struct timespec duration = { .tv_sec = 0, .tv_nsec = 300000000 };
  uint64_t start = mockClockNowNs();
  int returnValue = nanoOsNanosleep(&duration, NULL);
  uint64_t elapsed = mockClockNowNs() - start;

  NANO_OS_ASSERT_EQ_INT(0, returnValue);
  assertSleptFor(elapsed, 300000000ULL, &ticker);
}

NANO_OS_KERNEL_TEST(sleep, nanosleep_accepts_the_largest_nanosecond_value) {
  SleepTicker ticker;
  NANO_OS_ASSERT_TRUE(startTicker(&ticker, 1000000ULL, 999999999ULL));

  struct timespec duration = { .tv_sec = 0, .tv_nsec = 999999999 };
  uint64_t start = mockClockNowNs();
  int returnValue = nanoOsNanosleep(&duration, NULL);
  uint64_t elapsed = mockClockNowNs() - start;

  NANO_OS_ASSERT_EQ_INT(0, returnValue);
  assertSleptFor(elapsed, 999999999ULL, &ticker);
}

NANO_OS_KERNEL_TEST(sleep, nanosleep_of_zero_returns_zero) {
  SleepTicker ticker;
  NANO_OS_ASSERT_TRUE(startTicker(&ticker, 1000000ULL, 0));

  struct timespec duration = { .tv_sec = 0, .tv_nsec = 0 };
  uint64_t start = mockClockNowNs();
  int returnValue = nanoOsNanosleep(&duration, NULL);
  uint64_t elapsed = mockClockNowNs() - start;

  NANO_OS_ASSERT_EQ_INT(0, returnValue);
  assertSleptFor(elapsed, 0, &ticker);
}

NANO_OS_KERNEL_TEST(sleep, nanosleep_rejects_negative_nanoseconds) {
  assertRejected(1, -1);
}

NANO_OS_KERNEL_TEST(sleep, nanosleep_rejects_one_billion_nanoseconds) {
  assertRejected(0, 1000000000);
}

NANO_OS_KERNEL_TEST(sleep, nanosleep_rejects_negative_seconds) {
  assertRejected(-1, 0);
}

NANO_OS_KERNEL_TEST(sleep, nanosleep_ignored_signal_does_not_end_the_sleep) {
  SleepTicker ticker;
  NANO_OS_ASSERT_TRUE(startTicker(&ticker, 1000000ULL, 2000000000ULL));
  ticker.signal = SIGCHLD;
  ticker.signalAtNs = SLEEP_TEST_START_NS + 1000000000ULL;

  struct timespec duration = { .tv_sec = 2, .tv_nsec = 0 };
  struct timespec rem = { .tv_sec = 1234, .tv_nsec = 5678 };
  uint64_t start = mockClockNowNs();
  int returnValue = nanoOsNanosleep(&duration, &rem);
  uint64_t elapsed = mockClockNowNs() - start;

  NANO_OS_ASSERT_EQ_INT(0, returnValue);
  assertSleptFor(elapsed, 2000000000ULL, &ticker);
  NANO_OS_ASSERT_TRUE(ticker.signalSent);
  NANO_OS_ASSERT_EQ_INT(0, ticker.signalResult);
  NANO_OS_ASSERT_EQ_INT(1234, rem.tv_sec);
  NANO_OS_ASSERT_EQ_INT(5678, rem.tv_nsec);
}

NANO_OS_KERNEL_TEST(sleep, sleep_sleeps_for_the_requested_seconds) {
  SleepTicker ticker;
  NANO_OS_ASSERT_TRUE(startTicker(&ticker, 10000000ULL, 3000000000ULL));

  uint64_t start = mockClockNowNs();
  unsigned int returnValue = nanoOsSleep(3);
  uint64_t elapsed = mockClockNowNs() - start;

  NANO_OS_ASSERT_EQ_INT(0, returnValue);
  assertSleptFor(elapsed, 3000000000ULL, &ticker);
}

NANO_OS_KERNEL_TEST(sleep, sleep_of_zero_returns_zero) {
  SleepTicker ticker;
  NANO_OS_ASSERT_TRUE(startTicker(&ticker, 10000000ULL, 0));

  uint64_t start = mockClockNowNs();
  unsigned int returnValue = nanoOsSleep(0);
  uint64_t elapsed = mockClockNowNs() - start;

  NANO_OS_ASSERT_EQ_INT(0, returnValue);
  assertSleptFor(elapsed, 0, &ticker);
}

NANO_OS_KERNEL_TEST(sleep, sleep_ignored_signal_does_not_end_the_sleep) {
  SleepTicker ticker;
  NANO_OS_ASSERT_TRUE(startTicker(&ticker, 10000000ULL, 2000000000ULL));
  ticker.signal = SIGCHLD;
  ticker.signalAtNs = SLEEP_TEST_START_NS + 1000000000ULL;

  uint64_t start = mockClockNowNs();
  unsigned int returnValue = nanoOsSleep(2);
  uint64_t elapsed = mockClockNowNs() - start;

  NANO_OS_ASSERT_EQ_INT(0, returnValue);
  assertSleptFor(elapsed, 2000000000ULL, &ticker);
  NANO_OS_ASSERT_TRUE(ticker.signalSent);
  NANO_OS_ASSERT_EQ_INT(0, ticker.signalResult);
}
