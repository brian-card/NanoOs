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

#include "../../userspace/usr/src/lib/vdp.drv/main/VdpKeyboard.c"
#include "../../userspace/usr/src/lib/vdp.drv/Console/VdpLocks.c"
#include "../../userspace/usr/src/commands/sysnook/include/WmKeys.h"

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

NANO_OS_TEST(unit_vdp_keyboard, every_sequence_key_sends_exactly_its_sequence) {
  static const struct {
    uint8_t keycode;
    const char *sequence;
  } keys[] = {
    { KEYCODE_UP, "\x1b[A" }, { KEYCODE_KEYPAD_UP, "\x1b[A" },
    { KEYCODE_DOWN, "\x1b[B" }, { KEYCODE_KEYPAD_DOWN, "\x1b[B" },
    { KEYCODE_RIGHT, "\x1b[C" }, { KEYCODE_KEYPAD_RIGHT, "\x1b[C" },
    { KEYCODE_LEFT, "\x1b[D" }, { KEYCODE_KEYPAD_LEFT, "\x1b[D" },
    { KEYCODE_HOME, "\x1b[H" }, { KEYCODE_KEYPAD_HOME, "\x1b[H" },
    { KEYCODE_END, "\x1b[F" }, { KEYCODE_KEYPAD_END, "\x1b[F" },
    { KEYCODE_INSERT, "\x1b[2~" }, { KEYCODE_KEYPAD_INSERT, "\x1b[2~" },
    { KEYCODE_DELETE, "\x1b[3~" }, { KEYCODE_KEYPAD_DELETE, "\x1b[3~" },
    { KEYCODE_PAGEUP, "\x1b[5~" }, { KEYCODE_KEYPAD_PAGEUP, "\x1b[5~" },
    { KEYCODE_PAGEDOWN, "\x1b[6~" }, { KEYCODE_KEYPAD_PAGEDOWN, "\x1b[6~" },
    { KEYCODE_F1, "\x1bOP" }, { KEYCODE_F2, "\x1bOQ" },
    { KEYCODE_F3, "\x1bOR" }, { KEYCODE_F4, "\x1bOS" },
  };
  uint8_t output[VDP_KEY_MAX_BYTES];

  for (size_t ii = 0; ii < sizeof(keys) / sizeof(keys[0]); ii++) {
    size_t length = strlen(keys[ii].sequence);
    NANO_OS_ASSERT_EQ_INT((int) length, key(0, keys[ii].keycode, 1, output));
    NANO_OS_ASSERT_TRUE(memcmp(output, keys[ii].sequence, length) == 0);
  }
  NANO_OS_ASSERT_EQ_INT(0, key(0, KEYCODE_F5, 1, output));
  NANO_OS_ASSERT_EQ_INT(0, key(0, KEYCODE_PAUSE, 1, output));
}

/// @fn static uint8_t gfxKey(uint8_t ascii, uint8_t modifiers,
///   uint8_t keycode, bool numLock, uint8_t *typed)
///
/// @brief Get the GfxKey of a key press.
///
/// @param ascii The packet's ASCII value.
/// @param modifiers The packet's AGON_KEYMOD_* bits.
/// @param keycode The packet's FabGL keycode.
/// @param numLock Whether the driver's Num Lock is on.
/// @param typed Set to the character vdpGfxKey says the key types.
///
/// @return Returns what vdpGfxKey returns.
static uint8_t gfxKey(uint8_t ascii, uint8_t modifiers, uint8_t keycode,
  bool numLock, uint8_t *typed
) {
  uint8_t keyData[4] = { ascii, modifiers, keycode, 1 };
  return vdpGfxKey(keyData, numLock, typed);
}

NANO_OS_TEST(unit_vdp_keyboard, letters_name_the_lower_case_letter) {
  uint8_t typed = 0xff;

  NANO_OS_ASSERT_EQ_INT('a', gfxKey('a', 0, KEYCODE_a, false, &typed));
  NANO_OS_ASSERT_EQ_INT('a', typed);
  NANO_OS_ASSERT_EQ_INT('z', gfxKey('z', 0, KEYCODE_z, false, &typed));
  NANO_OS_ASSERT_EQ_INT('a',
    gfxKey('A', AGON_KEYMOD_SHIFT, KEYCODE_A, false, &typed));
  NANO_OS_ASSERT_EQ_INT('A', typed);
  NANO_OS_ASSERT_EQ_INT('z',
    gfxKey('Z', AGON_KEYMOD_SHIFT, KEYCODE_Z, false, &typed));
  NANO_OS_ASSERT_EQ_INT('c',
    gfxKey(0x03, AGON_KEYMOD_CTRL, KEYCODE_c, false, &typed));
  NANO_OS_ASSERT_EQ_INT(0x03, typed);
}

NANO_OS_TEST(unit_vdp_keyboard, digits_space_and_punctuation_name_themselves) {
  uint8_t typed = 0xff;

  NANO_OS_ASSERT_EQ_INT('0', gfxKey('0', 0, KEYCODE_0, false, &typed));
  NANO_OS_ASSERT_EQ_INT('9', gfxKey('9', 0, KEYCODE_9, false, &typed));
  NANO_OS_ASSERT_EQ_INT(' ', gfxKey(' ', 0, KEYCODE_SPACE, false, &typed));
  NANO_OS_ASSERT_EQ_INT('!',
    gfxKey('!', AGON_KEYMOD_SHIFT, KEYCODE_EXCLAIM, false, &typed));
  NANO_OS_ASSERT_EQ_INT('-',
    gfxKey('-', 0, KEYCODE_KEYPAD_MINUS, false, &typed));
  NANO_OS_ASSERT_EQ_INT('-', typed);
}

NANO_OS_TEST(unit_vdp_keyboard, named_keys_name_their_gfx_key) {
  static const struct {
    uint8_t keycode;
    uint8_t key;
  } keys[] = {
    { KEYCODE_LSHIFT, GFX_KEY_LEFT_SHIFT },
    { KEYCODE_RSHIFT, GFX_KEY_RIGHT_SHIFT },
    { KEYCODE_LALT, GFX_KEY_LEFT_ALT }, { KEYCODE_RALT, GFX_KEY_RIGHT_ALT },
    { KEYCODE_LCTRL, GFX_KEY_LEFT_CTRL },
    { KEYCODE_RCTRL, GFX_KEY_RIGHT_CTRL },
    { KEYCODE_LGUI, GFX_KEY_LEFT_GUI }, { KEYCODE_RGUI, GFX_KEY_RIGHT_GUI },
    { KEYCODE_ESCAPE, GFX_KEY_ESCAPE },
    { KEYCODE_PRINTSCREEN, GFX_KEY_PRINT_SCREEN },
    { KEYCODE_SYSREQ, GFX_KEY_SYSREQ }, { KEYCODE_INSERT, GFX_KEY_INSERT },
    { KEYCODE_DELETE, GFX_KEY_DELETE },
    { KEYCODE_BACKSPACE, GFX_KEY_BACKSPACE }, { KEYCODE_HOME, GFX_KEY_HOME },
    { KEYCODE_END, GFX_KEY_END }, { KEYCODE_PAUSE, GFX_KEY_PAUSE },
    { KEYCODE_BREAK, GFX_KEY_BREAK },
    { KEYCODE_SCROLLLOCK, GFX_KEY_SCROLL_LOCK },
    { KEYCODE_NUMLOCK, GFX_KEY_NUM_LOCK },
    { KEYCODE_CAPSLOCK, GFX_KEY_CAPS_LOCK }, { KEYCODE_TAB, GFX_KEY_TAB },
    { KEYCODE_RETURN, GFX_KEY_ENTER }, { KEYCODE_KEYPAD_ENTER, GFX_KEY_ENTER },
    { KEYCODE_APPLICATION, GFX_KEY_MENU },
    { KEYCODE_PAGEUP, GFX_KEY_PAGE_UP },
    { KEYCODE_PAGEDOWN, GFX_KEY_PAGE_DOWN }, { KEYCODE_UP, GFX_KEY_UP },
    { KEYCODE_DOWN, GFX_KEY_DOWN }, { KEYCODE_LEFT, GFX_KEY_LEFT },
    { KEYCODE_RIGHT, GFX_KEY_RIGHT }, { KEYCODE_F1, GFX_KEY_F1 },
    { KEYCODE_F2, GFX_KEY_F2 }, { KEYCODE_F3, GFX_KEY_F3 },
    { KEYCODE_F4, GFX_KEY_F4 }, { KEYCODE_F5, GFX_KEY_F5 },
    { KEYCODE_F6, GFX_KEY_F6 }, { KEYCODE_F7, GFX_KEY_F7 },
    { KEYCODE_F8, GFX_KEY_F8 }, { KEYCODE_F9, GFX_KEY_F9 },
    { KEYCODE_F10, GFX_KEY_F10 }, { KEYCODE_F11, GFX_KEY_F11 },
    { KEYCODE_F12, GFX_KEY_F12 },
  };
  uint8_t typed = 0xff;

  for (size_t ii = 0; ii < sizeof(keys) / sizeof(keys[0]); ii++) {
    NANO_OS_ASSERT_EQ_INT(keys[ii].key,
      gfxKey(0, 0, keys[ii].keycode, false, &typed));
  }
  NANO_OS_ASSERT_EQ_INT(GFX_KEY_ESCAPE,
    gfxKey(0x1b, 0, KEYCODE_ESCAPE, false, &typed));
  NANO_OS_ASSERT_EQ_INT(0x1b, typed);
}

NANO_OS_TEST(unit_vdp_keyboard, keypad_names_the_key_it_acts_as) {
  uint8_t typed = 0xff;

  NANO_OS_ASSERT_EQ_INT('8', gfxKey('8', 0, KEYCODE_KEYPAD_8, true, &typed));
  NANO_OS_ASSERT_EQ_INT('8', typed);
  NANO_OS_ASSERT_EQ_INT('8', gfxKey(0, 0, KEYCODE_KEYPAD_UP, true, &typed));
  NANO_OS_ASSERT_EQ_INT('.',
    gfxKey(0, 0, KEYCODE_KEYPAD_DELETE, true, &typed));
  NANO_OS_ASSERT_EQ_INT(GFX_KEY_UP,
    gfxKey('8', 0, KEYCODE_KEYPAD_8, false, &typed));
  NANO_OS_ASSERT_EQ_INT(0, typed);
  NANO_OS_ASSERT_EQ_INT(GFX_KEY_UP,
    gfxKey(0, AGON_KEYMOD_SHIFT, KEYCODE_KEYPAD_8, true, &typed));
  NANO_OS_ASSERT_EQ_INT(GFX_KEY_INSERT,
    gfxKey(0, 0, KEYCODE_KEYPAD_0, false, &typed));
  NANO_OS_ASSERT_EQ_INT(GFX_KEY_DELETE,
    gfxKey(0, 0, KEYCODE_KEYPAD_PERIOD, false, &typed));
  NANO_OS_ASSERT_EQ_INT(GFX_KEY_PAGE_DOWN,
    gfxKey(0, 0, KEYCODE_KEYPAD_PAGEDOWN, false, &typed));
  NANO_OS_ASSERT_EQ_INT(GFX_KEY_NONE,
    gfxKey(0, 0, KEYCODE_KEYPAD_5, false, &typed));
  NANO_OS_ASSERT_EQ_INT(GFX_KEY_NONE,
    gfxKey(0, 0, KEYCODE_KEYPAD_CENTER, false, &typed));
  NANO_OS_ASSERT_EQ_INT(0, typed);
}

NANO_OS_TEST(unit_vdp_keyboard, keys_past_f12_and_none_name_no_key) {
  uint8_t typed = 0xff;

  NANO_OS_ASSERT_EQ_INT(GFX_KEY_NONE,
    gfxKey(0xe4, 0, KEYCODE_UMLAUT_a, false, &typed));
  NANO_OS_ASSERT_EQ_INT(0xe4, typed);
  NANO_OS_ASSERT_EQ_INT(GFX_KEY_NONE,
    gfxKey(0, 0, KEYCODE_NONE, false, &typed));
  NANO_OS_ASSERT_EQ_INT(0, typed);
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

NANO_OS_TEST(unit_vdp_keyboard, ctrl_alt_backspace_going_down_is_the_release_key) {
  uint8_t keyData[4] = { 8, AGON_KEYMOD_CTRL | AGON_KEYMOD_ALT_LEFT,
    KEYCODE_BACKSPACE, 1 };
  NANO_OS_ASSERT_TRUE(vdpIsReleaseKey(keyData));
  keyData[1] = AGON_KEYMOD_CTRL | AGON_KEYMOD_ALT_RIGHT | AGON_KEYMOD_SHIFT;
  NANO_OS_ASSERT_TRUE(vdpIsReleaseKey(keyData));
}

NANO_OS_TEST(unit_vdp_keyboard, anything_short_of_ctrl_alt_backspace_down_is_not) {
  const uint8_t notRelease[][4] = {
    { 8, AGON_KEYMOD_CTRL | AGON_KEYMOD_ALT_LEFT, KEYCODE_BACKSPACE, 0 },
    { 8, AGON_KEYMOD_CTRL, KEYCODE_BACKSPACE, 1 },
    { 8, AGON_KEYMOD_ALT_LEFT, KEYCODE_BACKSPACE, 1 },
    { 8, 0, KEYCODE_BACKSPACE, 1 },
    { 0, AGON_KEYMOD_CTRL | AGON_KEYMOD_ALT_LEFT, KEYCODE_DELETE, 1 },
  };
  for (size_t ii = 0; ii < sizeof(notRelease) / sizeof(notRelease[0]); ii++) {
    NANO_OS_ASSERT_FALSE(vdpIsReleaseKey(notRelease[ii]));
  }
}

/// @var MOUSE_PACKET
///
/// @brief A mouse packet at (0x123, 0x1DF) with buttons 0x05, wheel -1, and
/// deltas (-2, 2), whose data bytes include two with the high bit set.
static const uint8_t MOUSE_PACKET[] = {
  0x89, 0x0a, 0x23, 0x01, 0xdf, 0x01, 0x05, 0xff, 0xfe, 0xff, 0x02, 0x00,
};

NANO_OS_TEST(unit_vdp_keyboard, mouse_packet_keeps_all_ten_bytes) {
  VdpPacketParser parser;
  memset(&parser, 0, sizeof(parser));

  NANO_OS_ASSERT_EQ_INT(11, feed(&parser, MOUSE_PACKET, sizeof(MOUSE_PACKET)));
  NANO_OS_ASSERT_EQ_INT(VDP_PACKET_MOUSE, parser.command);
  NANO_OS_ASSERT_EQ_INT(VDP_MOUSE_PACKET_LENGTH, parser.length);
  NANO_OS_ASSERT_EQ_INT(0, memcmp(&MOUSE_PACKET[2], parser.data,
    VDP_MOUSE_PACKET_LENGTH));
}

NANO_OS_TEST(unit_vdp_keyboard, mouse_header_with_length_four_is_abandoned) {
  const uint8_t bytes[] = {
    0x89, 0x04,
    0x81, 0x04, 0x61, 0x00, 0x16, 0x01,
  };
  uint8_t keys[4];

  NANO_OS_ASSERT_EQ_INT(1, parseKeys(bytes, sizeof(bytes), keys, 4));
  NANO_OS_ASSERT_EQ_INT('a', keys[0]);
}

NANO_OS_TEST(unit_vdp_keyboard, keys_on_either_side_of_a_mouse_packet_parse) {
  uint8_t bytes[6 + sizeof(MOUSE_PACKET) + 6] = {
    0x81, 0x04, 0x61, 0x00, 0x16, 0x01,
  };
  memcpy(&bytes[6], MOUSE_PACKET, sizeof(MOUSE_PACKET));
  const uint8_t keyB[] = { 0x81, 0x04, 0x62, 0x00, 0x17, 0x01 };
  memcpy(&bytes[6 + sizeof(MOUSE_PACKET)], keyB, sizeof(keyB));
  uint8_t keys[4];

  NANO_OS_ASSERT_EQ_INT(2, parseKeys(bytes, sizeof(bytes), keys, 4));
  NANO_OS_ASSERT_EQ_INT('a', keys[0]);
  NANO_OS_ASSERT_EQ_INT('b', keys[1]);
}

NANO_OS_TEST(unit_vdp_keyboard,
  the_window_server_sends_a_session_what_the_console_driver_would
) {
  static const uint8_t asciis[] = { 0, 'a', 'A', '1', ' ', '~', 0x1b, 0x03,
    0x7f, 0x0d, 0x08, 0x09 };
  static const uint8_t modifiers[] = { 0, AGON_KEYMOD_SHIFT,
    AGON_KEYMOD_CTRL };
  int compared = 0;
  for (int numLock = 0; numLock <= 1; numLock++) {
    for (int keycode = 0; keycode < 256; keycode++) {
      for (size_t ii = 0; ii < sizeof(asciis); ii++) {
        for (size_t jj = 0; jj < sizeof(modifiers); jj++) {
          uint8_t keyData[4] = { asciis[ii], modifiers[jj], (uint8_t) keycode,
            1 };
          uint8_t expected[VDP_KEY_MAX_BYTES] = { 0 };
          uint8_t actual[WM_KEY_MAX_BYTES] = { 0 };
          size_t expectedLength = vdpTranslateKey(keyData, numLock != 0,
            expected);
          uint8_t ascii = 0;
          uint8_t key = vdpGfxKey(keyData, numLock != 0, &ascii);
          uint8_t actualLength = wmTranslateKey(key, ascii, actual);
          NANO_OS_ASSERT_EQ_INT((int) expectedLength, (int) actualLength);
          NANO_OS_ASSERT_TRUE(
            memcmp(expected, actual, expectedLength) == 0);
          compared++;
        }
      }
    }
  }
  NANO_OS_ASSERT_EQ_INT(2 * 256 * 12 * 3, compared);
}
