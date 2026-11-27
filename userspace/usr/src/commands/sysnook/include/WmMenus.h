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


/// @file WmMenus.h
///
/// @brief The sizes of menu bars and their dropdowns, and where their
/// titles are, for the Menus and MenuPaint overlays.

#ifndef WM_MENUS_H
#define WM_MENUS_H

#include "Wm.h"

/// @def WM_MENU_INSET
///
/// @brief How far in from a menu bar's left edge its first title starts.
#define WM_MENU_INSET 2

/// @def WM_TITLE_PAD
///
/// @brief The space each side of a menu's title.
#define WM_TITLE_PAD 6

/// @def WM_ITEM_HEIGHT
///
/// @brief The height of a dropdown's item.
#define WM_ITEM_HEIGHT 12

/// @def WM_SEPARATOR_HEIGHT
///
/// @brief The height of a dropdown's separator.
#define WM_SEPARATOR_HEIGHT 5

/// @def WM_CHECK_GUTTER
///
/// @brief The width left of a dropdown item's text, where its check mark
/// goes.
#define WM_CHECK_GUTTER 12

/// @def WM_SHORTCUT_GAP
///
/// @brief The least space between a dropdown item's text and its shortcut.
#define WM_SHORTCUT_GAP 12

/// @def WM_ITEM_PAD
///
/// @brief The space right of a dropdown item's text or shortcut.
#define WM_ITEM_PAD 6

/// @def WM_MARK
///
/// @brief The byte before a menu title's marked letter.
#define WM_MARK '&'

/// @fn WM_SHARED uint8_t wmShown(const uint8_t *title)
///
/// @brief Count the bytes of a menu's title that are drawn:  all but its
/// marks.
///
/// @param title The title's length byte.
///
/// @return Returns the number of bytes.
WM_SHARED uint8_t wmShown(const uint8_t *title) {
  uint8_t shown = *title;
  for (uint8_t ii = 1; ii <= *title; ii++) {
    if (title[ii] == WM_MARK) {
      shown--;
    }
  }
  return shown;
}

/// @fn WM_SHARED void wmTitleRect(WmState *state, const WmWidget *bar,
///   const GfxRect *barRect, uint8_t index, GfxRect *rect)
///
/// @brief Find where one of a menu bar's titles is.
///
/// @param state The window manager's state.
/// @param bar The menu bar.
/// @param barRect Where the bar is.
/// @param index The menu's index.
/// @param rect Where to store where its title is.
///
/// @return This function returns no value.
WM_SHARED void wmTitleRect(WmState *state, const WmWidget *bar,
  const GfxRect *barRect, uint8_t index, GfxRect *rect
) {
  uint8_t glyphW = state->display.builtInFont.glyphW;
  *rect = *barRect;
  rect->x += WM_MENU_INSET;
  const uint8_t *title = wmMenu(bar, 0);
  for (uint8_t ii = 0; ; ii++) {
    rect->w = (int16_t) (wmShown(title) * glyphW + 2 * WM_TITLE_PAD);
    if (ii == index) {
      return;
    }
    rect->x += rect->w;
    title = wmMenuItem((uint8_t*) title, *wmSkipText(title));
  }
}

/// @fn WM_SHARED int16_t wmItemHeight(const uint8_t *item)
///
/// @brief Find the height of a dropdown's item.
///
/// @param item The item.
///
/// @return Returns its height.
WM_SHARED int16_t wmItemHeight(const uint8_t *item) {
  if ((item[1] & WIN_MENU_SEPARATOR) != 0) {
    return WM_SEPARATOR_HEIGHT;
  }
  return WM_ITEM_HEIGHT;
}

#endif // WM_MENUS_H
