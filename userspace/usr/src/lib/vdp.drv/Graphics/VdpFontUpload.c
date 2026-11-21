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

/// @file VdpFontUpload.c
///
/// @brief Uploading a font to one of the driver's font slots, which both the
/// Startup overlay, for the built-in font, and the Graphics overlay, for a
/// client's, link.

#include "VdpDriver.h"

/// @var _vdpZeros
///
/// @brief Blank glyph rows, written for the characters a font has no glyphs
/// for.
static const uint8_t _vdpZeros[32] = { 0 };

/// @fn static void vdpWriteZeros(size_t length)
///
/// @brief Write zero bytes to the VDP.
///
/// @param length The number of zero bytes to write.
///
/// @return This function returns no value.
static void vdpWriteZeros(size_t length) {
  while (length > 0) {
    size_t chunk
      = (length < sizeof(_vdpZeros)) ? length : sizeof(_vdpZeros);
    vdpWrite(_vdpZeros, chunk);
    length -= chunk;
  }
}

/// @fn uint8_t vdpUploadFontSlot(VdpOutput *vdpOutput, uint8_t slot,
///   const GfxUploadFontArgs *font)
///
/// @brief Upload a font to a font slot with VDU 23,0,&A0,id;2 to clear the
/// buffer, VDU 23,0,&A0,id;0,length; to write all 256 glyphs to it, blank for
/// the characters the font doesn't cover, and VDU 23,0,&95,1,id;width,height,
/// ascent,flags to make a font of it, then mark the slot used.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param slot The font slot.
/// @param font The font, whose bitmap is readable by the driver.
///
/// @return Returns GFX_OK on success, GFX_ERR_NOT_SUPPORTED if the font isn't
/// one the VDP can hold.
uint8_t vdpUploadFontSlot(VdpOutput *vdpOutput, uint8_t slot,
  const GfxUploadFontArgs *font
) {
  size_t glyphSize = ((font->glyphW + 7) / 8) * font->glyphH;
  size_t fontSize = glyphSize * font->numChars;
  size_t bufferSize = glyphSize * VDP_FONT_NUM_GLYPHS;
  if ((font->bitmap == NULL) || (glyphSize == 0) || (font->numChars == 0)
    || (font->firstChar + font->numChars > VDP_FONT_NUM_GLYPHS)
    || (font->size < fontSize) || (bufferSize > 0xFFFF)
  ) {
    return GFX_ERR_NOT_SUPPORTED;
  }

  vdpOutput->fontWidths[slot] = font->glyphW;
  vdpOutput->fontHeights[slot] = font->glyphH;
  vdpOutput->fontsUsed |= (uint8_t) (1 << slot);
  uint16_t fontId = VDP_FONT_ID_BASE + slot;
  const uint8_t upload[] = {
    23, 0, 0xA0, VDP_WORD(fontId), 2,
    23, 0, 0xA0, VDP_WORD(fontId), 0, VDP_WORD(bufferSize)
  };
  vdpWrite(upload, sizeof(upload));
  vdpWriteZeros(glyphSize * font->firstChar);
  vdpWrite((const uint8_t*) font->bitmap, fontSize);
  vdpWriteZeros(bufferSize - fontSize - (glyphSize * font->firstChar));
  const uint8_t define[] = {
    23, 0, 0x95, 1, VDP_WORD(fontId), font->glyphW, font->glyphH,
    font->glyphH, 0
  };
  vdpWrite(define, sizeof(define));
  return GFX_OK;
}
