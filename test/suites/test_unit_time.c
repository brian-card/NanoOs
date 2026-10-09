///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_time.c
///
/// @brief             Plain unit tests (no kernel boot) for the C library's
///                    time conversions.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include "user/NanoOsTime.h"

/// @fn static void assertGmtime(time_t t, int year, int mon, int mday,
///   int hour, int min, int sec, int wday, int yday)
///
/// @brief Assert every calendar field nanoOsGmtime_r produces for a time.
///
/// @param t The time to convert.
/// @param year The expected tm_year.
/// @param mon The expected tm_mon.
/// @param mday The expected tm_mday.
/// @param hour The expected tm_hour.
/// @param min The expected tm_min.
/// @param sec The expected tm_sec.
/// @param wday The expected tm_wday.
/// @param yday The expected tm_yday.
///
/// @return This function returns no value.
static void assertGmtime(time_t t, int year, int mon, int mday,
  int hour, int min, int sec, int wday, int yday
) {
  struct tm result;
  NANO_OS_ASSERT_TRUE(nanoOsGmtime_r(&t, &result) == &result);
  NANO_OS_ASSERT_EQ_INT(year, result.tm_year);
  NANO_OS_ASSERT_EQ_INT(mon, result.tm_mon);
  NANO_OS_ASSERT_EQ_INT(mday, result.tm_mday);
  NANO_OS_ASSERT_EQ_INT(hour, result.tm_hour);
  NANO_OS_ASSERT_EQ_INT(min, result.tm_min);
  NANO_OS_ASSERT_EQ_INT(sec, result.tm_sec);
  NANO_OS_ASSERT_EQ_INT(wday, result.tm_wday);
  NANO_OS_ASSERT_EQ_INT(yday, result.tm_yday);
}

NANO_OS_TEST(unit_time, gmtime_converts_the_epoch) {
  assertGmtime(0, 70, 0, 1, 0, 0, 0, 4, 0);
}

NANO_OS_TEST(unit_time, gmtime_converts_times_before_the_epoch) {
  assertGmtime(-1, 69, 11, 31, 23, 59, 59, 3, 364);
  assertGmtime(-28800, 69, 11, 31, 16, 0, 0, 3, 364);
  assertGmtime(-86400, 69, 11, 31, 0, 0, 0, 3, 364);
  assertGmtime(-31536001, 68, 11, 31, 23, 59, 59, 2, 365);
  assertGmtime(-2208988800LL, 0, 0, 1, 0, 0, 0, 1, 0);
}

NANO_OS_TEST(unit_time, gmtime_handles_leap_days_and_centuries) {
  assertGmtime(951782400, 100, 1, 29, 0, 0, 0, 2, 59);
  assertGmtime(951868800, 100, 2, 1, 0, 0, 0, 3, 60);
  assertGmtime(4107456000LL, 200, 1, 28, 0, 0, 0, 0, 58);
  assertGmtime(4107542400LL, 200, 2, 1, 0, 0, 0, 1, 59);
}

NANO_OS_TEST(unit_time, gmtime_converts_recent_times) {
  assertGmtime(1704067199, 123, 11, 31, 23, 59, 59, 0, 364);
  assertGmtime(1791489600, 126, 9, 8, 20, 0, 0, 4, 280);
}
