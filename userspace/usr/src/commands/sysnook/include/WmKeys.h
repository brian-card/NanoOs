///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.24.2026
///
/// @file              WmKeys.h
///
/// @brief             The bytes a terminal sends for a key, which the window
///                    server sends the session of the text grid with the keys.
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

#ifndef WM_KEYS_H
#define WM_KEYS_H

#include <stdint.h>

#include "../../../src/kernel/GraphicsProtocol.h"

/// @def WM_KEY_MAX_BYTES
///
/// @brief The most bytes wmTranslateKey writes for one key.
#define WM_KEY_MAX_BYTES 4

/// @var _wmKeySequenceEnds
///
/// @brief For each GfxKey from GFX_KEY_INSERT to GFX_KEY_RIGHT, the last
/// character of the escape sequence a terminal sends for it:  a letter for
/// ESC [ letter, a digit for ESC [ digit ~, or 0 for no sequence.
static const uint8_t _wmKeySequenceEnds[] = {
  '2', 0, '3', 0, 0, 'H', 0, 'F', 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, '5', 0,
  '6', 0, 'A', 0, 'B', 0, 'D', 0, 'C',
};

GFX_STATIC_ASSERT(sizeof(_wmKeySequenceEnds)
  == GFX_KEY_RIGHT - GFX_KEY_INSERT + 1,
  "_wmKeySequenceEnds must cover GFX_KEY_INSERT to GFX_KEY_RIGHT");

/// @fn static uint8_t wmTranslateKey(uint8_t key, uint8_t ascii,
///   uint8_t *output)
///
/// @brief Turn a key going down into the bytes vdp.drv's console sends for
/// it:  an escape sequence for a navigation key or F1 to F4, and the
/// character it types otherwise.
///
/// @param key The GfxKey.
/// @param ascii The character the key types, or 0.
/// @param output A buffer of at least WM_KEY_MAX_BYTES bytes to fill.
///
/// @return Returns the number of bytes written to output.
static uint8_t wmTranslateKey(uint8_t key, uint8_t ascii, uint8_t *output) {
  output[0] = '\x1b';
  if ((key >= GFX_KEY_F1) && (key <= GFX_KEY_F4)) {
    output[1] = 'O';
    output[2] = (uint8_t) ('P' + key - GFX_KEY_F1);
    return 3;
  }
  uint8_t end = 0;
  if ((key >= GFX_KEY_INSERT) && (key <= GFX_KEY_RIGHT)) {
    end = _wmKeySequenceEnds[key - GFX_KEY_INSERT];
  }
  if (end != 0) {
    output[1] = '[';
    output[2] = end;
    if (end >= 'A') {
      return 3;
    }
    output[3] = '~';
    return 4;
  } else if (ascii != 0) {
    output[0] = ascii;
    return 1;
  }
  return 0;
}

#endif // WM_KEYS_H
