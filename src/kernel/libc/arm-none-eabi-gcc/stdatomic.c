///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              01.05.2026
///
/// @file              stdatomic.c
///
/// @brief             NanoOs implementation of stdatomic.h functions missing
///                    from the arm-none-eabi-gcc standard C library
///                    implementation.
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

#if defined(__arm__)

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
/// fetched once per process image.  Separate from Processes.c's own copy
/// of this cache -- different translation unit, same pattern.
static bool _preemptionTimerCached = false;

/// @var _cachedPreemptionTimer
///
/// @brief Cached copy of the preemption timer's device ID, avoiding a
/// schedulerGetPreemptionTimer() call on this hot path once cached.
static int _cachedPreemptionTimer = -1;

bool __atomic_compare_exchange_4(void *ptr, void *expected, uint32_t desired,
  bool weak, int success_memorder, int failure_memorder
) {
  (void) weak;
  (void) success_memorder;
  (void) failure_memorder;

  if (_preemptionTimerCached == false) {
    _cachedPreemptionTimer = schedulerGetPreemptionTimer();
    _preemptionTimerCached = true;
  }

  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = HAL->timer.cancelAndGet(
    _cachedPreemptionTimer, NULL, &remainingNanoseconds, &callback);


  bool success = false;
  if (*((uint32_t*) ptr) == *((uint32_t*) expected)) {
    *((uint32_t*) ptr) = desired;
    success = true;
  } else {
    *((uint32_t*) expected) = *((uint32_t*) ptr);
  }

  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer.configOneShot(_cachedPreemptionTimer,
      remainingNanoseconds, callback);
  }

  return success;
}

#endif // defined(__arm__)

