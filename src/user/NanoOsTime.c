////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file NanoOsTime.c
///
/// @brief Kernel-side implementation of Unix time.h functionality.

#include "NanoOsTime.h"
#include "../kernel/Hal.h"
#include "../kernel/NanoOs.h"

// Standard C includes
#include <stddef.h> // For NULL

#define SECONDS_PER_MINUTE       ((time_t) 60)
#define SECONDS_PER_HOUR         ((time_t) (SECONDS_PER_MINUTE * ((time_t) 60)))
#define SECONDS_PER_DAY          ((time_t) (SECONDS_PER_HOUR   * ((time_t) 24)))

/// @var _timezone
///
/// @brief Implementation of the standard C timezone global variable.
static long _timezone = (8 * SECONDS_PER_HOUR);

/// @var dstInEffect
///
/// @brief 1 if Daylight Savings Time (DST) is in effect, 0 if it's not, -1 if
/// we don't know.  Initialize to -1 until proven otherwise.
static int _dstInEffect = 1;

/// @fn long* nanoOsTimezone(void)
///
/// @brief Get the address of the global _timezone variable.
///
/// @return Returns the address of the global _timezone variable.
long* nanoOsTimezone(void) {
  return &_timezone;
}

/// @fn int* nanoOsDstInEffect(void)
///
/// @brief Get the address of the global _dstInEffect variable.
///
/// @return Returns the address of the global _dstInEffect variable.
int* nanoOsDstInEffect(void) {
  return &_dstInEffect;
}

/// @fn time_t nanoOsTime(time_t *tloc)
///
/// @brief NanoOs implementation of the standard time function.
///
/// @param tloc A pointer to a time_t value to populate with the result.  This
///   parameter may be NULL.
///
/// @return Returns the number of seconds that have elapsed since midnight,
/// January 1st, 1970.
time_t nanoOsTime(time_t *tloc) {
  int64_t elapsedTime = 0;
  HAL->clock->getElapsedNanoseconds(0, &elapsedTime);
  if (tloc != NULL) {
    *tloc = (time_t) (elapsedTime / 1000000000LL);
    return *tloc;
  }

  return (time_t) (elapsedTime / 1000000000LL);
}

/// @fn struct tm* nanoOsGmtime_r(const time_t *timep, struct tm *result)
///
/// @brief NanoOs implementation of the gmtime_r function.
///
/// @param timep A pointer to the time_t value to break down.
/// @param result A pointer to the struct tm to store the results in.
///
/// @return This function always succeeds and always returns the value of the
/// result pointer provided.
struct tm* nanoOsGmtime_r(const time_t *timep, struct tm *result) {
  if ((timep == NULL) || (result == NULL)) {
    // Don't bother trying to parse anything.
    return NULL;
  }

  // Floor division, so that times before the epoch land on the previous day.
  time_t days = *timep / SECONDS_PER_DAY;
  time_t secondOfDay = *timep % SECONDS_PER_DAY;
  if (secondOfDay < 0) {
    secondOfDay += SECONDS_PER_DAY;
    days--;
  }

  result->tm_hour = (int) (secondOfDay / SECONDS_PER_HOUR);
  result->tm_min = (int) ((secondOfDay % SECONDS_PER_HOUR)
    / SECONDS_PER_MINUTE);
  result->tm_sec = (int) (secondOfDay % SECONDS_PER_MINUTE);

  // January 1, 1970 was a Thursday.
  result->tm_wday = (int) (((days % 7) + 11) % 7);

  // Howard Hinnant's civil_from_days algorithm, on a calendar whose years
  // start on March 1 so that a leap day is the last day of its year.
  long marchDays = (long) days + 719468L;
  long era = ((marchDays >= 0) ? marchDays : (marchDays - 146096L)) / 146097L;
  long dayOfEra = marchDays - (era * 146097L);
  long yearOfEra = (dayOfEra - (dayOfEra / 1460L) + (dayOfEra / 36524L)
    - (dayOfEra / 146096L)) / 365L;
  long dayOfYear = dayOfEra
    - ((365L * yearOfEra) + (yearOfEra / 4L) - (yearOfEra / 100L));
  long monthIndex = ((5L * dayOfYear) + 2L) / 153L;
  long month = (monthIndex < 10L) ? (monthIndex + 3L) : (monthIndex - 9L);
  long year = yearOfEra + (era * 400L) + (month <= 2L);
  bool leapYear = ((year % 4L) == 0L)
    && (((year % 100L) != 0L) || ((year % 400L) == 0L));

  result->tm_year = (int) (year - 1900L);
  result->tm_mon = (int) (month - 1L);
  result->tm_mday = (int) (dayOfYear - (((153L * monthIndex) + 2L) / 5L) + 1L);
  result->tm_yday = (int) ((month <= 2L)
    ? (dayOfYear - 306L) : (dayOfYear + 59L + leapYear));
  result->tm_isdst = dstInEffect;

  return result;
}

/// @fn struct tm* nanoOsLocaltime_r(const time_t *timep, struct tm *result)
///
/// @brief NanoOs implementation of the localtime_r function.
///
/// @param timep A pointer to the time_t value to break down.
/// @param result A pointer to the struct tm to store the results in.
///
/// @return This function always succeeds and always returns the value of the
/// result pointer provided.
struct tm* nanoOsLocaltime_r(const time_t *timep, struct tm *result) {
  time_t timev = *timep;
  timev -= (time_t) _timezone;
  timev += (time_t) ((_dstInEffect > 0) * 3600);
  return gmtime_r(&timev, result);
}

/// @fn int nanoOsTimespec_get(struct timespec* spec, int base)
///
/// @brief Get the current time in the form of a struct timespec.
///
/// @param spec A pointer to the sturct timespec to populate.
/// @param base The base for the time (TIME_UTC).
///
/// @return Returns the value of base on success, 0 on failure.
int nanoOsTimespec_get(struct timespec* spec, int base) {
  if (spec == NULL) {
    return 0;
  }
  
  int64_t now = 0;
  HAL->clock->getElapsedNanoseconds(0, &now);
  spec->tv_sec = (time_t) (now / ((int64_t) 1000000000));
  spec->tv_nsec = now % ((int64_t) 1000000000);

  return base;
}

