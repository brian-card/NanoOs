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
#include "stdarg.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @struct UnsupportedType
///
/// @brief Generic type to deal with types larger than built-in ints.  All other
/// support types must be compatible with this one.
///
/// @param signedType Boolean value to indicate whether or not the value is
///   intended to be treated as a signed type.
/// @param negative Boolean value to indicate whether or not the encoded value
///   is negative.
/// @param numUInts The number of unsigned int values the type holds.
/// @param uInts Flexible array of unsigned int values to hold the full value of
///   the represented value.  Declared as an array of one element here so that
///    C++ compilers won't complain.  Index 0 is the lowest-order int of the
///   value.  Higher indexes represent the higer-order ints.
typedef struct UnsupportedType {
  bool signedType;
  bool negative;
  int numUInts;
  unsigned int uInts[1];
} UnsupportedType;

/// @struct I64
///
/// @brief Type to deal with 64-bit values.
///
/// @param signedType Boolean value to indicate whether or not the value is
///   intended to be treated as a signed type.
/// @param negative Boolean value to indicate whether or not the encoded value
///   is negative.
/// @param numUInts The number of unsigned int values the type holds.
/// @param uInts Unsigned int values that represent the full 64-bit value.
typedef struct I64 {
  bool signedType;
  bool negative;
  int numUInts;
  unsigned int uInts[(sizeof(uint64_t) + (sizeof(int) - 1)) / sizeof(int)];
} I64;

/// @def unsupportedTypeToInt
///
/// @brief Cast an UnsupportedType to a standard system integer (int data type).
///
/// @param value A pointer to an UnsupportedType-compatible value.
#define unsupportedTypeToInt(value) ((int) (value)->uInts[0])

void unsupportedTypeInit_(UnsupportedType *value, bool signedType,
  int numUInts, ...);
#define unsupportedTypeInit(value, signedType, initialValue) \
  unsupportedTypeInit_((UnsupportedType*) (value), (signedType), \
  (sizeof(initialValue) + (sizeof(int) - 1)) / sizeof(int), (initialValue))
void unsupportedTypeCopy_(UnsupportedType *dest, const UnsupportedType *src);
#define unsupportedTypeCopy(dest, src) \
  unsupportedTypeCopy_((UnsupportedType*) (dest), (UnsupportedType*) (src))
void unsupportedTypeShiftLeft_(UnsupportedType *value, int numBits);
#define unsupportedTypeShiftLeft(value, numBits) \
  unsupportedTypeShiftLeft_((UnsupportedType*) (value), (numBits))
void unsupportedTypeShiftRight_(UnsupportedType *value, int numBits);
#define unsupportedTypeShiftRight(value, numBits) \
  unsupportedTypeShiftRight_((UnsupportedType*) (value), (numBits))
bool unsupportedTypeEqual_(const UnsupportedType *a, const UnsupportedType *b);
#define unsupportedTypeEqual(a, b) \
  unsupportedTypeEqual_((UnsupportedType*) (a), (UnsupportedType*) (b))
bool unsupportedTypeGreaterThan_(
  const UnsupportedType *a, const UnsupportedType *b);
#define unsupportedTypeGreaterThan(a, b) \
  unsupportedTypeGreaterThan_((UnsupportedType*) (a), (UnsupportedType*) (b))
bool unsupportedTypeLessThan_(
  const UnsupportedType *a, const UnsupportedType *b);
#define unsupportedTypeLessThan(a, b) \
  unsupportedTypeLessThan_((UnsupportedType*) (a), (UnsupportedType*) (b))
bool unsupportedTypeGreaterOrEqual_(
  const UnsupportedType *a, const UnsupportedType *b);
#define unsupportedTypeGreaterOrEqual(a, b) \
  unsupportedTypeGreaterOrEqual_((UnsupportedType*) (a), (UnsupportedType*) (b))
bool unsupportedTypeLessOrEqual_(
  const UnsupportedType *a, const UnsupportedType *b);
#define unsupportedTypeLessOrEqual(a, b) \
  unsupportedTypeLessOrEqual_((UnsupportedType*) (a), (UnsupportedType*) (b))
void unsupportedTypeAdd_(UnsupportedType *a, const UnsupportedType *b);
#define unsupportedTypeAdd(a, b) \
  unsupportedTypeAdd_((UnsupportedType*) (a), (UnsupportedType*) (b))
void unsupportedTypeSubtract_(UnsupportedType *a, const UnsupportedType *b);
#define unsupportedTypeSubtract(a, b) \
  unsupportedTypeSubtract_((UnsupportedType*) (a), (UnsupportedType*) (b))
void unsupportedTypeMultiply_(UnsupportedType *a, const UnsupportedType *b);
#define unsupportedTypeMultiply(a, b) \
  unsupportedTypeMultiply_((UnsupportedType*) (a), (UnsupportedType*) (b))
void unsupportedTypeDivide_(
  const UnsupportedType *dividend, const UnsupportedType *divisor,
  UnsupportedType *quotient, UnsupportedType *remainder);
#define unsupportedTypeDivide(dividend, divisor, quotient, remainder) \
  unsupportedTypeDivide_( \
    (UnsupportedType*) (dividend), \
    (UnsupportedType*) (divisor), \
    (UnsupportedType*) (quotient), \
    (UnsupportedType*) (remainder))

#ifdef __cplusplus
} // extern "C"
#endif

#endif // C_TYPE_SUPPORT_H
