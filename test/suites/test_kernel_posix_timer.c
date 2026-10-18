///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_posix_timer.c
///
/// @brief             Kernel tests for the POSIX HAL's software timers, called
///                    directly rather than through the mock HAL.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

int posixInitTimerDevice(int32_t deviceId, va_list args);
int posixConfigOneShotTimer(int32_t deviceId, va_list args);
int posixCancelTimer(int32_t deviceId, va_list args);
int posixCancelAndGetTimer(int32_t deviceId, va_list args);

static volatile int _timerFires = 0;

/// @fn int callPosixTimer(int (*function)(int32_t, va_list),
///   int32_t deviceId, ...)
///
/// @brief Call a POSIX timer implementation with its arguments packed into a
/// va_list, as callHal would.
///
/// @param function The POSIX timer implementation to call.
/// @param deviceId The timer to operate on.
/// @param ... The arguments the implementation reads from its va_list.
///
/// @return Returns the implementation's return value.
static int callPosixTimer(int (*function)(int32_t, va_list),
  int32_t deviceId, ...
) {
  va_list args;
  va_start(args, deviceId);
  int returnValue = function(deviceId, args);
  va_end(args);
  return returnValue;
}

/// @fn void countTimerFire(void)
///
/// @brief Timer callback that counts how many times it has run.
///
/// @return This function returns no value.
static void countTimerFire(void) {
  _timerFires++;
}

/// @fn void* sleepBriefly(void *arg)
///
/// @brief Thread function that sleeps long enough to still be running when the
/// test cancels the timer.
///
/// @param arg Unused.
///
/// @return Always returns NULL.
static void* sleepBriefly(void *arg) {
  (void) arg;
  struct timespec ts = { .tv_sec = 0, .tv_nsec = 200000000 };
  nanosleep(&ts, NULL);
  return NULL;
}

/// @fn void fireAndRearm(int (*cancel)(int32_t, va_list), void **bystanderResult)
///
/// @brief Let timer 0 fire with its signal blocked so the signal is still
/// pending after the timer thread exits, cancel and rearm it while another
/// thread runs, then let the stale signal through.
///
/// @param cancel The cancel implementation to call.
/// @param bystanderResult Where to store the other thread's exit value.
///
/// @return This function returns no value.
static void fireAndRearm(int (*cancel)(int32_t, va_list),
  void **bystanderResult
) {
  sigset_t timerSignal;
  sigset_t previousSignals;
  sigemptyset(&timerSignal);
  sigaddset(&timerSignal, SIGUSR1);

  callPosixTimer(posixInitTimerDevice, 0);
  pthread_sigmask(SIG_BLOCK, &timerSignal, &previousSignals);
  _timerFires = 0;
  callPosixTimer(posixConfigOneShotTimer, 0, (uint64_t) 1000000,
    countTimerFire);
  struct timespec ts = { .tv_sec = 0, .tv_nsec = 50000000 };
  nanosleep(&ts, NULL);

  pthread_t bystander;
  pthread_create(&bystander, NULL, sleepBriefly, NULL);
  if (cancel == posixCancelAndGetTimer) {
    uint64_t configured = 0, remaining = 0;
    void (*callback)(void) = NULL;
    callPosixTimer(cancel, 0, &configured, &remaining, &callback);
  } else {
    callPosixTimer(cancel, 0);
  }
  callPosixTimer(posixConfigOneShotTimer, 0, (uint64_t) 10000000000,
    countTimerFire);
  pthread_sigmask(SIG_SETMASK, &previousSignals, NULL);

  pthread_join(bystander, bystanderResult);
  callPosixTimer(posixCancelTimer, 0);
}

NANO_OS_KERNEL_TEST(posix_timer, cancel_after_the_timer_fires_spares_other_threads) {
  void *bystanderResult = NULL;
  fireAndRearm(posixCancelTimer, &bystanderResult);
  NANO_OS_ASSERT_TRUE(bystanderResult != PTHREAD_CANCELED);
  NANO_OS_ASSERT_EQ_INT(0, _timerFires);
}

NANO_OS_KERNEL_TEST(posix_timer, cancelAndGet_after_the_timer_fires_spares_other_threads) {
  void *bystanderResult = NULL;
  fireAndRearm(posixCancelAndGetTimer, &bystanderResult);
  NANO_OS_ASSERT_TRUE(bystanderResult != PTHREAD_CANCELED);
  NANO_OS_ASSERT_EQ_INT(0, _timerFires);
}
