///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              09.14.2026
///
/// @file              NanoOsTime.h
///
/// @brief             Definitions in support of the standard POSIX time.h
///                    functionality.
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

#ifndef NANO_OS_USER_TIME_H
#define NANO_OS_USER_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @def TIME_UTC
///
/// @brief Value to be passed to the "base" parameter of the timespec_get
/// function.
#ifndef TIME_UTC
#define TIME_UTC 1
#endif

// time_t, struct tm and struct timespec are all guarded against a *real*
// libc's own definitions of the same, using that libc's own guard macros
// (glibc's __time_t_defined/__struct_tm_defined/_STRUCT_TIMESPEC, plus the
// newlib equivalents), rather than this header's own top-of-file include
// guard. This header is reached early in every NanoOs translation unit --
// via Coroutines.h et al -- so on a POSIX build it normally wins the race
// and its guard defines below are what a real libc header defers to when
// it's dragged in later (e.g. transitively, through stdlib.h's sys/types.h
// -> sys/select.h -> struct_timespec.h, with no time.h in sight). The one
// deliberate exception is src/hal/HalPosixImpl.c, the actual backend of the
// POSIX simulator's HAL: it includes the real </usr/include/time.h> ahead
// of anything of ours specifically so that it -- alone -- ends up using the
// host's real types and real time functions (clock_gettime, nanosleep,
// etc.) instead of NanoOs's.
#if !defined(__time_t_defined) && !defined(_TIME_T_DECLARED)
typedef int64_t time_t;
#define __time_t_defined
#define _TIME_T_DECLARED
#endif

#if !defined(__struct_tm_defined) && !defined(_STRUCT_TM_DECLARED)
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
#define __struct_tm_defined
#define _STRUCT_TM_DECLARED
#endif

#if !defined(_STRUCT_TIMESPEC) && !defined(_SYS__TIMESPEC_H_)
struct timespec {
  time_t tv_sec;
  long   tv_nsec;
};
#define _STRUCT_TIMESPEC 1
#define _SYS__TIMESPEC_H_
#endif

long* nanoOsTimezone(void);
#define timezone (*nanoOsTimezone())
time_t nanoOsTime(time_t *tloc);
#define time(tloc) nanoOsTime(tloc)
struct tm* nanoOsGmtime_r(const time_t *timep, struct tm *result);
#define gmtime_r(timep, result) nanoOsGmtime_r(timep, result)
struct tm* nanoOsLocaltime_r(const time_t *timep, struct tm *result);
#define localtime_r(timep, result) nanoOsLocaltime_r(timep, result)
int nanoOsTimespec_get(struct timespec* spec, int base);
#define timespec_get(spec, base) nanoOsTimespec_get(spec, base)

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_TIME_H

