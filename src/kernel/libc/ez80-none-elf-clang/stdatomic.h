///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              10.16.2026
///
/// @file              stdatomic.h
///
/// @brief             eZ80 implementation of stdatomic.h.  Found ahead of
///                    the generic common/ version via the AgonLight2 include
///                    path order.
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


#if !defined(NANO_OS_STDATOMIC_H)
#define NANO_OS_STDATOMIC_H

// Standard C includes
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define _Atomic(type) type

/// @fn void* atomic_load(const volatile void *object)
///
/// @brief Atomically load a pointer.  A plain access is enough: the eZ80 loads
/// a 24-bit pointer in a single instruction, which an interrupt can't split.
///
/// @param object A pointer to the pointer to load.
///
/// @return Returns the value of the pointer.
static inline void* atomic_load(const volatile void *object) {
  return *((void * const volatile *) object);
}

/// @fn void atomic_store(volatile void *object, void *desired)
///
/// @brief Atomically store a pointer.  A plain access is enough: the eZ80
/// stores a 24-bit pointer in a single instruction, which an interrupt can't
/// split.
///
/// @param object A pointer to the pointer to store to.
/// @param desired The value to store.
///
/// @return This function returns no value.
static inline void atomic_store(volatile void *object, void *desired) {
  *((void * volatile *) object) = desired;
}

bool atomic_compare_exchange_strong(
  volatile void *object, void *expected, void *desired);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NANO_OS_STDATOMIC_H

