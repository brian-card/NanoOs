///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              09.01.2025
///
/// @file              NanoOsUser.h
///
/// @brief             Definitions used for exporting functionality of overlays
///                    and importing functionality from the kernel.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
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
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef NANO_OS_USER_H
#define NANO_OS_USER_H

// Headers from kernel space.
#include "../../src/kernel/NanoOsOverlay.h"
#include "../../src/user/NanoOsDebug.h"

#ifdef NANO_OS_USER_DEBUG
// All of the printDebug* functions print to serial port 0 immediately.  We
// want to enable a pure, user-space print function that will go through the
// console as well.
#define printDebug(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#endif // NANO_OS_USER_DEBUG

#ifdef __cplusplus
extern "C"
{
#endif

/// @var overlayMap
///
/// @brief Global variable that will enable access to the Kernel's std C API
/// implementation.
extern NanoOsOverlayMap overlayMap;


static inline void* callOverlayFunction(
  const char *overlayDir, const char *overlay, const char *function, void *args
) {
  return overlayMap.header.osApi->callOverlayFunction(overlayDir, overlay,
    function, args);
}

// Debug functions
static inline int printString_(const char *string) {
  return overlayMap.header.osApi->printString(string);
}
#define printString(str) printString_((const char*) (str))
static inline int printInt_(long long int integer) {
  return overlayMap.header.osApi->printInt(integer);
}
#define printInt(value) printInt_((long long int) (value))
static inline int printDouble(double floatingPointValue) {
  return overlayMap.header.osApi->printDouble(floatingPointValue);
}
static inline int printHex_(unsigned long long int integer) {
  return overlayMap.header.osApi->printHex(integer);
}
#define printHex(integer) printHex_((unsigned long long int) (integer))

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_H

