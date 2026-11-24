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

/// @def WM_DOUBLE_CLICK_MS
///
/// @brief The longest time between two presses on a title bar that make a
/// double-click.
#define WM_DOUBLE_CLICK_MS 500

/// @def WM_DOUBLE_CLICK_DISTANCE
///
/// @brief The farthest two presses on a title bar that make a double-click
/// can be apart on either axis.
#define WM_DOUBLE_CLICK_DISTANCE 4

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

/// @fn static uint32_t nowMs(WmState *state)
///
/// @brief Get the time since the window server started.  The clock is read
/// when an input message is handled, since GfxEvent carries no time.
///
/// @param state The window manager's state.
///
/// @return Returns the milliseconds since the window server started.
static uint32_t nowMs(WmState *state) {
  struct timespec now;
  timespec_get(&now, TIME_UTC);
  return (uint32_t) (((now.tv_sec - state->started.tv_sec) * 1000L)
    + ((now.tv_nsec - state->started.tv_nsec) / 1000000L));
}

/// @fn static bool doubleClick(WmState *state, const WmWindow *window,
///   int16_t pointX, int16_t pointY)
///
/// @brief Determine whether a press on a title bar is the second of a
/// double-click, and remember it as the first of the next one if not.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return Returns true if it is, false otherwise.
static bool doubleClick(WmState *state, const WmWindow *window,
  int16_t pointX, int16_t pointY
) {
  uint32_t now = nowMs(state);
  int16_t distanceX = pointX - state->lastPressAt.x;
  int16_t distanceY = pointY - state->lastPressAt.y;
  if ((state->lastPressWindow == window->id)
    && (now - state->lastPressTime <= WM_DOUBLE_CLICK_MS)
    && (distanceX <= WM_DOUBLE_CLICK_DISTANCE)
    && (distanceX >= -WM_DOUBLE_CLICK_DISTANCE)
    && (distanceY <= WM_DOUBLE_CLICK_DISTANCE)
    && (distanceY >= -WM_DOUBLE_CLICK_DISTANCE)
  ) {
    state->lastPressWindow = 0;
    return true;
  }
  state->lastPressWindow = window->id;
  state->lastPressTime = now;
  state->lastPressAt.x = pointX;
  state->lastPressAt.y = pointY;
  return false;
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

/// @fn static void press(WmState *state, int16_t pointX, int16_t pointY)
///
/// @brief Activate the window a press lands on, and press a caption button,
/// start a resize if it's on a grab zone, or start a move if it's on the
/// title bar.  A double-click on the title bar maximizes or restores.
///
/// @param state The window manager's state.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return This function returns no value.
static void press(WmState *state, int16_t pointX, int16_t pointY) {
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
    if (doubleClick(state, window, pointX, pointY)) {
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

/// @fn static void key(WmState *state, const GfxEvent *event)
///
/// @brief Handle a key:  Alt+Tab activates the next normal window, Alt+F4
/// asks the owner of the window with the keys to close it, and every other
/// key goes to that owner.
///
/// @param state The window manager's state.
/// @param event The key's event.
///
/// @return This function returns no value.
static void key(WmState *state, const GfxEvent *event) {
  WmWindow *window = wmFind(state, state->focus);
  if (((event->modifiers & GFX_MOD_ALT) != 0)
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
/// @brief Handle the press, release or key in the state's event.
///
/// @param args A pointer to the WmState.
///
/// @return Always returns the provided pointer.
void* wmInput(void *args) {
  WmState *state = (WmState*) args;
  const GfxEvent *event = state->event;
  if (event->type == GFX_EVENT_KEY) {
    key(state, event);
  } else if (state->restoreOnDrag && (state->grab == WM_GRAB_MOVE)) {
    restoreUnderPointer(state, event->x);
  } else if ((event->buttons & 1) != 0) {
    press(state, event->x, event->y);
  } else if (state->grab != WM_GRAB_NONE) {
    release(state, event->x, event->y);
  }
  return args;
}
