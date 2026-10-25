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

/// @file ScreenSetup.c
///
/// @brief The VDU commands the VDP driver sets up the screen with, kept apart
/// from Startup.c so that Graphics.h, whose kernel includes conflict with
/// ExecutiveProcesses.h, can supply the colors.

#include "Graphics.h"
#include "ScreenSetup.h"

/// @def CGA_INDEX_BLUE
///
/// @brief The index of CGA_BLUE in the CGA palette.
#define CGA_INDEX_BLUE 1

/// @def CGA_INDEX_YELLOW
///
/// @brief The index of CGA_YELLOW in the CGA palette.
#define CGA_INDEX_YELLOW 14

/// @def VDP_PALETTE_ENTRY
///
/// @brief The bytes of VDU 19,index,255,red,green,blue, which sets a logical
/// color of the VDP's palette to a GfxColor.
#define VDP_PALETTE_ENTRY(index, color) \
  19, (index), 255, \
  ((((GfxColor) (color)) >> 24) & 0xff), \
  ((((GfxColor) (color)) >> 16) & 0xff), \
  ((((GfxColor) (color)) >>  8) & 0xff)

/// @var vdpScreenSetup
///
/// @brief VDU commands sent once the VDP is listening: select the 640x480,
/// 16-color screen mode (VDU 22,0), select the US keyboard layout
/// (VDU 23,0,&81,1), set the palette to the CGA colors (VDU 19), draw yellow
/// text on blue (VDU 17), clear the screen (VDU 12), and turn the text cursor
/// on (VDU 23,1,1).  The palette follows the mode because changing the mode
/// resets it.
const uint8_t vdpScreenSetup[] = {
  22, 0,
  23, 0, 0x81, 1,
  VDP_PALETTE_ENTRY( 0, CGA_BLACK),
  VDP_PALETTE_ENTRY( 1, CGA_BLUE),
  VDP_PALETTE_ENTRY( 2, CGA_GREEN),
  VDP_PALETTE_ENTRY( 3, CGA_CYAN),
  VDP_PALETTE_ENTRY( 4, CGA_RED),
  VDP_PALETTE_ENTRY( 5, CGA_MAGENTA),
  VDP_PALETTE_ENTRY( 6, CGA_BROWN),
  VDP_PALETTE_ENTRY( 7, CGA_LIGHT_GRAY),
  VDP_PALETTE_ENTRY( 8, CGA_DARK_GRAY),
  VDP_PALETTE_ENTRY( 9, CGA_LIGHT_BLUE),
  VDP_PALETTE_ENTRY(10, CGA_LIGHT_GREEN),
  VDP_PALETTE_ENTRY(11, CGA_LIGHT_CYAN),
  VDP_PALETTE_ENTRY(12, CGA_LIGHT_RED),
  VDP_PALETTE_ENTRY(13, CGA_LIGHT_MAGENTA),
  VDP_PALETTE_ENTRY(14, CGA_YELLOW),
  VDP_PALETTE_ENTRY(15, CGA_WHITE),
  17, CGA_INDEX_YELLOW,
  17, 128 + CGA_INDEX_BLUE,
  12,
  23, 1, 1
};

const size_t vdpScreenSetupSize = sizeof(vdpScreenSetup);
