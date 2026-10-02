///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              04.02.2025
///
/// @file              CoroutineSync.h
///
/// @brief             Synchronization primitives for coroutines for C.
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

#ifndef COROUTINE_SYNC_H
#define COROUTINE_SYNC_H

#include "CAtomic.h"

#include "time.h"
#include "stdint.h"

#include "CoroutineConfig.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Forward declarations
typedef struct Coroutine Coroutine, coro_s, *coro_t;

// Mutex support.

// Mutex types
#define comutexPlain     0
#define comutexRecursive 1
#define comutexTimed     2

/// @struct Comutex
///
/// @brief Definition for a coroutine mutex to provide mutual exclusion between
/// coroutines.
///
/// @param lastYieldValue The last value returned by a yield call if a comutex
///   lock function blocked while acquiring the lock, or NULL if the lock was
///   acquired on the first attempt.
/// @param type The type of the mutex (see above type definitions)
/// @param coroutine The coroutine that has the lock.
/// @param recursionLevel The number of times this mutex has been successfully
///   locked in this coroutine.
/// @param head The next coroutine in the queue to lock this mutex.
/// @param tail The last coroutine in the lock queue, after which the next
///   coroutine will be added on a lock.
/// @param timeoutTime The time at which a call to comutexTimedLock will
///   timeout.
typedef struct Comutex {
  void *lastYieldValue;
  int type;
  _Atomic(Coroutine*) coroutine;
  int recursionLevel;
  Coroutine *head;
  Coroutine *tail;
  int64_t timeoutTime;
} Comutex, coro_mtx_t;

int comutexInit(Comutex *mtx, int type);
int comutexLock(Comutex *mtx);
int comutexUnlock(Comutex *mtx);
void comutexDestroy(Comutex *mtx);
int comutexTimedLock(Comutex *mtx, const struct timespec *ts);
int comutexTryLock(Comutex *mtx);
void* comutexLastYieldValue(Comutex *mtx);

// Condition support.

/// @struct Cocondition
///
/// @brief Definition for a coroutine condition for signaling between
/// coroutines.
///
/// @param lastYieldValue The last value returned by a yield call while a
///   cocondition wait function was blocked.
/// @param numWaiters The number of coroutines blocked waiting on this
///   condition.
/// @param numSignal The number of signals emitted for unblocking waiting
///   coroutines.
/// @param head The head of the coroutine queue (the next coroutine to signal).
/// @param tail The tail of the coroutine queue (where the next waiting
///   coroutine will be added).
/// @param timeoutTime The time at which a call to coconditionTimedWait will
///   timeout.
typedef struct Cocondition {
  void *lastYieldValue;
  int numWaiters;
  int numSignals;
  Coroutine *head;
  Coroutine *tail;
  int64_t timeoutTime;
} Cocondition, coro_cnd_t;

int coconditionBroadcast(Cocondition *cond);
void coconditionDestroy(Cocondition *cond);
int coconditionInit(Cocondition *cond);
int coconditionSignal(Cocondition *cond);
int coconditionTimedWait(Cocondition *cond, Comutex *mtx,
  const struct timespec *ts);
int coconditionWait(Cocondition *cond, Comutex *mtx);
void* coconditionLastYieldValue(Cocondition *cond);


#ifdef __cplusplus
} // extern "C"
#endif

#if !defined(SINGLE_CORE_COROUTINES) && !defined(THREAD_SAFE_COROUTINES)
#define THREAD_SAFE_COROUTINES
#include "CThreads.h"
#endif
#if defined(SINGLE_CORE_COROUTINES) && defined(THREAD_SAFE_COROUTINES)
#error "Only one of SINGLE_CORE_COROUTINES or THREAD_SAFE_COROUTINES may be defined."
#endif

#endif // COROUTINE_SYNC_H
