///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              01.05.2026
///
/// @file              stdatomic.c
///
/// @brief             NanoOs implementation of stdatomic.h functions missing
///                    from the avr-gcc standard C library implementation.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
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
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#if defined(__AVR__)

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "../../Hal.h"
#include "../../NanoOsTypes.h"
#include "../../Scheduler.h"

/// @var _preemptionTimerCached
///
/// @brief Whether _cachedPreemptionTimer has been fetched from the
/// scheduler yet.  The timer's device ID is fixed once the scheduler
/// initializes (never reassigned after that), so it only needs to be
/// fetched once per process image.  Separate from every other file's own
/// copy of this cache -- different translation unit, same pattern.
static bool _preemptionTimerCached = false;

/// @var _cachedPreemptionTimer
///
/// @brief Cached copy of the preemption timer's device ID, avoiding a
/// schedulerGetPreemptionTimer() call on this hot path once cached.
static int _cachedPreemptionTimer = -1;

/// @fn int getPreemptionTimer(void)
///
/// @brief Fetch and cache the preemption timer's device ID on first use.
static inline int getPreemptionTimer(void) {
  if (_preemptionTimerCached == false) {
    _cachedPreemptionTimer = schedulerGetPreemptionTimer();
    _preemptionTimerCached = true;
  }
  return _cachedPreemptionTimer;
}

bool __atomic_compare_exchange_2(void *ptr, void *expected, uint16_t desired,
  bool weak, int success_memorder, int failure_memorder
) {
  (void) weak;
  (void) success_memorder;
  (void) failure_memorder;

  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = HAL->timer.cancelAndGet(
    getPreemptionTimer(), NULL, &remainingNanoseconds, &callback);


  bool success = false;
  if (*((uint16_t*) ptr) == *((uint16_t*) expected)) {
    *((uint16_t*) ptr) = desired;
    success = true;
  } else {
    *((uint16_t*) expected) = *((uint16_t*) ptr);
  }

  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer.configOneShot(getPreemptionTimer(),
      remainingNanoseconds, callback);
  }

  return success;
}

void __atomic_store_2(void *ptr, uint16_t val, int memorder) {
  (void) memorder;

  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = HAL->timer.cancelAndGet(
    getPreemptionTimer(), NULL, &remainingNanoseconds, &callback);


  *((uint16_t*) ptr) = val;

  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer.configOneShot(getPreemptionTimer(),
      remainingNanoseconds, callback);
  }
}

uint16_t __atomic_load_2(const void *ptr, int memorder) {
  (void) memorder;

  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = HAL->timer.cancelAndGet(
    getPreemptionTimer(), NULL, &remainingNanoseconds, &callback);


  uint16_t returnValue = *((uint16_t*) ptr);

  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer.configOneShot(getPreemptionTimer(),
      remainingNanoseconds, callback);
  }

  return returnValue;
}

#endif // defined(__AVR__)

