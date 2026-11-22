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

/// @file VdpBuffer.c
///
/// @brief The buffer of console text the VDP driver holds while it can't draw
/// it.

#include <string.h>

#include "VdpText.h"

/// @fn void vdpBufferOutput(VdpOutputBuffer *buffer,
///   const uint8_t *data, size_t length)
///
/// @brief Append bytes to a VdpOutputBuffer, dropping its oldest bytes when
/// there isn't room for all of them.
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
