///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.02.2025
///
/// @file              NanoOsThreads.h
///
/// @brief             NanoOs implementation of the C standard threads.h.
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

#ifndef NANO_OS_THREADS_H
#define NANO_OS_THREADS_H

#include "../kernel/CoroutineSync.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Mutex support.

// Mutex types
#define mtx_plain     comutexPlain
#define mtx_recursive comutexRecursive
#define mtx_timed     comutexTimed

typedef Comutex mtx_t;

#define mtx_init(mtx, type) comutexInit(mtx, type)
#define mtx_lock(mtx) comutexLock(mtx)
#define mtx_unlock(mtx) comutexUnlock(mtx)
#define mtx_destroy(mtx) comutexDestroy(mtx)
#define mtx_timedlock(mtx, ts) comutexTimedLock(mtx, ts)
#define mtx_trylock(mtx) comutexTryLock(mtx)

// Condition support.

typedef Cocondition cnd_t;

#define cnd_broadcast(cond) coconditionBroadcast(cond)
#define cnd_destroy(cond) coconditionDestroy(cond)
#define cnd_init(cond) coconditionInit(cond)
#define cnd_signal(cond) coconditionSignal(cond)
#define cnd_timedwait(cond, mtx, ts) coconditionTimedWait(cond, mtx, ts)
#define cnd_wait(cond, mtx) coconditionWait(cond, mtx)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NANO_OS_THREADS_H
