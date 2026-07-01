///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              06.30.2026
///
/// @file              stdatomic.h
///
/// @brief             NanoOs-specific implementation of stdatomic.h.
///
/// @details
/// This header and its accompanying library implementation are a hack - and a
/// bad one at that.  This exists because some NanoOs target environments use
/// compilers that don't provide complete and/or standard-compliant C
/// implementations.
///
/// stdatomic.h is intended to use C17 generics.  This library does *NOT* use
/// C17 generics.  If I was intending to fully support stdatomic.h, I would
/// obviously have to use them.  As it is, atomic operations are only used by
/// the Coroutines library in a few places and only three of them are used.
//
/// The signatures I used for these functions are, quite honestly, poor.  It's
/// unbelievably bad practice to have void* arguments that are then cast to
/// double pointers.  However, no thanks to the way that C treats double void
/// pointers, this was the only realistic option.  Technically, since the calls
/// that are made to these functions only ever use pointers to Coroutine
/// structures, I could have made that the parameter type, but that seemed like
/// an even worse idea if I ever have to use these calls for something else
/// again later.
///
/// Hopefully, I don't have to make this library more full-featured.  For now,
/// it is what it is and I'm not going to put any more effort into this.
///
/// JBC 2026-06-30
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

void* atomic_load(const volatile void *object);
void atomic_store(volatile void *object, void *desired);
bool atomic_compare_exchange_strong(
  volatile void *object, void *expected, void *desired);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NANO_OS_STDATOMIC_H

