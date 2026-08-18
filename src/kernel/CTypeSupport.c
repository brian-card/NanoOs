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

// Standard C includes
#include "string.h"

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
/// @param ... A single, unsupported value that is a multiple of 32 bits in
///   size.  This will be broken up and read in as multiple 32-bit values.
///
/// @return This function returns no value.
void unsupportedTypeInit_(UnsupportedType *value, bool signedType,
  int numU32s, ...
) {
  value->signedType = signedType;
  value->negative = false;
  value->numU32s = numU32s;
  
  va_list args;
  va_start(args, numU32s);
  
  if (sizeof(uintptr_t) < 8) {
    if (HOST_IS_LITTLE_ENDIAN) {
      // LSB first, so set the values in order.
      for (int ii = 0; ii < numU32s; ii++) {
        value->u32s[ii] = va_arg(args, uint32_t);
      }
    } else {
      // MSB first, so set the values in reverse order.
      for (int ii = numU32s - 1; ii >= 0; ii--) {
        value->u32s[ii] = va_arg(args, uint32_t);
      }
    }
  } else {
    if (HOST_IS_LITTLE_ENDIAN) {
      // LSB first, so set the values in order.
      for (int ii = 0; ii < numU32s; ii += 2) {
        uint64_t arg = va_arg(args, uint64_t);
        memcpy(&value->u32s[ii], &arg, sizeof(arg));
      }
    } else {
      // MSB first, so set the values in reverse order.
      for (int ii = numU32s - 1; ii >= 0; ii -= 2) {
        uint64_t arg = va_arg(args, uint64_t);
        memcpy(&value->u32s[ii], &arg, sizeof(arg));
      }
    }
  }
  
  va_end(args);
  
  // Lowest-order 32 bits is now at value->u32s[0] and highest-order 32 bits
  // is at value->u32s[numU32s - 1].  If the value is signed and the most-
  // significant bit of the most-significant word is set, we need to mark the
  // value negative and negate the value.
  if ((signedType == true) && (value->u32s[numU32s - 1] & 0x80000000)) {
    value->negative = true;
    
    // The way to negate a twos-compliment value is to flip all the bits and
    // then add 1.  We have to do this in multiple steps.  Flip all the bits on
    // all the values first.
    for (int ii = 0; ii < numU32s; ii++) {
      value->u32s[ii] = ~value->u32s[ii];
    }
    
    // Add 1 to the low-order 32-bit value.  If the value is 0 afterward then
    // that means we've carried over into the next value and have to add 1 to
    // that as well.  Repeat the process as long as the next value is 0.
    value->u32s[0]++;
    for (int ii = 0; (ii < (numU32s - 1)) && (value->u32s[ii] == 0); ii++) {
      value->u32s[ii + 1]++;
    }
  }
}

/// @fn void unsupportedTypeCopy_(
///   UnsupportedType *dest, const UnsupportedType *src)
///
/// @brief Copy an already-initialized UnsupportedType value to a new one.
///
/// @param dest A pointer to the destination UnsupportedType value.
/// @param src A pointer to the source UnsupportedType value.
///
/// @return This function returns no value.
void unsupportedTypeCopy_(UnsupportedType *dest, const UnsupportedType *src) {
  dest->signedType = src->signedType;
  dest->negative = src->negative;
  dest->numU32s = src->numU32s;
  for (int ii = 0; ii < dest->numU32s; ii++) {
    dest->u32s[ii] = src->u32s[ii];
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
      | ((value->u32s[ii + 1] & (((uint32_t) 0xffffffff) >> (32 - numBits)))
        << (32 - numBits));
  }
  value->u32s[value->numU32s - 1] >>= numBits;
}

/// @fn bool unsupportedTypeEqual_(const UnsupportedType *a,
///   const UnsupportedType *b)
///
/// @brief Compare two UnsupportedType values for equality (a == b).
///
/// @param a The first value to compare.
/// @param b The second value to compare.
///
/// @return Returns true if the two values are equal, false if not.
bool unsupportedTypeEqual_(const UnsupportedType *a, const UnsupportedType *b) {
  if (a->negative != b->negative) {
    return false;
  }
  
  const UnsupportedType *bigger = a;
  const UnsupportedType *smaller = b;
  if (a->numU32s != b->numU32s) {
    bigger = (a->numU32s > b->numU32s) ? a : b;
    smaller = (a->numU32s > b->numU32s) ? b : a;
    
    for (int ii = smaller->numU32s; ii < bigger->numU32s; ii++) {
      if (bigger->u32s[ii] != 0) {
        return false;
      }
    }
  }
  
  for (int ii = 0; ii < smaller->numU32s; ii++) {
    if (bigger->u32s[ii] != smaller->u32s[ii]) {
      return false;
    }
  }
  
  return true;
}

/// @fn bool unsupportedTypeAbsValGreaterThan(
///   const UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Determine if the absolute value of one UnsupportedType value is
/// greater than the absolute value of another one (|a| > |b|).
///
/// @param a The first value to compare.
/// @param b The second value to compare.
///
/// @return Returns true if the absolute value of a is strictly greater than the
/// absolute value of b, false if not.
bool unsupportedTypeAbsValGreaterThan(
  const UnsupportedType *a, const UnsupportedType *b
) {
  const UnsupportedType *bigger = a;
  const UnsupportedType *smaller = b;
  if (a->numU32s != b->numU32s) {
    bigger = (a->numU32s > b->numU32s) ? a : b;
    smaller = (a->numU32s > b->numU32s) ? b : a;

    for (int ii = bigger->numU32s - 1; ii >= smaller->numU32s; ii--) {
      if (bigger->u32s[ii] != 0) {
        return bigger == a;
      }
    }
  }

  for (int ii = smaller->numU32s - 1; ii >= 0; ii--) {
    if (a->u32s[ii] != b->u32s[ii]) {
      return (a->u32s[ii] > b->u32s[ii]);
    }
  }

  return false;
}

/// @fn bool unsupportedTypeGreaterThan_(
///   const UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Determine if one UnsupportedType value is greater than another one
/// (a > b).
///
/// @param a The first value to compare.
/// @param b The second value to compare.
///
/// @return Returns true if a is strictly greater than b, false if not.
bool unsupportedTypeGreaterThan_(
  const UnsupportedType *a, const UnsupportedType *b
) {
  if (a->negative != b->negative) {
    return (a->negative == false);
  }

  if (a->negative == true) {
    return unsupportedTypeAbsValGreaterThan(b, a);
  }

  return unsupportedTypeAbsValGreaterThan(a, b);
}

/// @fn bool unsupportedTypeLessThan_(
///   const UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Determine if one UnsupportedType value is less than another one
/// (a < b).
///
/// @param a The first value to compare.
/// @param b The second value to compare.
///
/// @return Returns true if a is strictly less than b, false if not.
bool unsupportedTypeLessThan_(
  const UnsupportedType *a, const UnsupportedType *b
) {
  if (a->negative != b->negative) {
    return (a->negative == true);
  }

  if (a->negative == true) {
    return unsupportedTypeAbsValGreaterThan(a, b);
  }

  return unsupportedTypeAbsValGreaterThan(b, a);
}

/// @fn bool unsupportedTypeGreaterOrEqual_(
///   const UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Determine if one UnsupportedType value is greater than or equal to
/// another one (a >= b).
///
/// @param a The first value to compare.
/// @param b The second value to compare.
///
/// @return Returns true if a is greater than or equal to b, false if not.
bool unsupportedTypeGreaterOrEqual_(
  const UnsupportedType *a, const UnsupportedType *b
) {
  return (unsupportedTypeGreaterThan(a, b) || unsupportedTypeEqual(a, b));
}

/// @fn bool unsupportedTypeLessOrEqual_(
///   const UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Determine if one UnsupportedType value is less than or equal to
/// another one (a <= b).
///
/// @param a The first value to compare.
/// @param b The second value to compare.
///
/// @return Returns true if a is less than or equal to b, false if not.
bool unsupportedTypeLessOrEqual_(
  const UnsupportedType *a, const UnsupportedType *b
) {
  return (unsupportedTypeLessThan(a, b) || unsupportedTypeEqual(a, b));
}

/// @fn void unsupportedTypeAbsValueAdd(
///   UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Add the absolute value of one value to another (a = |a| + |b|).
///
/// @param a The value to add to.
/// @param b The value with the absolute value to add.
///
/// @return This function returns no value.
void unsupportedTypeAbsValueAdd(UnsupportedType *a, const UnsupportedType *b) {
  const UnsupportedType *smaller = (a->numU32s > b->numU32s) ? b : a;

  uint32_t carry = 0;
  for (int ii = 0; ii < smaller->numU32s; ii++) {
    uint32_t sum = a->u32s[ii] + carry;
    uint32_t carryNext = (sum < carry);
    sum += b->u32s[ii];
    carryNext |= (sum < b->u32s[ii]);
    a->u32s[ii] = sum;
    carry = carryNext;
  }

  if ((carry > 0) && (a->numU32s > b->numU32s)) {
    a->u32s[b->numU32s] += carry;
    for (int ii = b->numU32s;
      (ii < a->numU32s - 1) && (a->u32s[ii] == 0);
      ii++
    ) {
      a->u32s[ii + 1] += carry;
    }
  }
}

/// @fn void unsupportedTypeAbsValueSubtract(
///   UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Subtract the absolute value of one value from another
/// (a = |a| - |b|).  Assumes |a| >= |b|.  Results are undefined otherwise.
///
/// @param a The value to subtract from.
/// @param b The value with the absolute value to subtract.
///
/// @return This function returns no value.
void unsupportedTypeAbsValueSubtract(
  UnsupportedType *a, const UnsupportedType *b
) {
  uint32_t borrow = 0;
  for (int ii = 0; ii < a->numU32s; ii++) {
    uint32_t aWord = a->u32s[ii];
    uint32_t bWord = 0;
    if (ii < b->numU32s) {
      bWord = b->u32s[ii];
    }
    uint32_t borrowNext = (aWord < bWord) || ((borrow > 0) && (aWord == bWord));
    a->u32s[ii] = aWord - bWord - borrow;
    borrow = borrowNext;
  }
}

/// @fn void unsupportedTypeSignedAddSubtract(
///   UnsupportedType *a, const UnsupportedType *b, bool negateB)
///
/// @brief Perform signed addition or subtraction on two UnsupportedType values
/// (a = a +/- b).  The operation performed will be determined by the signs of
/// the input values and the value of the negateB flag.
///
/// @param a A pointer to the first value that will also hold the final result
///   of the operation.
/// @param b A pointer to the second value.
/// @param negateB Whether or not to invert the value of b->negative in the
///   logic of the operations.
///
/// @return This function returns no value.
void unsupportedTypeSignedAddSubtract(
  UnsupportedType *a, const UnsupportedType *b, bool negateB
) {
  bool bIsNegative = b->negative;
  if (negateB == true) {
    bIsNegative = !bIsNegative;
  }
  
  if (a->negative == bIsNegative) {
    unsupportedTypeAbsValueAdd(a, b);
  } else if (unsupportedTypeAbsValGreaterThan(a, b)) {
    unsupportedTypeAbsValueSubtract(a, b);
  } else { // Subtract a from b
    // We need to use the largest UnsupportedType-compatible types for bCopy.
    LargestUnsupportedType bCopy;
    unsupportedTypeCopy(&bCopy, b);
    unsupportedTypeAbsValueSubtract(
      (UnsupportedType*) &bCopy, (UnsupportedType*) a);
    if (a->numU32s < b->numU32s) {
      bCopy.numU32s = a->numU32s;
    }
    unsupportedTypeCopy(a, &bCopy);
    a->negative = bIsNegative;
  }
  
  bool aIsZero = true;
  for (int ii = 0; ii < a->numU32s; ii++) {
    if (a->u32s[ii] != 0) {
      aIsZero = false;
      break;
    }
  }
  if (aIsZero) {
    a->negative = false;
  }
}

/// @fn void unsupportedTypeAdd_(UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Add a number to a number (a = a + b).
///
/// @param a The value to add to.
/// @param b The value to add.
///
/// @return This function returns no value.
void unsupportedTypeAdd_(UnsupportedType *a, const UnsupportedType *b) {
  unsupportedTypeSignedAddSubtract(a, b, false);
}

/// @fn void unsupportedTypeSubtract_(
///   UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Subtract a number from a number (a = a - b).
///
/// @param a The value to subtract from.
/// @param b The value to subtract.
///
/// @return This function returns no value.
void unsupportedTypeSubtract_(UnsupportedType *a, const UnsupportedType *b) {
  unsupportedTypeSignedAddSubtract(a, b, true);
}

/// @fn void unsupportedTypeMultiply_(
///   UnsupportedType *a, const UnsupportedType *b)
///
/// @brief Multiply two UnsupportedType values (a = a * b).
///
/// @param a A pointer to the first value that will also hold the final
///   result of the operation.
/// @param b A pointer to the second value.
///
/// @return This function returns no value.
void unsupportedTypeMultiply_(UnsupportedType *a, const UnsupportedType *b) {
  bool resultIsNegative = (a->negative != b->negative);

  LargestUnsupportedType aCopy;
  unsupportedTypeCopy(&aCopy, a);

  memset(a->u32s, 0, a->numU32s * sizeof(uint32_t));
  a->negative = false;

  int totalBits = b->numU32s * 32;
  for (int ii = 0; ii < totalBits; ii++) {
    uint32_t bit = (b->u32s[ii / 32] >> (ii % 32)) & 1;
    if (bit != 0) {
      unsupportedTypeAbsValueAdd(
        (UnsupportedType*) a, (UnsupportedType*) &aCopy);
    }
    unsupportedTypeShiftLeft((UnsupportedType*) &aCopy, 1);
  }

  a->negative = resultIsNegative;

  bool aIsZero = true;
  for (int ii = 0; ii < a->numU32s; ii++) {
    if (a->u32s[ii] != 0) {
      aIsZero = false;
      break;
    }
  }
  if (aIsZero) {
    a->negative = false;
  }
}

/// @fn void unsupportedTypeDivide_(
///   const UnsupportedType *dividend, const UnsupportedType *divisor,
///   UnsupportedType *quotient, UnsupportedType *remainder)
///
/// @brief Divide a number by a number and get the quotient and remainder.
///
/// @param dividend The number to divide.
/// @param divisor The number to divide by.
/// @param quotient A pointer to the UnsupportedType to store the quotient in.
/// @param remainder A pointer to the UnsupportedType to store the remainder in.
///
/// @return This function returns no value.
void unsupportedTypeDivide_(
  const UnsupportedType *dividend, const UnsupportedType *divisor,
  UnsupportedType *quotient, UnsupportedType *remainder
) {
  const UnsupportedType *smaller
    = (dividend->numU32s > divisor->numU32s) ? divisor : dividend;
  
  // First, clear out the quotient and remainder.
  memset(quotient->u32s, 0, quotient->numU32s * sizeof(uint32_t));
  memset(remainder->u32s, 0, remainder->numU32s * sizeof(uint32_t));
  
  // Do the division one bit at a time.
  for (int ii = ((smaller->numU32s * 32) - 1); ii >= 0; ii--) {
    unsupportedTypeShiftLeft(remainder, 1);
    
    uint32_t bit = (dividend->u32s[ii / 32] >> (ii % 32)) & 1;
    remainder->u32s[0] |= bit;
    
    if (unsupportedTypeGreaterOrEqual(remainder, divisor)) {
      unsupportedTypeSubtract(remainder, divisor);
      quotient->u32s[ii / 32] |= (((uint32_t) 1) << (ii % 32));
    }
  }
}

