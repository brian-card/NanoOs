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

/// @file NanoOsConsole.h
///
/// @brief NanoOs-specific control of the console port a process owns.

#ifndef NANO_OS_CONSOLE_H
#define NANO_OS_CONSOLE_H

#include <stdint.h>

#include "NanoOsUser.h"
#include "../../src/kernel/GraphicsColor.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @fn static inline int getConsoleColors(void)
///
/// @brief Get the colors last set for the console port the process owns.
///
/// @return Returns the colors, with the CgaColorIndex of the background in the
/// upper 4 bits and of the foreground in the lower 4, on success, or -1 if the
/// colors have never been set or the process owns no console port.
static inline int getConsoleColors(void) {
  return overlayMap.header.osApi->userApi->getConsoleColors();
}

/// @fn static inline int setConsoleColors(uint8_t colors)
///
/// @brief Record the colors of the console port the process owns.  Writing
/// the escape sequence that changes them is left to the caller.
///
/// @param colors The colors, with the CgaColorIndex of the background in the
///   upper 4 bits and of the foreground in the lower 4.
///
/// @return Returns 0 on success, -1 if the process owns no console port.
static inline int setConsoleColors(uint8_t colors) {
  return overlayMap.header.osApi->userApi->setConsoleColors(colors);
}

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_CONSOLE_H
