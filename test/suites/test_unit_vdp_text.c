///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_vdp_text.c
///
/// @brief             Unit tests (no kernel boot) for vdpTranslateText, the
///                    VDP driver's console text to VDU byte translation.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "../../usr/src/lib/vdp.drv/main/VdpText.c"

/// @fn static size_t translateAll(VdpTextState *state, const char *text,
///   uint8_t *output, size_t outputSize)
///
/// @brief Translate a whole string through a buffer of outputSize bytes,
/// concatenating the results.
///
/// @param state A pointer to the VdpTextState to use.
/// @param text The NUL-terminated string to translate.
/// @param output The buffer to collect every call's bytes in.
/// @param outputSize The buffer size to give each call.
///
/// @return Returns the total number of bytes produced.
static size_t translateAll(VdpTextState *state, const char *text,
  uint8_t *output, size_t outputSize
) {
  size_t total = 0;
  while (*text != '\0') {
    size_t length = vdpTranslateText(state, &text, &output[total], outputSize);
    if (length == 0) {
      break;
    }
    total += length;
  }
  return total;
}

NANO_OS_TEST(unit_vdp_text, printable_text_passes_through) {
  VdpTextState state = { .column = 0 };
  uint8_t output[32];
  const char *text = "ls -l";

  NANO_OS_ASSERT_EQ_INT(5, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "ls -l", 5) == 0);
  NANO_OS_ASSERT_EQ_INT('\0', *text);
  NANO_OS_ASSERT_EQ_INT(5, state.column);
}

NANO_OS_TEST(unit_vdp_text, newline_becomes_return_and_line_feed) {
  VdpTextState state = { .column = 4 };
  uint8_t output[32];
  const char *text = "a\nb";

  NANO_OS_ASSERT_EQ_INT(4, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "a\r\nb", 4) == 0);
  NANO_OS_ASSERT_EQ_INT(1, state.column);
}

NANO_OS_TEST(unit_vdp_text, tab_pads_to_the_next_multiple_of_eight) {
  VdpTextState state = { .column = 0 };
  uint8_t output[32];
  const char *text = "abc\tx\t";

  NANO_OS_ASSERT_EQ_INT(16, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "abc     x       ", 16) == 0);
  NANO_OS_ASSERT_EQ_INT(16, state.column);
}

NANO_OS_TEST(unit_vdp_text, vdu_command_bytes_are_dropped) {
  VdpTextState state = { .column = 0 };
  uint8_t output[32];
  const char *text = "a\x16\x01\x11\x1b" "b\x7f" "c";

  NANO_OS_ASSERT_EQ_INT(3, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "abc", 3) == 0);
  NANO_OS_ASSERT_EQ_INT(3, state.column);
}

NANO_OS_TEST(unit_vdp_text, erase_bell_and_high_bytes_pass_through) {
  VdpTextState state = { .column = 0 };
  uint8_t output[32];
  const char *text = "ab\b \b\a\xe9";

  NANO_OS_ASSERT_EQ_INT(7, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "ab\b \b\a\xe9", 7) == 0);
  NANO_OS_ASSERT_EQ_INT(2, state.column);
}

NANO_OS_TEST(unit_vdp_text, a_full_buffer_never_splits_a_character) {
  VdpTextState state = { .column = 0 };
  uint8_t output[32];
  const char *text = "ab\n\t";

  NANO_OS_ASSERT_EQ_INT(2, vdpTranslateText(&state, &text, output, 3));
  NANO_OS_ASSERT_EQ_INT('\n', *text);
  NANO_OS_ASSERT_EQ_INT(2, vdpTranslateText(&state, &text, output, 3));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\r\n", 2) == 0);
  NANO_OS_ASSERT_EQ_INT(0, vdpTranslateText(&state, &text, output, 3));
  NANO_OS_ASSERT_EQ_INT('\t', *text);
}

NANO_OS_TEST(unit_vdp_text, minimum_buffer_translates_everything) {
  VdpTextState state = { .column = 1 };
  uint8_t output[64];
  const char *text = "\t\tx\ny";

  NANO_OS_ASSERT_EQ_INT(19,
    translateAll(&state, text, output, VDP_TEXT_MAX_EXPANSION));
  NANO_OS_ASSERT_TRUE(memcmp(output, "               x\r\ny", 19) == 0);
}

NANO_OS_TEST(unit_vdp_text, buffered_output_is_appended_in_order) {
  VdpOutputBuffer buffer = { .length = 0 };

  vdpBufferOutput(&buffer, (const uint8_t*) "abc", 3);
  vdpBufferOutput(&buffer, (const uint8_t*) "de", 2);

  NANO_OS_ASSERT_EQ_INT(5, buffer.length);
  NANO_OS_ASSERT_TRUE(memcmp(buffer.data, "abcde", 5) == 0);
}

NANO_OS_TEST(unit_vdp_text, full_buffer_drops_its_oldest_bytes) {
  VdpOutputBuffer buffer = { .length = 0 };
  uint8_t fill[VDP_OUTPUT_BUFFER_SIZE - 2];
  memset(fill, 'x', sizeof(fill));
  fill[0] = 'a';
  fill[1] = 'b';
  fill[2] = 'c';

  vdpBufferOutput(&buffer, fill, sizeof(fill));
  vdpBufferOutput(&buffer, (const uint8_t*) "1234", 4);

  NANO_OS_ASSERT_EQ_INT(VDP_OUTPUT_BUFFER_SIZE, buffer.length);
  NANO_OS_ASSERT_EQ_INT('c', buffer.data[0]);
  NANO_OS_ASSERT_EQ_INT('x', buffer.data[1]);
  NANO_OS_ASSERT_TRUE(
    memcmp(&buffer.data[VDP_OUTPUT_BUFFER_SIZE - 4], "1234", 4) == 0);
}

NANO_OS_TEST(unit_vdp_text, oversized_write_keeps_its_last_bytes) {
  VdpOutputBuffer buffer = { .length = 0 };
  uint8_t big[VDP_OUTPUT_BUFFER_SIZE + 10];
  for (size_t ii = 0; ii < sizeof(big); ii++) {
    big[ii] = (uint8_t) ii;
  }
  vdpBufferOutput(&buffer, (const uint8_t*) "old", 3);

  vdpBufferOutput(&buffer, big, sizeof(big));

  NANO_OS_ASSERT_EQ_INT(VDP_OUTPUT_BUFFER_SIZE, buffer.length);
  NANO_OS_ASSERT_TRUE(
    memcmp(buffer.data, &big[10], VDP_OUTPUT_BUFFER_SIZE) == 0);
}
