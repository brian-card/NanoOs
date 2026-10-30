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

/// @def VDU_HOME
///
/// @brief VDU 30, which moves the cursor to the top left corner.
#define VDU_HOME 30

/// @def VDU_PRINT_NEXT
///
/// @brief VDU 27,c, which draws the glyph of c even if c is a VDU command.
#define VDU_PRINT_NEXT 27

/// @def VDP_ESCAPE_MAX_OUTPUT
///
/// @brief The most VDU bytes a byte of an escape sequence translates to.
#define VDP_ESCAPE_MAX_OUTPUT 4

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

/// @fn static size_t vdpTranslateEscape(VdpTextState *state, uint8_t c,
///   uint8_t *output)
///
/// @brief Advance an escape sequence by one byte.  A control sequence that
/// sets colors, clears the screen, or homes the cursor becomes VDU bytes when
/// it ends, and every other escape sequence is dropped.
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
      state->foreground = VDP_TEXT_COLOR_UNCHANGED;
      state->background = VDP_TEXT_COLOR_UNCHANGED;
    } else if ((c < 0x20) || (c > 0x2f)) {
      // Anything but an intermediate byte ends a two-byte escape sequence.
      state->escapeState = VDP_ESCAPE_NONE;
    }
  } else if ((c >= '0') && (c <= '9')) {
    unsigned int parameter = (state->parameter * 10u) + (c - '0');
    state->parameter = (parameter > 255) ? 255 : (uint8_t) parameter;
  } else if (c == ';') {
    vdpEndParameter(state);
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
      } else if (((c == 'H') || (c == 'f')) && (state->maxParameter <= 1)) {
        output[length++] = VDU_HOME;
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
/// DEL are drawn as their glyphs with VDU_PRINT_NEXT.
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
  for (; **text != '\0'; (*text)++) {
    uint8_t c = (uint8_t) **text;
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
    } else if (c == '\t') {
      for (size_t ii = 0; ii < needed; ii++) {
        output[length++] = ' ';
      }
      state->column += (uint8_t) needed;
    } else if (c == '\r') {
      output[length++] = c;
      state->column = 0;
    } else if (c == '\b') {
      output[length++] = c;
      state->column--;
    } else if (c == '\a') {
      output[length++] = c;
    } else {
      if ((c < ' ') || (c == 127)) {
        output[length++] = VDU_PRINT_NEXT;
      }
      output[length++] = c;
      state->column++;
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
