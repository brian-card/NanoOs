///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              07.15.2026
///
/// @file              NanoOsExecutive.h
///
/// @brief             Exposed NanoOs kernel functionality related to executive
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

#ifndef NANO_OS_EXECUTIVE_H
#define NANO_OS_EXECUTIVE_H

#include "NanoOsUser.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Debug functions
static inline int printString_(const char *string) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->printString(string);
  }
  return -EPERM;
}
#define printString(str) printString_((const char*) (str))
static inline int printInt_(long long int integer) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->printInt(integer);
  }
  return -EPERM;
}
#define printInt(value) printInt_((long long int) (value))
static inline int printDouble(double floatingPointValue) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->printDouble(
      floatingPointValue);
  }
  return -EPERM;
}
static inline int printHex_(unsigned long long int integer) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->printHex(integer);
  }
  return -EPERM;
}
#define printHex(integer) printHex_((unsigned long long int) (integer))

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_EXECUTIVE_H

