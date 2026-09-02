///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_hal.c
///
/// @brief             Kernel tests for HAL calls against the mock HAL:
///                    deterministic clock, synchronous timers, console UART.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <stdint.h>
#include <string.h>

#include "kernel/Hal.h"
#include "HalMock.h"

// -------------------------------------------------------------------------
// Clock: virtual time advances only when asked, and the three unit getters
// stay consistent with each other.
// -------------------------------------------------------------------------

NANO_OS_KERNEL_TEST(hal_clock, elapsed_tracks_virtual_time) {
  int64_t ns0 = 0, us0 = 0, ms0 = 0;
  NANO_OS_ASSERT_EQ_INT(0, HAL->clock.getElapsedNanoseconds(0, &ns0));
  NANO_OS_ASSERT_EQ_INT(0, HAL->clock.getElapsedMicroseconds(0, &us0));
  NANO_OS_ASSERT_EQ_INT(0, HAL->clock.getElapsedMilliseconds(0, &ms0));

  mockClockAdvanceNs(2500000); // 2.5 ms

  int64_t ns1 = 0, us1 = 0, ms1 = 0;
  HAL->clock.getElapsedNanoseconds(0, &ns1);
  HAL->clock.getElapsedMicroseconds(0, &us1);
  HAL->clock.getElapsedMilliseconds(0, &ms1);

  NANO_OS_ASSERT_EQ_INT(2500000, ns1 - ns0);
  NANO_OS_ASSERT_EQ_INT(2500,    us1 - us0);
  NANO_OS_ASSERT_EQ_INT(2,       ms1 - ms0); // truncating division
}

NANO_OS_KERNEL_TEST(hal_clock, elapsed_is_relative_to_start_time) {
  mockClockAdvanceNs(10000000); // now = 10 ms
  int64_t rel = 0;
  HAL->clock.getElapsedMilliseconds(4, &rel); // start at 4 ms
  NANO_OS_ASSERT_EQ_INT(6, rel);
}

// -------------------------------------------------------------------------
// Timer: one-shot arm / fire / disarm and cancelAndGet semantics.
// -------------------------------------------------------------------------

static volatile int _timerFireCount = 0;
static void countingTimerCallback(void) {
  _timerFireCount++;
}

NANO_OS_KERNEL_TEST(hal_timer, one_shot_fires_once_on_demand) {
  _timerFireCount = 0;

  NANO_OS_ASSERT_EQ_INT(0,
    HAL->timer.configOneShot(0, 5000000, countingTimerCallback));

  uint64_t configured = 0;
  NANO_OS_ASSERT_EQ_INT(0, HAL->timer.configuredNanoseconds(0, &configured));
  NANO_OS_ASSERT_EQ_INT(5000000, configured);

  // Not fired until explicitly triggered (deterministic - no wall clock).
  NANO_OS_ASSERT_EQ_INT(0, _timerFireCount);

  NANO_OS_ASSERT_EQ_INT(0, mockTimerFire(0));
  NANO_OS_ASSERT_EQ_INT(1, _timerFireCount);

  // Second fire does nothing: the one-shot is disarmed.
  NANO_OS_ASSERT_EQ_INT(-1, mockTimerFire(0));
  NANO_OS_ASSERT_EQ_INT(1, _timerFireCount);
}

NANO_OS_KERNEL_TEST(hal_timer, cancel_disarms_before_fire) {
  _timerFireCount = 0;
  HAL->timer.configOneShot(1, 1000000, countingTimerCallback);
  NANO_OS_ASSERT_EQ_INT(0, HAL->timer.cancel(1));
  NANO_OS_ASSERT_EQ_INT(-1, mockTimerFire(1));
  NANO_OS_ASSERT_EQ_INT(0, _timerFireCount);
}

NANO_OS_KERNEL_TEST(hal_timer, cancelAndGet_returns_config_and_disarms) {
  _timerFireCount = 0;
  HAL->timer.configOneShot(0, 7000000, countingTimerCallback);
  mockClockAdvanceNs(3000000); // 3 ms elapsed, 4 ms remain

  uint64_t configured = 0, remaining = 0;
  void (*callback)(void) = NULL;
  NANO_OS_ASSERT_EQ_INT(0,
    HAL->timer.cancelAndGet(0, &configured, &remaining, &callback));

  NANO_OS_ASSERT_EQ_INT(7000000, configured);
  NANO_OS_ASSERT_EQ_INT(4000000, remaining);
  NANO_OS_ASSERT_EQ_PTR(countingTimerCallback, callback);

  NANO_OS_ASSERT_EQ_INT(-1, mockTimerFire(0)); // disarmed by cancelAndGet
  NANO_OS_ASSERT_EQ_INT(0, _timerFireCount);
}

// -------------------------------------------------------------------------
// Console UART: bytes fed in are pollable in order; bytes written by the
// kernel land in the drain buffer.
// -------------------------------------------------------------------------

NANO_OS_KERNEL_TEST(hal_uart, fed_bytes_poll_back_in_order) {
  mockUartFeed("Hi!", 3);
  NANO_OS_ASSERT_EQ_INT('H', HAL->uart.poll(1));
  NANO_OS_ASSERT_EQ_INT('i', HAL->uart.poll(1));
  NANO_OS_ASSERT_EQ_INT('!', HAL->uart.poll(1));
  NANO_OS_ASSERT_EQ_INT(-1,  HAL->uart.poll(1)); // empty -> non-blocking -1
}

NANO_OS_KERNEL_TEST(hal_uart, kernel_writes_reach_the_drain_buffer) {
  // Discard any console bring-up chatter already sitting in the tx buffer.
  char scratch[512];
  while (mockUartDrain(scratch, sizeof(scratch)) == sizeof(scratch)) {
    // keep draining
  }

  const char *msg = "xyz123";
  ssize_t written = 0;
  NANO_OS_ASSERT_EQ_INT(0,
    HAL->uart.write(1, (const uint8_t*) msg, 6, &written));
  NANO_OS_ASSERT_EQ_INT(6, written);

  char out[16];
  memset(out, 0, sizeof(out));
  size_t got = mockUartDrain(out, sizeof(out));
  NANO_OS_ASSERT_EQ_INT(6, got);
  NANO_OS_ASSERT_STR_EQ("xyz123", out);
}

NANO_OS_KERNEL_TEST(hal_uart, device_1_is_the_console) {
  bool isConsole = false;
  NANO_OS_ASSERT_EQ_INT(0, HAL->uart.isConsole(1, &isConsole));
  NANO_OS_ASSERT_TRUE(isConsole);

  isConsole = true;
  HAL->uart.isConsole(0, &isConsole);
  NANO_OS_ASSERT_FALSE(isConsole);
}
