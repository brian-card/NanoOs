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
/// @file VdpText.h
///
/// @brief Translation of console text into the VDU byte stream the Agon
/// Light 2 VDP draws.

#ifndef VDP_TEXT_H
#define VDP_TEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @def VDP_TEXT_MAX_EXPANSION
///
/// @brief The most VDU bytes a single character of text translates to.
#define VDP_TEXT_MAX_EXPANSION 8

/// @def VDP_OUTPUT_BUFFER_SIZE
///
/// @brief The number of VDU bytes kept for the VDP until it is listening.
#define VDP_OUTPUT_BUFFER_SIZE 256

/// @struct VdpOutputBuffer
///
/// @brief The most recent VDU bytes written before the VDP was listening.
///
/// @param length The number of bytes in data.
/// @param data The bytes, oldest first.
typedef struct VdpOutputBuffer {
  uint16_t length;
  uint8_t  data[VDP_OUTPUT_BUFFER_SIZE];
} VdpOutputBuffer;

/// @struct VdpTextState
///
/// @brief Translation state carried from one piece of text to the next.
///
/// @param column The column of the text cursor, used to expand tabs.  Only its
///   value modulo 8 matters, so it is allowed to wrap.
typedef struct VdpTextState {
  uint8_t column;
} VdpTextState;

size_t vdpTranslateText(VdpTextState *state, const char **text,
  uint8_t *output, size_t outputSize);
void vdpBufferOutput(VdpOutputBuffer *buffer,
  const uint8_t *data, size_t length);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VDP_TEXT_H
