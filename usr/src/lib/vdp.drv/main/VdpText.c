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
/// @file VdpText.c
///
/// @brief Translation of console text into the VDU byte stream the Agon
/// Light 2 VDP draws.

#include <string.h>

#include "VdpText.h"

/// @def VDU_CLEAR_SCREEN
///
/// @brief VDU 12, which clears the text area to the text background and
/// homes the cursor.
#define VDU_CLEAR_SCREEN 12

/// @def VDU_TEXT_COLOR
///
/// @brief VDU 17,color, which sets the text color, or the text background if
/// VDU_BACKGROUND is added to the color.
#define VDU_TEXT_COLOR 17

/// @def VDU_BACKGROUND
///
/// @brief The amount added to a VDU_TEXT_COLOR color to set the background.
#define VDU_BACKGROUND 128

/// @def VDU_TAB
///
/// @brief VDU 31,column,row, which moves the cursor.
#define VDU_TAB 31

/// @def VDU_PRINT_NEXT
///
/// @brief VDU 27,c, which draws the glyph of c even if c is a VDU command.
#define VDU_PRINT_NEXT 27

/// @def VDU_RESET_VIEWPORTS
///
/// @brief VDU 26, which makes the text viewport the whole screen again and
/// homes the cursor.
#define VDU_RESET_VIEWPORTS 26

/// @def VDU_TEXT_VIEWPORT
///
/// @brief VDU 28,left,bottom,right,top, which limits text, and scrolling, to
/// a rectangle of character cells.
#define VDU_TEXT_VIEWPORT 28

/// @def VDU_SYSTEM
///
/// @brief VDU 23, which starts a system command.
#define VDU_SYSTEM 23

/// @def VDU_SYSTEM_SCROLL
///
/// @brief VDU 23,7,extent,direction,movement, which scrolls a viewport.  A
/// movement of 0 scrolls by one character row.
#define VDU_SYSTEM_SCROLL 7

/// @def VDU_SCROLL_DOWN
///
/// @brief The VDU 23,7 direction that moves the text down the screen.
#define VDU_SCROLL_DOWN 2

/// @def VDU_SCROLL_UP
///
/// @brief The VDU 23,7 direction that moves the text up the screen.
#define VDU_SCROLL_UP 3

/// @def VDP_SCROLL_VIEWPORT
///
/// @brief The flag added to a VdpTextState's scrollDirection while the text
/// viewport to scroll has yet to be set.
#define VDP_SCROLL_VIEWPORT 0x80

/// @def VDP_ESCAPE_MAX_OUTPUT
///
/// @brief The most VDU bytes a byte of an escape sequence translates to.
#define VDP_ESCAPE_MAX_OUTPUT 4

/// @def VDP_SCROLL_MAX_OUTPUT
///
/// @brief The most VDU bytes a step of inserting or deleting rows writes.
#define VDP_SCROLL_MAX_OUTPUT 5

ANSI_TO_CGA_MAP

/// @fn static void vdpEndParameter(VdpTextState *state)
///
/// @brief Finish reading a control sequence parameter, applying it to the
/// colors the sequence selects as a Select Graphic Rendition parameter.
///
/// @param state A pointer to the VdpTextState of the text being written.
///
/// @return This function returns no value.
static void vdpEndParameter(VdpTextState *state) {
  uint8_t parameter = state->parameter;
  if (parameter > state->maxParameter) {
    state->maxParameter = parameter;
  }
  if (state->numParameters == 0) {
    state->firstParameter = parameter;
  } else if (state->numParameters == 1) {
    state->secondParameter = parameter;
  }
  state->numParameters++;
  state->parameter = 0;

  if (parameter == ANSI_SGR_RESET) {
    state->foreground = VDP_TEXT_DEFAULT_FOREGROUND;
    state->background = VDP_TEXT_DEFAULT_BACKGROUND;
  } else if (parameter == ANSI_SGR_FOREGROUND_DEFAULT) {
    state->foreground = VDP_TEXT_DEFAULT_FOREGROUND;
  } else if (parameter == ANSI_SGR_BACKGROUND_DEFAULT) {
    state->background = VDP_TEXT_DEFAULT_BACKGROUND;
  } else {
    // Folding the bright and background ranges onto the foreground range
    // keeps this small enough for the overlay to fit on the SAMD21.
    uint8_t bright = 0;
    uint8_t *color = &state->foreground;
    if (parameter >= ANSI_SGR_BRIGHT_FOREGROUND) {
      parameter -= ANSI_SGR_BRIGHT_FOREGROUND - ANSI_SGR_FOREGROUND;
      bright = 8;
    }
    if (parameter >= ANSI_SGR_BACKGROUND) {
      parameter -= ANSI_SGR_BACKGROUND - ANSI_SGR_FOREGROUND;
      color = &state->background;
    }
    parameter -= ANSI_SGR_FOREGROUND;
    if (parameter < 8) {
      *color = ansiToCgaMap[parameter + bright];
    }
  }
}

/// @fn static uint8_t vdpCursorIndex(uint8_t parameter, uint8_t limit)
///
/// @brief Convert a one-based cursor position parameter, where 0 means 1, to
/// a zero-based index less than a limit.
///
/// @param parameter The parameter.
/// @param limit The number of rows or columns.
///
/// @return Returns the index.
static uint8_t vdpCursorIndex(uint8_t parameter, uint8_t limit) {
  uint8_t index = (parameter > 1) ? (uint8_t) (parameter - 1) : 0;
  return (index < limit) ? index : (uint8_t) (limit - 1);
}

/// @fn static void vdpAdvanceCursor(VdpTextState *state)
///
/// @brief Track the cursor moving past a character the VDP draws.  With
/// scroll protection on, the VDP wraps to the next row as soon as the last
/// column is drawn, except on the bottom row, where it waits for the next
/// character so that drawing the last column doesn't scroll.
///
/// @param state A pointer to the VdpTextState of the text being written.
///
/// @return This function returns no value.
static void vdpAdvanceCursor(VdpTextState *state) {
  if (state->column >= state->numColumns) {
    state->column = 0;
  }
  state->column++;
  if ((state->column >= state->numColumns) && (state->row + 1 < state->numRows)) {
    state->column = 0;
    state->row++;
  }
}

/// @fn static size_t vdpTranslateEscape(VdpTextState *state, uint8_t c,
///   uint8_t *output)
///
/// @brief Advance an escape sequence by one byte.  A control sequence that
/// sets colors, clears the screen, or moves the cursor becomes VDU bytes when
/// it ends, one that inserts or deletes rows, erases to the end of the line,
/// or turns resize reports on or off records that it did, and every other
/// escape sequence is dropped.
///
/// @param state A pointer to the VdpTextState to advance.
/// @param c The byte of the escape sequence.
/// @param output A buffer of at least VDP_ESCAPE_MAX_OUTPUT bytes.
///
/// @return Returns the number of bytes written to output.
static size_t vdpTranslateEscape(VdpTextState *state, uint8_t c,
  uint8_t *output
) {
  size_t length = 0;

  if (c == '\033') {
    state->escapeState = VDP_ESCAPE_STARTED;
  } else if (state->escapeState == VDP_ESCAPE_STARTED) {
    if (c == '[') {
      state->escapeState = VDP_ESCAPE_CSI;
      state->parameter = 0;
      state->maxParameter = 0;
      state->numParameters = 0;
      state->secondParameter = 0;
      state->foreground = VDP_TEXT_COLOR_UNCHANGED;
      state->background = VDP_TEXT_COLOR_UNCHANGED;
    } else if ((c < 0x20) || (c > 0x2f)) {
      // Anything but an intermediate byte ends a two-byte escape sequence.
      state->escapeState = VDP_ESCAPE_NONE;
    }
  } else if (state->escapeState == VDP_ESCAPE_CSI_PRIVATE) {
    if ((c >= '0') && (c <= '9')) {
      state->privateMode = (state->privateMode < 1000)
        ? (uint16_t) ((state->privateMode * 10) + (c - '0')) : UINT16_MAX;
    } else if ((c >= 0x40) && (c <= 0x7e)) {
      if ((state->privateMode == VDP_RESIZE_REPORT_MODE)
        && ((c == 'h') || (c == 'l'))
      ) {
        state->resizeReports = (c == 'h');
        state->resizeReportWanted = (c == 'h');
      }
      state->escapeState = VDP_ESCAPE_NONE;
    }
  } else if ((c >= '0') && (c <= '9')) {
    unsigned int parameter = (state->parameter * 10u) + (c - '0');
    state->parameter = (parameter > 255) ? 255 : (uint8_t) parameter;
  } else if (c == ';') {
    vdpEndParameter(state);
  } else if ((c == '?') && (state->escapeState == VDP_ESCAPE_CSI)) {
    state->escapeState = VDP_ESCAPE_CSI_PRIVATE;
    state->privateMode = 0;
  } else if ((c >= 0x20) && (c <= 0x3f)) {
    state->escapeState = VDP_ESCAPE_CSI_IGNORED;
  } else {
    if ((state->escapeState == VDP_ESCAPE_CSI) && (c >= 0x40) && (c <= 0x7e)) {
      vdpEndParameter(state);
      if (c == 'm') {
        if (state->foreground != VDP_TEXT_COLOR_UNCHANGED) {
          output[length++] = VDU_TEXT_COLOR;
          output[length++] = state->foreground;
        }
        if (state->background != VDP_TEXT_COLOR_UNCHANGED) {
          output[length++] = VDU_TEXT_COLOR;
          output[length++] = VDU_BACKGROUND + state->background;
        }
      } else if ((c == 'J') && (state->maxParameter == 2)) {
        output[length++] = VDU_CLEAR_SCREEN;
        state->column = 0;
        state->row = 0;
      } else if ((c == 'H') || (c == 'f')) {
        state->row = vdpCursorIndex(state->firstParameter, state->numRows);
        state->column = vdpCursorIndex(state->secondParameter, state->numColumns);
        output[length++] = VDU_TAB;
        output[length++] = state->column;
        output[length++] = state->row;
      } else if ((c == 'K') && (state->maxParameter == 0)
        && (state->column < state->numColumns)
      ) {
        state->erasePending = state->numColumns - state->column;
      } else if ((c == 'L') || (c == 'M')) {
        state->scrollRows = vdpCursorIndex(state->firstParameter,
          state->numRows - state->row) + 1;
        state->scrollDirection = VDP_SCROLL_VIEWPORT
          + ((c == 'L') ? VDU_SCROLL_DOWN : VDU_SCROLL_UP);
        state->column = 0;
      }
    }
    state->escapeState = VDP_ESCAPE_NONE;
  }

  return length;
}

/// @fn size_t vdpTranslateText(VdpTextState *state, const char **text,
///   uint8_t *output, size_t outputSize)
///
/// @brief Translate as much of a NUL-terminated string as fits into a buffer
/// of VDU bytes.  Newlines become a carriage return and a line feed, tabs
/// become spaces to the next multiple of 8 columns, ANSI escape sequences
/// are translated by vdpTranslateEscape, and other control characters and
/// DEL are drawn as their glyphs with VDU_PRINT_NEXT.  An erase to the end of
/// the line, or an insertion or deletion of rows, that doesn't fit is
/// finished by the calls that follow, even once the string is used up.
///
/// @param state A pointer to the VdpTextState of the text being written.
/// @param text A pointer to the string, advanced past each character that is
///   translated.
/// @param output The buffer to fill with VDU bytes.
/// @param outputSize The size of the buffer.  At least VDP_TEXT_MAX_EXPANSION
///   bytes guarantees that each call translates at least one character.
///
/// @return Returns the number of bytes written to output.
size_t vdpTranslateText(VdpTextState *state, const char **text,
  uint8_t *output, size_t outputSize
) {
  size_t length = 0;
  for (;; (*text)++) {
    // Rows are inserted or deleted by scrolling a text viewport from the
    // cursor's row to the bottom.  Filling a step and copying it is smaller
    // on the eZ80 than writing each byte to output.
    while (state->scrollDirection != 0) {
      if (length + VDP_SCROLL_MAX_OUTPUT > outputSize) {
        return length;
      }
      uint8_t step[VDP_SCROLL_MAX_OUTPUT] = {
        VDU_SYSTEM, VDU_SYSTEM_SCROLL, 0, state->scrollDirection, 0
      };
      size_t stepLength = VDP_SCROLL_MAX_OUTPUT;
      if (state->scrollDirection & VDP_SCROLL_VIEWPORT) {
        step[0] = VDU_TEXT_VIEWPORT;
        step[1] = 0;
        step[2] = state->numRows - 1;
        step[3] = state->numColumns - 1;
        step[4] = state->row;
        state->scrollDirection &= ~VDP_SCROLL_VIEWPORT;
      } else if (state->scrollRows > 0) {
        state->scrollRows--;
      } else {
        step[0] = VDU_RESET_VIEWPORTS;
        step[1] = VDU_TAB;
        step[2] = state->column;
        step[3] = state->row;
        stepLength = 4;
        state->scrollDirection = 0;
      }
      memcpy(&output[length], step, stepLength);
      length += stepLength;
    }
    // Erasing writes spaces to the end of the line, then moves back.
    while (state->erasePending > 0) {
      if (length + 1 + VDP_ESCAPE_MAX_OUTPUT > outputSize) {
        return length;
      }
      output[length++] = ' ';
      if (--state->erasePending == 0) {
        output[length++] = VDU_TAB;
        output[length++] = state->column;
        output[length++] = state->row;
      }
    }
    uint8_t c = (uint8_t) **text;
    if (c == '\0') {
      break;
    }
    if ((c == '\033') || (state->escapeState != VDP_ESCAPE_NONE)) {
      if (length + VDP_ESCAPE_MAX_OUTPUT > outputSize) {
        break;
      }
      length += vdpTranslateEscape(state, c, &output[length]);
      continue;
    }

    size_t needed = ((c < ' ') || (c == 127)) ? 2 : 1;
    if (c == '\t') {
      needed = 8 - (state->column & 7);
    }
    if (length + needed > outputSize) {
      break;
    }

    if (c == '\n') {
      output[length++] = '\r';
      output[length++] = '\n';
      state->column = 0;
      if (state->row + 1 < state->numRows) {
        state->row++;
      }
    } else if (c == '\t') {
      for (size_t ii = 0; ii < needed; ii++) {
        output[length++] = ' ';
        vdpAdvanceCursor(state);
      }
    } else if (c == '\r') {
      output[length++] = c;
      state->column = 0;
    } else if (c == '\b') {
      output[length++] = c;
      if (state->column > 0) {
        state->column--;
      } else {
        // The VDP wraps to the end of the row above, or scrolls at the top.
        state->column = state->numColumns - 1;
        if (state->row > 0) {
          state->row--;
        }
      }
    } else if (c == '\a') {
      output[length++] = c;
    } else {
      if ((c < ' ') || (c == 127)) {
        output[length++] = VDU_PRINT_NEXT;
      }
      output[length++] = c;
      vdpAdvanceCursor(state);
    }
  }

  return length;
}

/// @fn void vdpBufferOutput(VdpOutputBuffer *buffer,
///   const uint8_t *data, size_t length)
///
/// @brief Append VDU bytes to a VdpOutputBuffer, dropping its oldest bytes
/// when there isn't room for all of them.
///
/// @param buffer A pointer to the VdpOutputBuffer.
/// @param data The bytes to append.
/// @param length The number of bytes in data.
///
/// @return This function returns no value.
void vdpBufferOutput(VdpOutputBuffer *buffer,
  const uint8_t *data, size_t length
) {
  if (length >= VDP_OUTPUT_BUFFER_SIZE) {
    data += length - VDP_OUTPUT_BUFFER_SIZE;
    length = VDP_OUTPUT_BUFFER_SIZE;
    buffer->length = 0;
  } else if (buffer->length + length > VDP_OUTPUT_BUFFER_SIZE) {
    size_t drop = buffer->length + length - VDP_OUTPUT_BUFFER_SIZE;
    buffer->length -= (uint16_t) drop;
    memmove(buffer->data, &buffer->data[drop], buffer->length);
  }
  memcpy(&buffer->data[buffer->length], data, length);
  buffer->length += (uint16_t) length;
}
