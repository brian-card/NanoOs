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

/// @file VdpColor.h
///
/// @brief Conversion of Graphics.h colors to the pixel formats of the Agon
/// Light 2 VDP.

#ifndef VDP_COLOR_H
#define VDP_COLOR_H

#include "Graphics.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def rgba8888ToVdpRgba2222
///
/// @brief Convert a GfxColor to the VDP's RGBA2222, which packs red in the
/// bottom two bits and alpha in the top two, the reverse of Graphics.h's
/// RGBA2222.  A macro so that it can initialize constant data.
#define rgba8888ToVdpRgba2222(color) ( \
    (colorChannelTo2Bits(color, 24) << 0) \
  | (colorChannelTo2Bits(color, 16) << 2) \
  | (colorChannelTo2Bits(color,  8) << 4) \
  | (colorChannelTo2Bits(color,  0) << 6) )

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VDP_COLOR_H
