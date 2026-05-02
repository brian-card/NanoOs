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

bool __atomic_compare_exchange_2(void *ptr, void *expected, uint16_t desired,
  bool weak, int success_memorder, int failure_memorder
) {
  (void) weak;
  (void) success_memorder;
  (void) failure_memorder;
  
  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = -1;
  if (HAL->timer != NULL) {
    cancelStatus = HAL->timer->cancelAndGetTimer(
      SCHEDULER_STATE->preemptionTimer, NULL, &remainingNanoseconds, &callback);
  }
  
  bool success = false;
  if (*((uint16_t*) ptr) == *((uint16_t*) expected)) {
    *((uint16_t*) ptr) = desired;
    success = true;
  } else {
    *((uint16_t*) expected) = *((uint16_t*) ptr);
  }
  
  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer->configOneShotTimer(SCHEDULER_STATE->preemptionTimer,
      remainingNanoseconds, callback);
  }
  
  return success;
}

void __atomic_store_2(void *ptr, uint16_t val, int memorder) {
  (void) memorder;
  
  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = -1;
  if (HAL->timer != NULL) {
    cancelStatus = HAL->timer->cancelAndGetTimer(
      SCHEDULER_STATE->preemptionTimer, NULL, &remainingNanoseconds, &callback);
  }
  
  *((uint16_t*) ptr) = val;
  
  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer->configOneShotTimer(SCHEDULER_STATE->preemptionTimer,
      remainingNanoseconds, callback);
  }
}

uint16_t __atomic_load_2(const void *ptr, int memorder) {
  (void) memorder;
  
  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = -1;
  if (HAL->timer != NULL) {
    cancelStatus = HAL->timer->cancelAndGetTimer(
      SCHEDULER_STATE->preemptionTimer, NULL, &remainingNanoseconds, &callback);
  }
  
  uint16_t returnValue = *((uint16_t*) ptr);
  
  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer->configOneShotTimer(SCHEDULER_STATE->preemptionTimer,
      remainingNanoseconds, callback);
  }
  
  return returnValue;
}

#endif // defined(__AVR__)

