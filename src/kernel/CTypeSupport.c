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

void unsupportedShiftLeft_(UnsupportedType *value, int numBits) {
  for (int ii = value->numU32s - 1; ii > 0; ii++) {
    value->u32s[ii] = (value->u32s[ii] << numBits)
      | (value->u32s[ii - 1] >> (32 - numBits));
  }
  value->u32s[0] <<= numBits;
}

void unsupportedShiftRight_(UnsupportedType *value, int numBits) {
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

