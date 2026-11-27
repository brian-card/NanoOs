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




/// @file Resize.c
///
/// @brief Making a text grid, making it follow its window's body when the
/// window is resized, and freeing it, which are kept out of the Grid and
/// Template overlays because they are needed only then.

#include <errno.h>
#include "Wm.h"
#include "UserProcesses.h"

/// @fn static void wmGridFollow(WmState *state, WmWindow *window)
///
/// @brief Make a text grid as big as its place in the body now is, keeping
/// its top-left cells, and tell the owner the body's and the grid's new
/// size.  A session's console is to be sent the size, and the session a
/// resize report if its program asked for them.
///
/// @param state The window manager's state.
/// @param window The window, whose content is a WmGrid.
///
/// @return This function returns no value.
static void wmGridFollow(WmState *state, WmWindow *window) {
  WmGrid *grid = (WmGrid*) window->content;
  grid->resized = false;
  GfxRect place;
  wmGridPlace(state, window, &place);
  uint8_t columns = (uint8_t) place.w;
  uint8_t rows = (uint8_t) place.h;
  wmGridCursor(grid);
  int32_t change = wmGridBytes(columns, rows)
    - wmGridBytes(grid->columns, grid->rows);
  if (((columns != grid->columns) || (rows != grid->rows))
    && wmCharge(state, window->owner, change)
  ) {
    uint8_t *cells
      = (uint8_t*) malloc((size_t) columns * rows * WM_GRID_PLANES);
    if (cells == NULL) {
      wmCharge(state, window->owner, -change);
    } else {
      size_t plane = (size_t) columns * rows;
      size_t oldPlane = (size_t) grid->columns * grid->rows;
      memset(cells, ' ', plane);
      memset(&cells[plane], WM_BLANK_ATTRIBUTE, plane);
      uint8_t keepColumns = (columns < grid->columns) ? columns
        : grid->columns;
      for (uint8_t row = 0; (row < rows) && (row < grid->rows); row++) {
        size_t to = (size_t) row * columns;
        size_t from = (size_t) row * grid->columns;
        memcpy(&cells[to], &grid->cells[from], keepColumns);
        memcpy(&cells[plane + to], &grid->cells[oldPlane + from],
          keepColumns);
      }
      free(grid->cells);
      grid->cells = cells;
      grid->columns = columns;
      grid->rows = rows;
      if (grid->cursorRow >= rows) {
        grid->cursorRow = rows - 1;
      }
      if (grid->cursorColumn >= columns) {
        grid->cursorColumn = columns - 1;
      }
      if (grid->port != WM_NO_PORT) {
        grid->winsizeWanted = true;
        grid->reportWanted = grid->reports;
        state->inputWaiting = true;
      }
    }
  }
  wmGridMargins(state, window, grid);
  wmGridCursor(grid);
  grid->scroll = 0;
  grid->noBlit = false;
  memset(grid->dirty, 0, sizeof(grid->dirty));
  wmGridDirty(state, grid, 0, grid->rows - 1);

  WinEvent *event = wmSend(state, window->owner, WIN_EVENT_RESIZED,
    window->id);
  if (event != NULL) {
    GfxRect body;
    wmBody(window, &body);
    event->widget = grid->widget;
    event->x = body.w;
    event->y = body.h;
    event->columns = grid->columns;
    event->rows = grid->rows;
  }
}

/// @fn void* wmResizeGrid(void *args)
///
/// @brief Make the job's window's text grid follow its body's new size.
///
/// @param args A pointer to the WmTemplateJob, whose state and window are
///   set.
///
/// @return Always returns the provided pointer.
void* wmResizeGrid(void *args) {
  WmTemplateJob *job = (WmTemplateJob*) args;
  wmGridFollow(job->state, job->window);
  return args;
}

/// @fn static void wmFreeGrid(WmState *state, ProcessId owner,
///   WmGrid *grid)
///
/// @brief Close a text grid's session, if it has one, free the grid, and
/// refund it to its window's owner.
///
/// @param state The window manager's state.
/// @param owner The ProcessId of the grid's window's owner.
/// @param grid The grid, or NULL.
///
/// @return This function returns no value.
static void wmFreeGrid(WmState *state, ProcessId owner, WmGrid *grid) {
  if (grid == NULL) {
    return;
  }
  wmCharge(state, owner, -wmGridBytes(grid->columns, grid->rows));
  if (grid->port != WM_NO_PORT) {
    windowServerCloseSession(grid->port);
  }
  wmGridUnbind(grid);
  free(grid->cells);
  free(grid);
}

/// @fn static uint8_t wmOpenSession(WmGrid *grid, ProcessId display)
///
/// @brief Open the session a text grid is bound to, as big as the grid,
/// whose output goes to the grid's owner.
///
/// @param grid The grid.
/// @param display The ProcessId of the window's owner.
///
/// @return Returns WIN_OK, or the WinError for why the session couldn't be
/// opened.
static uint8_t wmOpenSession(WmGrid *grid, ProcessId display) {
  grid->inputArgs = MESSAGE_ARGS_CREATE(ConsoleInputReadyArgs,
    MESSAGE_ARGS_ROUND_TRIP);
  grid->winsizeArgs = MESSAGE_ARGS_CREATE(ConsoleWinsizeArgs,
    MESSAGE_ARGS_ROUND_TRIP);
  if ((grid->inputArgs == NULL) || (grid->winsizeArgs == NULL)) {
    return WIN_ERR_NO_MEMORY;
  }
  uint8_t port = WM_NO_PORT;
  uint8_t shell = 0;
  if (windowServerOpenSession(grid->rows, grid->columns, display, &port,
      &shell) != 0
  ) {
    if (errno == EPERM) {
      return WIN_ERR_PERMISSION;
    } else if (errno == EINVAL) {
      return WIN_ERR_INVALID;
    }
    return WIN_ERR_NO_MEMORY;
  }
  grid->port = port;
  grid->shell = shell;
  return WIN_OK;
}

/// @fn static uint8_t wmMakeGrid(WmState *state, WmWindow *window,
///   const uint8_t *widgets, uint16_t size)
///
/// @brief Make a window's content from its template, which may be one text
/// grid record and nothing else, and open the grid's session if its record
/// asks for one.
///
/// @param state The window manager's state.
/// @param window The window, whose frame is set.
/// @param widgets The template.
/// @param size The number of bytes in the template.
///
/// @return Returns WIN_OK, or the WinError for why the content couldn't be
/// made, in which case the window has none.
static uint8_t wmMakeGrid(WmState *state, WmWindow *window,
  const uint8_t *widgets, uint16_t size
) {
  if ((widgets == NULL) || (size != WIN_RECORD_HEADER_SIZE)
    || (widgets[WIN_RECORD_KIND] != WIN_WIDGET_TEXT_GRID)
  ) {
    return WIN_ERR_NOT_SUPPORTED;
  }
  WmGrid *grid = (WmGrid*) calloc(1, sizeof(WmGrid));
  if (grid == NULL) {
    return WIN_ERR_NO_MEMORY;
  }
  window->content = grid;
  window->face = WM_GRID_FACE;
  grid->widget = widgets[WIN_RECORD_ID];
  grid->flags = widgets[WIN_RECORD_FLAGS];
  grid->anchor = widgets[WIN_RECORD_ANCHOR];
  grid->port = WM_NO_PORT;
  memcpy(&grid->rect, &widgets[WIN_RECORD_RECT], sizeof(grid->rect));
  GfxRect body;
  wmBody(window, &body);
  grid->left = body.x - window->frame.x + grid->rect.x;
  grid->top = body.y - window->frame.y + grid->rect.y;
  grid->marginRight = body.w - grid->rect.x - grid->rect.w;
  grid->marginBottom = body.h - grid->rect.y - grid->rect.h;
  GfxRect place;
  wmGridPlace(state, window, &place);
  grid->columns = (uint8_t) place.w;
  grid->rows = (uint8_t) place.h;
  wmGridMargins(state, window, grid);
  if (!wmCharge(state, window->owner,
      wmGridBytes(grid->columns, grid->rows))
  ) {
    free(grid);
    window->content = NULL;
    return WIN_ERR_NO_MEMORY;
  }
  size_t plane = (size_t) grid->columns * grid->rows;
  grid->cells = (uint8_t*) malloc(plane * WM_GRID_PLANES);
  uint8_t status = WIN_ERR_NO_MEMORY;
  if (grid->cells != NULL) {
    memset(grid->cells, ' ', plane);
    memset(&grid->cells[plane], WM_BLANK_ATTRIBUTE, plane);
    grid->cells[plane]
      = WIN_ATTRIBUTE(WIN_ANSI_BLACK, WIN_ANSI_BRIGHT_WHITE);
    grid->cursorShown = true;
    status = WIN_OK;
    if ((grid->flags & WIN_WIDGET_SESSION) != 0) {
      status = wmOpenSession(grid, window->owner);
    }
  }
  if (status != WIN_OK) {
    wmFreeGrid(state, window->owner, grid);
    window->content = NULL;
  }
  return status;
}

/// @fn void* wmResizeMakeGrid(void *args)
///
/// @brief Make the job's window's text grid from its template, one text grid
/// record, for the Template overlay.
///
/// @param args A pointer to the WmTemplateJob, whose status is set.
///
/// @return Always returns the provided pointer.
void* wmResizeMakeGrid(void *args) {
  WmTemplateJob *job = (WmTemplateJob*) args;
  job->status = wmMakeGrid(job->state, job->window, job->widgets, job->size);
  return args;
}

/// @fn void* wmResizeFreeGrid(void *args)
///
/// @brief Free the job's window's text grid, for the Template overlay.
///
/// @param args A pointer to the WmTemplateJob.
///
/// @return Always returns the provided pointer.
void* wmResizeFreeGrid(void *args) {
  WmTemplateJob *job = (WmTemplateJob*) args;
  wmFreeGrid(job->state, job->window->owner,
    (WmGrid*) job->window->content);
  job->window->content = NULL;
  return args;
}
