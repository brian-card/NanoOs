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

#include "VdpText.h"

/// @fn size_t vdpTranslateText(VdpTextState *state, const char **text,
///   uint8_t *output, size_t outputSize)
///
/// @brief Translate as much of a NUL-terminated string as fits into a buffer
/// of VDU bytes.  Newlines become a carriage return and a line feed, tabs
/// become spaces to the next multiple of 8 columns, and control characters
/// the VDP would read as the start of a VDU command are dropped.
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
    size_t needed = 1;
    if (c == '\n') {
      needed = 2;
    } else if (c == '\t') {
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
    } else if ((c >= ' ') && (c != 127)) {
      output[length++] = c;
      state->column++;
    }
  }

  return length;
}
