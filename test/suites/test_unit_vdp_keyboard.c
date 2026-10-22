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
  return vdpTranslateKey(keyData, output);
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
