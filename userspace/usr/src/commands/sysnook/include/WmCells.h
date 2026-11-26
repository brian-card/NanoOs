///////////////////////////////////////////////////////////////////////////////
///
/// @file              WmCells.h
///
/// @brief             The demonstration window's cells, which the Start
///                    overlay preloads and the Paint overlay draws.
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


#ifndef WM_CELLS_H
#define WM_CELLS_H

#include "Wm.h"

/// @def WM_CELL_EDGE
///
/// @brief The colour of a cell's outline.
#define WM_CELL_EDGE CGA_BLACK

/// @def WM_ROW_TEXT
///
/// @brief The colour of the text across every third row of cells.
#define WM_ROW_TEXT CGA_WHITE

/// @def WM_CELL_FILLS
///
/// @brief The number of fill colours the cells take in turn.
#define WM_CELL_FILLS 4

/// @def WM_TEXT_ROW_EVERY
///
/// @brief How many rows of cells there are to each row with a line of text.
#define WM_TEXT_ROW_EVERY 3

/// @def WM_ROW_TEXT_TOP
///
/// @brief How far below the top of its row of cells a line of text is.
#define WM_ROW_TEXT_TOP 6

/// @def WM_PASS_FILL
///
/// @brief The pass over a row that records the cells' fills.
#define WM_PASS_FILL 0

/// @def WM_PASS_OUTLINE
///
/// @brief The pass over a row that records the cells' outlines, after their
/// fills.
#define WM_PASS_OUTLINE 1

/// @var _wmCellFills
///
/// @brief The fill colours of the cells, in turn along each row.
static const GfxColor _wmCellFills[WM_CELL_FILLS] = {
  CGA_GREEN, CGA_RED, CGA_MAGENTA, CGA_BROWN,
};

/// @var _wmRowText
///
/// @brief The text across every third row of cells, as wide as a row.
static const char _wmRowText[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcd";

/// @fn static bool wmIntersect(const GfxRect *first, const GfxRect *second,
///   GfxRect *result)
///
/// @brief Find where two rectangles overlap.
///
/// @param first One rectangle.
/// @param second The other.
/// @param result Where to store the overlap, which may be either input.
///
/// @return Returns true if they overlap, false otherwise.
WM_SHARED bool wmIntersect(const GfxRect *first, const GfxRect *second,
  GfxRect *result
) {
  int16_t left = first->x;
  int16_t top = first->y;
  int16_t right = first->x + first->w;
  int16_t bottom = first->y + first->h;
  if (second->x > left) {
    left = second->x;
  }
  if (second->y > top) {
    top = second->y;
  }
  if (second->x + second->w < right) {
    right = second->x + second->w;
  }
  if (second->y + second->h < bottom) {
    bottom = second->y + second->h;
  }
  if ((left >= right) || (top >= bottom)) {
    return false;
  }
  result->x = left;
  result->y = top;
  result->w = right - left;
  result->h = bottom - top;
  return true;
}

/// @fn static void wmRecordRow(GfxCmdBuf *cmd, uint8_t row, uint8_t cells,
///   GfxPoint origin, const GfxRect *clip)
///
/// @brief Record one row of the full window's cells:  their fills, then
/// their outlines, then, on every third row, a line of text.
///
/// @param cmd The command buffer.
/// @param row The row.
/// @param cells The number of cells in the whole window.
/// @param origin Where the body's top-left corner is.
/// @param clip The cells outside this rectangle are left out, or NULL to
///   record them all.
///
/// @return This function returns no value.
WM_SHARED void wmRecordRow(GfxCmdBuf *cmd, uint8_t row, uint8_t cells,
  GfxPoint origin, const GfxRect *clip
) {
  uint8_t first = row * WM_CELL_COLUMNS;
  uint8_t count = cells - first;
  if (count > WM_CELL_COLUMNS) {
    count = WM_CELL_COLUMNS;
  }
  int16_t top = origin.y + (int16_t) (row * WM_CELL_SIZE);
  for (uint8_t pass = WM_PASS_FILL; pass <= WM_PASS_OUTLINE; pass++) {
    if (pass == WM_PASS_OUTLINE) {
      gfxCmdSetFgColor(cmd, WM_CELL_EDGE);
    }
    for (uint8_t column = 0; column < count; column++) {
      GfxRect cell = { origin.x + (int16_t) (column * WM_CELL_SIZE), top,
        WM_CELL_SIZE, WM_CELL_SIZE };
      GfxRect overlap;
      if ((clip != NULL) && !wmIntersect(&cell, clip, &overlap)) {
        continue;
      }
      if (pass == WM_PASS_FILL) {
        gfxCmdSetFgColor(cmd, _wmCellFills[(row + column) % WM_CELL_FILLS]);
      }
      gfxCmdDrawRect(cmd, &cell, pass == WM_PASS_FILL);
    }
  }
  if ((row % WM_TEXT_ROW_EVERY) == 0) {
    GfxPoint position = { origin.x, top + WM_ROW_TEXT_TOP };
    gfxCmdSetFgColor(cmd, WM_ROW_TEXT);
    gfxCmdDrawText(cmd, position, _wmRowText);
  }
}

#endif // WM_CELLS_H
