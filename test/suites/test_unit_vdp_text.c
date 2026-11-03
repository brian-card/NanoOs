///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_vdp_text.c
///
/// @brief             Unit tests (no kernel boot) for vdpTranslateText, the
///                    VDP driver's console text to VDU byte translation.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <stdio.h>
#include <string.h>

#include "../../usr/src/lib/vdp.drv/main/VdpText.c"

CGA_TO_ANSI_MAP

/// @fn static size_t translateAll(VdpTextState *state, const char *text,
///   uint8_t *output, size_t outputSize)
///
/// @brief Translate a whole string through a buffer of outputSize bytes,
/// concatenating the results, and finish what it leaves pending, as
/// vdp.drv's vdpWriteText does.
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
  while ((*text != '\0') || (state->erasePending > 0)
    || (state->scrollDirection != 0)
  ) {
    size_t length = vdpTranslateText(state, &text, &output[total], outputSize);
    if (length == 0) {
      break;
    }
    total += length;
  }
  return total;
}

NANO_OS_TEST(unit_vdp_text, printable_text_passes_through) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "ls -l";

  NANO_OS_ASSERT_EQ_INT(5, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "ls -l", 5) == 0);
  NANO_OS_ASSERT_EQ_INT('\0', *text);
  NANO_OS_ASSERT_EQ_INT(5, state.column);
}

NANO_OS_TEST(unit_vdp_text, newline_becomes_return_and_line_feed) {
  VdpTextState state = { .column = 4, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "a\nb";

  NANO_OS_ASSERT_EQ_INT(4, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "a\r\nb", 4) == 0);
  NANO_OS_ASSERT_EQ_INT(1, state.column);
}

NANO_OS_TEST(unit_vdp_text, tab_pads_to_the_next_multiple_of_eight) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "abc\tx\t";

  NANO_OS_ASSERT_EQ_INT(16, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "abc     x       ", 16) == 0);
  NANO_OS_ASSERT_EQ_INT(16, state.column);
}

NANO_OS_TEST(unit_vdp_text, control_bytes_and_del_are_drawn_as_glyphs) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "a\x16\x01\x11" "b\x7f" "c\x0c\x1f";

  NANO_OS_ASSERT_EQ_INT(15, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output,
    "a\x1b\x16\x1b\x01\x1b\x11" "b\x1b\x7f" "c\x1b\x0c\x1b\x1f", 15) == 0);
  NANO_OS_ASSERT_EQ_INT(9, state.column);
}

NANO_OS_TEST(unit_vdp_text, a_full_buffer_never_splits_a_control_glyph) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "ab\x01";

  NANO_OS_ASSERT_EQ_INT(2, vdpTranslateText(&state, &text, output, 3));
  NANO_OS_ASSERT_EQ_INT('\x01', *text);
  NANO_OS_ASSERT_EQ_INT(2, vdpTranslateText(&state, &text, output, 3));
  NANO_OS_ASSERT_TRUE(memcmp(output, "\x1b\x01", 2) == 0);
  NANO_OS_ASSERT_EQ_INT(3, state.column);
}

NANO_OS_TEST(unit_vdp_text, erase_bell_and_high_bytes_pass_through) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "ab\b \b\a\xe9";

  NANO_OS_ASSERT_EQ_INT(7, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "ab\b \b\a\xe9", 7) == 0);
  NANO_OS_ASSERT_EQ_INT(2, state.column);
}

NANO_OS_TEST(unit_vdp_text, a_full_buffer_never_splits_a_character) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
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
  VdpTextState state = { .column = 1, .numRows = 60, .numColumns = 106 };
  uint8_t output[64];
  const char *text = "\t\tx\ny";

  NANO_OS_ASSERT_EQ_INT(19,
    translateAll(&state, text, output, VDP_TEXT_MAX_EXPANSION));
  NANO_OS_ASSERT_TRUE(memcmp(output, "               x\r\ny", 19) == 0);
}

NANO_OS_TEST(unit_vdp_text, sgr_colors_become_text_color_commands) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[44;93mX";
  const uint8_t expected[] = { 17, CGA_INDEX_YELLOW,
    17, 128 + CGA_INDEX_BLUE, 'X' };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
  NANO_OS_ASSERT_EQ_INT(1, state.column);
  NANO_OS_ASSERT_EQ_INT(VDP_ESCAPE_NONE, state.escapeState);
}

NANO_OS_TEST(unit_vdp_text, every_cga_color_round_trips_through_sgr) {
  for (int ii = 0; ii < CGA_NUM_COLORS; ii++) {
    VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
    uint8_t output[32];
    char sequence[16];
    snprintf(sequence, sizeof(sequence), "\x1b[%d;%dm",
      cgaToAnsiForeground(ii), cgaToAnsiBackground(ii));
    const char *text = sequence;
    const uint8_t expected[] = { 17, (uint8_t) ii, 17, (uint8_t) (128 + ii) };

    NANO_OS_ASSERT_EQ_INT(sizeof(expected),
      vdpTranslateText(&state, &text, output, 32));
    NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
  }
}

NANO_OS_TEST(unit_vdp_text, sgr_resets_select_the_default_colors) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[0m\x1b[m\x1b[39;49m";
  const uint8_t defaults[] = { 17, CGA_INDEX_WHITE,
    17, 128 + CGA_INDEX_BLACK };

  NANO_OS_ASSERT_EQ_INT(3 * sizeof(defaults),
    vdpTranslateText(&state, &text, output, 32));
  for (int ii = 0; ii < 3; ii++) {
    NANO_OS_ASSERT_TRUE(
      memcmp(&output[ii * sizeof(defaults)], defaults, sizeof(defaults))
      == 0);
  }
}

NANO_OS_TEST(unit_vdp_text, sgr_sets_only_the_colors_it_names) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[31m\x1b[1;102m";
  const uint8_t expected[] = { 17, CGA_INDEX_RED,
    17, 128 + CGA_INDEX_LIGHT_GREEN };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
}

NANO_OS_TEST(unit_vdp_text, oversized_parameter_is_not_mistaken_for_a_color) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[1000034;44m";
  const uint8_t expected[] = { 17, 128 + CGA_INDEX_BLUE };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
}

NANO_OS_TEST(unit_vdp_text, clear_and_home_become_vdu_commands) {
  VdpTextState state = { .column = 5, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[2J\x1b[H\x1b[1;1f";
  const uint8_t expected[] = { 12, 31, 0, 0, 31, 0, 0 };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
  NANO_OS_ASSERT_EQ_INT(0, state.column);
}

NANO_OS_TEST(unit_vdp_text, other_escape_sequences_are_dropped) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text =
    "a\x1b[?25lb\x1b[3Jc\x1b[5;10rd\x1b(Be\x1b" "cf\x1b[1mg\x1b[2\x01h";

  NANO_OS_ASSERT_EQ_INT(8, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "abcdefgh", 8) == 0);
  NANO_OS_ASSERT_EQ_INT(8, state.column);
}

NANO_OS_TEST(unit_vdp_text, resize_report_mode_is_recorded_not_drawn) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "a\x1b[?2048hb";

  NANO_OS_ASSERT_EQ_INT(2, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, "ab", 2) == 0);
  NANO_OS_ASSERT_TRUE(state.resizeReports);
  NANO_OS_ASSERT_TRUE(state.resizeReportWanted);

  state.resizeReportWanted = false;
  text = "\x1b[?2048l";
  NANO_OS_ASSERT_EQ_INT(0, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_FALSE(state.resizeReports);
  NANO_OS_ASSERT_FALSE(state.resizeReportWanted);
}

NANO_OS_TEST(unit_vdp_text, turning_resize_reports_on_again_wants_a_report) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[?2048h";
  translateAll(&state, text, output, 32);
  state.resizeReportWanted = false;

  text = "\x1b[?20";
  NANO_OS_ASSERT_EQ_INT(0, vdpTranslateText(&state, &text, output, 32));
  text = "48h";
  NANO_OS_ASSERT_EQ_INT(0, vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(state.resizeReportWanted);
}

NANO_OS_TEST(unit_vdp_text, other_private_modes_leave_resize_reports_alone) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[?2048h\x1b[?204h\x1b[?20480l\x1b[2048lx";

  NANO_OS_ASSERT_EQ_INT(1, translateAll(&state, text, output, 32));
  NANO_OS_ASSERT_EQ_INT('x', output[0]);
  NANO_OS_ASSERT_TRUE(state.resizeReports);
}

NANO_OS_TEST(unit_vdp_text, cursor_position_becomes_a_tab_to_the_cell) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[5;10H\x1b[99;200H\x1b[3H\x1b[;7f";
  const uint8_t expected[] = {
    31, 9, 4,  31, 105, 59,  31, 0, 2,  31, 6, 0
  };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    translateAll(&state, text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
  NANO_OS_ASSERT_EQ_INT(6, state.column);
  NANO_OS_ASSERT_EQ_INT(0, state.row);
}

NANO_OS_TEST(unit_vdp_text, erase_writes_spaces_to_the_end_then_moves_back) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[4;101H\x1b[Kx";
  const uint8_t expected[] = {
    31, 100, 3,  ' ', ' ', ' ', ' ', ' ', ' ',  31, 100, 3,  'x'
  };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    translateAll(&state, text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
  NANO_OS_ASSERT_EQ_INT(101, state.column);
  NANO_OS_ASSERT_EQ_INT(3, state.row);
}

NANO_OS_TEST(unit_vdp_text, erase_that_does_not_fit_is_finished_later) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  uint8_t all[160];
  size_t total = 0;
  const char *text = "\x1b[2;1H\x1b[K";

  int calls = 0;
  while (((*text != '\0') || (state.erasePending > 0)) && (calls < 20)) {
    size_t length = vdpTranslateText(&state, &text, output, sizeof(output));
    NANO_OS_ASSERT_TRUE(length <= sizeof(output));
    memcpy(&all[total], output, length);
    total += length;
    calls++;
  }

  NANO_OS_ASSERT_EQ_INT(3 + 106 + 3, total);
  NANO_OS_ASSERT_EQ_INT(31, all[0]);
  for (size_t ii = 3; ii < 3 + 106; ii++) {
    NANO_OS_ASSERT_EQ_INT(' ', all[ii]);
  }
  const uint8_t back[] = { 31, 0, 1 };
  NANO_OS_ASSERT_TRUE(memcmp(&all[3 + 106], back, sizeof(back)) == 0);
}

NANO_OS_TEST(unit_vdp_text, erase_with_a_parameter_is_dropped) {
  VdpTextState state = { .column = 3, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[1K\x1b[2Ka";

  NANO_OS_ASSERT_EQ_INT(1, translateAll(&state, text, output, 32));
  NANO_OS_ASSERT_EQ_INT('a', output[0]);
  NANO_OS_ASSERT_EQ_INT(0, state.erasePending);
}

NANO_OS_TEST(unit_vdp_text, insert_line_scrolls_down_from_the_cursor_row) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[64];
  const uint8_t expected[] = {
    31, 8, 4,  28, 0, 59, 105, 4,  23, 7, 0, 2, 0,  26, 31, 0, 4,  'x'
  };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    translateAll(&state, "\x1b[5;9H\x1b[Lx", output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
  NANO_OS_ASSERT_EQ_INT(1, state.column);
  NANO_OS_ASSERT_EQ_INT(4, state.row);
}

NANO_OS_TEST(unit_vdp_text, delete_lines_scroll_up_once_per_row) {
  VdpTextState state = { .column = 7, .row = 10, .numRows = 60,
    .numColumns = 106 };
  uint8_t output[64];
  const uint8_t expected[] = {
    28, 0, 59, 105, 10,
    23, 7, 0, 3, 0,  23, 7, 0, 3, 0,  23, 7, 0, 3, 0,
    26, 31, 0, 10
  };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    translateAll(&state, "\x1b[3M", output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
  NANO_OS_ASSERT_EQ_INT(0, state.column);
  NANO_OS_ASSERT_EQ_INT(10, state.row);
  NANO_OS_ASSERT_EQ_INT(0, state.scrollDirection);
}

NANO_OS_TEST(unit_vdp_text, a_zero_count_inserts_or_deletes_one_row) {
  VdpTextState state = { .row = 2, .numRows = 60, .numColumns = 106 };
  uint8_t output[64];
  const uint8_t expected[] = {
    28, 0, 59, 105, 2,  23, 7, 0, 3, 0,  26, 31, 0, 2
  };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    translateAll(&state, "\x1b[0M", output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
}

NANO_OS_TEST(unit_vdp_text, insert_and_delete_never_scroll_past_the_bottom) {
  VdpTextState state = { .row = 1, .numRows = 3, .numColumns = 4 };
  uint8_t output[64];
  const uint8_t expected[] = {
    28, 0, 2, 3, 1,  23, 7, 0, 2, 0,  23, 7, 0, 2, 0,  26, 31, 0, 1
  };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    translateAll(&state, "\x1b[9L", output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);

  state.row = 2;
  const uint8_t bottom[] = { 28, 0, 2, 3, 2,  23, 7, 0, 3, 0,  26, 31, 0, 2 };
  NANO_OS_ASSERT_EQ_INT(sizeof(bottom),
    translateAll(&state, "\x1b[200M", output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, bottom, sizeof(bottom)) == 0);
}

NANO_OS_TEST(unit_vdp_text, insert_line_split_across_calls_is_translated) {
  VdpTextState state = { .row = 6, .numRows = 60, .numColumns = 106 };
  uint8_t output[64];
  size_t total = translateAll(&state, "\x1b[", output, 32);
  total += translateAll(&state, "2", &output[total], 32);
  total += translateAll(&state, "L", &output[total], 32);
  const uint8_t expected[] = {
    28, 0, 59, 105, 6,  23, 7, 0, 2, 0,  23, 7, 0, 2, 0,  26, 31, 0, 6
  };

  NANO_OS_ASSERT_EQ_INT(sizeof(expected), total);
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
}

NANO_OS_TEST(unit_vdp_text, insert_lines_that_do_not_fit_are_finished_later) {
  VdpTextState state = { .row = 20, .numRows = 60, .numColumns = 106 };
  uint8_t output[VDP_TEXT_MAX_EXPANSION];
  uint8_t all[128];
  size_t total = 0;
  const char *text = "\x1b[4Lab";

  int calls = 0;
  while (((*text != '\0') || (state.scrollDirection != 0)) && (calls < 40)) {
    size_t length = vdpTranslateText(&state, &text, output, sizeof(output));
    NANO_OS_ASSERT_TRUE(length <= sizeof(output));
    memcpy(&all[total], output, length);
    total += length;
    calls++;
  }

  const uint8_t expected[] = {
    28, 0, 59, 105, 20,
    23, 7, 0, 2, 0,  23, 7, 0, 2, 0,  23, 7, 0, 2, 0,  23, 7, 0, 2, 0,
    26, 31, 0, 20,  'a', 'b'
  };
  NANO_OS_ASSERT_EQ_INT(sizeof(expected), total);
  NANO_OS_ASSERT_TRUE(memcmp(all, expected, sizeof(expected)) == 0);
}

NANO_OS_TEST(unit_vdp_text, cursor_wraps_like_the_vdp_with_scroll_protection) {
  VdpTextState state = { .column = 0, .numRows = 3, .numColumns = 4 };
  uint8_t output[32];

  translateAll(&state, "abcd", output, 32);
  NANO_OS_ASSERT_EQ_INT(0, state.column);
  NANO_OS_ASSERT_EQ_INT(1, state.row);

  translateAll(&state, "\n\x1b[3;4Hz", output, 32);
  NANO_OS_ASSERT_EQ_INT(4, state.column);
  NANO_OS_ASSERT_EQ_INT(2, state.row);

  translateAll(&state, "y", output, 32);
  NANO_OS_ASSERT_EQ_INT(1, state.column);
  NANO_OS_ASSERT_EQ_INT(2, state.row);

  translateAll(&state, "\n\n\t", output, 32);
  NANO_OS_ASSERT_EQ_INT(2, state.row);
}

NANO_OS_TEST(unit_vdp_text, backspace_at_the_left_edge_goes_up_a_row) {
  VdpTextState state = { .column = 0, .numRows = 3, .numColumns = 4 };
  uint8_t output[32];

  translateAll(&state, "\x1b[2;1H\b", output, 32);
  NANO_OS_ASSERT_EQ_INT(3, state.column);
  NANO_OS_ASSERT_EQ_INT(0, state.row);

  translateAll(&state, "\b\x1b[1;1H\b", output, 32);
  NANO_OS_ASSERT_EQ_INT(3, state.column);
  NANO_OS_ASSERT_EQ_INT(0, state.row);
}

NANO_OS_TEST(unit_vdp_text, escape_sequence_spans_separate_writes) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[4";
  const uint8_t expected[] = { 17, 128 + CGA_INDEX_RED };

  NANO_OS_ASSERT_EQ_INT(0, vdpTranslateText(&state, &text, output, 32));
  text = "1m";
  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    vdpTranslateText(&state, &text, output, 32));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
}

NANO_OS_TEST(unit_vdp_text, a_full_buffer_never_splits_an_escape_sequence) {
  VdpTextState state = { .column = 0, .numRows = 60, .numColumns = 106 };
  uint8_t output[32];
  const char *text = "\x1b[44;93m";
  const uint8_t expected[] = { 17, CGA_INDEX_YELLOW,
    17, 128 + CGA_INDEX_BLUE };

  NANO_OS_ASSERT_EQ_INT(0, vdpTranslateText(&state, &text, output, 3));
  NANO_OS_ASSERT_EQ_INT('\x1b', *text);
  NANO_OS_ASSERT_EQ_INT(VDP_ESCAPE_NONE, state.escapeState);
  NANO_OS_ASSERT_EQ_INT(sizeof(expected),
    vdpTranslateText(&state, &text, output, 4));
  NANO_OS_ASSERT_TRUE(memcmp(output, expected, sizeof(expected)) == 0);
  NANO_OS_ASSERT_EQ_INT('\0', *text);
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
