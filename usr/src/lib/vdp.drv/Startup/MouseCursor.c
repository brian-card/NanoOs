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

/// @file MouseCursor.c
///
/// @brief Definition of the mouse cursor pixels to use for the VDP pointer.

#include "Graphics.h"
#include "MouseCursor.h"

#define B CGA_BLACK        /* black  (border)      */
#define W CGA_WHITE        /* white  (fill)        */
#define _ GFX_TRANSPARENT  /* transparent          */

const GfxColor cursorPixels[MOUSE_CURSOR_WIDTH * MOUSE_CURSOR_HEIGHT] = {
 /* 0    1    2    3    4    5    6    7    8    9   10   11  */
    B,   _,   _,   _,   _,   _,   _,   _,   _,   _,   _,   _,  //  0
    B,   B,   _,   _,   _,   _,   _,   _,   _,   _,   _,   _,  //  1
    B,   W,   B,   _,   _,   _,   _,   _,   _,   _,   _,   _,  //  2
    B,   W,   W,   B,   _,   _,   _,   _,   _,   _,   _,   _,  //  3
    B,   W,   W,   W,   B,   _,   _,   _,   _,   _,   _,   _,  //  4
    B,   W,   W,   W,   W,   B,   _,   _,   _,   _,   _,   _,  //  5
    B,   W,   W,   W,   W,   W,   B,   _,   _,   _,   _,   _,  //  6
    B,   W,   W,   W,   W,   W,   W,   B,   _,   _,   _,   _,  //  7
    B,   W,   W,   W,   W,   W,   W,   W,   B,   _,   _,   _,  //  8
    B,   W,   W,   W,   W,   W,   W,   W,   W,   B,   _,   _,  //  9
    B,   W,   W,   W,   W,   W,   B,   B,   B,   B,   _,   _,  // 10
    B,   W,   W,   B,   W,   W,   B,   _,   _,   _,   _,   _,  // 11
    B,   W,   B,   _,   B,   W,   W,   B,   _,   _,   _,   _,  // 12
    B,   B,   _,   _,   B,   W,   W,   B,   _,   _,   _,   _,  // 13
    B,   _,   _,   _,   _,   B,   W,   W,   B,   _,   _,   _,  // 14
    _,   _,   _,   _,   _,   B,   W,   W,   B,   _,   _,   _,  // 15
    _,   _,   _,   _,   _,   _,   B,   W,   B,   _,   _,   _,  // 16
    _,   _,   _,   _,   _,   _,   _,   B,   _,   _,   _,   _,  // 17
};

#undef B
#undef W
#undef _

/// @fn static inline uint8_t rgba2222ToVdp(uint8_t color)
///
/// @brief Convert a Graphics.h RGBA2222 color, which packs red in the top two
/// bits, to the VDP's RGBA2222, which packs red in the bottom two.
///
/// @param color The Graphics.h RGBA2222 color.
///
/// @return Returns the color in the VDP's RGBA2222 layout.
static inline uint8_t rgba2222ToVdp(uint8_t color) {
  return ((color >> 6) & 0x03) | ((color >> 2) & 0x0c)
    | ((color << 2) & 0x30) | ((color << 6) & 0xc0);
}

void mouseCursorRowToVdp(uint8_t row, uint8_t *vdpPixels) {
  const GfxColor *pixel = &cursorPixels[row * MOUSE_CURSOR_WIDTH];
  for (uint8_t column = 0; column < MOUSE_CURSOR_WIDTH; column++) {
    vdpPixels[column] = rgba2222ToVdp(rgba8888ToRgba2222(pixel[column]));
  }
}
