///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              06.30.2026
///
/// @file              stdatomic.c
///
/// @brief             NanoOs implementation of stdatomic.h functions missing
///                    from standard C library implementations.
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


#ifdef NANO_OS_COMMON_STDATOMIC_C

#include "../../../include/common/stdatomic.h"
#include "../../Hal.h"
#include "../../NanoOsTypes.h"
#include "../../Scheduler.h"

void* atomic_load(const volatile void *object) {
  void **objectPtr = (void**) object;
  
  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = -1;
  if (HAL->timer != NULL) {
    cancelStatus = HAL->timer->cancelAndGet(
      SCHEDULER_STATE->preemptionTimer, NULL, &remainingNanoseconds, &callback);
  }
  
  void *returnValue = *objectPtr;
  
  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer->configOneShot(SCHEDULER_STATE->preemptionTimer,
      remainingNanoseconds, callback);
  }
  
  return returnValue;
}

void atomic_store(volatile void *object, void *desired) {
  void **objectPtr = (void**) object;
  
  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = -1;
  if (HAL->timer != NULL) {
    cancelStatus = HAL->timer->cancelAndGet(
      SCHEDULER_STATE->preemptionTimer, NULL, &remainingNanoseconds, &callback);
  }
  
  *objectPtr = desired;
  
  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer->configOneShot(SCHEDULER_STATE->preemptionTimer,
      remainingNanoseconds, callback);
  }
}

bool atomic_compare_exchange_strong(
  volatile void *object, void *expected, void *desired
) {
  void **objectPtr = (void**) object;
  void **expectedPtr = (void**) expected;
  
  uint64_t remainingNanoseconds;
  void (*callback)(void);
  int cancelStatus = -1;
  if (HAL->timer != NULL) {
    cancelStatus = HAL->timer->cancelAndGet(
      SCHEDULER_STATE->preemptionTimer, NULL, &remainingNanoseconds, &callback);
  }
  
  bool success = false;
  if (*objectPtr == *expectedPtr) {
    *objectPtr = desired;
    success = true;
  } else {
    *expectedPtr = *objectPtr;
  }
  
  if (cancelStatus == 0) {
    // A timer was active when we were called.  Restore it.
    HAL->timer->configOneShot(SCHEDULER_STATE->preemptionTimer,
      remainingNanoseconds, callback);
  }
  
  return success;
}

#endif // NANO_OS_COMMON_STDATOMIC_C

