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



/// @file Grid.c
///
/// @brief The window server's text grids:  carrying out a client's grid
/// operations, making a grid follow its body when the window is resized, and
/// unbinding a grid from a session that has ended.

#include "Wm.h"

/// @fn static void wmGridBlank(WmGrid *grid, uint8_t row, uint8_t left,
///   uint8_t count, uint8_t attribute)
///
/// @brief Blank cells of a row of a text grid in an attribute.
///
/// @param grid The grid.
/// @param row The row.
/// @param left The first column.
/// @param count The number of cells.
/// @param attribute The attribute.
///
/// @return This function returns no value.
static void wmGridBlank(WmGrid *grid, uint8_t row, uint8_t left,
  uint8_t count, uint8_t attribute
) {
  size_t start = (((size_t) row) * grid->columns) + left;
  memset(&grid->cells[start], ' ', count);
  memset(&grid->cells[(((size_t) grid->rows) * grid->columns) + start],
    attribute, count);
}

/// @fn static void wmGridScroll(WmState *state, WmGrid *grid, uint8_t top,
///   uint8_t bottom, int8_t count, uint8_t attribute)
///
/// @brief Move rows of a text grid up, or down if count is negative,
/// blanking the rows uncovered.  The rows' marks to be repainted move with
/// them, so that the next frame can move their pixels and repaint the rest,
/// unless a scroll since the last frame moved other rows or the other way.
///
/// @param state The window manager's state.
/// @param grid The grid.
/// @param top The first row.
/// @param bottom The last row.
/// @param count The number of rows to move by.
/// @param attribute The attribute of the rows uncovered.
///
/// @return This function returns no value.
static void wmGridScroll(WmState *state, WmGrid *grid, uint8_t top,
  uint8_t bottom, int8_t count, uint8_t attribute
) {
  if (bottom >= grid->rows) {
    bottom = grid->rows - 1;
  }
  if ((top > bottom) || (count == 0)) {
    return;
  }

  uint8_t height = bottom - top + 1;
  uint8_t moved = (uint8_t) ((count > 0) ? count : -count);

  wmGridDirty(state, grid, grid->cursorRow, grid->cursorRow);

  if (moved >= height) {
    for (uint8_t row = top; row <= bottom; row++) {
      wmGridBlank(grid, row, 0, grid->columns, attribute);
    }

    wmGridDirty(state, grid, top, bottom);
    return;
  }

  size_t columns = grid->columns;
  size_t plane = ((size_t) grid->rows) * columns;
  size_t from = ((size_t) (top + moved)) * columns;
  size_t to = ((size_t) top) * columns;

  if (count < 0) {
    from = to;
    to = ((size_t) (top + moved)) * columns;
  }

  size_t bytes = ((size_t) (height - moved)) * columns;
  memmove(&grid->cells[to], &grid->cells[from], bytes);
  memmove(&grid->cells[plane + to], &grid->cells[plane + from], bytes);

  uint8_t dirty[WM_GRID_DIRTY_BYTES];
  memcpy(dirty, grid->dirty, sizeof(dirty));

  for (uint8_t row = top; row <= bottom; row++) {
    int source = ((int) row) + count;

    grid->dirty[WM_GRID_DIRTY_BYTE(row)] &= (uint8_t) ~WM_GRID_DIRTY_BIT(row);

    if ((source < top) || (source > bottom)) {
      wmGridBlank(grid, row, 0, grid->columns, attribute);
      wmGridDirty(state, grid, row, row);
    } else if ((dirty[WM_GRID_DIRTY_BYTE(source)]
      & WM_GRID_DIRTY_BIT(source)) != 0
    ) {
      wmGridDirty(state, grid, row, row);
    }
  }

  wmGridDirty(state, grid, grid->cursorRow, grid->cursorRow);

  int total = grid->scroll + count;

  if (grid->noBlit) {
    wmGridDirty(state, grid, top, bottom);
  } else if (grid->scroll == 0) {
    grid->scroll = count;
    grid->scrollTop = top;
    grid->scrollBottom = bottom;
  } else if ((top == grid->scrollTop) && (bottom == grid->scrollBottom)
    && ((grid->scroll > 0) == (count > 0)) && (total < height)
    && (total > (-height))
  ) {
    grid->scroll = (int8_t) total;
  } else {
    grid->noBlit = true;
    wmGridDirty(state, grid, top, bottom);
  }

  state->gridsDirty = true;
}

/// @fn static uint8_t wmGridApply(WmState *state, WmGrid *grid,
///   const uint8_t *data, uint16_t size)
///
/// @brief Carry out a client's text grid operations, in order.  Cells
/// outside the grid, which a resize the client hasn't been told of yet can
/// leave, are skipped.
///
/// @param state The window manager's state.
/// @param grid The grid.
/// @param data The operations.
/// @param size The number of bytes of operations.
///
/// @return Returns WIN_OK, or WIN_ERR_INVALID if an operation is unknown or
/// cut short, in which case those before it have been carried out.
static uint8_t wmGridApply(WmState *state, WmGrid *grid,
  const uint8_t *data, uint16_t size
) {
  uint16_t at = 0;
  while (at < size) {
    const uint8_t *op = &data[at];
    uint16_t remaining = size - at;
    uint16_t length = WIN_GRID_REPORTS_SIZE;

    if (op[WIN_GRID_TAG] == WIN_GRID_PUT) {
      length = (remaining < WIN_GRID_PUT_SIZE(0)) ? (remaining + 1)
        : WIN_GRID_PUT_SIZE(op[WIN_GRID_PUT_LENGTH]);
    } else if (op[WIN_GRID_TAG] == WIN_GRID_CLEAR) {
      length = WIN_GRID_CLEAR_SIZE;
    } else if (op[WIN_GRID_TAG] == WIN_GRID_SCROLL) {
      length = WIN_GRID_SCROLL_SIZE;
    } else if (op[WIN_GRID_TAG] == WIN_GRID_CURSOR) {
      length = WIN_GRID_CURSOR_SIZE;
    } else if (op[WIN_GRID_TAG] != WIN_GRID_REPORTS) {
      return WIN_ERR_INVALID;
    }

    if (length > remaining) {
      return WIN_ERR_INVALID;
    }

    at += length;

    if (op[WIN_GRID_TAG] == WIN_GRID_PUT) {
      uint8_t row = op[WIN_GRID_PUT_ROW];
      uint8_t column = op[WIN_GRID_PUT_COLUMN];

      if ((row >= grid->rows) || (column >= grid->columns)) {
        continue;
      }

      uint8_t count = op[WIN_GRID_PUT_LENGTH];
      if (count > (grid->columns - column)) {
        count = grid->columns - column;
      }

      size_t start = (((size_t) row) * grid->columns) + column;
      memcpy(&grid->cells[start], &op[WIN_GRID_PUT_TEXT], count);
      memset(&grid->cells[(((size_t) grid->rows) * grid->columns) + start],
        op[WIN_GRID_PUT_ATTRIBUTE], count);
      wmGridDirty(state, grid, row, row);
    } else if (op[WIN_GRID_TAG] == WIN_GRID_CLEAR) {
      uint8_t top = op[WIN_GRID_CLEAR_TOP];
      uint8_t left = op[WIN_GRID_CLEAR_LEFT];
      uint8_t bottom = op[WIN_GRID_CLEAR_BOTTOM];
      uint8_t right = op[WIN_GRID_CLEAR_RIGHT];

      if (bottom >= grid->rows) {
        bottom = grid->rows - 1;
      }

      if (right >= grid->columns) {
        right = grid->columns - 1;
      }

      for (uint8_t row = top; (row <= bottom) && (left <= right); row++) {
        wmGridBlank(grid, row, left, right - left + 1,
          op[WIN_GRID_CLEAR_ATTRIBUTE]);
      }

      wmGridDirty(state, grid, top, bottom);
    } else if (op[WIN_GRID_TAG] == WIN_GRID_SCROLL) {
      wmGridScroll(state, grid, op[WIN_GRID_SCROLL_TOP],
        op[WIN_GRID_SCROLL_BOTTOM], (int8_t) op[WIN_GRID_SCROLL_COUNT],
        op[WIN_GRID_SCROLL_ATTRIBUTE]);
    } else if (op[WIN_GRID_TAG] == WIN_GRID_CURSOR) {
      wmGridDirty(state, grid, grid->cursorRow, grid->cursorRow);
      grid->cursorRow = op[WIN_GRID_CURSOR_ROW];
      grid->cursorColumn = op[WIN_GRID_CURSOR_COLUMN];
      grid->cursorShown = (op[WIN_GRID_CURSOR_SHOWN] != 0);
      wmGridDirty(state, grid, grid->cursorRow, grid->cursorRow);
    } else {
      grid->reports = (op[WIN_GRID_REPORTS_ON] != 0);
      if (grid->reports && (grid->port != WM_NO_PORT)) {
        grid->reportWanted = true;
        state->inputWaiting = true;
      }
    }
  }

  return WIN_OK;
}

/// @fn static void wmGridWrite(WmState *state, ProcessMessage *message)
///
/// @brief Handle a WIN_MSG_GRID_WRITE:  carry out its operations on a text
/// grid of the sender's, and finish its message.
///
/// @param state The window manager's state.
/// @param message The message.
///
/// @return This function returns no value.
static void wmGridWrite(WmState *state, ProcessMessage *message) {
  WinBufferArgs *bufferArgs = (WinBufferArgs*) processMessageData(message);
  bool waiting = processMessageWaiting(message);

  if ((bufferArgs != NULL) && waiting) {
    WmWindow *window = wmFind(state, bufferArgs->window);
    WmGrid *grid = NULL;

    if (window != NULL) {
      grid = (WmGrid*) window->content;
    }
    if ((window != NULL) && (window->owner
      != processMessageFrom(message)->processId)
    ) {
      bufferArgs->status = WIN_ERR_PERMISSION;
    } else if ((grid == NULL) || (grid->widget != bufferArgs->widget)) {
      bufferArgs->status = WIN_ERR_NOT_FOUND;
    } else {
      wmGridCursor(grid);
      bufferArgs->status = wmGridApply(state, grid, bufferArgs->data,
        bufferArgs->size);
      wmGridCursor(grid);
    }
  } else if (bufferArgs != NULL) {
    MESSAGE_ARGS_DESTROY(bufferArgs);
  }

  processMessageSetDone(message);

  if (waiting == false) {
    processMessageRelease(message);
  }
}

/// @fn static void wmGridEnded(WmState *state, ProcessMessage *message)
///
/// @brief Handle the scheduler's CONSOLE_SESSION_ENDED:  unbind the text
/// grid of the session that ended, and tell its owner.
///
/// @param state The window manager's state.
/// @param message The message.
///
/// @return This function returns no value.
static void wmGridEnded(WmState *state, ProcessMessage *message) {
  uint8_t port = (uint8_t) (uintptr_t) processMessageData(message);

  processMessageRelease(message);

  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    WmWindow *window = &state->windows[ii];
    WmGrid *grid = (WmGrid*) window->content;

    if ((grid != NULL) && (grid->port == port)) {
      wmGridUnbind(grid);
      WinEvent *event = wmSend(state, window->owner,
        WIN_EVENT_SESSION_ENDED, window->id);
      if (event != NULL) {
        event->widget = grid->widget;
      }
    }
  }
}

/// @fn static bool overlaps(const GfxRect *first, const GfxRect *second)
///
/// @brief Determine whether two rectangles overlap.
///
/// @param first One rectangle.
/// @param second The other.
///
/// @return Returns true if they do, false otherwise.
static bool overlaps(const GfxRect *first, const GfxRect *second) {
  return (first->x < (second->x + second->w))
    && (second->x < (first->x + first->w))
    && (first->y < (second->y + second->h))
    && (second->y < (first->y + first->h));
}

/// @fn static bool blittable(WmState *state, uint8_t index,
///   const GfxRect *band)
///
/// @brief Determine whether a scrolled band of a text grid can be painted
/// by copying its pixels:  no other copy is waiting, it is all on the
/// screen, nothing is drawn over it, and nothing under it waits to be
/// repainted, so the screen shows it as it was.
///
/// @param state The window manager's state.
/// @param index The index of the grid's window.
/// @param band The band, in screen coordinates.
///
/// @return Returns true if it can, false otherwise.
static bool blittable(WmState *state, uint8_t index, const GfxRect *band) {
  if (state->paintAll || state->copy || (state->grab != WM_GRAB_NONE)
    || (state->outlineShown.w != 0) || (band->x < 0) || (band->y < 0)
    || ((band->x + band->w) > ((int16_t) state->display.width))
    || ((band->y + band->h) > ((int16_t) state->display.height))
  ) {
    return false;
  }

  for (uint8_t ii = index + 1; ii < state->numWindows; ii++) {
    if ((state->windows[ii].state != WIN_STATE_MINIMIZED)
      && overlaps(&state->windows[ii].frame, band)
    ) {
      return false;
    }
  }

  for (uint8_t ii = 0; ii < state->numDamage; ii++) {
    if (overlaps(&state->damage[ii], band)) {
      return false;
    }
  }

  return true;
}

/// @fn static void wmGridPrepare(WmState *state)
///
/// @brief Turn what changed in the text grids since the last frame into the
/// next frame's work, which is painted before anything else changes:  a
/// scroll that can be is a copy of its pixels, and each run of rows to
/// repaint is damage.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void wmGridPrepare(WmState *state) {
  uint8_t glyphW = state->display.builtInFont.glyphW;
  uint8_t glyphH = state->display.builtInFont.glyphH;

  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    WmWindow *window = &state->windows[ii];
    WmGrid *grid = (WmGrid*) window->content;

    if (grid == NULL) {
      continue;
    }

    bool shown = (window->state != WIN_STATE_MINIMIZED);
    GfxRect rows = { window->frame.x + grid->left, window->frame.y + grid->top,
      (int16_t) (grid->columns * glyphW), 0 };

    if (grid->scroll != 0) {
      GfxRect band = rows;
      band.y += (int16_t) (grid->scrollTop * glyphH);
      band.h = (int16_t) ((grid->scrollBottom - grid->scrollTop + 1) * glyphH);
      int16_t distance = (int16_t) (grid->scroll * glyphH);

      if ((!grid->noBlit) && shown && blittable(state, ii, &band)) {
        state->copy = true;
        state->copySource = band;
        state->copySource.h -= (distance < 0) ? -distance : distance;
        state->copyTarget.x = band.x;
        state->copyTarget.y = band.y;

        if (distance > 0) {
          state->copySource.y += distance;
        } else {
          state->copyTarget.y -= distance;
        }
      } else {
        wmGridDirty(state, grid, grid->scrollTop, grid->scrollBottom);
      }

      grid->scroll = 0;
      grid->noBlit = false;
    }

    uint8_t row = 0;
    while (shown && (row < grid->rows)) {
      uint8_t first = row;

      while ((row < grid->rows)
        && ((grid->dirty[WM_GRID_DIRTY_BYTE(row)] & WM_GRID_DIRTY_BIT(row))
          != 0)
      ) {
        row++;
      }

      if (row > first) {
        GfxRect damage = rows;
        damage.y += (int16_t) (first * glyphH);
        damage.h = (int16_t) ((row - first) * glyphH);
        wmDamageOwned(state, &damage, window->id);
        state->gridPainted = true;
      }

      row++;
    }

    memset(grid->dirty, 0, sizeof(grid->dirty));
  }

  state->gridsDirty = false;
}

/// @fn void* wmGrid(void *args)
///
/// @brief Handle the state's message, if it has one:  a WIN_MSG_GRID_WRITE
/// or the scheduler's CONSOLE_SESSION_ENDED, make every text grid whose body
/// changed size follow it, and prepare the next frame's grid work.  After a
/// write, the frame is put off if another message comes within
/// WM_WRITE_WAIT_NS, which main then handles, so that a burst of writes is
/// painted once:  a yield wouldn't let a writer below the executive level
/// run.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmGrid(void *args) {
  WmState *state = (WmState*) args;
  ProcessMessage *message = state->message;
  bool written = false;

  state->deferring = false;

  if (message != NULL) {
    written = (processMessageType(message)
      == (WIN_COMMAND_SIGNATURE | WIN_MSG_GRID_WRITE));
    if (written) {
      wmGridWrite(state, message);
    } else {
      wmGridEnded(state, message);
    }
  }

  if (state->gridsResized) {
    state->gridsResized = false;

    for (uint8_t ii = 0; ii < state->numWindows; ii++) {
      WmWindow *window = &state->windows[ii];
      if ((window->content != NULL)
        && ((WmGrid*) window->content)->resized
      ) {
        WmTemplateJob job;
        job.state = state;
        job.window = window;
        callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Resize", "wmResizeGrid",
          &job);
      }
    }
  }

  if (written && state->gridsDirty && (state->deferred < WM_MAX_DEFERRED)) {
    struct timespec soon;
    timespec_get(&soon, TIME_UTC);
    soon.tv_nsec += WM_WRITE_WAIT_NS;

    if (soon.tv_nsec >= 1000000000L) {
      soon.tv_sec++;
      soon.tv_nsec -= 1000000000L;
    }

    state->next = processMessageQueueWait(&soon);
    if (state->next != NULL) {
      state->deferred++;
      state->deferring = true;
      return args;
    }
  }

  state->deferred = 0;
  wmGridPrepare(state);

  return args;
}
