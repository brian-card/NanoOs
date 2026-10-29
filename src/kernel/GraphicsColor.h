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

/// @file GraphicsColor.h
///
/// @brief The color definitions of Graphics.h, kept apart from it so that
/// code that can't include the kernel headers Graphics.h depends on can use
/// them.

#ifndef GRAPHICS_COLOR_H
#define GRAPHICS_COLOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Packed RGBA color: 0xRRGGBBAA.
//
// The VDP backend maps this to the nearest entry in its 64-color palette.
// A GPU backend passes it through to the fragment pipeline or clear color.
typedef uint32_t GfxColor;

#define GFX_RGBA(r,g,b,a) \
    (((GfxColor)(r) << 24) | \
     ((GfxColor)(g) << 16) | \
     ((GfxColor)(b) <<  8) | \
     ((GfxColor)(a)))

#define GFX_RGB(r,g,b)  GFX_RGBA(r, g, b, 0xFF)
#define GFX_TRANSPARENT GFX_RGBA(0, 0, 0, 0x00)

// Common palette entries that map cleanly to the VDP's 64-color set.
//                         RGBA8888       RGB2222
#define CGA_BLACK          0x000000FF  // (0,0,0,3)
#define CGA_BLUE           0x0000AAFF  // (0,0,2,3)
#define CGA_GREEN          0x00AA00FF  // (0,2,0,3)
#define CGA_CYAN           0x00AAAAFF  // (0,2,2,3)
#define CGA_RED            0xAA0000FF  // (2,0,0,3)
#define CGA_MAGENTA        0xAA00AAFF  // (2,0,2,3)
#define CGA_BROWN          0xAA5500FF  // (2,1,0,3)
#define CGA_LIGHT_GRAY     0xAAAAAAFF  // (2,2,2,3)
#define CGA_DARK_GRAY      0x555555FF  // (1,1,1,3)
#define CGA_LIGHT_BLUE     0x5555FFFF  // (1,1,3,3)
#define CGA_LIGHT_GREEN    0x55FF55FF  // (1,3,1,3)
#define CGA_LIGHT_CYAN     0x55FFFFFF  // (1,3,3,3)
#define CGA_LIGHT_RED      0xFF5555FF  // (3,1,1,3)
#define CGA_LIGHT_MAGENTA  0xFF55FFFF  // (3,1,3,3)
#define CGA_YELLOW         0xFFFF55FF  // (3,3,1,3)
#define CGA_WHITE          0xFFFFFFFF  // (3,3,3,3)

extern const GfxColor cgaColorPalette[16];
#define CGA_COLOR_PALETTE \
const GfxColor cgaColorPalette[16] = { \
  0x000000FF, /* CGA_BLACK         */ \
  0x0000AAFF, /* CGA_BLUE          */ \
  0x00AA00FF, /* CGA_GREEN         */ \
  0x00AAAAFF, /* CGA_CYAN          */ \
  0xAA0000FF, /* CGA_RED           */ \
  0xAA00AAFF, /* CGA_MAGENTA       */ \
  0xAA5500FF, /* CGA_BROWN         */ \
  0xAAAAAAFF, /* CGA_LIGHT_GRAY    */ \
  0x555555FF, /* CGA_DARK_GRAY     */ \
  0x5555FFFF, /* CGA_LIGHT_BLUE    */ \
  0x55FF55FF, /* CGA_LIGHT_GREEN   */ \
  0x55FFFFFF, /* CGA_LIGHT_CYAN    */ \
  0xFF5555FF, /* CGA_LIGHT_RED     */ \
  0xFF55FFFF, /* CGA_LIGHT_MAGENTA */ \
  0xFFFF55FF, /* CGA_YELLOW        */ \
  0xFFFFFFFF, /* CGA_WHITE         */ \
};

extern const uint8_t rgb222ToCgaMap[64];
#define RGB222_TO_CGA_MAP \
const uint8_t rgb222ToCgaMap[64] = { \
  0,  0,  1,  1,  0,  8,  1,  9, \
  2,  2,  3,  3,  2, 10,  3, 11, \
  0,  8,  1,  9,  6,  8,  8,  9, \
  2,  8,  3,  9, 10, 10, 10, 11, \
  4,  4,  5,  5,  6,  6,  5,  9, \
  6,  7,  7,  7, 10, 10,  7, 11, \
  4, 12,  5, 13,  6, 12, 12, 13, \
  6, 12,  7, 13, 14, 14, 14, 15, \
};

// Common conversions.
#define rgba8888ToRgb888(color) ((color) >> 8)
#define colorChannelTo2Bits(color, shift) \
    ((((((GfxColor) (color)) >> (shift)) & 0xff) + 42) / 85)
#define rgba8888ToRgba2222(color) ( \
    (colorChannelTo2Bits(color, 24) << 6) \
  | (colorChannelTo2Bits(color, 16) << 4) \
  | (colorChannelTo2Bits(color,  8) << 2) \
  | (colorChannelTo2Bits(color,  0) << 0) )
#define rgba8888ToRgb222(color) (rgba8888ToRgba2222(color) >> 2)
#define rgb888ToRgb222(color) ( \
    (colorChannelTo2Bits(color, 16) << 4) \
  | (colorChannelTo2Bits(color,  8) << 2) \
  | (colorChannelTo2Bits(color,  0) << 0) )
#define rgba2222ToRgb222(color) ((color) >> 2)

#define rgb222ToCga(color)   rgb222ToCgaMap[(color)]
#define rgba2222ToCga(color) rgb222ToCgaMap[rgba2222ToRgb222(color)]
#define rgba2222Alpha(color) (color & 0x03)
#define rgb888ToCga(color)   rgb222ToCgaMap[rgb888ToRgb222(color)]
#define rgba8888ToCga(color) rgb222ToCgaMap[rgba8888ToRgb222(color)]
#define rgba8888Alpha(color) ((color) & 0xff)

// Indices of the colors in cgaColorPalette.
typedef enum CgaColorIndex {
  CGA_INDEX_BLACK,
  CGA_INDEX_BLUE,
  CGA_INDEX_GREEN,
  CGA_INDEX_CYAN,
  CGA_INDEX_RED,
  CGA_INDEX_MAGENTA,
  CGA_INDEX_BROWN,
  CGA_INDEX_LIGHT_GRAY,
  CGA_INDEX_DARK_GRAY,
  CGA_INDEX_LIGHT_BLUE,
  CGA_INDEX_LIGHT_GREEN,
  CGA_INDEX_LIGHT_CYAN,
  CGA_INDEX_LIGHT_RED,
  CGA_INDEX_LIGHT_MAGENTA,
  CGA_INDEX_YELLOW,
  CGA_INDEX_WHITE,
  CGA_NUM_COLORS
} CgaColorIndex;

// ANSI SGR (Select Graphic Rendition) parameters for setting terminal colors.
#define ANSI_SGR_RESET                0
#define ANSI_SGR_FOREGROUND           30
#define ANSI_SGR_FOREGROUND_DEFAULT   39
#define ANSI_SGR_BACKGROUND           40
#define ANSI_SGR_BACKGROUND_DEFAULT   49
#define ANSI_SGR_BRIGHT_FOREGROUND    90
#define ANSI_SGR_BRIGHT_BACKGROUND   100

// ANSI SGR foreground parameter of each CGA color, by CgaColorIndex.
extern const uint8_t cgaToAnsiMap[16];
#define CGA_TO_ANSI_MAP \
const uint8_t cgaToAnsiMap[16] = { \
  30, /* CGA_BLACK         */ \
  34, /* CGA_BLUE          */ \
  32, /* CGA_GREEN         */ \
  36, /* CGA_CYAN          */ \
  31, /* CGA_RED           */ \
  35, /* CGA_MAGENTA       */ \
  33, /* CGA_BROWN         */ \
  37, /* CGA_LIGHT_GRAY    */ \
  90, /* CGA_DARK_GRAY     */ \
  94, /* CGA_LIGHT_BLUE    */ \
  92, /* CGA_LIGHT_GREEN   */ \
  96, /* CGA_LIGHT_CYAN    */ \
  91, /* CGA_LIGHT_RED     */ \
  95, /* CGA_LIGHT_MAGENTA */ \
  93, /* CGA_YELLOW        */ \
  97, /* CGA_WHITE         */ \
};

// CgaColorIndex of each ANSI color, indexed by the ANSI color number (0-7),
// plus 8 for the bright variants.
extern const uint8_t ansiToCgaMap[16];
#define ANSI_TO_CGA_MAP \
const uint8_t ansiToCgaMap[16] = { \
  CGA_INDEX_BLACK,         /* black          */ \
  CGA_INDEX_RED,           /* red            */ \
  CGA_INDEX_GREEN,         /* green          */ \
  CGA_INDEX_BROWN,         /* yellow         */ \
  CGA_INDEX_BLUE,          /* blue           */ \
  CGA_INDEX_MAGENTA,       /* magenta        */ \
  CGA_INDEX_CYAN,          /* cyan           */ \
  CGA_INDEX_LIGHT_GRAY,    /* white          */ \
  CGA_INDEX_DARK_GRAY,     /* bright black   */ \
  CGA_INDEX_LIGHT_RED,     /* bright red     */ \
  CGA_INDEX_LIGHT_GREEN,   /* bright green   */ \
  CGA_INDEX_YELLOW,        /* bright yellow  */ \
  CGA_INDEX_LIGHT_BLUE,    /* bright blue    */ \
  CGA_INDEX_LIGHT_MAGENTA, /* bright magenta */ \
  CGA_INDEX_LIGHT_CYAN,    /* bright cyan    */ \
  CGA_INDEX_WHITE,         /* bright white   */ \
};

#define cgaToAnsiForeground(index) cgaToAnsiMap[(index)]
#define cgaToAnsiBackground(index) \
    (cgaToAnsiMap[(index)] + ANSI_SGR_BACKGROUND - ANSI_SGR_FOREGROUND)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // GRAPHICS_COLOR_H
