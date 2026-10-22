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
/// @file BootTrace.h
///
/// @brief Temporary boot-stage markers for diagnosing an Agon Light 2 that
/// boots silently.  They expand to nothing on every other platform.

#ifndef BOOT_TRACE_H
#define BOOT_TRACE_H

#include "NanoOs.h"

#ifdef NANO_OS_AGON_LIGHT_2

void agonLight2TraceToScreen(const char *string);
void agonLight2TraceString(const char *string);
void agonLight2TraceValue(
  const char *text, unsigned long value, unsigned int base);

/// @def bootTrace
///
/// @brief Print a boot-stage marker on UART1, by polling, and on the screen
/// once the VDP has answered its general poll.
#define bootTrace(text) \
  do { \
    static const char _bootTraceText[] KEEP_IN_FLASH = "[boot] " text "\n"; \
    agonLight2TraceString(_bootTraceText); \
    agonLight2TraceToScreen(_bootTraceText); \
  } while (0)

/// @def bootTraceValue
///
/// @brief Print a boot-stage marker followed by a decimal value.
#define bootTraceValue(text, value) \
  do { \
    static const char _bootTraceText[] KEEP_IN_FLASH = "[boot] " text " "; \
    long _bootTraceNumber = (long) (value); \
    if (_bootTraceNumber < 0) { \
      static const char _bootTraceMinus[] KEEP_IN_FLASH = "[boot] " text " -"; \
      agonLight2TraceValue(_bootTraceMinus, \
        (unsigned long) -_bootTraceNumber, 10); \
    } else { \
      agonLight2TraceValue(_bootTraceText, \
        (unsigned long) _bootTraceNumber, 10); \
    } \
  } while (0)

/// @def bootTraceHex
///
/// @brief Print a boot-stage marker followed by a hexadecimal value.
#define bootTraceHex(text, value) \
  do { \
    static const char _bootTraceText[] KEEP_IN_FLASH = "[boot] " text " 0x"; \
    agonLight2TraceValue(_bootTraceText, (unsigned long) (value), 16); \
  } while (0)

#else // NANO_OS_AGON_LIGHT_2

#define bootTrace(text) do { } while (0)
#define bootTraceValue(text, value) do { (void) (value); } while (0)
#define bootTraceHex(text, value) do { (void) (value); } while (0)

#endif // NANO_OS_AGON_LIGHT_2

#endif // BOOT_TRACE_H
