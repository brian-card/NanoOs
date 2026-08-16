///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              08.16.2026
///
/// @file              CTypeSupport.h
///
/// @brief             Functionality for types that don't have native support
///                    on some platforms.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef C_TYPE_SUPPORT_H
#define C_TYPE_SUPPORT_H

// Standard C includes
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @struct UnsupportedType
///
/// @brief Generic type to deal with types larger than 32 bits.  All other
/// support types must be compatible with this one.
///
/// @param negative Boolean value to indicate whether or not the encoded value
///   is negative.
/// @param numU32s The number of unsigned, 32-bit values the type holds.
/// @param u32s Flexible array of uint32_t values to hold the full value of the
///   represented value.  Declared as an array of one element here so that C++
///   compilers won't complain.  Index 0 is the lowest-order 32 bits of the
///   value.  Higher indexes represent the higer-order groups of 32 bits.
typedef struct UnsupportedType {
  bool negative;
  int numU32s;
  uint32_t u32s[1];
} UnsupportedType;

/// @struct U64
///
/// @brief Type to deal with 64-bit values.
///
/// @param negative Boolean value to indicate whether or not the encoded value
///   is negative.
/// @param numU32s The number of unsigned, 32-bit values the type holds.  This
///   value will always be 2 in this structure.
/// @param u32s Two unsigned, 32-bit values that represent the full 64-bit
///   value.
typedef struct U64 {
  bool negative;
  int numU32s;
  uint32_t u32s[2];
} U64;

void unsupportedTypeShiftLeft_(UnsupportedType *value, int numBits);
#define unsupportedTypeShiftLeft(value, numBits) \
  unsupportedTypeShiftLeft_((UnsupportedType*) (value), (numBits))
void unsupportedTypeShiftRight_(UnsupportedType *value, int numBits);
#define unsupportedTypeShiftRight(value, numBits) \
  unsupportedTypeShiftRight_((UnsupportedType*) (value), (numBits))

#ifdef __cplusplus
} // extern "C"
#endif

#endif // C_TYPE_SUPPORT_H
