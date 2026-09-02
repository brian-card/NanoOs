///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_strtoll.c
///
/// @brief             Plain unit tests for nanoOsStrtoll (NanoOs' hand-rolled
///                    strtoll).
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include "user/NanoOsLibC.h"

// Several tests below are KNOWN FAILURES documenting BUG-2 (see test/BUGS.txt):
// nanoOsStrtoll values letter digits 10 too low, so every base > 10 is wrong
// and trailing letters are swallowed as digits in base 10.  They are left red
// on purpose.

NANO_OS_TEST(strtoll, decimal_basic) {
  NANO_OS_ASSERT_EQ_INT(0,      nanoOsStrtoll("0", NULL, 10));
  NANO_OS_ASSERT_EQ_INT(42,     nanoOsStrtoll("42", NULL, 10));
  NANO_OS_ASSERT_EQ_INT(-7,     nanoOsStrtoll("-7", NULL, 10));
  NANO_OS_ASSERT_EQ_INT(123,    nanoOsStrtoll("+123", NULL, 10));
  NANO_OS_ASSERT_EQ_INT(5,      nanoOsStrtoll("   5", NULL, 10));
}

NANO_OS_TEST_TODO(strtoll, endptr_points_past_the_number,
  "BUG-2: nanoOsStrtoll values letter digits 10 too low") {
  char *end = NULL;
  long long v = nanoOsStrtoll("100abc", &end, 10);
  NANO_OS_ASSERT_EQ_INT(100, v);
  NANO_OS_ASSERT_NOT_NULL(end);
  NANO_OS_ASSERT_STR_EQ("abc", end);
}

NANO_OS_TEST_TODO(strtoll, hexadecimal_digits_af,
  "BUG-2: nanoOsStrtoll values letter digits 10 too low") {
  // 'a'..'f' / 'A'..'F' must map to 10..15.
  NANO_OS_ASSERT_EQ_INT(255,   nanoOsStrtoll("ff", NULL, 16));
  NANO_OS_ASSERT_EQ_INT(255,   nanoOsStrtoll("FF", NULL, 16));
  NANO_OS_ASSERT_EQ_INT(2748,  nanoOsStrtoll("abc", NULL, 16));
  NANO_OS_ASSERT_EQ_INT(0xdead, nanoOsStrtoll("dead", NULL, 16));
}

NANO_OS_TEST_TODO(strtoll, base0_autodetects_hex_prefix,
  "BUG-2: nanoOsStrtoll values letter digits 10 too low") {
  NANO_OS_ASSERT_EQ_INT(255,   nanoOsStrtoll("0xff", NULL, 0));
  NANO_OS_ASSERT_EQ_INT(16,    nanoOsStrtoll("0x10", NULL, 0));
}

NANO_OS_TEST(strtoll, base0_autodetects_octal) {
  NANO_OS_ASSERT_EQ_INT(8,     nanoOsStrtoll("010", NULL, 0));
  NANO_OS_ASSERT_EQ_INT(83,    nanoOsStrtoll("0123", NULL, 0));
}

NANO_OS_TEST_TODO(strtoll, base36,
  "BUG-2: nanoOsStrtoll values letter digits 10 too low") {
  NANO_OS_ASSERT_EQ_INT(35,    nanoOsStrtoll("z", NULL, 36));
  NANO_OS_ASSERT_EQ_INT(71,    nanoOsStrtoll("1z", NULL, 36));
}

NANO_OS_TEST_TODO(strtoll, letter_is_not_a_decimal_digit,
  "BUG-2: nanoOsStrtoll values letter digits 10 too low") {
  // "a" in base 10 is not a digit: value 0, endptr left at 'a'.
  char *end = NULL;
  long long v = nanoOsStrtoll("a", &end, 10);
  NANO_OS_ASSERT_EQ_INT(0, v);
  NANO_OS_ASSERT_NOT_NULL(end);
  NANO_OS_ASSERT_STR_EQ("a", end);
}
