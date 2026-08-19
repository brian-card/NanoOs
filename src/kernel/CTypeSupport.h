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

/// @struct Integer
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
typedef struct Integer {
  bool signedType;
  bool negative;
  int numUInts;
  unsigned int uInts[1];
} Integer;

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

/// @struct I32
///
/// @brief Type to deal with 32-bit values.
///
/// @param signedType Boolean value to indicate whether or not the value is
///   intended to be treated as a signed type.
/// @param negative Boolean value to indicate whether or not the encoded value
///   is negative.
/// @param numUInts The number of unsigned int values the type holds.
/// @param uInts Unsigned int values that represent the full 32-bit value.
typedef struct I32 {
  bool signedType;
  bool negative;
  int numUInts;
  unsigned int uInts[(sizeof(uint32_t) + (sizeof(int) - 1)) / sizeof(int)];
} I32;

/// @def integerToInt
///
/// @brief Cast an Integer to a standard system integer (int data type).
///
/// @param value A pointer to an Integer-compatible value.
#define integerToInt(value) ((int) (value)->uInts[0])

void integerInit_(Integer *value, bool signedType,
  int numUInts, ...);
#define integerInit(value, signedType, initialValue) \
  integerInit_((Integer*) (value), (signedType), \
  (sizeof(initialValue) + (sizeof(int) - 1)) / sizeof(int), (initialValue))
void integerCopy_(Integer *dest, const Integer *src);
#define integerCopy(dest, src) \
  integerCopy_((Integer*) (dest), (Integer*) (src))
void integerShiftLeft_(Integer *value, int numBits);
#define integerShiftLeft(value, numBits) \
  integerShiftLeft_((Integer*) (value), (numBits))
void integerShiftRight_(Integer *value, int numBits);
#define integerShiftRight(value, numBits) \
  integerShiftRight_((Integer*) (value), (numBits))
bool integerEqual_(const Integer *a, const Integer *b);
#define integerEqual(a, b) \
  integerEqual_((Integer*) (a), (Integer*) (b))
bool integerGreaterThan_(
  const Integer *a, const Integer *b);
#define integerGreaterThan(a, b) \
  integerGreaterThan_((Integer*) (a), (Integer*) (b))
bool integerLessThan_(
  const Integer *a, const Integer *b);
#define integerLessThan(a, b) \
  integerLessThan_((Integer*) (a), (Integer*) (b))
bool integerGreaterOrEqual_(
  const Integer *a, const Integer *b);
#define integerGreaterOrEqual(a, b) \
  integerGreaterOrEqual_((Integer*) (a), (Integer*) (b))
bool integerLessOrEqual_(
  const Integer *a, const Integer *b);
#define integerLessOrEqual(a, b) \
  integerLessOrEqual_((Integer*) (a), (Integer*) (b))
void integerAdd_(Integer *a, const Integer *b);
#define integerAdd(a, b) \
  integerAdd_((Integer*) (a), (Integer*) (b))
void integerSubtract_(Integer *a, const Integer *b);
#define integerSubtract(a, b) \
  integerSubtract_((Integer*) (a), (Integer*) (b))
void integerMultiply_(Integer *a, const Integer *b);
#define integerMultiply(a, b) \
  integerMultiply_((Integer*) (a), (Integer*) (b))
void integerDivide_(
  const Integer *dividend, const Integer *divisor,
  Integer *quotient, Integer *remainder);
#define integerDivide(dividend, divisor, quotient, remainder) \
  integerDivide_( \
    (Integer*) (dividend), \
    (Integer*) (divisor), \
    (Integer*) (quotient), \
    (Integer*) (remainder))

#ifdef __cplusplus
} // extern "C"
#endif

#endif // C_TYPE_SUPPORT_H
