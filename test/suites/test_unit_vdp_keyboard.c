///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_vdp_keyboard.c
///
/// @brief             Unit tests (no kernel boot) for the VDP driver's packet
///                    parser and keyboard translation.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "../../usr/src/lib/vdp.drv/main/VdpKeyboard.c"

/// @fn static int feed(VdpPacketParser *parser, const uint8_t *bytes,
///   size_t length)
///
/// @brief Feed bytes to a packet parser.
///
/// @param parser A pointer to the VdpPacketParser.
/// @param bytes The bytes to feed.
/// @param length The number of bytes to feed.
///
/// @return Returns the index of the byte that completed a packet, or -1 if
/// none did.
static int feed(VdpPacketParser *parser, const uint8_t *bytes, size_t length) {
  int completedAt = -1;
  for (size_t ii = 0; ii < length; ii++) {
    if (vdpParseByte(parser, bytes[ii])) {
      completedAt = (int) ii;
    }
  }
  return completedAt;
}

/// @fn static size_t key(uint8_t ascii, uint8_t keycode, uint8_t down,
///   uint8_t *output)
///
/// @brief Translate a key event built from its parts.
///
/// @param ascii The key's ASCII value.
/// @param keycode The key's FabGL keycode.
/// @param down 1 if the key went down, 0 if it was released.
/// @param output A buffer of at least VDP_KEY_MAX_BYTES bytes.
///
/// @return Returns the number of bytes vdpTranslateKey wrote.
static size_t key(uint8_t ascii, uint8_t keycode, uint8_t down,
  uint8_t *output
) {
  uint8_t keyData[4] = { ascii, 0, keycode, down };
  return vdpTranslateKey(keyData, false, output);
}

/// @fn static size_t keypad(uint8_t ascii, uint8_t modifiers, uint8_t keycode,
///   bool numLock, uint8_t *output)
///
/// @brief Translate the press of a key with modifiers under a given driver Num
/// Lock.
///
/// @param ascii The key's ASCII value.
/// @param modifiers The AGON_KEYMOD_* bits of the packet.
/// @param keycode The key's FabGL keycode.
/// @param numLock Whether the driver's Num Lock is on.
/// @param output A buffer of at least VDP_KEY_MAX_BYTES bytes.
///
/// @return Returns the number of bytes vdpTranslateKey wrote.
static size_t keypad(uint8_t ascii, uint8_t modifiers, uint8_t keycode,
  bool numLock, uint8_t *output
) {
  uint8_t keyData[4] = { ascii, modifiers, keycode, 1 };
  return vdpTranslateKey(keyData, numLock, output);
}

/// @fn static bool release(uint8_t modifiers, uint8_t keycode, bool *numLock,
///   uint8_t *leds)
///
/// @brief Pass the release of a key to vdpUpdateLocks.
///
/// @param modifiers The AGON_KEYMOD_* bits of the packet.
/// @param keycode The key's FabGL keycode.
/// @param numLock A pointer to the driver's Num Lock.
/// @param leds Where vdpUpdateLocks puts the LED byte.
///
/// @return Returns what vdpUpdateLocks returns.
static bool release(uint8_t modifiers, uint8_t keycode, bool *numLock,
  uint8_t *leds
) {
  uint8_t keyData[4] = { 0, modifiers, keycode, 0 };
  return vdpUpdateLocks(keyData, numLock, leds);
}

NANO_OS_TEST(unit_vdp_keyboard, keyboard_packet_completes_on_its_last_byte) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t packet[] = { 0x81, 0x04, 'q', 0x00, KEYCODE_q, 0x01 };

  NANO_OS_ASSERT_EQ_INT(5, feed(&parser, packet, sizeof(packet)));
  NANO_OS_ASSERT_EQ_INT(VDP_PACKET_KEYBOARD, parser.command);
  NANO_OS_ASSERT_EQ_INT(4, parser.length);
  NANO_OS_ASSERT_TRUE(memcmp(parser.data, &packet[2], 4) == 0);
}

NANO_OS_TEST(unit_vdp_keyboard, other_packets_are_parsed_past) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t bytes[] = {
    0x86, 0x0a, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    0x80, 0x01, 0x4e,
    0x81, 0x04, 'x', 0x00, KEYCODE_x, 0x01,
  };

  NANO_OS_ASSERT_EQ_INT(sizeof(bytes) - 1, feed(&parser, bytes, sizeof(bytes)));
  NANO_OS_ASSERT_EQ_INT(VDP_PACKET_KEYBOARD, parser.command);
  NANO_OS_ASSERT_EQ_INT('x', parser.data[0]);
}

NANO_OS_TEST(unit_vdp_keyboard, stray_bytes_before_a_header_are_skipped) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t bytes[] = { 0x00, 0x41, 0x81, 0x04, 'z', 0x00, KEYCODE_z, 1 };

  NANO_OS_ASSERT_EQ_INT(7, feed(&parser, bytes, sizeof(bytes)));
  NANO_OS_ASSERT_EQ_INT('z', parser.data[0]);
}

NANO_OS_TEST(unit_vdp_keyboard, empty_packet_completes_on_its_length) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t bytes[] = { 0x8a, 0x00 };

  NANO_OS_ASSERT_EQ_INT(1, feed(&parser, bytes, sizeof(bytes)));
  NANO_OS_ASSERT_EQ_INT(0x0a, parser.command);
}

NANO_OS_TEST(unit_vdp_keyboard, printable_and_control_keys_send_their_ascii) {
  uint8_t output[VDP_KEY_MAX_BYTES];

  NANO_OS_ASSERT_EQ_INT(1, key('a', KEYCODE_a, 1, output));
  NANO_OS_ASSERT_EQ_INT('a', output[0]);
  NANO_OS_ASSERT_EQ_INT(1, key(0x03, KEYCODE_c, 1, output));
  NANO_OS_ASSERT_EQ_INT(0x03, output[0]);
  NANO_OS_ASSERT_EQ_INT(1, key('\r', KEYCODE_RETURN, 1, output));
  NANO_OS_ASSERT_EQ_INT('\r', output[0]);
}

NANO_OS_TEST(unit_vdp_keyboard, key_release_sends_nothing) {
  uint8_t output[VDP_KEY_MAX_BYTES];

  NANO_OS_ASSERT_EQ_INT(0, key('a', KEYCODE_a, 0, output));
  NANO_OS_ASSERT_EQ_INT(0, key(0, KEYCODE_UP, 0, output));
}

NANO_OS_TEST(unit_vdp_keyboard, arrows_send_escape_sequences_over_ascii) {
  uint8_t output[VDP_KEY_MAX_BYTES];

  NANO_OS_ASSERT_EQ_INT(3, key(0x0b, KEYCODE_UP, 1, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[A", 3) == 0);
  NANO_OS_ASSERT_EQ_INT(3, key(0x0a, KEYCODE_DOWN, 1, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[B", 3) == 0);
  NANO_OS_ASSERT_EQ_INT(3, key(0x15, KEYCODE_RIGHT, 1, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[C", 3) == 0);
  NANO_OS_ASSERT_EQ_INT(3, key(0x08, KEYCODE_LEFT, 1, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[D", 3) == 0);
}

NANO_OS_TEST(unit_vdp_keyboard, editing_and_function_keys_send_sequences) {
  uint8_t output[VDP_KEY_MAX_BYTES];

  NANO_OS_ASSERT_EQ_INT(4, key(0x7f, KEYCODE_DELETE, 1, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[3~", 4) == 0);
  NANO_OS_ASSERT_EQ_INT(3, key(0, KEYCODE_HOME, 1, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[H", 3) == 0);
  NANO_OS_ASSERT_EQ_INT(4, key(0, KEYCODE_KEYPAD_PAGEDOWN, 1, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[6~", 4) == 0);
  NANO_OS_ASSERT_EQ_INT(3, key(0, KEYCODE_F1, 1, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1bOP", 3) == 0);
}

NANO_OS_TEST(unit_vdp_keyboard, keys_without_ascii_or_sequence_send_nothing) {
  uint8_t output[VDP_KEY_MAX_BYTES];

  NANO_OS_ASSERT_EQ_INT(0, key(0, KEYCODE_NONE, 1, output));
}

NANO_OS_TEST(unit_vdp_keyboard, data_bytes_with_the_high_bit_are_data) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t packet[] = { 0x81, 0x04, 0x00, 0x82, KEYCODE_LEFT, 0x01 };

  NANO_OS_ASSERT_EQ_INT(5, feed(&parser, packet, sizeof(packet)));
  NANO_OS_ASSERT_EQ_INT(VDP_PACKET_KEYBOARD, parser.command);
  NANO_OS_ASSERT_EQ_INT(KEYCODE_LEFT, parser.data[2]);
}

/// @fn static int parseKeys(const uint8_t *bytes, size_t length,
///   uint8_t *keys, int maxKeys)
///
/// @brief Parse bytes from the VDP and collect the ASCII value of each key
/// that went down.
///
/// @param bytes The bytes to parse.
/// @param length The number of bytes.
/// @param keys The array to collect the ASCII values in.
/// @param maxKeys The size of keys.
///
/// @return Returns the number of keys collected.
static int parseKeys(const uint8_t *bytes, size_t length, uint8_t *keys,
  int maxKeys
) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  int numKeys = 0;
  for (size_t ii = 0; ii < length; ii++) {
    if (vdpParseByte(&parser, bytes[ii])
      && (parser.command == VDP_PACKET_KEYBOARD)
      && (parser.data[3] == 1) && (numKeys < maxKeys)
    ) {
      keys[numKeys++] = parser.data[0];
    }
  }
  return numKeys;
}

NANO_OS_TEST(unit_vdp_keyboard, damaged_poll_replies_do_not_swallow_keys) {
  // The bytes an Agon Light 2 sent after power-on, Oct 2026: a burst of
  // replies to queued general polls with bytes lost, then 'a' and 'b'.
  const uint8_t bytes[] = {
    0x80, 0x01, 0x4e,
    0x86, 0x08, 0x80, 0x02, 0xe0, 0x01, 0x50, 0x3c, 0x10, 0x00,
    0x80, 0x01, 0x4e, 0x80, 0x01, 0x4e,
    0x80, 0x4e, 0x80, 0x80, 0x01, 0x80, 0x80, 0x80, 0x80,
    0x81, 0x04, 0x61, 0x00, 0x16, 0x01,
    0x81, 0x04, 0x61, 0x00, 0x16, 0x00,
    0x81, 0x04, 0x62, 0x00, 0x17, 0x01,
    0x81, 0x04, 0x62, 0x00, 0x17, 0x00,
  };
  uint8_t keys[4];

  NANO_OS_ASSERT_EQ_INT(2, parseKeys(bytes, sizeof(bytes), keys, 4));
  NANO_OS_ASSERT_EQ_INT('a', keys[0]);
  NANO_OS_ASSERT_EQ_INT('b', keys[1]);
}

NANO_OS_TEST(unit_vdp_keyboard, poll_reply_missing_its_cookie_keeps_next_key) {
  const uint8_t bytes[] = {
    0x80, 0x01,
    0x81, 0x04, 0x61, 0x00, 0x16, 0x01,
    0x81, 0x04, 0x62, 0x00, 0x17, 0x01,
  };
  uint8_t keys[4];

  NANO_OS_ASSERT_EQ_INT(2, parseKeys(bytes, sizeof(bytes), keys, 4));
  NANO_OS_ASSERT_EQ_INT('a', keys[0]);
  NANO_OS_ASSERT_EQ_INT('b', keys[1]);
}

NANO_OS_TEST(unit_vdp_keyboard, implausible_lengths_do_not_swallow_keys) {
  const uint8_t bytes[] = {
    0xff, 0x81, 0x04, 0x61, 0x00, 0x16, 0x01,
    0x81, 0x4e, 0x81, 0x04, 0x62, 0x00, 0x17, 0x01,
  };
  uint8_t keys[4];

  NANO_OS_ASSERT_EQ_INT(2, parseKeys(bytes, sizeof(bytes), keys, 4));
  NANO_OS_ASSERT_EQ_INT('a', keys[0]);
  NANO_OS_ASSERT_EQ_INT('b', keys[1]);
}

NANO_OS_TEST(unit_vdp_keyboard, poll_reply_with_cookie_is_recognized) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t reply[] = { 0x80, 0x01, VDP_GENERAL_POLL_COOKIE };

  NANO_OS_ASSERT_EQ_INT(2, feed(&parser, reply, sizeof(reply)));
  NANO_OS_ASSERT_TRUE(vdpIsPollReply(&parser, VDP_GENERAL_POLL_COOKIE));
}

NANO_OS_TEST(unit_vdp_keyboard, poll_reply_with_another_cookie_is_not_ours) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t reply[] = { 0x80, 0x01, VDP_GENERAL_POLL_COOKIE + 2 };

  NANO_OS_ASSERT_EQ_INT(2, feed(&parser, reply, sizeof(reply)));
  NANO_OS_ASSERT_FALSE(vdpIsPollReply(&parser, VDP_GENERAL_POLL_COOKIE));
}

NANO_OS_TEST(unit_vdp_keyboard, key_carrying_the_cookie_is_not_a_poll_reply) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t packet[] = { 0x81, 0x04, VDP_GENERAL_POLL_COOKIE, 0, 0, 1 };

  NANO_OS_ASSERT_EQ_INT(5, feed(&parser, packet, sizeof(packet)));
  NANO_OS_ASSERT_FALSE(vdpIsPollReply(&parser, VDP_GENERAL_POLL_COOKIE));
}

NANO_OS_TEST(unit_vdp_keyboard, cookie_stays_below_the_header_bit) {
  NANO_OS_ASSERT_TRUE(VDP_GENERAL_POLL_COOKIE < 0x80);
  NANO_OS_ASSERT_TRUE(VDP_SETUP_POLL_COOKIE < 0x80);
}

NANO_OS_TEST(unit_vdp_keyboard, setup_and_handshake_replies_are_told_apart) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t handshake[] = { 0x80, 0x01, VDP_GENERAL_POLL_COOKIE };
  const uint8_t setup[] = { 0x80, 0x01, VDP_SETUP_POLL_COOKIE };

  NANO_OS_ASSERT_TRUE(VDP_SETUP_POLL_COOKIE != VDP_GENERAL_POLL_COOKIE);
  NANO_OS_ASSERT_EQ_INT(2, feed(&parser, handshake, sizeof(handshake)));
  NANO_OS_ASSERT_FALSE(vdpIsPollReply(&parser, VDP_SETUP_POLL_COOKIE));
  NANO_OS_ASSERT_EQ_INT(2, feed(&parser, setup, sizeof(setup)));
  NANO_OS_ASSERT_TRUE(vdpIsPollReply(&parser, VDP_SETUP_POLL_COOKIE));
  NANO_OS_ASSERT_FALSE(vdpIsPollReply(&parser, VDP_GENERAL_POLL_COOKIE));
}

NANO_OS_TEST(unit_vdp_keyboard, other_command_carrying_the_cookie_is_not_ours) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));
  const uint8_t packet[] = { 0x82, 0x01, VDP_GENERAL_POLL_COOKIE };

  NANO_OS_ASSERT_EQ_INT(2, feed(&parser, packet, sizeof(packet)));
  NANO_OS_ASSERT_FALSE(vdpIsPollReply(&parser, VDP_GENERAL_POLL_COOKIE));
}

NANO_OS_TEST(unit_vdp_keyboard, keypad_types_digits_under_the_drivers_num_lock) {
  uint8_t output[VDP_KEY_MAX_BYTES];

  NANO_OS_ASSERT_EQ_INT(1,
    keypad(0, 0, KEYCODE_KEYPAD_END, true, output));
  NANO_OS_ASSERT_EQ_INT('1', output[0]);
  NANO_OS_ASSERT_EQ_INT(1,
    keypad(0, 0, KEYCODE_KEYPAD_INSERT, true, output));
  NANO_OS_ASSERT_EQ_INT('0', output[0]);
  NANO_OS_ASSERT_EQ_INT(1,
    keypad(0, 0, KEYCODE_KEYPAD_CENTER, true, output));
  NANO_OS_ASSERT_EQ_INT('5', output[0]);
  NANO_OS_ASSERT_EQ_INT(1,
    keypad(0, 0, KEYCODE_KEYPAD_PAGEUP, true, output));
  NANO_OS_ASSERT_EQ_INT('9', output[0]);
  NANO_OS_ASSERT_EQ_INT(1,
    keypad(0, 0, KEYCODE_KEYPAD_DELETE, true, output));
  NANO_OS_ASSERT_EQ_INT('.', output[0]);
  NANO_OS_ASSERT_EQ_INT(1,
    keypad('7', AGON_KEYMOD_NUM_LOCK, KEYCODE_KEYPAD_7, true, output));
  NANO_OS_ASSERT_EQ_INT('7', output[0]);
}

NANO_OS_TEST(unit_vdp_keyboard, keypad_navigates_without_the_drivers_num_lock) {
  uint8_t output[VDP_KEY_MAX_BYTES];

  // The VDP's own Num Lock is on here, so it reports the digit forms.
  NANO_OS_ASSERT_EQ_INT(3,
    keypad('1', AGON_KEYMOD_NUM_LOCK, KEYCODE_KEYPAD_1, false, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[F", 3) == 0);
  NANO_OS_ASSERT_EQ_INT(4,
    keypad('.', AGON_KEYMOD_NUM_LOCK, KEYCODE_KEYPAD_PERIOD, false, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[3~", 4) == 0);
  NANO_OS_ASSERT_EQ_INT(4,
    keypad('0', AGON_KEYMOD_NUM_LOCK, KEYCODE_KEYPAD_0, false, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[2~", 4) == 0);
  NANO_OS_ASSERT_EQ_INT(0,
    keypad('5', AGON_KEYMOD_NUM_LOCK, KEYCODE_KEYPAD_5, false, output));
  NANO_OS_ASSERT_EQ_INT(3,
    keypad(0, 0, KEYCODE_KEYPAD_LEFT, false, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[D", 3) == 0);
}

NANO_OS_TEST(unit_vdp_keyboard, shifted_keypad_navigates_under_num_lock) {
  uint8_t output[VDP_KEY_MAX_BYTES];

  NANO_OS_ASSERT_EQ_INT(3,
    keypad(0, AGON_KEYMOD_SHIFT, KEYCODE_KEYPAD_UP, true, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[A", 3) == 0);
}

NANO_OS_TEST(unit_vdp_keyboard, non_keypad_keys_ignore_num_lock) {
  uint8_t output[VDP_KEY_MAX_BYTES];

  NANO_OS_ASSERT_EQ_INT(3, keypad(0, 0, KEYCODE_END, true, output));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b[F", 3) == 0);
  NANO_OS_ASSERT_EQ_INT(1, keypad('1', 0, KEYCODE_1, false, output));
  NANO_OS_ASSERT_EQ_INT('1', output[0]);
  NANO_OS_ASSERT_EQ_INT(1, keypad('-', 0, KEYCODE_KEYPAD_MINUS, false, output));
  NANO_OS_ASSERT_EQ_INT('-', output[0]);
}

NANO_OS_TEST(unit_vdp_keyboard, num_lock_release_toggles_and_lights_its_led) {
  bool numLock = true;
  uint8_t leds = 0xff;

  NANO_OS_ASSERT_TRUE(release(0, KEYCODE_NUMLOCK, &numLock, &leds));
  NANO_OS_ASSERT_FALSE(numLock);
  NANO_OS_ASSERT_EQ_INT(0, leds);
  NANO_OS_ASSERT_TRUE(release(AGON_KEYMOD_NUM_LOCK, KEYCODE_NUMLOCK,
    &numLock, &leds));
  NANO_OS_ASSERT_TRUE(numLock);
  NANO_OS_ASSERT_EQ_INT(VDP_LED_NUM_LOCK, leds);
}

NANO_OS_TEST(unit_vdp_keyboard, other_lock_releases_keep_the_num_lock_led) {
  bool numLock = true;
  uint8_t leds = 0;

  NANO_OS_ASSERT_TRUE(release(0, KEYCODE_CAPSLOCK, &numLock, &leds));
  NANO_OS_ASSERT_TRUE(numLock);
  NANO_OS_ASSERT_EQ_INT(VDP_LED_NUM_LOCK | VDP_LED_CAPS_LOCK, leds);
  NANO_OS_ASSERT_TRUE(release(AGON_KEYMOD_CAPS_LOCK, KEYCODE_SCROLLLOCK,
    &numLock, &leds));
  NANO_OS_ASSERT_EQ_INT(
    VDP_LED_NUM_LOCK | VDP_LED_CAPS_LOCK | VDP_LED_SCROLL_LOCK, leds);
  NANO_OS_ASSERT_TRUE(release(
    AGON_KEYMOD_CAPS_LOCK | AGON_KEYMOD_SCROLL_LOCK, KEYCODE_CAPSLOCK,
    &numLock, &leds));
  NANO_OS_ASSERT_EQ_INT(VDP_LED_NUM_LOCK | VDP_LED_SCROLL_LOCK, leds);
}

NANO_OS_TEST(unit_vdp_keyboard, lock_presses_and_other_releases_change_nothing) {
  bool numLock = false;
  uint8_t leds = 0xff;
  uint8_t press[4] = { 0, 0, KEYCODE_NUMLOCK, 1 };

  NANO_OS_ASSERT_FALSE(vdpUpdateLocks(press, &numLock, &leds));
  NANO_OS_ASSERT_FALSE(release(0, KEYCODE_a, &numLock, &leds));
  NANO_OS_ASSERT_FALSE(numLock);
  NANO_OS_ASSERT_EQ_INT(0xff, leds);
}
