///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              03.15.2026
///
/// @file              NanoOsDebug.h
///
/// @brief             NanoOs-specific functionality for debugging in userspace
///                    processes.
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

#ifndef NANO_OS_USER_DEBUG_H
#define NANO_OS_USER_DEBUG_H

#if defined(NANO_OS_USER_DEBUG) || defined(NANO_OS_DEBUG)

/// @def startDebugMessage
///
/// @brief Print a non-newline-terminated debug message.
#define startDebugMessage(message) \
  printString("["); \
  printInt(HAL->getElapsedMicroseconds(0)); \
  printString(" Task "); \
  printInt(getRunningTaskId()); \
  printString(" "); \
  printString((strrchr(__FILE__, '/')) \
    ? (strrchr(__FILE__, '/')  + 1) \
    : __FILE__); \
  printString(":"); \
  printString(__func__); \
  printString("."); \
  printInt(__LINE__); \
  printString("] "); \
  printString(message);

/// @def printDebugStackDepth()
///
/// @brief Print the depth of the current coroutine stack.
#define printDebugStackDepth() \
  do { \
    char temp; \
    printString("Stack depth: "); \
    printInt(ABS_DIFF((uintptr_t) &temp, (uintptr_t) getRunningCoroutine())); \
    printString("\n"); \
  } while (0)

/// @def printDebugString
///
/// @brief Print a string value when userspace debugging is enabled.
#define printDebugString(message) printString(message)

/// @def printDebugInt
///
/// @brief Print an integer value when userspace debugging is enabled.
#define printDebugInt(intValue) printInt(intValue)

/// @def printDebugDouble
///
/// @brief Print a double value when userspace debugging is enabled.
#define printDebugDouble(floatingPointValue) printDouble(floatingPointValue)

/// @def printDebugHex
///
/// @brief Print a hex value when userspace debugging is enabled.
#define printDebugHex(hexValue) printHex(hexValue)

#else // defined(NANO_OS_USER_DEBUG) || defined(NANO_OS_DEBUG)

#define startDebugMessage(message) {}
#define printDebugStackDepth() {}
#define printDebugString(message) {}
#define printDebugInt(intValue) {}
#define printDebugDouble(floatingPointValue) {}
#define printDebugHex(hexValue) {}

#endif // defined(NANO_OS_USER_DEBUG) || defined(NANO_OS_DEBUG)


#endif // NANO_OS_USER_DEBUG_H

