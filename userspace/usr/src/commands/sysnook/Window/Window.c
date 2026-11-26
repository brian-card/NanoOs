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


/// @file Window.c
///
/// @brief The window server's windows:  making, changing and taking away
/// clients' windows, as their requests ask.  Entered once per request, and
/// by the Client and Input overlays to take a client's windows away and to
/// change a window's state.

#include <errno.h>
#include "Wm.h"
#include "UserProcesses.h"

/// @fn static bool wmConnected(WmState *state, ProcessId pid)
///
/// @brief Determine whether a process is a connected client.
///
/// @param state The window manager's state.
/// @param pid The process's ProcessId.
///
/// @return Returns true if it is, false otherwise.
static bool wmConnected(WmState *state, ProcessId pid) {
  for (uint8_t ii = 0; ii < state->numClients; ii++) {
    if ((state->clients[ii]->pid == pid) && !state->clients[ii]->leaving) {
      return true;
    }
  }
  return false;
}

/// @fn static void wmWorkArea(WmState *state)
///
/// @brief Work out the work area again:  the screen less the strip each
/// shown reserving window takes at its edge.  If it changed, each maximized
/// window fills the new one and its owner is told.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void wmWorkArea(WmState *state) {
  int16_t left = 0;
  int16_t top = 0;
  int16_t right = (int16_t) state->display.width;
  int16_t bottom = (int16_t) state->display.height;
  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    const WmWindow *window = &state->windows[ii];
    const GfxRect *frame = &window->frame;
    if (window->state == WIN_STATE_MINIMIZED) {
      continue;
    }
    if (((window->flags & WIN_FLAG_RESERVE_TOP) != 0)
      && (frame->y + frame->h > top)
    ) {
      top = frame->y + frame->h;
    }
    if (((window->flags & WIN_FLAG_RESERVE_BOTTOM) != 0)
      && (frame->y < bottom)
    ) {
      bottom = frame->y;
    }
    if (((window->flags & WIN_FLAG_RESERVE_LEFT) != 0)
      && (frame->x + frame->w > left)
    ) {
      left = frame->x + frame->w;
    }
    if (((window->flags & WIN_FLAG_RESERVE_RIGHT) != 0)
      && (frame->x < right)
    ) {
      right = frame->x;
    }
  }
  GfxRect area = { left, top, right - left, bottom - top };
  if (memcmp(&area, &state->workArea, sizeof(area)) == 0) {
    return;
  }
  state->workArea = area;
  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    WmWindow *window = &state->windows[ii];
    if (window->state == WIN_STATE_MAXIMIZED) {
      GfxRect before = window->frame;
      wmDamage(state, &before);
      window->frame = area;
      wmDamage(state, &area);
      wmResized(state, window, &before);
    }
  }
}

/// @fn static void wmSetState(WmState *state, WmWindow *window,
///   uint8_t newState)
///
/// @brief Minimize, maximize or restore a window.  Maximizing keeps its
/// frame to restore and fills the work area; minimizing hides it and
/// activates the normal window in front; showing it again activates it.
/// The owner is told the new state, and the new size if it changed.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param newState The WinState.
///
/// @return This function returns no value.
static void wmSetState(WmState *state, WmWindow *window, uint8_t newState) {
  uint8_t old = window->state;
  if (old == newState) {
    return;
  }
  uint8_t shown = old;
  if (old == WIN_STATE_MINIMIZED) {
    shown = window->restoreState;
  } else {
    wmDamage(state, &window->frame);
  }
  GfxRect before = window->frame;
  if (newState == WIN_STATE_MINIMIZED) {
    window->restoreState = shown;
  } else if ((newState == WIN_STATE_MAXIMIZED)
    && (shown == WIN_STATE_NORMAL)
  ) {
    window->restored = window->frame;
    window->frame = state->workArea;
  } else if ((newState == WIN_STATE_NORMAL)
    && (shown == WIN_STATE_MAXIMIZED)
  ) {
    window->frame = window->restored;
  }
  window->state = newState;
  uint8_t id = window->id;
  if (newState != WIN_STATE_MINIMIZED) {
    wmDamage(state, &window->frame);
    wmResized(state, window, &before);
  }
  WinEvent *event = wmSend(state, window->owner, WIN_EVENT_STATE, id);
  if (event != NULL) {
    event->detail = newState;
  }
  if ((window->flags & (WIN_FLAG_RESERVE_TOP | WIN_FLAG_RESERVE_BOTTOM
    | WIN_FLAG_RESERVE_LEFT | WIN_FLAG_RESERVE_RIGHT)) != 0
  ) {
    wmWorkArea(state);
  }
  window = wmFind(state, id);
  if (newState == WIN_STATE_MINIMIZED) {
    if ((state->active == id) || (state->focus == id)) {
      wmActivate(state, wmTopNormal(state));
    }
  } else if (old == WIN_STATE_MINIMIZED) {
    wmActivate(state, window);
  }
}

/// @fn static void wmFreeGrid(WmGrid *grid)
///
/// @brief Close a text grid's session, if it has one, and free the grid.
///
/// @param grid The grid, or NULL.
///
/// @return This function returns no value.
static void wmFreeGrid(WmGrid *grid) {
  if (grid == NULL) {
    return;
  }
  if (grid->port != WM_NO_PORT) {
    windowServerCloseSession(grid->port);
  }
  wmGridUnbind(grid);
  free(grid->cells);
  free(grid);
}

/// @fn static void wmRemoveWindow(WmState *state, WmWindow *window)
///
/// @brief Take a window away, repainting where it was.  If it was active or
/// had the keys, the normal window in front is activated.
///
/// @param state The window manager's state.
/// @param window The window.
///
/// @return This function returns no value.
static void wmRemoveWindow(WmState *state, WmWindow *window) {
  if (window->state != WIN_STATE_MINIMIZED) {
    wmDamage(state, &window->frame);
  }
  bool lost = (state->active == window->id) || (state->focus == window->id);
  if (state->active == window->id) {
    state->active = 0;
  }
  if (state->focus == window->id) {
    state->focus = 0;
  }
  if (state->grabbed == window->id) {
    state->grab = WM_GRAB_NONE;
    state->grabbed = 0;
    memset(&state->outline, 0, sizeof(state->outline));
  }
  free(window->title);
  wmFreeGrid((WmGrid*) window->content);
  uint8_t index = (uint8_t) (window - state->windows);
  state->numWindows--;
  memmove(window, window + 1,
    sizeof(WmWindow) * (size_t) (state->numWindows - index));
  wmWorkArea(state);
  if (lost) {
    wmActivate(state, wmTopNormal(state));
  }
}

/// @fn static void wmRemoveWindowsOf(WmState *state, ProcessId pid)
///
/// @brief Take away every window a client owns.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
///
/// @return This function returns no value.
static void wmRemoveWindowsOf(WmState *state, ProcessId pid) {
  uint8_t ii = 0;
  while (ii < state->numWindows) {
    if (state->windows[ii].owner == pid) {
      wmRemoveWindow(state, &state->windows[ii]);
    } else {
      ii++;
    }
  }
}

/// @fn static char* wmCopyTitle(const char *title, uint8_t length)
///
/// @brief Copy a title from a client's message to the heap.
///
/// @param title The title, which has no terminator.
/// @param length Its length.
///
/// @return Returns the copy, or NULL if there was no title or no memory.
static char* wmCopyTitle(const char *title, uint8_t length) {
  if ((title == NULL) || (length == 0)) {
    return NULL;
  }
  if (length > WIN_TITLE_MAX) {
    length = WIN_TITLE_MAX;
  }
  char *copy = (char*) malloc(length + 1);
  if (copy != NULL) {
    memcpy(copy, title, length);
    copy[length] = '\0';
  }
  return copy;
}

/// @fn static uint8_t wmFreeId(WmState *state)
///
/// @brief Find an identifier no window has.
///
/// @param state The window manager's state.
///
/// @return Returns the identifier.
static uint8_t wmFreeId(WmState *state) {
  while ((state->nextId == 0) || (wmFind(state, state->nextId) != NULL)) {
    state->nextId++;
  }
  return state->nextId++;
}

/// @fn static void wmLimit(WmState *state, WmWindow *window)
///
/// @brief Fit a window's frame to the rules:  a desktop window fills the
/// screen, and a decorated window is at least its minimum size.
///
/// @param state The window manager's state.
/// @param window The window.
///
/// @return This function returns no value.
static void wmLimit(WmState *state, WmWindow *window) {
  GfxRect *frame = &window->frame;
  if (window->layer == WIN_LAYER_DESKTOP) {
    frame->x = 0;
    frame->y = 0;
    frame->w = (int16_t) state->display.width;
    frame->h = (int16_t) state->display.height;
  } else if ((window->flags & WIN_FLAG_DECORATED) != 0) {
    if (frame->w < WM_MIN_WIDTH) {
      frame->w = WM_MIN_WIDTH;
    }
    if (frame->h < WM_MIN_HEIGHT) {
      frame->h = WM_MIN_HEIGHT;
    }
  }
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
  grid->coversBody = wmGridPlace(state, window, &place);
  grid->columns = (uint8_t) place.w;
  grid->rows = (uint8_t) place.h;
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
    wmFreeGrid(grid);
    window->content = NULL;
  }
  return status;
}

/// @fn static void wmCreateWindow(WmState *state, ProcessId pid,
///   WinCreateWindowArgs *args)
///
/// @brief Make a client's window, in front of the others of its layer, and
/// activate it if it's a normal window or a popup.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
/// @param args The message's arguments.
///
/// @return This function returns no value.
static void wmCreateWindow(WmState *state, ProcessId pid,
  WinCreateWindowArgs *args
) {
  bool desktop = false;
  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    desktop |= (state->windows[ii].layer == WIN_LAYER_DESKTOP);
  }
  if (state->numWindows == WM_MAX_WINDOWS) {
    args->status = WIN_ERR_NO_MEMORY;
    return;
  } else if ((args->layer > WIN_LAYER_POPUP)
    || (args->state > WIN_STATE_MAXIMIZED)
    || ((args->layer == WIN_LAYER_DESKTOP) && desktop)
  ) {
    args->status = WIN_ERR_INVALID;
    return;
  }

  WmWindow window;
  memset(&window, 0, sizeof(window));
  window.layer = args->layer;
  window.flags = args->flags;
  window.owner = pid;
  window.face = WM_FACE;
  window.frame = args->frame;
  wmLimit(state, &window);
  if (args->templateSize != 0) {
    args->status = wmMakeGrid(state, &window, args->widgets,
      args->templateSize);
    if (args->status != WIN_OK) {
      return;
    }
  }
  window.title = wmCopyTitle(args->title, args->titleLength);
  if ((args->titleLength != 0) && (window.title == NULL)) {
    wmFreeGrid((WmGrid*) window.content);
    args->status = WIN_ERR_NO_MEMORY;
    return;
  }
  window.id = wmFreeId(state);

  uint8_t index = 0;
  while ((index < state->numWindows)
    && (state->windows[index].layer <= window.layer)
  ) {
    index++;
  }
  memmove(&state->windows[index + 1], &state->windows[index],
    sizeof(WmWindow) * (size_t) (state->numWindows - index));
  state->windows[index] = window;
  state->numWindows++;
  wmDamage(state, &window.frame);
  if ((window.layer == WIN_LAYER_NORMAL) || (window.layer == WIN_LAYER_POPUP)) {
    wmActivate(state, &state->windows[index]);
  }
  wmWorkArea(state);
  if (args->state != WIN_STATE_NORMAL) {
    wmSetState(state, wmFind(state, window.id), args->state);
  }
  args->window = window.id;
  args->status = WIN_OK;
}

/// @fn static WmWindow* wmOwnWindow(WmState *state, ProcessId pid,
///   uint8_t id, uint8_t *status)
///
/// @brief Find a window a request names and check that the sender owns it.
///
/// @param state The window manager's state.
/// @param pid The sender's ProcessId.
/// @param id The window's identifier.
/// @param status Where to store WIN_ERR_NOT_FOUND or WIN_ERR_PERMISSION if
///   the request can't go ahead.
///
/// @return Returns the window, or NULL if the request can't go ahead.
static WmWindow* wmOwnWindow(WmState *state, ProcessId pid, uint8_t id,
  uint8_t *status
) {
  WmWindow *window = wmFind(state, id);
  if (window == NULL) {
    *status = WIN_ERR_NOT_FOUND;
  } else if (window->owner != pid) {
    *status = WIN_ERR_PERMISSION;
    window = NULL;
  }
  return window;
}

/// @fn static void wmSetWindow(WmState *state, ProcessId pid,
///   WinSetWindowArgs *args)
///
/// @brief Change a client's window's title, frame or state.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
/// @param args The message's arguments.
///
/// @return This function returns no value.
static void wmSetWindow(WmState *state, ProcessId pid,
  WinSetWindowArgs *args
) {
  WmWindow *window = wmOwnWindow(state, pid, args->window, &args->status);
  if (window == NULL) {
    return;
  } else if (((args->changes & WIN_SET_STATE) != 0)
    && (args->state > WIN_STATE_MAXIMIZED)
  ) {
    args->status = WIN_ERR_INVALID;
    return;
  }
  uint8_t id = window->id;
  if ((args->changes & WIN_SET_TITLE) != 0) {
    char *title = wmCopyTitle(args->title, args->titleLength);
    if ((args->titleLength != 0) && (title == NULL)) {
      args->status = WIN_ERR_NO_MEMORY;
      return;
    }
    free(window->title);
    window->title = title;
    GfxRect bar = { window->frame.x, window->frame.y, window->frame.w,
      WM_FRAME_TOP };
    wmDamage(state, &bar);
  }
  if ((args->changes & WIN_SET_FRAME) != 0) {
    wmDamage(state, &window->frame);
    window->frame = args->frame;
    wmLimit(state, window);
    wmDamage(state, &window->frame);
    wmWorkArea(state);
  }
  window = wmFind(state, id);
  if ((window != NULL) && ((args->changes & WIN_SET_STATE) != 0)) {
    wmSetState(state, window, args->state);
  }
  args->status = WIN_OK;
}

/// @fn void* wmWindowSetState(void *args)
///
/// @brief Give the window state->opWindow the WinState state->opState, for
/// the Input overlay.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmWindowSetState(void *args) {
  WmState *state = (WmState*) args;
  WmWindow *window = wmFind(state, state->opWindow);
  if (window != NULL) {
    wmSetState(state, window, state->opState);
  }
  return args;
}

/// @fn void* wmWindowRemoveOwned(void *args)
///
/// @brief Take away every window of the client state->opPid.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmWindowRemoveOwned(void *args) {
  WmState *state = (WmState*) args;
  wmRemoveWindowsOf(state, state->opPid);
  return args;
}

/// @fn static void wmRequest(WmState *state, ProcessId pid, uint8_t type,
///   void *data, bool waiting)
///
/// @brief Carry out a window request:  the window requests make and change
/// windows, and every other request is answered WIN_ERR_NOT_SUPPORTED.  A
/// process that isn't connected is answered WIN_ERR_PERMISSION.
///
/// @param state The window manager's state.
/// @param pid The sender's ProcessId.
/// @param type The WinMsgType.
/// @param data The request's arguments.
/// @param waiting Whether the sender waits for an answer.
///
/// @return This function returns no value.
static void wmRequest(WmState *state, ProcessId pid, uint8_t type,
  void *data, bool waiting
) {
  if (!wmConnected(state, pid)) {
    if (waiting) {
      *((uint8_t*) data) = WIN_ERR_PERMISSION;
    }
  } else if (type == WIN_MSG_CREATE_WINDOW) {
    wmCreateWindow(state, pid, (WinCreateWindowArgs*) data);
  } else if (type == WIN_MSG_SET_WINDOW) {
    wmSetWindow(state, pid, (WinSetWindowArgs*) data);
  } else if (type == WIN_MSG_DESTROY_WINDOW) {
    uint8_t status = WIN_OK;
    WmWindow *window = wmOwnWindow(state, pid,
      ((WinWindowArgs*) data)->window, &status);
    if (window != NULL) {
      wmRemoveWindow(state, window);
    }
  } else if (waiting) {
    *((uint8_t*) data) = WIN_ERR_NOT_SUPPORTED;
  }
}

/// @fn void* wmWindowMessage(void *args)
///
/// @brief Handle the window request in the state, and finish its message.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmWindowMessage(void *args) {
  WmState *state = (WmState*) args;
  ProcessMessage *message = state->message;
  void *data = processMessageData(message);
  bool waiting = processMessageWaiting(message);
  if (data != NULL) {
    wmRequest(state, processMessageFrom(message)->processId,
      (uint8_t) processMessageType(message), data, waiting);
    if (waiting == false) {
      MESSAGE_ARGS_DESTROY(data);
    }
  }
  processMessageSetDone(message);
  if (waiting == false) {
    processMessageRelease(message);
  }
  return args;
}
