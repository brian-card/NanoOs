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

/// @file VdpFont.h
///
/// @brief The VDP driver's built-in font.

#ifndef VDP_FONT_H
#define VDP_FONT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @def VDP_FONT_WIDTH
///
/// @brief The width, in pixels, of the built-in font's glyphs.
#define VDP_FONT_WIDTH 6

/// @def VDP_FONT_HEIGHT
///
/// @brief The height, in pixels, of the built-in font's glyphs.
#define VDP_FONT_HEIGHT 8

/// @def VDP_FONT_FIRST_CHAR
///
/// @brief The first character the built-in font has a glyph for.
#define VDP_FONT_FIRST_CHAR ' '

/// @def VDP_FONT_NUM_CHARS
///
/// @brief The number of characters the built-in font has glyphs for:  the
/// printable ASCII characters.
#define VDP_FONT_NUM_CHARS 95

extern const uint8_t vdpFont[VDP_FONT_NUM_CHARS * VDP_FONT_HEIGHT];

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VDP_FONT_H
