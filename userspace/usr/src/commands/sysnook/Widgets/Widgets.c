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




/// @file Widgets.c
///
/// @brief Painting the widgets of a window's body, entered from the Paint
/// overlay once for each window with widgets that a rectangle being painted
/// meets.

#include "Wm.h"

/// @def WM_GROUP_CAPTION_INSET
///
/// @brief How far in from a group box's left edge its caption starts.
#define WM_GROUP_CAPTION_INSET 6

/// @def WM_GROUP_CAPTION_GAP
///
/// @brief How far the gap in a group box's top line reaches past each end of
/// its caption.
#define WM_GROUP_CAPTION_GAP 1

/// @def WM_BOX_SIZE
///
/// @brief The width and height of a check box's or radio button's box.
#define WM_BOX_SIZE 9

/// @def WM_BOX_GAP
///
/// @brief How far a check box's or radio button's text is right of its box.
#define WM_BOX_GAP 4

/// @def WM_RADIO_INSET
///
/// @brief How far in from a radio button's box its square is.
#define WM_RADIO_INSET 2

/// @def WM_FOCUS_INSET
///
/// @brief How far in from a button's edge its focus outline is.
#define WM_FOCUS_INSET 2

/// @def WM_CARET_HEIGHT
///
/// @brief The height of a text field's caret.
#define WM_CARET_HEIGHT 10

/// @var _wmStars
///
/// @brief What a password field shows in place of its text, a run at a
/// time.
static const char _wmStars[] = "********************************";

/// @def WM_DEFAULT_BORDER
///
/// @brief The width of the default button's accent border.
#define WM_DEFAULT_BORDER 2

/// @fn static void wmFill(GfxCmdBuf *cmd, GfxColor color,
///   const GfxRect *rect)
///
/// @brief Record a filled rectangle of a color.
///
/// @param cmd The command buffer.
/// @param color The color.
/// @param rect The rectangle.
///
/// @return This function returns no value.
static void wmFill(GfxCmdBuf *cmd, GfxColor color, const GfxRect *rect) {
  gfxCmdSetFgColor(cmd, color);
  gfxCmdDrawRect(cmd, rect, true);
}

/// @fn static void wmText(GfxCmdBuf *cmd, GfxColor color, int16_t left,
///   int16_t top, const char *text, uint8_t length)
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
  int16_t top, const char *text, uint8_t length
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

/// @fn static GfxColor wmInk(const WmWidget *widget)
///
/// @brief Get the color of a widget's text:  dark gray if it is disabled.
///
/// @param widget The widget.
///
/// @return Returns the color.
static GfxColor wmInk(const WmWidget *widget) {
  if ((widget->flags & WIN_WIDGET_DISABLED) != 0) {
    return WM_DARK;
  }

  return WM_TEXT;
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

/// @fn static void wmLine(GfxCmdBuf *cmd, int16_t fromX, int16_t fromY,
///   int16_t toX, int16_t toY)
///
/// @brief Record a line in the foreground color.
///
/// @param cmd The command buffer.
/// @param fromX The column of one end.
/// @param fromY The row of one end.
/// @param toX The column of the other end.
/// @param toY The row of the other end.
///
/// @return This function returns no value.
static void wmLine(GfxCmdBuf *cmd, int16_t fromX, int16_t fromY,
  int16_t toX, int16_t toY
) {
  GfxPoint from = { fromX, fromY };
  GfxPoint to = { toX, toY };

  gfxCmdDrawLine(cmd, from, to);
}

/// @fn static void wmPaintLabel(WmState *state, const WmWidget *widget,
///   const GfxRect *rect)
///
/// @brief Record a label:  its text, aligned in its rectangle and centered
/// from top to bottom.
///
/// @param state The window manager's state.
/// @param widget The label.
/// @param rect Where the label is.
///
/// @return This function returns no value.
static void wmPaintLabel(WmState *state, const WmWidget *widget,
  const GfxRect *rect
) {
  int16_t width
    = (int16_t) (widget->textLength * state->display.builtInFont.glyphW);
  int16_t left = rect->x;

  if (widget->align == WIN_ALIGN_CENTER) {
    left += (rect->w - width) / 2;
  } else if (widget->align == WIN_ALIGN_RIGHT) {
    left += rect->w - width;
  }

  int16_t top = rect->y + ((rect->h - state->display.builtInFont.glyphH) / 2);
  wmText(state->cmd, wmInk(widget), left, top, widget->text,
    widget->textLength);
}

/// @fn static void wmPaintButton(WmState *state, const WmWidget *widget,
///   const GfxRect *rect, bool pressed, bool focused)
///
/// @brief Record a push or toggle button:  its face, a dark border, or the
/// default button's 2-pixel accent one, its text centered, and the focus
/// outline inside the border.  A button held down has a dark face, and a
/// toggle button that is on an accent one, each with white text.
///
/// @param state The window manager's state.
/// @param widget The button.
/// @param rect Where the button is.
/// @param pressed Whether it is held down with the pointer on it.
/// @param focused Whether it shows the focus.
///
/// @return This function returns no value.
static void wmPaintButton(WmState *state, const WmWidget *widget,
  const GfxRect *rect, bool pressed, bool focused
) {
  GfxCmdBuf *cmd = state->cmd;
  GfxColor face = WM_LIGHT;
  GfxColor ink = wmInk(widget);

  if (pressed) {
    face = WM_DARK;
    ink = WM_ON_ACCENT;
  } else if ((widget->kind == WIN_WIDGET_TOGGLE) && (widget->value != 0)) {
    face = WM_ACCENT;
    ink = WM_ON_ACCENT;
  }

  wmFill(cmd, face, rect);

  if ((widget->flags & WIN_WIDGET_DEFAULT) != 0) {
    for (int16_t ii = 0; ii < WM_DEFAULT_BORDER; ii++) {
      wmOutline(cmd, WM_ACCENT, rect->x + ii, rect->y + ii,
        rect->w - (2 * ii), rect->h - (2 * ii));
    }
  } else {
    wmOutline(cmd, WM_DARK, rect->x, rect->y, rect->w, rect->h);
  }

  int16_t width
    = (int16_t) (widget->textLength * state->display.builtInFont.glyphW);
  wmText(cmd, ink, rect->x + ((rect->w - width) / 2),
    rect->y + ((rect->h - state->display.builtInFont.glyphH) / 2),
    widget->text, widget->textLength);

  if (focused) {
    wmOutline(cmd, WM_ACCENT, rect->x + WM_FOCUS_INSET,
      rect->y + WM_FOCUS_INSET, rect->w - (2 * WM_FOCUS_INSET),
      rect->h - (2 * WM_FOCUS_INSET));
  }
}

/// @fn static void wmPaintBox(WmState *state, const WmWidget *widget,
///   const GfxRect *rect, bool pressed, bool focused)
///
/// @brief Record a check box or radio button:  its box, centered from top to
/// bottom at its left edge, with a check of two lines or a filled square if
/// it is set, its text right of the box, and the focus outline around the
/// text.  A box held down is light gray.
///
/// @param state The window manager's state.
/// @param widget The check box or radio button.
/// @param rect Where it is.
/// @param pressed Whether it is held down with the pointer on it.
/// @param focused Whether it shows the focus.
///
/// @return This function returns no value.
static void wmPaintBox(WmState *state, const WmWidget *widget,
  const GfxRect *rect, bool pressed, bool focused
) {
  GfxCmdBuf *cmd = state->cmd;
  GfxColor ink = wmInk(widget);
  GfxRect box = { rect->x, rect->y + ((rect->h - WM_BOX_SIZE) / 2),
    WM_BOX_SIZE, WM_BOX_SIZE };

  wmFill(cmd, pressed ? WM_LIGHT : WM_FACE, &box);
  wmOutline(cmd, WM_DARK, box.x, box.y, box.w, box.h);

  if ((widget->value != 0) && (widget->kind == WIN_WIDGET_CHECK_BOX)) {
    gfxCmdSetFgColor(cmd, ink);
    wmLine(cmd, box.x + 2, box.y + 4, box.x + 4, box.y + 6);
    wmLine(cmd, box.x + 4, box.y + 6, box.x + 6, box.y + 2);
  } else if (widget->value != 0) {
    GfxRect square = { box.x + WM_RADIO_INSET, box.y + WM_RADIO_INSET,
      WM_BOX_SIZE - (2 * WM_RADIO_INSET), WM_BOX_SIZE - (2 * WM_RADIO_INSET) };
    wmFill(cmd, ink, &square);
  }

  uint8_t glyphH = state->display.builtInFont.glyphH;
  int16_t left = rect->x + WM_BOX_SIZE + WM_BOX_GAP;
  int16_t top = rect->y + ((rect->h - glyphH) / 2);

  wmText(cmd, ink, left, top, widget->text, widget->textLength);

  if (focused) {
    wmOutline(cmd, WM_ACCENT, left - 1, top - 1,
      (int16_t) ((widget->textLength * state->display.builtInFont.glyphW) + 2),
      glyphH + 2);
  }
}

/// @fn static void wmPaintField(WmState *state, const WmWindow *window,
///   const WmWidget *widget, const GfxRect *rect, bool focused)
///
/// @brief Record a text field:  white inside a dark border, an accent one
/// when it has the focus, its text from its scroll on, a star for each byte
/// of a password, and the caret before its byte when it has the focus.
///
/// @param state The window manager's state.
/// @param window The field's window.
/// @param widget The text field.
/// @param rect Where it is.
/// @param focused Whether it shows the focus.
///
/// @return This function returns no value.
static void wmPaintField(WmState *state, const WmWindow *window,
  const WmWidget *widget, const GfxRect *rect, bool focused
) {
  GfxCmdBuf *cmd = state->cmd;
  uint8_t glyphW = state->display.builtInFont.glyphW;

  wmFill(cmd, WM_FACE, rect);
  wmOutline(cmd, focused ? WM_ACCENT : WM_DARK, rect->x, rect->y, rect->w,
    rect->h);
  uint8_t scroll = wmFieldScroll(state, window, widget);
  int16_t left = rect->x + WM_FIELD_INSET;
  int16_t top = rect->y + ((rect->h - state->display.builtInFont.glyphH) / 2);
  int16_t shown = (rect->w - (2 * WM_FIELD_INSET)) / glyphW;
  uint8_t end = widget->textLength;

  if ((end - scroll) > shown) {
    end = (uint8_t) (scroll + shown);
  }

  if ((widget->flags & WIN_WIDGET_PASSWORD) != 0) {
    for (uint8_t at = scroll; at < end; ) {
      uint8_t run = end - at;
      if (run > (sizeof(_wmStars) - 1)) {
        run = sizeof(_wmStars) - 1;
      }

      wmText(cmd, wmInk(widget), left + ((at - scroll) * glyphW), top,
        _wmStars, run);
      at += run;
    }
  } else if (end > scroll) {
    wmText(cmd, wmInk(widget), left, top, &widget->text[scroll],
      end - scroll);
  }

  if (focused) {
    GfxRect caret = { left + ((widget->caret - scroll) * glyphW) - 1,
      rect->y + ((rect->h - WM_CARET_HEIGHT) / 2), 1, WM_CARET_HEIGHT };
    wmFill(cmd, WM_TEXT, &caret);
  }
}

/// @fn static void wmPaintGroup(WmState *state, const WmWindow *window,
///   const WmWidget *widget, const GfxRect *rect)
///
/// @brief Record a group box:  a line around it, from the middle of its
/// caption's height down, broken where the caption is.
///
/// @param state The window manager's state.
/// @param window The group box's window.
/// @param widget The group box.
/// @param rect Where the group box is.
///
/// @return This function returns no value.
static void wmPaintGroup(WmState *state, const WmWindow *window,
  const WmWidget *widget, const GfxRect *rect
) {
  GfxCmdBuf *cmd = state->cmd;
  int16_t half = state->display.builtInFont.glyphH / 2;
  GfxRect box = { rect->x, rect->y + half, rect->w, rect->h - half };

  gfxCmdSetFgColor(cmd, WM_DARK);
  gfxCmdDrawRect(cmd, &box, false);

  if (widget->textLength == 0) {
    return;
  }

  int16_t left = rect->x + WM_GROUP_CAPTION_INSET;
  GfxRect gap = { left - WM_GROUP_CAPTION_GAP, rect->y,
    (int16_t) ((widget->textLength * state->display.builtInFont.glyphW)
      + (2 * WM_GROUP_CAPTION_GAP)),
    state->display.builtInFont.glyphH };

  wmFill(cmd, window->face, &gap);
  wmText(cmd, wmInk(widget), left, rect->y, widget->text,
    widget->textLength);
}

/// @fn static void wmPaintCanvas(WmState *state, const WmWidget *widget,
///   const GfxRect *rect, const GfxRect *overlap)
///
/// @brief Record a canvas:  a call of each of its preuploads with the
/// origin at its corner, inside the part of it being painted.
///
/// @param state The window manager's state.
/// @param widget The canvas.
/// @param rect Where it is.
/// @param overlap The part of it being painted.
///
/// @return This function returns no value.
static void wmPaintCanvas(WmState *state, const WmWidget *widget,
  const GfxRect *rect, const GfxRect *overlap
) {
  const WmCanvas *canvas = (const WmCanvas*) widget->text;
  if ((canvas == NULL) || (canvas->count == 0)) {
    return;
  }

  GfxCmdBuf *cmd = state->cmd;
  GfxPoint origin = { rect->x, rect->y };
  GfxPoint home = { 0, 0 };

  gfxCmdPushScissor(cmd, overlap);
  gfxCmdSetOrigin(cmd, origin);

  for (uint8_t ii = 0; ii < canvas->count; ii++) {
    gfxCmdCall(cmd, canvas->preloads[ii]);
  }

  gfxCmdSetOrigin(cmd, home);
  gfxCmdPopScissor(cmd);
}

/// @fn static bool wmSpills(const WmState *state, const WmWidget *widget,
///   const GfxRect *rect)
///
/// @brief Determine whether a widget's drawing could reach outside its
/// rectangle:  its text, or a box's box, being wider or taller than the
/// rectangle has room for.  A text field's text is cut to fit already.
///
/// @param state The window manager's state.
/// @param widget The widget.
/// @param rect Where it is.
///
/// @return Returns true if it could, false otherwise.
static bool wmSpills(const WmState *state, const WmWidget *widget,
  const GfxRect *rect
) {
  uint8_t glyphH = state->display.builtInFont.glyphH;
  int16_t width
    = (int16_t) (widget->textLength * state->display.builtInFont.glyphW);
  int16_t height = glyphH;

  if ((widget->kind == WIN_WIDGET_TEXT_FIELD)
    || (widget->kind == WIN_WIDGET_CANVAS)
  ) {
    return false;
  } else if (widget->kind == WIN_WIDGET_LIST) {
    return rect->h < (WM_LIST_ITEM_HEIGHT + 2);
  } else if ((widget->kind == WIN_WIDGET_BUTTON)
    || (widget->kind == WIN_WIDGET_TOGGLE)
  ) {
    width += 2;
  } else if ((widget->kind == WIN_WIDGET_CHECK_BOX)
    || (widget->kind == WIN_WIDGET_RADIO)
  ) {
    width += WM_BOX_SIZE + WM_BOX_GAP;

    if (height < WM_BOX_SIZE) {
      height = WM_BOX_SIZE;
    }
  } else if (widget->kind == WIN_WIDGET_GROUP) {
    width += WM_GROUP_CAPTION_INSET + WM_GROUP_CAPTION_GAP;
  }

  return (width > rect->w) || (height > rect->h);
}

/// @fn void* wmWidgetsPaint(void *args)
///
/// @brief Record a window's widgets that meet the rectangle being painted,
/// in template order, each inside its own rectangle.  Only a widget whose
/// drawing could spill out of it, or a canvas, is drawn inside a scissor of
/// its own, since the VDP answers each change of the clip with a mode packet,
/// and too many of them at once overrun the UART's receive ring and lose
/// the keys that follow.
///
/// @param args A pointer to the WmPaintJob.
///
/// @return Always returns the provided pointer.
void* wmWidgetsPaint(void *args) {
  WmPaintJob *job = (WmPaintJob*) args;
  WmState *state = job->state;
  const WmWindow *window = job->window;
  const WmWidgets *widgets = (const WmWidgets*) window->widgets;
  bool active = (state->active == window->id);

  for (uint8_t ii = 0; (widgets != NULL) && (ii < widgets->count); ii++) {
    const WmWidget *widget = &widgets->widget[ii];
    GfxRect rect;
    GfxRect overlap;

    wmWidgetRect(window, widget, &rect);

    if (((widget->flags & WIN_WIDGET_HIDDEN) != 0)
      || (!wmIntersect(&rect, job->clip, &overlap))
    ) {
      continue;
    }

    bool spills = wmSpills(state, widget, &rect);
    if (spills) {
      gfxCmdPushScissor(state->cmd, &overlap);
    }

    if (widget->kind == WIN_WIDGET_CANVAS) {
      wmPaintCanvas(state, widget, &rect, &overlap);
    } else if (widget->kind == WIN_WIDGET_LABEL) {
      wmPaintLabel(state, widget, &rect);
    } else if (widget->kind == WIN_WIDGET_GROUP) {
      wmPaintGroup(state, window, widget, &rect);
    } else {
      bool pressed = (widgets->pressed == ii) && widgets->pressInside;
      bool focused = active && (widgets->focus == ii)
        && ((widget->flags & WIN_WIDGET_DISABLED) == 0);

      if ((widget->kind == WIN_WIDGET_BUTTON)
        || (widget->kind == WIN_WIDGET_TOGGLE)
      ) {
        wmPaintButton(state, widget, &rect, pressed, focused);
      } else if (widget->kind == WIN_WIDGET_TEXT_FIELD) {
        wmPaintField(state, window, widget, &rect, focused);
      } else if ((widget->kind == WIN_WIDGET_LIST)
        || (widget->kind == WIN_WIDGET_MENU_BAR)
      ) {
        WmWidgetJob pass = { state, (WmWindow*) window, (WmWidget*) widget,
          rect, NULL, focused, false };
        bool list = (widget->kind == WIN_WIDGET_LIST);

        callOverlayFunction(OVERLAY_SAME_NAMESPACE,
          list ? "Lists" : "MenuPaint",
          list ? "wmListsPaint" : "wmMenusPaintBar", &pass);
      } else {
        wmPaintBox(state, widget, &rect, pressed, focused);
      }
    }

    if (spills) {
      gfxCmdPopScissor(state->cmd);
    }
  }

  return args;
}
