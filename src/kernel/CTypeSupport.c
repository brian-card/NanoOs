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

/// @file CTypeSupport.c
///
/// @brief Functionality for C types that don't have native support on some
/// platforms.

#include "CTypeSupport.h"

/// @var _endianDetector
///
/// @brief File-level variable used to determine whether or not the host we're
/// running on stores integers as big-endian or little-endian values.
static union {
  int integer;
  char character;
} _endianDetector = { .integer = 1 };

/// @def HOST_IS_LITTLE_ENDIAN
///
/// @brief Convenience macro to determine whether or not the host is a little-
/// endian system.
#define HOST_IS_LITTLE_ENDIAN (_endianDetector.character)

/// @fn void unsupportedTypeInit_(UnsupportedType *value, bool signedType,
///   int numU32s, ...)
///
/// @brief Initialize all the member variables of an UnsupportedType-compatible
/// value.
///
/// @param value Pointer to an UnsupportedType-compatible value.
/// @param signedType Whether or not the type is intended to be treated as
///   signed.
/// @param numU32s The number of uint32_t values that will be in the unsupported
///   value.
///
/// @return This function returns no value.
void unsupportedTypeInit_(UnsupportedType *value, bool signedType,
  int numU32s, ...
) {
  value->numU32s = numU32s;
  uint32_t msw = 0;
  
  va_list args;
  va_start(args, numU32s);
  
  if (HOST_IS_LITTLE_ENDIAN) {
    // LSB first, so set the values in order and msw comes last.
    for (int ii = 0; ii < numU32s; ii++) {
      value->u32s[ii] = va_arg(args, uint32_t);
    }
    msw = value->u32s[numU32s - 1];
  } else {
    for (int ii = numU32s - 1; ii > 0; ii--) {
      value->u32s[ii] = va_arg(args, uint32_t);
    }
    msw = value->u32s[0];
  }
  
  va_end(args);
  
  if ((signedType == true) && (msw & 0x80000000)) {
    value->negative = true;
  }
}

/// @fn void unsupportedTypeShiftLeft_(UnsupportedType *value, int numBits)
///
/// @brief Do a logical left bit shift of the value of an unsupported type.
///
/// @param value Pointer to an UnsupportedType-compatible value.
/// @param numBits The number of bits to shift the value left by.
///
/// @return This function returns no value.
void unsupportedTypeShiftLeft_(UnsupportedType *value, int numBits) {
  for (int ii = value->numU32s - 1; ii > 0; ii--) {
    value->u32s[ii] = (value->u32s[ii] << numBits)
      | (value->u32s[ii - 1] >> (32 - numBits));
  }
  value->u32s[0] <<= numBits;
}

/// @fn void unsupportedTypeShiftRight_(UnsupportedType *value, int numBits)
///
/// @brief Do a logical right bit shift of the value of an unsupported type.
///
/// @param value Pointer to an UnsupportedType-compatible value.
/// @param numBits The number of bits to shift the value right by.
///
/// @return This function returns no value.
void unsupportedTypeShiftRight_(UnsupportedType *value, int numBits) {
  for (int ii = 0; ii < (value->numU32s - 1); ii++) {
    value->u32s[ii] = (value->u32s[ii] >> numBits)
      | (value->u32s[ii + 1] & (((uint32_t) 0xffffffff) >> (32 - numBits)));
  }
  value->u32s[value->numU32s - 1] >>= numBits;
  if (value->negative == true) {
    value->u32s[value->numU32s - 1]
      |= ((uint32_t) 0xffffffff) << (32 - numBits);
  }
}

