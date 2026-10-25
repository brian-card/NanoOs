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

/// @file MouseCursor.h
///
/// @brief The mouse cursor the VDP driver defines as the VDP's pointer.

#ifndef MOUSE_CURSOR_H
#define MOUSE_CURSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @def MOUSE_CURSOR_WIDTH
///
/// @brief The width of the mouse cursor, in pixels.
#define MOUSE_CURSOR_WIDTH 12

/// @def MOUSE_CURSOR_HEIGHT
///
/// @brief The height of the mouse cursor, in pixels.
#define MOUSE_CURSOR_HEIGHT 18

/// @var cursorPixels
///
/// @brief The mouse cursor's pixels in the VDP's RGBA2222, row by row.
extern const uint8_t cursorPixels[MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT];

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MOUSE_CURSOR_H
