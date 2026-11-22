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
/// @brief The window manager's painting:  turning a change of the state into
/// the records that bring the screen up to date.

#include "Wm.h"

/// @def WM_DESKTOP
///
/// @brief The colour of the desktop.
#define WM_DESKTOP CGA_CYAN

/// @def WM_TITLE_BAR
///
/// @brief The colour of a title bar.
#define WM_TITLE_BAR CGA_BLUE

/// @def WM_TITLE_TEXT
///
/// @brief The colour of a title.
#define WM_TITLE_TEXT CGA_WHITE

/// @def WM_GRIP
///
/// @brief The colour of a resize grip.
#define WM_GRIP CGA_DARK_GRAY

/// @def WM_CELL_EDGE
///
/// @brief The colour of a cell's outline.
#define WM_CELL_EDGE CGA_BLACK

/// @def WM_ROW_TEXT
///
/// @brief The colour of the text across every third row of cells.
#define WM_ROW_TEXT CGA_WHITE

/// @def WM_OUTLINE
///
/// @brief The colour an outline is drawn with, by XOR.
#define WM_OUTLINE CGA_WHITE

/// @var _cellFills
///
/// @brief The fill colours of the cells, in turn along each row.
static const GfxColor _cellFills[4] = {
  CGA_GREEN, CGA_RED, CGA_MAGENTA, CGA_BROWN,
};

/// @var _rowText
///
/// @brief The text across every third row of cells, as wide as a row.
static const char _rowText[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcd";

/// @fn static bool intersect(const GfxRect *first, const GfxRect *second,
///   GfxRect *result)
///
/// @brief Find where two rectangles overlap.
///
/// @param first One rectangle.
/// @param second The other.
/// @param result Where to store the overlap, which may be either input.
///
/// @return Returns true if they overlap, false otherwise.
static bool intersect(const GfxRect *first, const GfxRect *second,
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

/// @fn static void addDamage(WmState *state, int16_t left, int16_t top,
///   int16_t width, int16_t height)
///
/// @brief Add a rectangle to the frame's damage, unless it's empty.  If the
/// list is full, the last rectangle grows to cover it as well.
///
/// @param state The window manager's state.
/// @param left The rectangle's left edge.
/// @param top The rectangle's top edge.
/// @param width The rectangle's width.
/// @param height The rectangle's height.
///
/// @return This function returns no value.
static void addDamage(WmState *state, int16_t left, int16_t top,
  int16_t width, int16_t height
) {
  if ((width <= 0) || (height <= 0)) {
    return;
  }
  if (state->numDamage < WM_MAX_DAMAGE) {
    GfxRect *rect = &state->damage[state->numDamage++];
    rect->x = left;
    rect->y = top;
    rect->w = width;
    rect->h = height;
    return;
  }
  GfxRect *last = &state->damage[WM_MAX_DAMAGE - 1];
  int16_t right = last->x + last->w;
  int16_t bottom = last->y + last->h;
  if (left + width > right) {
    right = left + width;
  }
  if (top + height > bottom) {
    bottom = top + height;
  }
  if (left < last->x) {
    last->x = left;
  }
  if (top < last->y) {
    last->y = top;
  }
  last->w = right - last->x;
  last->h = bottom - last->y;
}

/// @fn static void addRect(WmState *state, const GfxRect *rect)
///
/// @brief Add a rectangle to the frame's damage.
///
/// @param state The window manager's state.
/// @param rect The rectangle.
///
/// @return This function returns no value.
static void addRect(WmState *state, const GfxRect *rect) {
  addDamage(state, rect->x, rect->y, rect->w, rect->h);
}

/// @fn static void subtract(WmState *state, const GfxRect *from,
///   const GfxRect *away)
///
/// @brief Add the part of one rectangle that another doesn't cover to the
/// frame's damage, as up to four rectangles.
///
/// @param state The window manager's state.
/// @param from The rectangle.
/// @param away The rectangle taken away from it.
///
/// @return This function returns no value.
static void subtract(WmState *state, const GfxRect *from,
  const GfxRect *away
) {
  GfxRect overlap;
  if (!intersect(from, away, &overlap)) {
    addRect(state, from);
    return;
  }
  addDamage(state, from->x, from->y, from->w, overlap.y - from->y);
  addDamage(state, from->x, overlap.y + overlap.h, from->w,
    from->y + from->h - (overlap.y + overlap.h));
  addDamage(state, from->x, overlap.y, overlap.x - from->x, overlap.h);
  addDamage(state, overlap.x + overlap.w, overlap.y,
    from->x + from->w - (overlap.x + overlap.w), overlap.h);
}

/// @fn static void fill(GfxCmdBuf *cmd, GfxColor colour, int16_t left,
///   int16_t top, int16_t width, int16_t height)
///
/// @brief Record a filled rectangle of a colour.
///
/// @param cmd The command buffer.
/// @param colour The colour.
/// @param left The rectangle's left edge.
/// @param top The rectangle's top edge.
/// @param width The rectangle's width.
/// @param height The rectangle's height.
///
/// @return This function returns no value.
static void fill(GfxCmdBuf *cmd, GfxColor colour, int16_t left,
  int16_t top, int16_t width, int16_t height
) {
  GfxRect rect = { left, top, width, height };
  gfxCmdSetFgColor(cmd, colour);
  gfxCmdDrawRect(cmd, &rect, true);
}

/// @fn static void recordRow(GfxCmdBuf *cmd, uint8_t row, uint8_t cells,
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
static void recordRow(GfxCmdBuf *cmd, uint8_t row, uint8_t cells,
  GfxPoint origin, const GfxRect *clip
) {
  uint8_t first = row * WM_CELL_COLUMNS;
  uint8_t count = cells - first;
  if (count > WM_CELL_COLUMNS) {
    count = WM_CELL_COLUMNS;
  }
  int16_t top = origin.y + (int16_t) (row * WM_CELL_SIZE);
  for (uint8_t pass = 0; pass < 2; pass++) {
    if (pass == 1) {
      gfxCmdSetFgColor(cmd, WM_CELL_EDGE);
    }
    for (uint8_t column = 0; column < count; column++) {
      GfxRect cell = { origin.x + (int16_t) (column * WM_CELL_SIZE), top,
        WM_CELL_SIZE, WM_CELL_SIZE };
      GfxRect overlap;
      if ((clip != NULL) && !intersect(&cell, clip, &overlap)) {
        continue;
      }
      if (pass == 0) {
        gfxCmdSetFgColor(cmd, _cellFills[(row + column) & 3]);
      }
      gfxCmdDrawRect(cmd, &cell, pass == 0);
    }
  }
  if ((row % 3) == 0) {
    GfxPoint position = { origin.x, top + 6 };
    gfxCmdSetFgColor(cmd, WM_ROW_TEXT);
    gfxCmdDrawText(cmd, position, _rowText);
  }
}

/// @fn static void paintCells(WmState *state, const WmWindow *window,
///   const GfxRect *clip)
///
/// @brief Record the rows of a window's cells that meet a rectangle, by
/// calling their preloads or by sending their records.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param clip The rectangle being painted.
///
/// @return This function returns no value.
static void paintCells(WmState *state, const WmWindow *window,
  const GfxRect *clip
) {
  GfxCmdBuf *cmd = state->cmd;
  GfxPoint origin = { window->frame.x,
    window->frame.y + WM_TITLE_HEIGHT };
  uint8_t rows = (window->cells + WM_CELL_COLUMNS - 1) / WM_CELL_COLUMNS;
  bool moved = false;
  for (uint8_t row = 0; row < rows; row++) {
    GfxRect band = { origin.x, origin.y + (int16_t) (row * WM_CELL_SIZE),
      WM_CELL_COLUMNS * WM_CELL_SIZE, WM_CELL_SIZE };
    if (!intersect(&band, clip, &band)) {
      continue;
    }
    if (!state->contentByCall) {
      recordRow(cmd, row, window->cells, origin, clip);
      continue;
    }
    if (!moved) {
      gfxCmdSetOrigin(cmd, origin);
      moved = true;
    }
    gfxCmdCall(cmd, state->rows[row]);
  }
  if (moved) {
    GfxPoint home = { 0, 0 };
    gfxCmdSetOrigin(cmd, home);
  }
}

/// @fn static void paintWindow(WmState *state, const WmWindow *window,
///   const GfxRect *clip)
///
/// @brief Record a window's title bar, body, cells and grip, inside its
/// frame and a rectangle.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param clip The rectangle being painted.
///
/// @return This function returns no value.
static void paintWindow(WmState *state, const WmWindow *window,
  const GfxRect *clip
) {
  GfxCmdBuf *cmd = state->cmd;
  const GfxRect *frame = &window->frame;
  char title[] = "Window 0";
  title[7] = (char) ('0' + window->number);
  GfxPoint titleAt = { frame->x + 4, frame->y + 2 };

  gfxCmdPushScissor(cmd, frame);
  fill(cmd, WM_TITLE_BAR, frame->x, frame->y, frame->w, WM_TITLE_HEIGHT);
  gfxCmdSetFgColor(cmd, WM_TITLE_TEXT);
  gfxCmdDrawText(cmd, titleAt, title);
  fill(cmd, window->body, frame->x, frame->y + WM_TITLE_HEIGHT, frame->w,
    frame->h - WM_TITLE_HEIGHT);
  if (window->cells > 0) {
    paintCells(state, window, clip);
  }
  fill(cmd, WM_GRIP, frame->x + frame->w - WM_GRIP_SIZE,
    frame->y + frame->h - WM_GRIP_SIZE, WM_GRIP_SIZE, WM_GRIP_SIZE);
  gfxCmdPopScissor(cmd);
}

/// @fn static void paintRect(WmState *state, const GfxRect *rect)
///
/// @brief Record the desktop and every window that meets a rectangle, back
/// to front, inside the rectangle.
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
  if (!intersect(rect, &screen, &clip)) {
    return;
  }
  gfxCmdPushScissor(cmd, &clip);
  gfxCmdSetBgColor(cmd, WM_DESKTOP);
  gfxCmdClear(cmd);
  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    const WmWindow *window = &state->windows[ii];
    GfxRect overlap;
    if (intersect(&window->frame, &clip, &overlap)) {
      paintWindow(state, window, &clip);
    }
  }
  gfxCmdPopScissor(cmd);
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

/// @fn static void changeTop(WmState *state)
///
/// @brief Work out the damage of a change to where the top window is shown:
/// a move copies what's on the screen of it and repaints the rest, and a
/// resize repaints what the old and new frames don't share, the title bar,
/// and both grips.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void changeTop(WmState *state) {
  const WmWindow *window = wmTop(state);
  const GfxRect *shown = &window->shown;
  const GfxRect *frame = &window->frame;
  GfxRect screen = { 0, 0, (int16_t) state->display.width,
    (int16_t) state->display.height };
  GfxRect source;
  if ((shown->w == frame->w) && (shown->h == frame->h)
    && intersect(shown, &screen, &source)
  ) {
    GfxPoint target = { source.x + frame->x - shown->x,
      source.y + frame->y - shown->y };
    GfxRect copied = { target.x, target.y, source.w, source.h };
    gfxCmdCopyRect(state->cmd, &source, target);
    subtract(state, shown, frame);
    subtract(state, frame, &copied);
  } else if ((shown->x == frame->x) && (shown->y == frame->y)) {
    GfxRect oldGrip = { shown->x + shown->w - WM_GRIP_SIZE,
      shown->y + shown->h - WM_GRIP_SIZE, WM_GRIP_SIZE, WM_GRIP_SIZE };
    subtract(state, shown, frame);
    subtract(state, frame, shown);
    addDamage(state, frame->x, frame->y, frame->w, WM_TITLE_HEIGHT);
    addDamage(state, frame->x + frame->w - WM_GRIP_SIZE,
      frame->y + frame->h - WM_GRIP_SIZE, WM_GRIP_SIZE, WM_GRIP_SIZE);
    if (intersect(&oldGrip, frame, &oldGrip)) {
      addRect(state, &oldGrip);
    }
  } else {
    addRect(state, shown);
    addRect(state, frame);
  }
}

/// @fn static void paintFrame(WmState *state)
///
/// @brief Record and submit what brings the screen up to date with the
/// state:  the old outline erased, the top window moved, resized or
/// repainted, the damage repainted, and the new outline drawn.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void paintFrame(WmState *state) {
  GfxCmdBuf *cmd = state->cmd;
  WmWindow *top = wmTop(state);
  bool outlineMoved = (memcmp(&state->outline, &state->outlineShown,
    sizeof(GfxRect)) != 0);
  state->numDamage = 0;
  if (outlineMoved && (state->outlineShown.w != 0)) {
    xorOutline(cmd, &state->outlineShown);
  }

  if (state->paintAll) {
    addDamage(state, 0, 0, (int16_t) state->display.width,
      (int16_t) state->display.height);
  } else if (state->paintTop) {
    subtract(state, &top->shown, &top->frame);
    addRect(state, &top->frame);
  } else if (memcmp(&top->frame, &top->shown, sizeof(GfxRect)) != 0) {
    changeTop(state);
  }
  for (uint8_t ii = 0; ii < state->numDamage; ii++) {
    paintRect(state, &state->damage[ii]);
  }

  if (outlineMoved && (state->outline.w != 0)) {
    xorOutline(cmd, &state->outline);
  }
  state->outlineShown = state->outline;
  top->shown = top->frame;
  state->paintAll = false;
  state->paintTop = false;
  state->status = (uint8_t) gfxSubmit(cmd);
}

/// @fn static void preloadRows(WmState *state)
///
/// @brief Preload each row of the full window's cells, one preload per row,
/// because a preload has to fit one 512-byte buffer.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void preloadRows(WmState *state) {
  const WmWindow *window = &state->windows[state->numWindows - 1];
  uint8_t rows = (window->cells + WM_CELL_COLUMNS - 1) / WM_CELL_COLUMNS;
  GfxPoint origin = { 0, 0 };
  for (uint8_t row = 0; (row < rows) && (state->status == GFX_OK); row++) {
    gfxCmdBufBegin(state->cmd, GFX_CMDBUF_NO_FLUSH);
    recordRow(state->cmd, row, window->cells, origin, NULL);
    state->status = (uint8_t) gfxPreupload(state->cmd, &state->rows[row]);
  }
  gfxCmdBufBegin(state->cmd, 0);
}

/// @fn void* wmPaint(void *args)
///
/// @brief Carry out the state's operation:  WM_PAINT_START creates the
/// command buffer, preloads the full window's cells if they're painted by
/// call, and paints the whole screen; WM_PAINT_FRAME brings the screen up to
/// date; WM_PAINT_END destroys the command buffer.  The status goes in the
/// state.
///
/// @param args A pointer to the WmState.
///
/// @return Always returns the provided pointer.
void* wmPaint(void *args) {
  WmState *state = (WmState*) args;
  state->status = GFX_OK;
  if (state->operation == WM_PAINT_START) {
    state->cmd = gfxCmdBufCreate(&state->display, 512);
    if (state->cmd == NULL) {
      state->status = GFX_ERR_OUT_OF_MEMORY;
      return args;
    }
    gfxCmdBufBegin(state->cmd, 0);
    if (state->contentByCall) {
      preloadRows(state);
    }
    if (state->status == GFX_OK) {
      paintFrame(state);
    }
  } else if (state->operation == WM_PAINT_FRAME) {
    paintFrame(state);
  } else {
    state->cmd = gfxCmdBufDestroy(state->cmd);
  }
  return args;
}
