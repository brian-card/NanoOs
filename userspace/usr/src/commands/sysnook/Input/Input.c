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


/// @file Input.c
///
/// @brief What the window manager does when a button is pressed or let go
/// and when a key is pressed:  activating the window under the pointer,
/// pressing its caption buttons, starting and ending a move or resize,
/// closing popups, and giving keys to the window that has them.  Entered once
/// per press, release, and key.

#include "Wm.h"
#include "WmKeys.h"

/// @fn static void setState(WmState *state, uint8_t id, uint8_t newState)
///
/// @brief Have the Window overlay minimize, maximize or restore a window.
///
/// @param state The window manager's state.
/// @param id The window's identifier.
/// @param newState The WinState.
///
/// @return This function returns no value.
static void setState(WmState *state, uint8_t id, uint8_t newState) {
  state->opWindow = id;
  state->opState = newState;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Window", "wmWindowSetState",
    state);
}

/// @fn static bool resizable(const WmWindow *window)
///
/// @brief Determine whether a window's edges resize it:  a decorated,
/// resizable window in the normal state.
///
/// @param window The window.
///
/// @return Returns true if they do, false otherwise.
static bool resizable(const WmWindow *window) {
  return ((window->flags & (WIN_FLAG_RESIZABLE | WIN_FLAG_DECORATED))
      == (WIN_FLAG_RESIZABLE | WIN_FLAG_DECORATED))
    && (window->state == WIN_STATE_NORMAL);
}

/// @fn static WmWindow* windowAt(WmState *state, int16_t pointX,
///   int16_t pointY)
///
/// @brief Find the shown window in front at a point, counting the part of a
/// resizable window's grab zones outside its frame.
///
/// @param state The window manager's state.
/// @param pointX The point's column.
/// @param pointY The point's row.
///
/// @return Returns the window, or NULL if the point is on the desktop.
static WmWindow* windowAt(WmState *state, int16_t pointX, int16_t pointY) {
  for (int ii = state->numWindows - 1; ii >= 0; ii--) {
    WmWindow *window = &state->windows[ii];
    GfxRect area = window->frame;
    if (resizable(window)) {
      area.x -= WM_ZONE_OUTSIDE;
      area.y -= WM_ZONE_OUTSIDE;
      area.w += 2 * WM_ZONE_OUTSIDE;
      area.h += 2 * WM_ZONE_OUTSIDE;
    }
    if ((window->state != WIN_STATE_MINIMIZED)
      && wmContains(&area, pointX, pointY)
    ) {
      return window;
    }
  }
  return NULL;
}

/// @fn static uint8_t edgesAt(const WmWindow *window, int16_t pointX,
///   int16_t pointY)
///
/// @brief Find which edges a press resizes:  each edge has a grab zone
/// WM_ZONE_INSIDE pixels inside the frame and WM_ZONE_OUTSIDE outside, and a
/// press on a side within WM_ZONE_CORNER pixels of a corner resizes both
/// ways.
///
/// @param window The window.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return Returns the WM_EDGE_* bits, or 0 if the press isn't on a zone.
static uint8_t edgesAt(const WmWindow *window, int16_t pointX,
  int16_t pointY
) {
  if (!resizable(window)) {
    return 0;
  }
  const GfxRect *frame = &window->frame;
  int16_t right = frame->x + frame->w;
  int16_t bottom = frame->y + frame->h;
  uint8_t edges = 0;
  if (pointX < frame->x + WM_ZONE_INSIDE) {
    edges |= WM_EDGE_LEFT;
  } else if (pointX >= right - WM_ZONE_INSIDE) {
    edges |= WM_EDGE_RIGHT;
  }
  if (pointY < frame->y + WM_ZONE_INSIDE) {
    edges |= WM_EDGE_TOP;
  } else if (pointY >= bottom - WM_ZONE_INSIDE) {
    edges |= WM_EDGE_BOTTOM;
  }
  if ((edges & (WM_EDGE_LEFT | WM_EDGE_RIGHT)) != 0) {
    if (pointY < frame->y + WM_ZONE_CORNER) {
      edges |= WM_EDGE_TOP;
    } else if (pointY >= bottom - WM_ZONE_CORNER) {
      edges |= WM_EDGE_BOTTOM;
    }
  }
  if ((edges & (WM_EDGE_TOP | WM_EDGE_BOTTOM)) != 0) {
    if (pointX < frame->x + WM_ZONE_CORNER) {
      edges |= WM_EDGE_LEFT;
    } else if (pointX >= right - WM_ZONE_CORNER) {
      edges |= WM_EDGE_RIGHT;
    }
  }
  return edges;
}

/// @fn static void closePopups(WmState *state, int16_t pointX,
///   int16_t pointY)
///
/// @brief Hide every popup a press lands outside, and ask its owner to
/// close it.
///
/// @param state The window manager's state.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return This function returns no value.
static void closePopups(WmState *state, int16_t pointX, int16_t pointY) {
  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    WmWindow *window = &state->windows[ii];
    if ((window->layer == WIN_LAYER_POPUP)
      && (window->state != WIN_STATE_MINIMIZED)
      && !wmContains(&window->frame, pointX, pointY)
    ) {
      window->state = WIN_STATE_MINIMIZED;
      wmDamage(state, &window->frame);
      wmSend(state, window->owner, WIN_EVENT_CLOSE_REQUESTED, window->id);
      if (state->focus == window->id) {
        wmActivate(state, wmFind(state, state->active));
      }
    }
  }
}

/// @fn static bool controls(WmState *state, WmWindow *window,
///   const char *function)
///
/// @brief Have the Controls overlay handle the state's event for a window's
/// widgets.  Having this here keeps one copy of the job's setup.
///
/// @param state The window manager's state.
/// @param window The window, or NULL.
/// @param function The Controls overlay's function.
///
/// @return Returns true if a widget used the event, false otherwise.
static bool controls(WmState *state, WmWindow *window, const char *function) {
  WmInputJob job = { state, window, state->event, false };
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Controls", function, &job);
  return job.handled;
}

/// @fn static void press(WmState *state, int16_t pointX, int16_t pointY)
///
/// @brief Activate the window a press lands on, and press a caption button,
/// start a resize if it's on a grab zone, start a move if it's on the title
/// bar, or hold a widget it's on.  A double-click on the title bar maximizes
/// or restores.
///
/// @param state The window manager's state.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return This function returns no value.
static void press(WmState *state, int16_t pointX, int16_t pointY) {
  // A move or resize released in the same input message as this press
  // hasn't been painted yet, and the grab this press starts replaces it.
  WmWindow *moved = wmFind(state, state->grabbed);
  if ((moved != NULL)
    && (memcmp(&moved->frame, &state->shown, sizeof(GfxRect)) != 0)
  ) {
    wmDamage(state, &state->shown);
    wmDamage(state, &moved->frame);
  }
  closePopups(state, pointX, pointY);
  WmWindow *window = windowAt(state, pointX, pointY);
  if (window == NULL) {
    return;
  }
  window = wmActivate(state, window);

  state->shown = window->frame;
  state->grabStart = window->frame;
  uint8_t edges = edgesAt(window, pointX, pointY);
  uint8_t button = wmButtonAt(window, pointX, pointY);
  if ((edges == 0) && (button != WM_BUTTON_NONE)) {
    state->grab = WM_GRAB_BUTTON;
    state->grabbed = window->id;
    state->pressButton = button;
    state->pressInside = true;
    wmDamageButton(state, window->id, button);
    return;
  }

  const GfxRect *frame = &window->frame;
  if (edges != 0) {
    state->grab = WM_GRAB_RESIZE;
    state->edges = edges;
    state->offset.x = frame->x + frame->w - pointX;
    if ((edges & WM_EDGE_LEFT) != 0) {
      state->offset.x = pointX - frame->x;
    }
    state->offset.y = frame->y + frame->h - pointY;
    if ((edges & WM_EDGE_TOP) != 0) {
      state->offset.y = pointY - frame->y;
    }
  } else if (((window->flags & WIN_FLAG_DECORATED) != 0)
    && (pointY < frame->y + WM_FRAME_TOP)
  ) {
    if (wmDoubleClick(state, window, pointX, pointY)) {
      setState(state, window->id,
        (window->state == WIN_STATE_MAXIMIZED) ? WIN_STATE_NORMAL
        : WIN_STATE_MAXIMIZED);
      return;
    }
    state->grab = WM_GRAB_MOVE;
    state->restoreOnDrag = (window->state == WIN_STATE_MAXIMIZED);
    state->offset.x = pointX - frame->x;
    state->offset.y = pointY - frame->y;
  } else {
    if (window->widgets != NULL) {
      controls(state, window, "wmControlsPress");
    }
    return;
  }
  state->grabbed = window->id;
  bool live = state->liveResize;
  if (state->grab == WM_GRAB_MOVE) {
    live = state->liveMove;
  }
  if (!live) {
    state->outline = *frame;
  }
}

/// @fn static void releaseButton(WmState *state, WmWindow *window,
///   int16_t pointX, int16_t pointY)
///
/// @brief End a press on a caption button, which acts only if the pointer
/// is still on it:  close asks the window's owner to close it.
///
/// @param state The window manager's state.
/// @param window The window whose button was pressed, or NULL if it has
///   gone.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return This function returns no value.
static void releaseButton(WmState *state, WmWindow *window, int16_t pointX,
  int16_t pointY
) {
  uint8_t button = state->pressButton;
  state->grab = WM_GRAB_NONE;
  state->pressButton = WM_BUTTON_NONE;
  state->pressInside = false;
  if (window == NULL) {
    return;
  }
  wmDamageButton(state, window->id, button);
  if (wmButtonAt(window, pointX, pointY) != button) {
    return;
  } else if (button == WM_BUTTON_CLOSE) {
    wmSend(state, window->owner, WIN_EVENT_CLOSE_REQUESTED, window->id);
  } else if (button == WM_BUTTON_MINIMIZE) {
    setState(state, window->id, WIN_STATE_MINIMIZED);
  } else if (window->state == WIN_STATE_MAXIMIZED) {
    setState(state, window->id, WIN_STATE_NORMAL);
  } else {
    setState(state, window->id, WIN_STATE_MAXIMIZED);
  }
}

/// @fn static void restoreUnderPointer(WmState *state, int16_t pointX)
///
/// @brief Restore a maximized window that is being moved by its title bar,
/// keeping the pointer at the same fraction of the title bar's width, so
/// that the move goes on with the restored window.
///
/// @param state The window manager's state.
/// @param pointX The pointer's column.
///
/// @return This function returns no value.
static void restoreUnderPointer(WmState *state, int16_t pointX) {
  state->restoreOnDrag = false;
  WmWindow *window = wmFind(state, state->grabbed);
  if (window == NULL) {
    return;
  }
  int16_t width = window->frame.w;
  setState(state, window->id, WIN_STATE_NORMAL);
  window = wmFind(state, state->grabbed);
  if ((window == NULL) || (width <= 0)) {
    return;
  }
  state->offset.x = (int16_t) (((int32_t) state->offset.x * window->frame.w)
    / width);
  window->frame.x = pointX - state->offset.x;
  state->shown = window->frame;
  if (!state->liveMove) {
    state->outline = window->frame;
  }
}

/// @fn static void release(WmState *state, int16_t pointX, int16_t pointY)
///
/// @brief End a caption button's press, or a move or resize, putting the
/// window where its outline is if an outline followed the pointer, and
/// telling its owner if its size changed.
///
/// @param state The window manager's state.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return This function returns no value.
static void release(WmState *state, int16_t pointX, int16_t pointY) {
  WmWindow *window = wmFind(state, state->grabbed);
  if (state->grab == WM_GRAB_BUTTON) {
    releaseButton(state, window, pointX, pointY);
    return;
  }
  if ((window != NULL) && (state->outline.w != 0)) {
    window->frame = state->outline;
  }
  if (window != NULL) {
    wmResized(state, window, &state->grabStart);
  }
  memset(&state->outline, 0, sizeof(state->outline));
  state->grab = WM_GRAB_NONE;
}

/// @fn static void altTab(WmState *state)
///
/// @brief Activate the next normal window:  the one at the back, so that
/// pressing Alt+Tab again goes through them all.  A minimized one is shown
/// again.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void altTab(WmState *state) {
  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    WmWindow *window = &state->windows[ii];
    if ((window->layer == WIN_LAYER_NORMAL)
      && ((window->id != state->active)
        || (window->state == WIN_STATE_MINIMIZED))
    ) {
      if (window->state == WIN_STATE_MINIMIZED) {
        state->opWindow = window->id;
        state->opState = window->restoreState;
        callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Window",
          "wmWindowSetState", state);
        window = wmFind(state, state->opWindow);
      }
      if (window != NULL) {
        wmActivate(state, window);
      }
      return;
    }
  }
}

/// @fn static bool sendToConsole(WmState *state, ProcessMessage **message,
///   ConsoleCommand command, void *args, size_t size)
///
/// @brief Send the console a message for a session and keep it, without
/// waiting for it, since the console may be waiting on the session's
/// display, which may be waiting on the window server.
///
/// @param state The window manager's state.
/// @param message Where to keep the message.
/// @param command The ConsoleCommand.
/// @param args The arguments.
/// @param size The size of the arguments.
///
/// @return Returns true if it was sent, false otherwise.
static bool sendToConsole(WmState *state, ProcessMessage **message,
  ConsoleCommand command, void *args, size_t size
) {
  if ((state->consolePid == 0)
    && (findProcessByName(CONSOLE_PROCESS_NAME, &state->consolePid) != 0)
  ) {
    return false;
  }
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    return false;
  }
  processMessageInit(sent, CONSOLE_COMMAND_SIGNATURE | command, args, size,
    /* waiting= */ true);
  if (sendProcessMessageToPid(state->consolePid, sent) != processSuccess) {
    processMessageRelease(sent);
    return false;
  }
  *message = sent;
  return true;
}

/// @fn static bool finished(ProcessMessage **message)
///
/// @brief Let go of a message the console has finished.
///
/// @param message Where the message is kept, NULL if there is none.
///
/// @return Returns true if no message is kept any more, false if the
/// console hasn't finished it.
static bool finished(ProcessMessage **message) {
  if ((*message != NULL) && processMessageDone(*message)) {
    processMessageRelease(*message);
    *message = NULL;
  }
  return *message == NULL;
}

/// @fn static bool flushGrid(WmState *state, WmGrid *grid)
///
/// @brief Send a session's console what is waiting for it:  the session's
/// size once it changes, then the keys held for it and a resize report the
/// window server makes from the grid's size, up to CONSOLE_INPUT_READY_SIZE
/// bytes at a time, each once the console has read the last.
///
/// @param state The window manager's state.
/// @param grid The session's text grid.
///
/// @return Returns true if something is still waiting to be sent, false
/// otherwise.
static bool flushGrid(WmState *state, WmGrid *grid) {
  if (grid->port == WM_NO_PORT) {
    return false;
  }
  if (finished(&grid->winsize) && grid->winsizeWanted) {
    grid->winsizeArgs->rows = grid->rows;
    grid->winsizeArgs->columns = grid->columns;
    grid->winsizeArgs->returnValue = 0;
    grid->winsizeWanted = !sendToConsole(state, &grid->winsize,
      CONSOLE_SET_WINSIZE, grid->winsizeArgs, sizeof(*grid->winsizeArgs));
  }
  if (grid->reportWanted) {
    char report[WM_GRID_HOLD + 1];
    int length = snprintf(report, sizeof(report), "\033[48;%u;%u;%u;%ut",
      (unsigned int) grid->rows, (unsigned int) grid->columns,
      (unsigned int) (grid->rows * state->display.builtInFont.glyphH),
      (unsigned int) (grid->columns * state->display.builtInFont.glyphW));
    if ((length > 0) && (length <= WM_GRID_HOLD - grid->numHeld)) {
      memcpy(&grid->held[grid->numHeld], report, (size_t) length);
      grid->numHeld += (uint8_t) length;
      grid->reportWanted = false;
    }
  }
  if (finished(&grid->input) && (grid->numHeld > 0)
    && !grid->winsizeWanted
  ) {
    uint8_t count = grid->numHeld;
    if (count > CONSOLE_INPUT_READY_SIZE) {
      count = CONSOLE_INPUT_READY_SIZE;
    }
    grid->inputArgs->portId = grid->port;
    grid->inputArgs->numBytes = count;
    memcpy(grid->inputArgs->bytes, grid->held, count);
    if (sendToConsole(state, &grid->input, CONSOLE_INPUT_READY,
        grid->inputArgs, sizeof(*grid->inputArgs))
    ) {
      grid->numHeld -= count;
      memmove(grid->held, &grid->held[count], grid->numHeld);
    }
  }
  return (grid->numHeld > 0) || grid->winsizeWanted || grid->reportWanted;
}

/// @fn static void flushSessions(WmState *state)
///
/// @brief Send each session's console what is waiting for it, and note
/// whether anything still is.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void flushSessions(WmState *state) {
  state->inputWaiting = false;
  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    WmGrid *grid = (WmGrid*) state->windows[ii].content;
    if ((grid != NULL) && flushGrid(state, grid)) {
      state->inputWaiting = true;
    }
  }
}

/// @fn static void key(WmState *state, const GfxEvent *event)
///
/// @brief Handle a key:  Alt+Tab activates the next normal window, Alt+F4
/// asks the owner of the window with the keys to close it, every other key
/// going down goes to the session of a window whose text grid has one, as
/// the bytes a terminal sends, and every other key goes to the owner.
///
/// @param state The window manager's state.
/// @param event The key's event.
///
/// @return This function returns no value.
static void key(WmState *state, const GfxEvent *event) {
  WmWindow *window = wmFind(state, state->focus);
  if (((event->modifiers & WM_MOD_ALT) != 0)
    && ((event->keycode == GFX_KEY_TAB) || (event->keycode == GFX_KEY_F4))
  ) {
    if (event->down == 0) {
      return;
    } else if (event->keycode == GFX_KEY_TAB) {
      altTab(state);
    } else if (window != NULL) {
      wmSend(state, window->owner, WIN_EVENT_CLOSE_REQUESTED, window->id);
    }
    return;
  } else if (window == NULL) {
    return;
  }
  WmGrid *grid = (WmGrid*) window->content;
  if ((grid != NULL) && (grid->port != WM_NO_PORT)) {
    uint8_t bytes[WM_KEY_MAX_BYTES];
    uint8_t count = 0;
    if (event->down != 0) {
      count = wmTranslateKey(event->keycode, event->ascii, bytes);
    }
    if ((count > 0) && (count <= WM_GRID_HOLD - grid->numHeld)) {
      memcpy(&grid->held[grid->numHeld], bytes, count);
      grid->numHeld += count;
      if ((state->times != NULL) && !state->echoKey) {
        timespec_get(&state->keySent, TIME_UTC);
        state->echoKey = true;
      }
      flushSessions(state);
    }
    return;
  }
  if ((event->down == 0) && (event->keycode == state->widgetKey)) {
    state->widgetKey = 0;
    if (state->menu.swallow == event->keycode) {
      state->menu.swallow = 0;
    }
    return;
  } else if ((window->widgets != NULL)
    && controls(state, window, "wmControlsKey")
  ) {
    return;
  }
  WinEvent *sent = wmSend(state, window->owner, WIN_EVENT_KEY, window->id);
  if (sent != NULL) {
    sent->key = event->keycode;
    sent->ascii = event->ascii;
    sent->modifiers = event->modifiers;
    sent->detail = event->down;
  }
}

/// @fn void* wmInput(void *args)
///
/// @brief Handle the press, release, wheel or key in the state's event, or,
/// with no event, send each session's console what is waiting for it.
///
/// @param args A pointer to the WmState.
///
/// @return Always returns the provided pointer.
void* wmInput(void *args) {
  WmState *state = (WmState*) args;
  const GfxEvent *event = state->event;
  if (event == NULL) {
    flushSessions(state);
  } else if (event->type == GFX_EVENT_KEY) {
    key(state, event);
  } else if (state->grab >= WM_GRAB_WIDGET) {
    controls(state, wmFind(state, state->grabbed), "wmControlsPointer");
  } else if (state->restoreOnDrag && (state->grab == WM_GRAB_MOVE)) {
    restoreUnderPointer(state, event->x);
  } else if ((event->wheel != 0) && (state->grab == WM_GRAB_NONE)
    && ((event->buttons & GFX_BUTTON_LEFT) == 0)
  ) {
    WmWindow *window = windowAt(state, event->x, event->y);
    if ((window != NULL) && (window->widgets != NULL)) {
      controls(state, window, "wmControlsWheel");
    }
  } else if ((event->buttons & GFX_BUTTON_LEFT) != 0) {
    press(state, event->x, event->y);
  } else if (state->grab != WM_GRAB_NONE) {
    release(state, event->x, event->y);
  }
  return args;
}
