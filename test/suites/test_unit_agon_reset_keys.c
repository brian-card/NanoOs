///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_agon_reset_keys.c
///
/// @brief             Unit tests (no kernel boot) for the UART0 watch for
///                    Ctrl+Alt+Del.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "../../src/hal/AgonLight2/AgonResetKeys.c"

/// @def KEY
///
/// @brief The bytes of a VDP keyboard packet for a key going down or up.
#define KEY(keycode, down) 0x81, 0x04, 0x00, 0x00, (keycode), (down)

/// @fn static int feed(const uint8_t *bytes, size_t length, int *count)
///
/// @brief Feed bytes to a freshly zeroed AgonResetKeys.
///
/// @param bytes The bytes to feed.
/// @param length The number of bytes.
/// @param count Set to the number of bytes that completed a Ctrl+Alt+Del.
///
/// @return Returns the index of the last byte that completed a Ctrl+Alt+Del,
/// or -1 if none did.
static int feed(const uint8_t *bytes, size_t length, int *count) {
  AgonResetKeys keys;
  memset(&keys, 0, sizeof(keys));
  int completedAt = -1;
  *count = 0;
  for (size_t ii = 0; ii < length; ii++) {
    if (agonResetKeysScanByte(&keys, bytes[ii])) {
      completedAt = (int) ii;
      (*count)++;
    }
  }
  return completedAt;
}

NANO_OS_TEST(unit_agon_reset_keys, left_ctrl_left_alt_del_resets) {
  const uint8_t bytes[] = {
    KEY(KEYCODE_LCTRL, 1), KEY(KEYCODE_LALT, 1), KEY(KEYCODE_DELETE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(sizeof(bytes) - 1, feed(bytes, sizeof(bytes), &count));
  NANO_OS_ASSERT_EQ_INT(1, count);
}

NANO_OS_TEST(unit_agon_reset_keys, right_ctrl_right_alt_keypad_del_resets) {
  const uint8_t bytes[] = {
    KEY(KEYCODE_RALT, 1), KEY(KEYCODE_RCTRL, 1),
    KEY(KEYCODE_KEYPAD_DELETE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(sizeof(bytes) - 1, feed(bytes, sizeof(bytes), &count));
  NANO_OS_ASSERT_EQ_INT(1, count);
}

NANO_OS_TEST(unit_agon_reset_keys, left_ctrl_right_alt_del_resets) {
  const uint8_t bytes[] = {
    KEY(KEYCODE_LCTRL, 1), KEY(KEYCODE_RALT, 1), KEY(KEYCODE_DELETE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(sizeof(bytes) - 1, feed(bytes, sizeof(bytes), &count));
}

NANO_OS_TEST(unit_agon_reset_keys, del_without_both_modifiers_does_not_reset) {
  const uint8_t ctrlOnly[] = { KEY(KEYCODE_LCTRL, 1), KEY(KEYCODE_DELETE, 1) };
  const uint8_t altOnly[] = { KEY(KEYCODE_RALT, 1), KEY(KEYCODE_DELETE, 1) };
  const uint8_t neither[] = { KEY(KEYCODE_DELETE, 1) };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(-1, feed(ctrlOnly, sizeof(ctrlOnly), &count));
  NANO_OS_ASSERT_EQ_INT(-1, feed(altOnly, sizeof(altOnly), &count));
  NANO_OS_ASSERT_EQ_INT(-1, feed(neither, sizeof(neither), &count));
}

NANO_OS_TEST(unit_agon_reset_keys, ctrl_alt_with_another_key_does_not_reset) {
  const uint8_t bytes[] = {
    KEY(KEYCODE_LCTRL, 1), KEY(KEYCODE_LALT, 1), KEY(KEYCODE_BACKSPACE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(-1, feed(bytes, sizeof(bytes), &count));
}

NANO_OS_TEST(unit_agon_reset_keys, released_modifier_is_no_longer_held) {
  const uint8_t bytes[] = {
    KEY(KEYCODE_LCTRL, 1), KEY(KEYCODE_LALT, 1), KEY(KEYCODE_LCTRL, 0),
    KEY(KEYCODE_DELETE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(-1, feed(bytes, sizeof(bytes), &count));
}

NANO_OS_TEST(unit_agon_reset_keys, other_side_still_holds_after_a_release) {
  const uint8_t bytes[] = {
    KEY(KEYCODE_LCTRL, 1), KEY(KEYCODE_RCTRL, 1), KEY(KEYCODE_LCTRL, 0),
    KEY(KEYCODE_LALT, 1), KEY(KEYCODE_DELETE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(sizeof(bytes) - 1, feed(bytes, sizeof(bytes), &count));
}

NANO_OS_TEST(unit_agon_reset_keys, only_del_going_down_resets) {
  const uint8_t bytes[] = {
    KEY(KEYCODE_LCTRL, 1), KEY(KEYCODE_LALT, 1), KEY(KEYCODE_DELETE, 0),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(-1, feed(bytes, sizeof(bytes), &count));
}

NANO_OS_TEST(unit_agon_reset_keys, key_bytes_inside_another_packet_are_ignored) {
  const uint8_t bytes[] = {
    0x89, 0x0c, KEY(KEYCODE_LCTRL, 1), KEY(KEYCODE_LALT, 1),
    KEY(KEYCODE_DELETE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(-1, feed(bytes, sizeof(bytes), &count));
}

NANO_OS_TEST(unit_agon_reset_keys, poll_reply_missing_its_cookie_resyncs) {
  const uint8_t bytes[] = {
    0x80, 0x01, KEY(KEYCODE_LCTRL, 1), KEY(KEYCODE_LALT, 1),
    KEY(KEYCODE_DELETE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(sizeof(bytes) - 1, feed(bytes, sizeof(bytes), &count));
}

NANO_OS_TEST(unit_agon_reset_keys, header_with_a_bad_length_resyncs) {
  const uint8_t bytes[] = {
    0x81, 0x05, KEY(KEYCODE_LCTRL, 1), 0x83, 0x81, KEY(KEYCODE_LALT, 1),
    KEY(KEYCODE_DELETE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(sizeof(bytes) - 1, feed(bytes, sizeof(bytes), &count));
}

NANO_OS_TEST(unit_agon_reset_keys, other_packets_shaped_like_keys_are_ignored) {
  const uint8_t bytes[] = {
    0x89, 0x04, 0x00, 0x00, KEYCODE_LCTRL, 0x01,
    0x89, 0x04, 0x00, 0x00, KEYCODE_LALT, 0x01,
    KEY(KEYCODE_DELETE, 1),
  };
  int count = 0;

  NANO_OS_ASSERT_EQ_INT(-1, feed(bytes, sizeof(bytes), &count));
}
