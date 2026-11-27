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


/// @file MenuPaint.c
///
/// @brief Painting a menu bar and its open menu's dropdown, entered from
/// the Widgets and Paint overlays.

#include "WmMenus.h"

/// @fn static void wmFill(GfxCmdBuf *cmd, GfxColor color, int16_t left,
///   int16_t top, int16_t width, int16_t height)
///
/// @brief Record a filled rectangle of a color.
///
/// @param cmd The command buffer.
/// @param color The color.
/// @param left The rectangle's left edge.
/// @param top Its top edge.
/// @param width Its width.
/// @param height Its height.
///
/// @return This function returns no value.
static void wmFill(GfxCmdBuf *cmd, GfxColor color, int16_t left,
  int16_t top, int16_t width, int16_t height
) {
  GfxRect rect = { left, top, width, height };

  gfxCmdSetFgColor(cmd, color);
  gfxCmdDrawRect(cmd, &rect, true);
}

/// @fn static void wmText(GfxCmdBuf *cmd, GfxColor color, int16_t left,
///   int16_t top, const uint8_t *text, uint8_t length)
///
/// @brief Record a run of text, which has no terminator, in a color.
///
/// @param cmd The command buffer.
/// @param color The color.
/// @param left The text's left edge.
/// @param top The text's top edge.
/// @param text The text.
/// @param length The number of bytes of text.
///
/// @return This function returns no value.
static void wmText(GfxCmdBuf *cmd, GfxColor color, int16_t left,
  int16_t top, const uint8_t *text, uint8_t length
) {
  if (length == 0) {
    return;
  }

  gfxCmdSetFgColor(cmd, color);
  uint8_t *record = gfxCmdBufReserve_(cmd, GFX_CMD_DRAW_TEXT_SIZE(length));
  if (record != NULL) {
    GfxPoint at = { left, top };

    record[GFX_CMD_TAG] = GFX_CMD_DRAW_TEXT;
    memcpy(&record[GFX_CMD_DRAW_TEXT_AT], &at, sizeof(at));
    record[GFX_CMD_DRAW_TEXT_LENGTH] = length;
    memcpy(&record[GFX_CMD_DRAW_TEXT_TEXT], text, length);
  }
}

/// @fn void* wmMenusPaintBar(void *args)
///
/// @brief Record the job's menu bar:  light gray, its titles with their
/// marked letters underlined, the open menu's title in accent with white
/// text.
///
/// @param args A pointer to the WmWidgetJob.
///
/// @return Always returns the provided pointer.
void* wmMenusPaintBar(void *args) {
  WmWidgetJob *job = (WmWidgetJob*) args;
  WmState *state = job->state;
  GfxCmdBuf *cmd = state->cmd;
  const WmWidget *bar = job->widget;
  const WmMenuState *open = &state->menu;
  uint8_t glyphW = state->display.builtInFont.glyphW;
  uint8_t glyphH = state->display.builtInFont.glyphH;
  const GfxRect *rect = &job->rect;
  WmWidgets *widgets = (WmWidgets*) job->window->widgets;
  bool barOpen = open->open && (open->window == job->window->id)
    && (open->bar == ((uint8_t) (bar - widgets->widget)));

  wmFill(cmd, WM_LIGHT, rect->x, rect->y, rect->w, rect->h);
  int16_t top = rect->y + ((rect->h - glyphH) / 2);

  for (uint8_t ii = 0; ii < bar->numItems; ii++) {
    GfxRect title;
    wmTitleRect(state, bar, rect, ii, &title);
    GfxColor ink = WM_TEXT;

    if ((bar->flags & WIN_WIDGET_DISABLED) != 0) {
      ink = WM_DARK;
    }

    if (barOpen && (open->menu == ii)) {
      wmFill(cmd, WM_ACCENT, title.x, title.y, title.w, title.h);
      ink = WM_ON_ACCENT;
    }

    const uint8_t *text = wmMenu(bar, ii);
    uint8_t length = *text++;
    int16_t left = title.x + WM_TITLE_PAD;
    uint8_t run = 0;

    for (uint8_t at = 0; at <= length; at++) {
      if ((at < length) && (text[at] != WM_MARK)) {
        run++;
        continue;
      }

      wmText(cmd, ink, left, top, &text[at - run], run);
      left += run * glyphW;
      run = 0;

      if ((at + 1) < length) {
        wmFill(cmd, ink, left, top + glyphH, glyphW, 1);
      }
    }
  }

  return args;
}

/// @fn void* wmMenusPaintDropdown(void *args)
///
/// @brief Record the open menu's dropdown, leaving out the items that don't
/// meet the rectangle being painted:  white inside a dark border, each
/// item's text with its shortcut right of it, dark gray if it is
/// disabled, a check mark left of it if it is checked, the highlighted one
/// in accent with white text, and each separator a dark line.
///
/// @param args A pointer to the WmPaintJob, whose window is ignored.
///
/// @return Always returns the provided pointer.
void* wmMenusPaintDropdown(void *args) {
  WmPaintJob *job = (WmPaintJob*) args;
  WmState *state = job->state;
  GfxCmdBuf *cmd = state->cmd;
  const WmMenuState *open = &state->menu;
  const WmWindow *window = wmFind(state, open->window);
  const WmWidgets *widgets = (window != NULL)
    ? (const WmWidgets*) window->widgets : NULL;

  if ((widgets == NULL) || (open->bar >= widgets->count)) {
    return args;
  }

  uint8_t glyphW = state->display.builtInFont.glyphW;
  uint8_t glyphH = state->display.builtInFont.glyphH;
  const GfxRect *rect = &open->rect;
  uint8_t *menu = wmMenu(&widgets->widget[open->bar], open->menu);
  uint8_t count = *wmSkipText(menu);

  wmFill(cmd, WM_FACE, rect->x, rect->y, rect->w, rect->h);
  gfxCmdSetFgColor(cmd, WM_DARK);
  gfxCmdDrawRect(cmd, rect, false);
  int16_t top = rect->y + 1;
  const GfxRect *clip = job->clip;

  for (uint8_t ii = 0; ii < count; ii++) {
    const uint8_t *item = wmMenuItem(menu, ii);
    int16_t height = wmItemHeight(item);

    if (((top + height) <= clip->y) || (top >= (clip->y + clip->h))) {
      top += height;
      continue;
    } else if ((item[1] & WIN_MENU_SEPARATOR) != 0) {
      wmFill(cmd, WM_DARK, rect->x + 2, top + (WM_SEPARATOR_HEIGHT / 2),
        rect->w - 4, 1);
      top += WM_SEPARATOR_HEIGHT;
      continue;
    }

    GfxColor ink = WM_TEXT;

    if ((item[1] & WIN_MENU_DISABLED) != 0) {
      ink = WM_DARK;
    }

    if (ii == open->item) {
      wmFill(cmd, WM_ACCENT, rect->x + 1, top, rect->w - 2, WM_ITEM_HEIGHT);
      ink = WM_ON_ACCENT;
    }

    if ((item[1] & WIN_MENU_CHECKED) != 0) {
      GfxPoint corner = { rect->x + 4, top + 5 };
      GfxPoint bottom = { rect->x + 6, top + 7 };
      GfxPoint tip = { rect->x + 9, top + 3 };

      gfxCmdSetFgColor(cmd, ink);
      gfxCmdDrawLine(cmd, corner, bottom);
      gfxCmdDrawLine(cmd, bottom, tip);
    }

    int16_t textTop = top + ((WM_ITEM_HEIGHT - glyphH) / 2);
    wmText(cmd, ink, rect->x + 1 + WM_CHECK_GUTTER, textTop, &item[3],
      item[2]);
    const uint8_t *keys = wmSkipText(&item[2]);
    wmText(cmd, ink, rect->x + rect->w - 1 - WM_ITEM_PAD - (*keys * glyphW),
      textTop, &keys[1], *keys);
    top += WM_ITEM_HEIGHT;
  }

  return args;
}
