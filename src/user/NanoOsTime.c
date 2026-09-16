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

// Standard C includes
#include <stddef.h> // For NULL

#define AVERAGE_HOURS_PER_YEAR   ((time_t) ((365 * 24) + 6))
#define HOURS_PER_NORMAL_YEAR    (365 * 24)
#define HOURS_PER_LEAP_YEAR      (366 * 24)
#define SECONDS_PER_MINUTE       ((time_t) 60)
#define SECONDS_PER_HOUR         ((time_t) (SECONDS_PER_MINUTE * ((time_t) 60)))
#define SECONDS_PER_DAY          ((time_t) (SECONDS_PER_HOUR   * ((time_t) 24)))
#define AVERAGE_SECONDS_PER_YEAR \
  (((time_t) AVERAGE_HOURS_PER_YEAR) * SECONDS_PER_HOUR)

/// @var timezone
///
/// @brief Implementation of the standard C timezone global variable.
long timezone = 0;

/// @var dstInEffect
///
/// @brief 1 if Daylight Savings Time (DST) is in effect, 0 if it's not, -1 if
/// we don't know.  Initialize to -1 until proven otherwise.
int dstInEffect = -1;

/// @fn long* nanoOsTimezone(void)
///
/// @brief Get the value of the global timezone variable.
///
/// @return Returns the value of the global timezone variable.
long nanoOsTimezone(void) {
  return timezone;
}

/// @fn time_t time(time_t *tloc)
///
/// @brief NanoOs implementation of the standard time function.
///
/// @param tloc A pointer to a time_t value to populate with the result.  This
///   parameter may be NULL.
///
/// @return Returns the number of seconds that have elapsed since midnight,
/// January 1st, 1970.
time_t time(time_t *tloc) {
  int64_t elapsedTime = 0;
  HAL->clock.getElapsedNanoseconds(0, &elapsedTime);
  if (tloc != NULL) {
    *tloc = (time_t) (elapsedTime / 1000000000LL);
    return *tloc;
  }

  return (time_t) (elapsedTime / 1000000000LL);
}

/// @fn struct tm* gmtime_r(const time_t *timep, struct tm *result)
///
/// @brief NanoOs implementation of the gmtime_r function.
///
/// @param timep A pointer to the time_t value to break down.
/// @param result A pointer to the struct tm to store the results in.
///
/// @return This function always succeeds and always returns the value of the
/// result pointer provided.
struct tm* gmtime_r(const time_t *timep, struct tm *result) {
  if ((timep == NULL) || (result == NULL)) {
    // Don't bother trying to parse anything.
    return NULL;
  }

  time_t timev = *timep;
  int daysPerMonth[12] = {
    31, // January
    28, // February
    31, // March
    30, // April
    31, // May
    30, // June
    31, // July
    31, // August
    30, // September
    31, // October
    30, // November
    31, // December
  };

  time_t epochHours = timev / SECONDS_PER_HOUR;
  time_t epochYears = timev / AVERAGE_SECONDS_PER_YEAR;
  result->tm_year = ((int) epochYears) + 70;
  int lastLeapYear = result->tm_year & ~3;
  int yearsSinceLeapYear = result->tm_year & 3;
  int yearHour = (int) (epochHours
    - (((time_t) lastLeapYear) * AVERAGE_HOURS_PER_YEAR));
  if (yearsSinceLeapYear > 0) {
    yearHour -= HOURS_PER_LEAP_YEAR;
    yearsSinceLeapYear--;
    for (; yearsSinceLeapYear > 0; yearsSinceLeapYear--) {
      yearHour -= HOURS_PER_NORMAL_YEAR;
    }
  }

  int yearDay = yearHour / 24;
  result->tm_yday = yearDay;
  int month;
  for (month = 0; (month < 12) && (yearDay > 0); month++) {
    yearDay -= daysPerMonth[month];
  }
  month--;
  yearDay += daysPerMonth[month];
  result->tm_mon = month;
  result->tm_mday = yearDay + 1;

  result->tm_hour = yearHour - (result->tm_yday * 24);

  timev -= epochHours * SECONDS_PER_HOUR;
  result->tm_min = (int) (timev / SECONDS_PER_MINUTE);
  result->tm_sec = (int) (timev % SECONDS_PER_MINUTE);

  result->tm_wday = -1;
  result->tm_isdst = dstInEffect;

  return result;
}

/// @fn struct tm* localtime_r(const time_t *timep, struct tm *result)
///
/// @brief NanoOs implementation of the localtime_r function.
///
/// @param timep A pointer to the time_t value to break down.
/// @param result A pointer to the struct tm to store the results in.
///
/// @return This function always succeeds and always returns the value of the
/// result pointer provided.
struct tm* localtime_r(const time_t *timep, struct tm *result) {
  return gmtime_r(timep, result);
}

/// @fn int timespec_get(struct timespec* spec, int base)
///
/// @brief Get the current time in the form of a struct timespec.
///
/// @param spec A pointer to the sturct timespec to populate.
/// @param base The base for the time (TIME_UTC).
///
/// @return Returns the value of base on success, 0 on failure.
int timespec_get(struct timespec* spec, int base) {
  if (spec == NULL) {
    return 0;
  }
  
  int64_t now = 0;
  HAL->clock.getElapsedNanoseconds(0, &now);
  spec->tv_sec = (time_t) (now / ((int64_t) 1000000000));
  spec->tv_nsec = now % ((int64_t) 1000000000);

  return base;
}

