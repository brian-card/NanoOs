///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              12.20.2025
///
/// @file              time.h
///
/// @brief             Functionality in the standard C time library.
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

#ifndef TIME_H
#define TIME_H

#include "NanoOsUser.h"

#undef timezone
#undef dstInEffect
#undef time
#undef gmtime_r
#undef localtime_r
#undef timespec_get

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef NANO_OS_USER_TIME_H

/// @def TIME_UTC
///
/// @brief Value to be passed to the "base" parameter of the timespec_get
/// function.
#define TIME_UTC 1

typedef int64_t time_t;

struct tm {
  int tm_sec;   // 0 to 60
  int tm_min;   // 0 to 59
  int tm_hour;  // 0 to 23
  int tm_mday;  // 1 to 31
  int tm_mon;   // 0 to 11
  int tm_year;  // since 1900
  int tm_wday;  // 0 to 6
  int tm_yday;  // 0 to 365
  int tm_isdst; // 1=yes, 0=no, -1=unknown
};

struct timespec {
  time_t tv_sec;
  long   tv_nsec;
};

#endif // NANO_OS_USER_TIME_H

static inline long* timezone_(void) {
  return overlayMap.header.osApi->nanoOsTimezone();
}
#define timezone (*timezone_())
static inline int* dstInEffect_(void) {
  return overlayMap.header.osApi->nanoOsDstInEffect();
}
#define dstInEffect (*dstInEffect_())
static inline time_t time(time_t *tloc) {
  return overlayMap.header.osApi->nanoOsTime(tloc);
}
static inline struct tm* gmtime_r(const time_t *timep, struct tm *result) {
  return overlayMap.header.osApi->nanoOsGmtime_r(timep, result);
}
static inline struct tm* localtime_r(const time_t *timep, struct tm *result) {
  return overlayMap.header.osApi->nanoOsLocaltime_r(timep, result);
}
static inline int timespec_get(struct timespec* spec, int base) {
  return overlayMap.header.osApi->nanoOsTimespec_get(spec, base);
}

#ifdef __cplusplus
}
#endif

#endif // TIME_H

