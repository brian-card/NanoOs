///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              10.21.2025
///
/// @file              stdlib.h
///
/// @brief             Functionality in the C stdlib library.
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

#ifndef STDLIB_H
#define STDLIB_H

#include <stdbool.h>
#include "NanoOsUser.h"

#ifdef __cplusplus
extern "C"
{
#endif

static inline void free(void *ptr) {
  overlayMap.header.osApi->free(ptr);
}
static inline void* realloc(void *ptr, size_t size) {
  return overlayMap.header.osApi->realloc(ptr, size);
}
static inline void* malloc(size_t size) {
  return overlayMap.header.osApi->malloc(size);
}
static inline void* calloc(size_t nmemb, size_t size) {
  return overlayMap.header.osApi->calloc(nmemb, size);
}

static inline char *getenv(const char *name) {
  // The 'args' parameter of callOverlayFunction is a void* without the 'const'
  // qualifier, so we have to cast it here to avoid a compiler warning.
  return overlayMap.header.osApi->callOverlayFunction(
    "/usr/lib/stdlib", "getenv", "getenv", (void*) name);
}

static inline long strtol(const char *nptr, char **endptr, int base) {
  unsigned long long returnValue = (unsigned long long)
    overlayMap.header.osApi->strtoll(nptr, endptr, base);

  if (sizeof(long long) > sizeof(long)) {
    unsigned long max = (unsigned long) -1;
    if (returnValue > max) {
      bool negative
        = ((returnValue & (1ULL << ((sizeof(long long) << 3) - 1))));
      returnValue = 1LL << ((sizeof(long) << 3) - 1); // LONG_MIN
      if (!negative) {
        returnValue--; // LONG_MAX
      }
    }
  }

  return (long) returnValue;
}
static inline long long strtoll(const char *nptr, char **endptr, int base) {
  return overlayMap.header.osApi->strtoll(nptr, endptr, base);
}

#ifdef __cplusplus
}
#endif

#endif // STDLIB_H

