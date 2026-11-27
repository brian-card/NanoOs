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


/// @file Paint.c
///
/// @brief The window manager's painting:  turning the damage main has worked
/// out into the records that bring the screen up to date.  Entered once per
/// frame.

#include "Wm.h"

/// @def WM_OUTLINE
///
/// @brief The color an outline is drawn with, by XOR.
#define WM_OUTLINE CGA_WHITE

/// @def WM_ELLIPSIS_LENGTH
///
/// @brief The number of dots that end a title cut short to fit its bar.
#define WM_ELLIPSIS_LENGTH 3

/// @fn static bool meets(const GfxRect *rect, const GfxRect *clip)
///
/// @brief Determine whether a rectangle overlaps the one being painted.
///
/// @param rect The rectangle.
/// @param clip The rectangle being painted.
///
/// @return Returns true if they overlap, false otherwise.
static __attribute__((noinline)) bool meets(
  const GfxRect *rect, const GfxRect *clip) {
  GfxRect overlap;
  return wmIntersect(rect, clip, &overlap);
}

/// @fn static void fill(GfxCmdBuf *cmd, GfxColor color, int16_t left,
///   int16_t top, int16_t width, int16_t height)
///
/// @brief Record a filled rectangle of a color.
///
/// @param cmd The command buffer.
/// @param color The color.
/// @param left The rectangle's left edge.
/// @param top The rectangle's top edge.
/// @param width The rectangle's width.
/// @param height The rectangle's height.
///
/// @return This function returns no value.
static void fill(GfxCmdBuf *cmd, GfxColor color, int16_t left,
  int16_t top, int16_t width, int16_t height
) {
  GfxRect rect = { left, top, width, height };
  gfxCmdSetFgColor(cmd, color);
  gfxCmdDrawRect(cmd, &rect, true);
}

/// @fn static void line(GfxCmdBuf *cmd, int16_t fromX, int16_t fromY,
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
static __attribute__((noinline)) void line(
  GfxCmdBuf *cmd, int16_t fromX, int16_t fromY, int16_t toX,
  int16_t toY
) {
  GfxPoint from = { fromX, fromY };
  GfxPoint to = { toX, toY };
  gfxCmdDrawLine(cmd, from, to);
}

/// @fn static void paintButton(WmState *state, const WmWindow *window,
///   uint8_t button, GfxColor bar, GfxColor ink)
///
/// @brief Record one of a window's caption buttons and its 7x7 glyph:  on
/// the title bar's color, pressed while it's held down with the pointer on
/// it, and light, or light red for close, under the pointer.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param button The WmButton.
/// @param bar The color of the title bar.
/// @param ink The color of the title bar's text.
///
/// @return This function returns no value.
static __attribute__((noinline)) void paintButton(
  WmState *state, const WmWindow *window,
  uint8_t button, GfxColor bar, GfxColor ink
) {
  GfxCmdBuf *cmd = state->cmd;
  GfxRect rect;
  wmButtonRect(window, button, &rect);
  GfxColor face = bar;
  if ((state->grab == WM_GRAB_BUTTON) && (state->grabbed == window->id)) {
    if ((state->pressButton == button) && state->pressInside) {
      face = WM_DARK;
      ink = WM_ON_ACCENT;
    }
  } else if ((state->hoverWindow == window->id)
    && (state->hoverButton == button)
  ) {
    face = WM_LIGHT;
    ink = WM_TEXT;
    if (button == WM_BUTTON_CLOSE) {
      face = WM_CLOSE_HOT;
      ink = WM_ON_ACCENT;
    }
  }
  fill(cmd, face, rect.x, rect.y, rect.w, rect.h);
  int16_t left = rect.x + (WM_BUTTON_WIDTH - WM_GLYPH_SIZE) / 2;
  int16_t top = rect.y + (WM_TITLE_HEIGHT - WM_GLYPH_SIZE) / 2;
  int16_t right = left + WM_GLYPH_SIZE - 1;
  int16_t bottom = top + WM_GLYPH_SIZE - 1;
  gfxCmdSetFgColor(cmd, ink);
  if (button == WM_BUTTON_MINIMIZE) {
    line(cmd, left, bottom, right, bottom);
  } else if (button == WM_BUTTON_CLOSE) {
    line(cmd, left, top, right, bottom);
    line(cmd, right, top, left, bottom);
  } else if (window->state == WIN_STATE_MAXIMIZED) {
    GfxRect back = { left + WM_RESTORE_OFFSET, top,
      WM_GLYPH_SIZE - WM_RESTORE_OFFSET, WM_GLYPH_SIZE - WM_RESTORE_OFFSET };
    GfxRect front = { left, top + WM_RESTORE_OFFSET,
      WM_GLYPH_SIZE - WM_RESTORE_OFFSET, WM_GLYPH_SIZE - WM_RESTORE_OFFSET };
    gfxCmdDrawRect(cmd, &back, false);
    fill(cmd, face, front.x, front.y, front.w, front.h);
    gfxCmdSetFgColor(cmd, ink);
    gfxCmdDrawRect(cmd, &front, false);
  } else {
    GfxRect box = { left, top, WM_GLYPH_SIZE, WM_GLYPH_SIZE };
    gfxCmdDrawRect(cmd, &box, false);
  }
}

/// @fn static void paintTitle(WmState *state, const WmWindow *window,
///   const GfxRect *clip)
///
/// @brief Record a decorated window's border, title bar, title and caption
/// buttons, each only if it meets the rectangle being painted, in the accent
/// color if it's active and gray if not.  A title too long for the space
/// before the buttons ends with "...".
///
/// @param state The window manager's state.
/// @param window The window.
/// @param clip The rectangle being painted.
///
/// @return This function returns no value.
static __attribute__((noinline)) void paintTitle(
  WmState *state, const WmWindow *window,
  const GfxRect *clip
) {
  GfxCmdBuf *cmd = state->cmd;
  const GfxRect *frame = &window->frame;
  GfxColor bar = WM_LIGHT;
  GfxColor border = WM_DARK;
  GfxColor ink = WM_DARK;
  if ((window->id == state->active)
    || ((window->layer != WIN_LAYER_NORMAL) && (window->id == state->focus))
  ) {
    bar = WM_ACCENT;
    border = WM_ACCENT;
    ink = WM_ON_ACCENT;
  }
  GfxRect inner = { frame->x + WM_BORDER, frame->y + WM_BORDER,
    frame->w - 2 * WM_BORDER, frame->h - 2 * WM_BORDER };
  GfxRect overlap;
  if (!wmIntersect(clip, &inner, &overlap)
    || (overlap.w != clip->w) || (overlap.h != clip->h)
  ) {
    gfxCmdSetFgColor(cmd, border);
    gfxCmdDrawRect(cmd, frame, false);
  }
  GfxRect text = { inner.x, inner.y, inner.w - WM_BUTTONS_WIDTH,
    WM_TITLE_HEIGHT };
  if (wmIntersect(&text, clip, &overlap)) {
    fill(cmd, bar, overlap.x, overlap.y, overlap.w, overlap.h);
    uint8_t glyphW = state->display.builtInFont.glyphW;
    int16_t room = (text.w - WM_TITLE_INSET) / glyphW;
    if ((window->title != NULL) && (room > 0)) {
      char shown[WIN_TITLE_MAX + 1];
      size_t length = strlen(window->title);
      memcpy(shown, window->title, length + 1);
      if ((int16_t) length > room) {
        length = (size_t) room;
        shown[length] = '\0';
        size_t firstDot = 0;
        if (length > WM_ELLIPSIS_LENGTH) {
          firstDot = length - WM_ELLIPSIS_LENGTH;
        }
        for (size_t ii = firstDot; ii < length; ii++) {
          shown[ii] = '.';
        }
      }
      GfxPoint titleAt = { text.x + WM_TITLE_INSET,
        text.y + WM_TITLE_TEXT_TOP };
      gfxCmdSetFgColor(cmd, ink);
      gfxCmdDrawText(cmd, titleAt, shown);
    }
  }
  for (uint8_t button = WM_BUTTON_MINIMIZE; button <= WM_BUTTON_CLOSE;
    button++
  ) {
    GfxRect rect;
    wmButtonRect(window, button, &rect);
    if (meets(&rect, clip)) {
      paintButton(state, window, button, bar, ink);
    }
  }
}

/// @var _ansiColors
///
/// @brief The color of each ANSI color index a text grid's attributes use.
static const GfxColor _ansiColors[WIN_ANSI_NUM_COLORS] = WM_ANSI_COLORS;

/// @fn static void drawRun(GfxCmdBuf *cmd, int16_t left, int16_t top,
///   const uint8_t *text, uint8_t length)
///
/// @brief Record a run of a text grid's characters, which have no
/// terminator, in the foreground color.
///
/// @param cmd The command buffer.
/// @param left The run's left edge.
/// @param top The run's top edge.
/// @param text The characters.
/// @param length The number of characters.
///
/// @return This function returns no value.
static void drawRun(GfxCmdBuf *cmd, int16_t left, int16_t top,
  const uint8_t *text, uint8_t length
) {
  uint8_t *record = gfxCmdBufReserve_(cmd, GFX_CMD_DRAW_TEXT_SIZE(length));
  if (record != NULL) {
    GfxPoint at = { left, top };
    record[GFX_CMD_TAG] = GFX_CMD_DRAW_TEXT;
    memcpy(&record[GFX_CMD_DRAW_TEXT_AT], &at, sizeof(at));
    record[GFX_CMD_DRAW_TEXT_LENGTH] = length;
    memcpy(&record[GFX_CMD_DRAW_TEXT_TEXT], text, length);
  }
}

/// @fn static void paintGrid(WmState *state, const WmWindow *window,
///   const GfxRect *clip)
///
/// @brief Record the rows of a window's text grid that meet the rectangle
/// being painted, as runs of one attribute:  a background, then the run's
/// text up to its last character that isn't a space.
///
/// @param state The window manager's state.
/// @param window The window, whose content is a WmGrid.
/// @param clip The rectangle being painted.
///
/// @return This function returns no value.
static __attribute__((noinline)) void paintGrid(
  WmState *state, const WmWindow *window, const GfxRect *clip
) {
  GfxCmdBuf *cmd = state->cmd;
  const WmGrid *grid = (const WmGrid*) window->content;
  uint8_t glyphW = state->display.builtInFont.glyphW;
  uint8_t glyphH = state->display.builtInFont.glyphH;
  uint8_t columns = grid->columns;
  const uint8_t *chars = grid->cells;
  size_t plane = (size_t) columns * grid->rows;
  int16_t top = window->frame.y + grid->top;
  int16_t gridLeft = window->frame.x + grid->left;
  for (uint8_t row = 0; row < grid->rows;
    row++, chars += columns, top += glyphH
  ) {
    if ((top + glyphH <= clip->y) || (top >= clip->y + clip->h)) {
      continue;
    }
    uint8_t column = 0;
    while (column < columns) {
      uint8_t attribute = chars[plane + column];
      uint8_t end = column;
      uint8_t last = column;
      do {
        if (chars[end] != ' ') {
          last = end + 1;
        }
        end++;
      } while ((end < columns) && (chars[plane + end] == attribute));
      int16_t left = gridLeft + (int16_t) (column * glyphW);
      fill(cmd, _ansiColors[WIN_ATTRIBUTE_BACKGROUND(attribute)], left, top,
        (int16_t) ((end - column) * glyphW), glyphH);
      if (last > column) {
        gfxCmdSetFgColor(cmd,
          _ansiColors[WIN_ATTRIBUTE_FOREGROUND(attribute)]);
        drawRun(cmd, left, top, &chars[column], last - column);
      }
      column = end;
    }
  }
}

/// @fn static void paintWindow(WmState *state, const WmWindow *window,
///   const GfxRect *clip)
///
/// @brief Record a window's decorations and body, and through the Widgets
/// overlay its widgets, inside its frame, leaving out what doesn't meet the
/// rectangle being painted.  Under a text grid, only the grid's margins are
/// filled with the body's color, so that no cell shows it between its old
/// colors and its new.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param clip The rectangle being painted.
///
/// @return This function returns no value.
static __attribute__((noinline)) void paintWindow(
  WmState *state, const WmWindow *window,
  const GfxRect *clip
) {
  GfxCmdBuf *cmd = state->cmd;
  const GfxRect *frame = &window->frame;
  GfxRect body = *frame;
  gfxCmdPushScissor(cmd, frame);
  if ((window->flags & WIN_FLAG_DECORATED) != 0) {
    paintTitle(state, window, clip);
    body.x += WM_BORDER;
    body.y += WM_FRAME_TOP;
    body.w -= 2 * WM_BORDER;
    body.h -= WM_FRAME_TOP + WM_BORDER;
  }
  GfxRect overlap;
  if (wmIntersect(&body, clip, &overlap)) {
    const WmGrid *grid = (const WmGrid*) window->content;
    if (grid != NULL) {
      const GfxRect *end = &grid->margins[grid->numMargins];
      for (const GfxRect *margin = grid->margins; margin < end; margin++) {
        fill(cmd, window->face, frame->x + margin->x, frame->y + margin->y,
          margin->w, margin->h);
      }
      paintGrid(state, window, clip);
    } else {
      fill(cmd, window->face, overlap.x, overlap.y, overlap.w, overlap.h);
      if (window->widgets != NULL) {
        WmPaintJob job = { state, window, clip };
        callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Widgets",
          "wmWidgetsPaint", &job);
      }
    }
  }
  gfxCmdPopScissor(cmd);
}

/// @fn static bool covered(WmState *state, const GfxRect *rect)
///
/// @brief Determine whether a shown window covers the whole of a rectangle,
/// so that it needs no desktop under it.
///
/// @param state The window manager's state.
/// @param rect The rectangle.
///
/// @return Returns true if one does, false otherwise.
static __attribute__((noinline)) bool covered(
  WmState *state, const GfxRect *rect) {
  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    const WmWindow *window = &state->windows[ii];
    GfxRect overlap;
    if ((window->state != WIN_STATE_MINIMIZED)
      && wmIntersect(&window->frame, rect, &overlap)
      && (overlap.w == rect->w) && (overlap.h == rect->h)
    ) {
      return true;
    }
  }
  return false;
}

/// @fn static void paintRect(WmState *state, const GfxRect *rect)
///
/// @brief Record the desktop, every shown window and the open menu that meet
/// a rectangle, back to front, inside the rectangle:  only the menu if the
/// rectangle is inside it.
///
/// @param state The window manager's state.
/// @param rect The rectangle.
///
/// @return This function returns no value.
static void paintRect(WmState *state, const GfxRect *rect) {
  GfxCmdBuf *cmd = state->cmd;
  GfxRect screen = { 0, 0, (int16_t) state->display.width,
    (int16_t) state->display.height };
  GfxRect clip;
  if (!wmIntersect(rect, &screen, &clip)) {
    return;
  }
  gfxCmdPushScissor(cmd, &clip);
  const GfxRect *menu = &state->menu.rect;
  bool inMenu = state->menu.open && (clip.x >= menu->x) && (clip.y >= menu->y)
    && (clip.x + clip.w <= menu->x + menu->w)
    && (clip.y + clip.h <= menu->y + menu->h);
  if (!inMenu && !covered(state, &clip)) {
    gfxCmdSetBgColor(cmd, WM_DESKTOP);
    gfxCmdClear(cmd);
  }
  for (uint8_t ii = 0; !inMenu && (ii < state->numWindows); ii++) {
    const WmWindow *window = &state->windows[ii];
    if ((window->state != WIN_STATE_MINIMIZED)
      && meets(&window->frame, &clip)
    ) {
      paintWindow(state, window, &clip);
    }
  }
  if (state->menu.open && meets(&state->menu.rect, &clip)) {
    WmPaintJob job = { state, NULL, &clip };
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "MenuPaint",
      "wmMenusPaintDropdown", &job);
  }
  gfxCmdPopScissor(cmd);
}

/// @fn static void paintOwned(WmState *state, const GfxRect *rect,
///   uint8_t owner)
///
/// @brief Record a rectangle of damage:  one where a window's own contents
/// changed, which the Restack overlay has cut down to where the window
/// shows, is repainted with that window alone; any other is repainted with
/// everything in it.
///
/// @param state The window manager's state.
/// @param rect The rectangle.
/// @param owner The identifier of the window whose contents changed, or 0.
///
/// @return This function returns no value.
static void paintOwned(WmState *state, const GfxRect *rect, uint8_t owner) {
  WmWindow *window = wmFind(state, owner);
  if (window == NULL) {
    paintRect(state, rect);
    return;
  }
  gfxCmdPushScissor(state->cmd, rect);
  paintWindow(state, window, rect);
  gfxCmdPopScissor(state->cmd);
}

/// @fn static void xorOutline(GfxCmdBuf *cmd, const GfxRect *outline)
///
/// @brief Record a 1-pixel outline drawn by XOR, which drawing again erases.
///
/// @param cmd The command buffer.
/// @param outline The outline.
///
/// @return This function returns no value.
static void xorOutline(GfxCmdBuf *cmd, const GfxRect *outline) {
  gfxCmdSetFgColor(cmd, WM_OUTLINE);
  gfxCmdSetBlend(cmd, GFX_BLEND_XOR);
  gfxCmdDrawRect(cmd, outline, false);
  gfxCmdSetBlend(cmd, GFX_BLEND_OPAQUE);
}

/// @fn static void paintFrame(WmState *state)
///
/// @brief Record and submit what brings the screen up to date with the
/// state:  the old outline erased, the pixels of the grabbed window or a
/// scrolled text grid copied if main or the Grid overlay asked for it, the
/// damage repainted, and the new outline drawn.  While an
/// outline stays on the screen, the damage is kept for the frame that takes
/// it away, because painting under it would leave the outline's XOR to undo
/// pixels it wasn't drawn over.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void paintFrame(WmState *state) {
  GfxCmdBuf *cmd = state->cmd;
  bool outlineMoved = (memcmp(&state->outline, &state->outlineShown,
    sizeof(GfxRect)) != 0);
  bool hold = (state->outline.w != 0) && (state->outlineShown.w != 0);
  if (outlineMoved && (state->outlineShown.w != 0)) {
    xorOutline(cmd, &state->outlineShown);
  }
  if (state->copy) {
    gfxCmdCopyRect(cmd, &state->copySource, state->copyTarget);
  }

  if (!hold) {
    if (state->paintAll) {
      GfxRect screen = { 0, 0, (int16_t) state->display.width,
        (int16_t) state->display.height };
      paintRect(state, &screen);
    } else {
      uint8_t top = state->windows[state->numWindows - 1].id;
      bool hidden = false;
      for (uint8_t ii = 0; ii < state->numDamage; ii++) {
        hidden = hidden || ((state->damageOwner[ii] != 0)
          && ((state->damageOwner[ii] != top) || state->menu.open));
      }
      if (hidden) {
        callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Restack", "wmOcclude",
          state);
      }
      for (uint8_t ii = 0; ii < state->numDamage; ii++) {
        paintOwned(state, &state->damage[ii], state->damageOwner[ii]);
      }
    }
    state->paintAll = false;
    state->numDamage = 0;
  }
  if (outlineMoved && (state->outline.w != 0)) {
    xorOutline(cmd, &state->outline);
  }
  state->outlineShown = state->outline;
  state->copy = false;
  state->status = (uint8_t) gfxSubmit(cmd);
}

/// @fn void* wmPaint(void *args)
///
/// @brief Bring the screen up to date with the state.  The status goes in
/// the state.
///
/// @param args A pointer to the WmState.
///
/// @return Always returns the provided pointer.
void* wmPaint(void *args) {
  WmState *state = (WmState*) args;
  state->status = GFX_OK;
  if ((state->refocus[0] | state->refocus[1] | state->raised) != 0) {
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Restack", "wmRestack",
      state);
  }
  paintFrame(state);
  return args;
}
