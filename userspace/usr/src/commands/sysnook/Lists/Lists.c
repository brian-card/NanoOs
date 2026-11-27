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


/// @file Lists.c
///
/// @brief Painting a list, and what a press, the wheel or a key does to one,
/// entered from the Widgets and Controls overlays only for lists.

#include "Wm.h"

/// @def WM_LIST_TEXT_INSET
///
/// @brief How far an item's text is right of its row's left edge.
#define WM_LIST_TEXT_INSET 2

/// @def WM_THUMB_MIN
///
/// @brief The shortest a scroll bar's thumb is drawn.
#define WM_THUMB_MIN 4

/// @def WM_ARROW_ROWS
///
/// @brief The rows of a scroll bar arrow's triangle.
#define WM_ARROW_ROWS 3

/// @def WM_ARROW_TOP
///
/// @brief How far a scroll bar arrow's triangle is below its box's top.
#define WM_ARROW_TOP 4

/// @def WM_ARROW_CENTER
///
/// @brief The column of a scroll bar arrow's tip, from the bar's left edge.
#define WM_ARROW_CENTER 4

/// @def WM_WHEEL_ROWS
///
/// @brief The rows one step of the wheel scrolls a list.
#define WM_WHEEL_ROWS 3

/// @struct WmListLayout
///
/// @brief Where a list's parts are.
///
/// @param rows The number of rows it shows.
/// @param maxTop The highest first item it can show.
/// @param rowsWidth The width of its rows.
/// @param barX The scroll bar's left edge, if it has one.
/// @param trackTop The top of the scroll bar's track.
/// @param trackLength The height of the track.
/// @param thumbTop The top of the thumb.
/// @param thumbLength The height of the thumb.
/// @param bar Whether it has a scroll bar:  whether it has more items than
///   rows.
typedef struct WmListLayout {
  uint16_t rows;
  uint16_t maxTop;
  int16_t  rowsWidth;
  int16_t  barX;
  int16_t  trackTop;
  int16_t  trackLength;
  int16_t  thumbTop;
  int16_t  thumbLength;
  bool     bar;
} WmListLayout;

/// @fn static void wmListLayout(const WmWidget *list, const GfxRect *rect,
///   WmListLayout *layout)
///
/// @brief Work out where a list's rows and scroll bar are.
///
/// @param list The list.
/// @param rect Where it is.
/// @param layout Where to store where its parts are.
///
/// @return This function returns no value.
static void wmListLayout(const WmWidget *list, const GfxRect *rect,
  WmListLayout *layout
) {
  layout->rows = wmListRows(rect);
  layout->bar = (list->numItems > layout->rows);
  layout->maxTop = 0;
  layout->rowsWidth = rect->w - 2;

  if (!layout->bar) {
    return;
  }

  layout->maxTop = (uint16_t) (list->numItems - layout->rows);
  layout->rowsWidth -= WM_SCROLL_BAR_WIDTH;
  layout->barX = rect->x + rect->w - 1 - WM_SCROLL_BAR_WIDTH;
  layout->trackTop = rect->y + 1 + WM_SCROLL_BAR_WIDTH;
  layout->trackLength = rect->h - 2 - (2 * WM_SCROLL_BAR_WIDTH);
  if (layout->trackLength < 0) {
    layout->trackLength = 0;
  }

  layout->thumbLength = (int16_t) ((((int32_t) layout->trackLength)
    * layout->rows) / list->numItems);
  if (layout->thumbLength < WM_THUMB_MIN) {
    layout->thumbLength = WM_THUMB_MIN;
  }
  if (layout->thumbLength > layout->trackLength) {
    layout->thumbLength = layout->trackLength;
  }

  uint16_t top = list->top;
  if (top > layout->maxTop) {
    top = layout->maxTop;
  }

  layout->thumbTop = layout->trackTop + ((int16_t) ((((int32_t)
    (layout->trackLength - layout->thumbLength)) * top) / layout->maxTop));
}

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

/// @fn static void wmOutline(GfxCmdBuf *cmd, GfxColor color, int16_t left,
///   int16_t top, int16_t width, int16_t height)
///
/// @brief Record a 1-pixel outline of a rectangle in a color.
///
/// @param cmd The command buffer.
/// @param color The color.
/// @param left The rectangle's left edge.
/// @param top Its top edge.
/// @param width Its width.
/// @param height Its height.
///
/// @return This function returns no value.
static void wmOutline(GfxCmdBuf *cmd, GfxColor color, int16_t left,
  int16_t top, int16_t width, int16_t height
) {
  GfxRect rect = { left, top, width, height };

  gfxCmdSetFgColor(cmd, color);
  gfxCmdDrawRect(cmd, &rect, false);
}

/// @fn static void wmArrow(GfxCmdBuf *cmd, int16_t barX, int16_t boxTop,
///   bool up)
///
/// @brief Record one of a scroll bar's arrows:  a dark outline of its box
/// and a triangle pointing up or down in it.
///
/// @param cmd The command buffer.
/// @param barX The scroll bar's left edge.
/// @param boxTop The top of the arrow's box.
/// @param up Whether it points up.
///
/// @return This function returns no value.
static void wmArrow(GfxCmdBuf *cmd, int16_t barX, int16_t boxTop, bool up) {
  wmOutline(cmd, WM_DARK, barX, boxTop, WM_SCROLL_BAR_WIDTH,
    WM_SCROLL_BAR_WIDTH);

  for (int16_t row = 0; row < WM_ARROW_ROWS; row++) {
    int16_t half = up ? row : (WM_ARROW_ROWS - 1 - row);
    wmFill(cmd, WM_DARK, barX + WM_ARROW_CENTER - half,
      boxTop + WM_ARROW_TOP + row, (2 * half) + 1, 1);
  }
}

/// @fn static void wmListPaint(WmWidgetJob *job)
///
/// @brief Record a list:  white inside a dark border, an accent one when it
/// has the focus, a row for each item it shows, cut to fit, the selection in
/// accent with white text, and a scroll bar when it has more items than
/// rows.
///
/// @param job The job.
///
/// @return This function returns no value.
static void wmListPaint(WmWidgetJob *job) {
  WmState *state = job->state;
  GfxCmdBuf *cmd = state->cmd;
  const WmWidget *list = job->widget;
  const GfxRect *rect = &job->rect;
  uint8_t glyphW = state->display.builtInFont.glyphW;
  uint8_t glyphH = state->display.builtInFont.glyphH;
  bool enabled = (list->flags & WIN_WIDGET_DISABLED) == 0;
  WmListLayout layout;

  wmListLayout(list, rect, &layout);
  wmFill(cmd, WM_FACE, rect->x, rect->y, rect->w, rect->h);
  int16_t textLeft = rect->x + 1 + WM_LIST_TEXT_INSET;
  int16_t fits = (layout.rowsWidth - WM_LIST_TEXT_INSET) / glyphW;

  if (fits > UINT8_MAX) {
    fits = UINT8_MAX;
  }

  for (uint16_t row = 0; row < layout.rows; row++) {
    uint16_t index = list->top + row;
    if (index >= list->numItems) {
      break;
    }

    int16_t rowTop = rect->y + 1 + ((int16_t) (row * WM_LIST_ITEM_HEIGHT));
    GfxColor ink = enabled ? WM_TEXT : WM_DARK;

    if (enabled && (index == list->value)) {
      wmFill(cmd, WM_ACCENT, rect->x + 1, rowTop, layout.rowsWidth,
        WM_LIST_ITEM_HEIGHT);
      ink = WM_ON_ACCENT;
    }

    uint16_t length = 0;
    const char *text = wmItem(list, index, &length);

    if (((int16_t) length) > fits) {
      length = (uint16_t) fits;
    }
    if (length == 0) {
      continue;
    }

    gfxCmdSetFgColor(cmd, ink);
    uint8_t *record = gfxCmdBufReserve_(cmd, GFX_CMD_DRAW_TEXT_SIZE(length));
    if (record != NULL) {
      GfxPoint at = { textLeft,
        rowTop + ((WM_LIST_ITEM_HEIGHT - glyphH) / 2) };

      record[GFX_CMD_TAG] = GFX_CMD_DRAW_TEXT;
      memcpy(&record[GFX_CMD_DRAW_TEXT_AT], &at, sizeof(at));
      record[GFX_CMD_DRAW_TEXT_LENGTH] = (uint8_t) length;
      memcpy(&record[GFX_CMD_DRAW_TEXT_TEXT], text, length);
    }
  }

  if (layout.bar) {
    wmFill(cmd, WM_LIGHT, layout.barX, rect->y + 1, WM_SCROLL_BAR_WIDTH,
      rect->h - 2);
    wmArrow(cmd, layout.barX, rect->y + 1, true);
    wmArrow(cmd, layout.barX, rect->y + rect->h - 1 - WM_SCROLL_BAR_WIDTH,
      false);
    wmFill(cmd, WM_DARK, layout.barX + 1, layout.thumbTop,
      WM_SCROLL_BAR_WIDTH - 2, layout.thumbLength);
  }

  wmOutline(cmd, job->focused ? WM_ACCENT : WM_DARK, rect->x, rect->y,
    rect->w, rect->h);
}

/// @fn static void wmScrollTo(WmWidgetJob *job, int32_t top)
///
/// @brief Scroll a list to show an item first, as near it as it can.
///
/// @param job The job.
/// @param top The item's index, which may be out of range.
///
/// @return This function returns no value.
static void wmScrollTo(WmWidgetJob *job, int32_t top) {
  WmListLayout layout;
  wmListLayout(job->widget, &job->rect, &layout);

  if (top > layout.maxTop) {
    top = layout.maxTop;
  }
  if (top < 0) {
    top = 0;
  }

  job->widget->top = (uint16_t) top;
}

/// @fn static void wmSelect(WmWidgetJob *job, int32_t index)
///
/// @brief Select a list's item, as near the one asked for as there is,
/// scroll to show it, and tell the list's owner with WIN_EVENT_CHANGED if
/// the selection changed.
///
/// @param job The job.
/// @param index The item's index, which may be out of range.
///
/// @return This function returns no value.
static void wmSelect(WmWidgetJob *job, int32_t index) {
  WmWidget *list = job->widget;
  if (list->numItems == 0) {
    return;
  }
  if (index >= list->numItems) {
    index = list->numItems - 1;
  }
  if (index < 0) {
    index = 0;
  }

  uint16_t old = list->value;

  list->value = (uint16_t) index;
  wmShowSelection(job->window, list);

  if (list->value != old) {
    wmChanged(job->state, job->window, list, list->value);
  }
}

/// @fn static void wmCommand(WmWidgetJob *job)
///
/// @brief Tell a list's owner with WIN_EVENT_COMMAND that its selection was
/// chosen, if it has one.
///
/// @param job The job.
///
/// @return This function returns no value.
static void wmCommand(WmWidgetJob *job) {
  WmWidget *list = job->widget;
  if (list->value == WIN_NO_ITEM) {
    return;
  }

  WinEvent *event = wmSend(job->state, job->window->owner,
    WIN_EVENT_COMMAND, job->window->id);
  if (event != NULL) {
    event->widget = list->id;
    event->value = list->value;
  }
}

/// @fn static void wmListPress(WmWidgetJob *job)
///
/// @brief Carry out a press on a list:  on the scroll bar, an arrow scrolls
/// one row and the track above or below the thumb a page; on an item, it is
/// selected, and a second press on it soon after sends WIN_EVENT_COMMAND.
///
/// @param job The job.
///
/// @return This function returns no value.
static void wmListPress(WmWidgetJob *job) {
  WmWidget *list = job->widget;
  const GfxRect *rect = &job->rect;
  int16_t pointX = job->event->x;
  int16_t pointY = job->event->y;
  WmListLayout layout;

  wmListLayout(list, rect, &layout);
  int32_t top = list->top;

  if (layout.bar && (pointX >= layout.barX)) {
    if (pointY < layout.trackTop) {
      top--;
    } else if (pointY >= (layout.trackTop + layout.trackLength)) {
      top++;
    } else if (pointY < layout.thumbTop) {
      top -= layout.rows;
    } else if (pointY >= (layout.thumbTop + layout.thumbLength)) {
      top += layout.rows;
    }

    wmScrollTo(job, top);
    return;
  }

  int16_t offset = pointY - rect->y;
  if (offset < 1) {
    return;
  }

  uint16_t row = (uint16_t) ((offset - 1) / WM_LIST_ITEM_HEIGHT);
  uint32_t index = top + row;

  if ((row >= layout.rows) || (index >= list->numItems)) {
    return;
  }

  bool again = wmDoubleClick(job->state, job->window, pointX, pointY)
    && (index == list->value);

  wmSelect(job, (int32_t) index);

  if (again) {
    wmCommand(job);
  }
}

/// @fn static void wmListKey(WmWidgetJob *job)
///
/// @brief Carry out a key on a list going down:  Up, Down, Page Up, Page
/// Down, Home and End move the selection, and Page Up and Page Down the
/// view with it, and Enter sends WIN_EVENT_COMMAND.  The release of each of
/// these keys is the list's too.
///
/// @param job The job, whose handled is set if the list uses the key.
///
/// @return This function returns no value.
static void wmListKey(WmWidgetJob *job) {
  WmWidget *list = job->widget;
  uint8_t key = job->event->keycode;
  WmListLayout layout;

  wmListLayout(list, &job->rect, &layout);
  int32_t index = (list->value == WIN_NO_ITEM) ? -1 : list->value;
  int32_t page = layout.rows;

  if ((key == GFX_KEY_UP) || (key == GFX_KEY_PAGE_UP)) {
    page = -page;
  }
  if ((key != GFX_KEY_UP) && (key != GFX_KEY_DOWN)
    && (key != GFX_KEY_PAGE_UP) && (key != GFX_KEY_PAGE_DOWN)
    && (key != GFX_KEY_HOME) && (key != GFX_KEY_END)
    && (key != GFX_KEY_ENTER)
  ) {
    return;
  }

  job->handled = true;

  if (job->event->down == 0) {
    return;
  } else if (key == GFX_KEY_ENTER) {
    wmCommand(job);
  } else if (key == GFX_KEY_HOME) {
    wmSelect(job, 0);
  } else if (key == GFX_KEY_END) {
    wmSelect(job, list->numItems);
  } else if ((key == GFX_KEY_UP) || (key == GFX_KEY_DOWN)) {
    if (index < 0) {
      index = 0;
    } else {
      index += (page < 0) ? -1 : 1;
    }

    wmSelect(job, index);
  } else {
    wmScrollTo(job, ((int32_t) list->top) + page);
    wmSelect(job, (index < 0) ? 0 : (index + page));
  }
}

/// @fn void* wmListsPaint(void *args)
///
/// @brief Record the job's list.
///
/// @param args A pointer to the WmWidgetJob.
///
/// @return Always returns the provided pointer.
void* wmListsPaint(void *args) {
  wmListPaint((WmWidgetJob*) args);

  return args;
}

/// @fn void* wmListsInput(void *args)
///
/// @brief Carry out the job's event on its list:  a key, a press, or else
/// the wheel, which scrolls it, and repaint it.
///
/// @param args A pointer to the WmWidgetJob, whose handled is set if the list
///   used the event.
///
/// @return Always returns the provided pointer.
void* wmListsInput(void *args) {
  WmWidgetJob *job = (WmWidgetJob*) args;
  const GfxEvent *event = job->event;

  if (event->type == GFX_EVENT_KEY) {
    wmListKey(job);
  } else if ((event->buttons & GFX_BUTTON_LEFT) != 0) {
    wmListPress(job);
    job->handled = true;
  } else {
    wmScrollTo(job, ((int32_t) job->widget->top)
      - (((int32_t) event->wheel) * WM_WHEEL_ROWS));
    job->handled = true;
  }

  if (job->handled) {
    wmDamageWidget(job->state, job->window, job->widget);
  }

  return args;
}
