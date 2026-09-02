///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_misc.c
///
/// @brief             Plain unit tests (no kernel boot) for small, pure-ish
///                    kernel helpers.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/NanoOs.h"

// -------------------------------------------------------------------------
// raiseUInt - integer power.
// -------------------------------------------------------------------------

NANO_OS_TEST(unit_math, raiseUInt_basic_powers) {
  NANO_OS_ASSERT_EQ_INT(1u,     raiseUInt(10, 0));
  NANO_OS_ASSERT_EQ_INT(10u,    raiseUInt(10, 1));
  NANO_OS_ASSERT_EQ_INT(1000u,  raiseUInt(10, 3));
  NANO_OS_ASSERT_EQ_INT(1u,     raiseUInt(1, 100));
  NANO_OS_ASSERT_EQ_INT(1024u,  raiseUInt(2, 10));
  NANO_OS_ASSERT_EQ_INT(1u,     raiseUInt(0, 0)); // 0^0 conventionally 1
  NANO_OS_ASSERT_EQ_INT(0u,     raiseUInt(0, 5));
}

// -------------------------------------------------------------------------
// getUserIdByUsername / getUsernameByUserId - the built-in user table.
// -------------------------------------------------------------------------

NANO_OS_TEST(unit_users, lookup_round_trips) {
  NANO_OS_ASSERT_EQ_INT(0, getUserIdByUsername("root"));
  NANO_OS_ASSERT_STR_EQ("root", getUsernameByUserId(0));

  UserId u1 = getUserIdByUsername("user1");
  NANO_OS_ASSERT_NE_INT(-1, u1);
  NANO_OS_ASSERT_STR_EQ("user1", getUsernameByUserId(u1));
}

NANO_OS_TEST(unit_users, unknown_user_is_reported_consistently) {
  NANO_OS_ASSERT_EQ_INT(NO_USER_ID, getUserIdByUsername("nosuchuser"));
  // An unowned uid maps to a well-known sentinel name, not a crash / NULL.
  const char *name = getUsernameByUserId(31337);
  NANO_OS_ASSERT_NOT_NULL(name);
}

// -------------------------------------------------------------------------
// timespecFromDelay - build a struct timespec from a millisecond delay.
// -------------------------------------------------------------------------

// Documents BUG-1 (test/BUGS.txt): timespecFromDelay double-counts whole
// seconds and never normalises tv_nsec back below 1e9.
NANO_OS_TEST_TODO(unit_time, timespecFromDelay_splits_ms_into_s_and_ns,
  "BUG-1: timespecFromDelay double-counts seconds, tv_nsec not normalised") {
  struct timespec ts;

  memset(&ts, 0, sizeof(ts));
  timespecFromDelay(&ts, 0);
  NANO_OS_ASSERT_EQ_INT(0, ts.tv_sec);
  NANO_OS_ASSERT_EQ_INT(0, ts.tv_nsec);

  memset(&ts, 0, sizeof(ts));
  timespecFromDelay(&ts, 1500);
  // 1500 ms == 1 s + 500 ms.  tv_nsec must be a valid < 1e9 value.
  NANO_OS_ASSERT_TRUE(ts.tv_nsec >= 0);
  NANO_OS_ASSERT_TRUE(ts.tv_nsec < 1000000000L);
  long long totalMs = (long long) ts.tv_sec * 1000
    + (long long) ts.tv_nsec / 1000000;
  NANO_OS_ASSERT_EQ_INT(1500, totalMs);
}
