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

/// @file main.c
///
/// @brief Nook's window server:  it takes the display, draws the desktop and
/// its clients' windows, moves and resizes them with the mouse, and ends
/// when the display is taken back.  This overlay is the loop and what every
/// frame does; the others are entered for starting and ending, for presses,
/// releases and keys, for clients' messages, and for painting.

#include "Wm.h"

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
/// @brief Add a rectangle to the frame's damage, unless it's empty.
///
/// @param state The window manager's state.
/// @param left The rectangle's left edge.
/// @param top The rectangle's top edge.
/// @param width The rectangle's width.
/// @param height The rectangle's height.
///
/// @return This function returns no value.
static __attribute__((noinline)) void addDamage(WmState *state,
  int16_t left, int16_t top,
  int16_t width, int16_t height
) {
  if ((width > 0) && (height > 0)) {
    GfxRect rect = { left, top, width, height };
    wmDamage(state, &rect);
  }
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

/// @fn static int16_t clamp(int16_t value, int16_t low, int16_t high)
///
/// @brief Limit a value to a range.
///
/// @param value The value.
/// @param low The lowest value allowed.
/// @param high The highest value allowed, which wins over low.
///
/// @return Returns the limited value.
static int16_t clamp(int16_t value, int16_t low, int16_t high) {
  if (value < low) {
    value = low;
  }
  if (value > high) {
    value = high;
  }
  return value;
}

/// @fn static void snap(int16_t *start, int16_t *size, int16_t around,
///   int16_t minimum, uint8_t cell, bool leading)
///
/// @brief Shorten a resized window's width or height to hold whole cells of
/// its text grid, or lengthen it by one if that would make it too small,
/// keeping the edge the resize doesn't move still.
///
/// @param start The window's left or top edge.
/// @param size The window's width or height.
/// @param around How much of the width or height isn't the grid's.
/// @param minimum The window's minimum width or height.
/// @param cell The width or height of a cell.
/// @param leading Whether the resize moves the left or top edge.
///
/// @return This function returns no value.
static __attribute__((noinline)) void snap(int16_t *start, int16_t *size,
  int16_t around, int16_t minimum, uint8_t cell, bool leading
) {
  int16_t extra = (int16_t) ((*size - around) % cell);
  if (extra < 0) {
    extra += cell;
  }
  int16_t snapped = *size - extra;
  if (snapped < minimum) {
    snapped += cell;
  }
  if (leading) {
    *start += *size - snapped;
  }
  *size = snapped;
}

/// @fn static void drag(WmState *state, int16_t pointX, int16_t pointY)
///
/// @brief Move or resize the grabbed window, or its outline, to follow the
/// pointer, within the limits:  32 pixels of title bar stay on the screen,
/// and a resize holds the edges it doesn't move still and keeps the window at
/// least its minimum size.
///
/// @param state The window manager's state.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return This function returns no value.
static void drag(WmState *state, int16_t pointX, int16_t pointY) {
  WmWindow *window = wmFind(state, state->grabbed);
  if (window == NULL) {
    return;
  }
  GfxRect target = window->frame;
  int16_t width = (int16_t) state->display.width;
  int16_t height = (int16_t) state->display.height;
  bool live = state->liveResize;
  if (state->grab == WM_GRAB_MOVE) {
    target.x = clamp(pointX - state->offset.x, WM_KEEP_TITLE - target.w,
      width - WM_KEEP_TITLE);
    target.y = clamp(pointY - state->offset.y, 0, height - WM_FRAME_TOP);
    live = state->liveMove;
  } else {
    int16_t right = target.x + target.w;
    int16_t bottom = target.y + target.h;
    if ((state->edges & WM_EDGE_RIGHT) != 0) {
      target.w = clamp(pointX + state->offset.x - target.x, WM_MIN_WIDTH,
        width - target.x);
    }
    if ((state->edges & WM_EDGE_BOTTOM) != 0) {
      target.h = clamp(pointY + state->offset.y - target.y, WM_MIN_HEIGHT,
        height - target.y);
    }
    if ((state->edges & WM_EDGE_LEFT) != 0) {
      target.x = clamp(pointX - state->offset.x, 0, right - WM_MIN_WIDTH);
      target.w = right - target.x;
    }
    if ((state->edges & WM_EDGE_TOP) != 0) {
      target.y = clamp(pointY - state->offset.y, 0, bottom - WM_MIN_HEIGHT);
      target.h = bottom - target.y;
    }
  }
  const WmGrid *grid = (const WmGrid*) window->content;
  if ((state->grab == WM_GRAB_RESIZE) && (grid != NULL)
    && (grid->anchor == (WIN_ANCHOR_LEFT | WIN_ANCHOR_TOP | WIN_ANCHOR_RIGHT
      | WIN_ANCHOR_BOTTOM))
  ) {
    int16_t across = grid->rect.x + grid->marginRight;
    int16_t down = grid->rect.y + grid->marginBottom;
    if ((window->flags & WIN_FLAG_DECORATED) != 0) {
      across += 2 * WM_BORDER;
      down += WM_FRAME_TOP + WM_BORDER;
    }
    snap(&target.x, &target.w, across, WM_MIN_WIDTH,
      state->display.builtInFont.glyphW,
      (state->edges & WM_EDGE_LEFT) != 0);
    snap(&target.y, &target.h, down, WM_MIN_HEIGHT,
      state->display.builtInFont.glyphH,
      (state->edges & WM_EDGE_TOP) != 0);
  }
  if (live) {
    window->frame = target;
  } else {
    state->outline = target;
  }
}

/// @fn static void hover(WmState *state, int16_t pointX, int16_t pointY)
///
/// @brief Follow the pointer over caption buttons:  while one is held down,
/// whether the pointer is still on it, and otherwise which one it's over.
/// Only a change repaints, and only the buttons it changes.
///
/// @param state The window manager's state.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return This function returns no value.
static void hover(WmState *state, int16_t pointX, int16_t pointY) {
  if (state->grab == WM_GRAB_BUTTON) {
    WmWindow *window = wmFind(state, state->grabbed);
    bool inside = (window != NULL)
      && (wmButtonAt(window, pointX, pointY) == state->pressButton);
    if (inside != state->pressInside) {
      state->pressInside = inside;
      wmDamageButton(state, state->grabbed, state->pressButton);
    }
    return;
  }
  uint8_t id = 0;
  uint8_t button = WM_BUTTON_NONE;
  WmWindow *window = wmWindowAt(state, pointX, pointY);
  if (window != NULL) {
    button = wmButtonAt(window, pointX, pointY);
    if (button != WM_BUTTON_NONE) {
      id = window->id;
    }
  }
  if ((id != state->hoverWindow) || (button != state->hoverButton)) {
    wmDamageButton(state, state->hoverWindow, state->hoverButton);
    state->hoverWindow = id;
    state->hoverButton = button;
    wmDamageButton(state, id, button);
  }
}

/// @fn static void input(WmState *state, const GfxEvent *event)
///
/// @brief Have the Input overlay handle a press, a release or a key.
///
/// @param state The window manager's state.
/// @param event The event.
///
/// @return This function returns no value.
static void input(WmState *state, const GfxEvent *event) {
  state->event = event;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Input", "wmInput", state);
}

/// @fn static void applyEvents(WmState *state,
///   const GfxInputEventsArgs *events)
///
/// @brief Apply a message's input events to the state.  Motion is handled
/// here, since it comes many times a frame; presses, releases and keys go
/// to the Input overlay.
///
/// @param state The window manager's state.
/// @param events The events.
///
/// @return This function returns no value.
static void applyEvents(WmState *state, const GfxInputEventsArgs *events) {
  for (uint8_t ii = 0; ii < events->count; ii++) {
    const GfxEvent *event = &events->events[ii];
    if (event->type == GFX_EVENT_KEY) {
      // A session is sent only keys going down, so a release in a window
      // with a session's grid costs no overlay call.
      const WmWindow *focus = wmFind(state, state->focus);
      if ((event->down != 0) || (focus == NULL) || (focus->content == NULL)
        || (((const WmGrid*) focus->content)->port == WM_NO_PORT)
      ) {
        input(state, event);
      }
      continue;
    } else if (event->type != GFX_EVENT_POINTER) {
      continue;
    }
    bool wasDown = (state->buttons & GFX_BUTTON_LEFT) != 0;
    bool down = (event->buttons & GFX_BUTTON_LEFT) != 0;
    state->buttons = event->buttons;
    bool dragging = (state->grab == WM_GRAB_MOVE)
      || (state->grab == WM_GRAB_RESIZE);
    if (dragging) {
      if (state->restoreOnDrag && down) {
        input(state, event);
      }
      drag(state, event->x, event->y);
    } else if ((down == wasDown) && (state->grab >= WM_GRAB_WIDGET)) {
      input(state, event);
    } else if (down == wasDown) {
      hover(state, event->x, event->y);
    }
    if ((down != wasDown) || (event->wheel != 0)) {
      input(state, event);
    }
  }
}

/// @fn static void changeGrabbed(WmState *state, const WmWindow *window)
///
/// @brief Work out the damage of a change to where the grabbed window is
/// shown.  A move of a window nothing covers, when nothing else waits to be
/// repainted, has Paint copy what's on the screen of it and repaints the
/// rest; when something does wait, the screen may not show the window as it
/// is, so both frames are repainted instead.  A resize that keeps the
/// top-left corner repaints what the old and new frames don't share, the
/// title bar, and the old and new right and bottom borders; anything else
/// repaints both frames.
///
/// @param state The window manager's state.
/// @param window The grabbed window.
///
/// @return This function returns no value.
static void changeGrabbed(WmState *state, const WmWindow *window) {
  const GfxRect *shown = &state->shown;
  const GfxRect *frame = &window->frame;
  GfxRect screen = { 0, 0, (int16_t) state->display.width,
    (int16_t) state->display.height };
  bool alone = (state->numDamage == 0);
  for (uint8_t ii = (uint8_t) (window - state->windows) + 1;
    ii < state->numWindows; ii++
  ) {
    const WmWindow *above = &state->windows[ii];
    GfxRect overlap;
    if ((above->state != WIN_STATE_MINIMIZED)
      && (intersect(&above->frame, shown, &overlap)
        || intersect(&above->frame, frame, &overlap))
    ) {
      alone = false;
    }
  }
  GfxRect source;
  if (alone && (shown->w == frame->w) && (shown->h == frame->h)
    && intersect(shown, &screen, &source)
  ) {
    GfxPoint target = { source.x + frame->x - shown->x,
      source.y + frame->y - shown->y };
    GfxRect copied = { target.x, target.y, source.w, source.h };
    state->copy = true;
    state->copySource = source;
    state->copyTarget = target;
    subtract(state, shown, frame);
    subtract(state, frame, &copied);
  } else if ((shown->x == frame->x) && (shown->y == frame->y)) {
    subtract(state, shown, frame);
    subtract(state, frame, shown);
    addDamage(state, frame->x, frame->y, frame->w, WM_FRAME_TOP);
    addDamage(state, frame->x + frame->w - WM_BORDER, frame->y, WM_BORDER,
      frame->h);
    addDamage(state, frame->x, frame->y + frame->h - WM_BORDER, frame->w,
      WM_BORDER);
    addDamage(state, shown->x + shown->w - WM_BORDER, shown->y, WM_BORDER,
      shown->h);
    addDamage(state, shown->x, shown->y + shown->h - WM_BORDER, shown->w,
      WM_BORDER);
  } else {
    addRect(state, shown);
    addRect(state, frame);
  }
}

/// @fn static bool frameNeeded(WmState *state)
///
/// @brief Determine whether anything on the screen has to change.
///
/// @param state The window manager's state.
///
/// @return Returns true if a frame has to be painted, false otherwise.
static bool frameNeeded(WmState *state) {
  const WmWindow *window = wmFind(state, state->grabbed);
  return state->paintAll || (state->numDamage > 0)
    || ((window != NULL)
      && (memcmp(&window->frame, &state->shown, sizeof(GfxRect)) != 0))
    || (memcmp(&state->outline, &state->outlineShown, sizeof(GfxRect)) != 0);
}

/// @fn static GfxError paint(WmState *state)
///
/// @brief Work out the damage of the grabbed window's change, have the Paint
/// overlay bring the screen up to date, and wait until it's drawn if --finish
/// asked.
///
/// @param state The window manager's state.
///
/// @return Returns the frame's status.
static GfxError paint(WmState *state) {
  WmWindow *window = wmFind(state, state->grabbed);
  if ((window != NULL) && !state->paintAll
    && (memcmp(&window->frame, &state->shown, sizeof(GfxRect)) != 0)
  ) {
    changeGrabbed(state, window);
  }
  state->status = GFX_ERR_DEVICE_LOST;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Paint", "wmPaint", state);
  if (state->finishFrames) {
    gfxFinish(&state->display);
  }
  if (window != NULL) {
    state->shown = window->frame;
  }
  if (state->grab == WM_GRAB_NONE) {
    state->grabbed = 0;
  }
  return (GfxError) state->status;
}

/// @fn static void timeFrame(WmState *state, uint8_t kind,
///   const struct timespec *start)
///
/// @brief Wait until a frame is drawn and keep how long it took since what
/// it shows happened.
///
/// @param state The window manager's state.
/// @param kind The WmTimed the frame belongs to.
/// @param start When the frame's events arrived, or the key it echoes was
///   sent.
///
/// @return This function returns no value.
static void timeFrame(WmState *state, uint8_t kind,
  const struct timespec *start
) {
  gfxFinish(&state->display);
  struct timespec now;
  timespec_get(&now, TIME_UTC);
  WmTimes *times = &state->times[kind];
  if (times->count < WM_TIMED_FRAMES) {
    times->ms[times->count] = (uint16_t) (((now.tv_sec - start->tv_sec)
      * 1000L) + ((now.tv_nsec - start->tv_nsec) / 1000000L));
  }
  times->count++;
}

/// @fn static bool postsDone(WmState *state)
///
/// @brief Determine whether a client has marked an events message done, so
/// that the Client overlay can collect it and send the client more.
///
/// @param state The window manager's state.
///
/// @return Returns true if one has, false otherwise.
static bool postsDone(WmState *state) {
  for (uint8_t ii = 0; ii < state->numClients; ii++) {
    ProcessMessage *message = state->clients[ii]->message;
    if ((message != NULL) && processMessageDone(message)) {
      return true;
    }
  }
  return false;
}

/// @fn static struct timespec* pollTime(WmState *state,
///   struct timespec *poll)
///
/// @brief Work out how long the next wait for a message may be:  for as long
/// as it takes, unless a client has events waiting, which are tried again
/// WM_POLL_MS from now.
///
/// @param state The window manager's state.
/// @param poll Where to store the time to stop waiting.
///
/// @return Returns poll, or NULL to wait for as long as it takes.
static struct timespec* pollTime(WmState *state, struct timespec *poll) {
  if ((state->numPosts == 0) && !state->eventsWaiting
    && !state->inputWaiting
  ) {
    return NULL;
  }
  timespec_get(poll, TIME_UTC);
  poll->tv_nsec += WM_POLL_MS * 1000000L;
  if (poll->tv_nsec >= 1000000000L) {
    poll->tv_sec++;
    poll->tv_nsec -= 1000000000L;
  }
  return poll;
}

/// @fn static bool run(WmState *state)
///
/// @brief Apply input events and clients' messages, and paint what they
/// change, until the display is taken back.
///
/// @param state The window manager's state.
///
/// @return Returns true, since the display was taken back.
static bool run(WmState *state) {
  while (true) {
    struct timespec poll;
    ProcessMessage *message = state->next;
    state->next = NULL;
    if (message == NULL) {
      message = processMessageQueueWait(pollTime(state, &poll));
    }
    uint8_t grab = state->grab;
    ProcessMessage *gridMessage = NULL;
    struct timespec start;
    bool events = (message != NULL) && gfxIsInputEvents(message);
    if (events) {
      if (state->times != NULL) {
        timespec_get(&start, TIME_UTC);
      }
      applyEvents(state, gfxInputEvents(message));
      gfxInputEventsDone(message);
    } else if (message != NULL) {
      int64_t type = processMessageType(message);
      state->message = message;
      if ((type == (WIN_COMMAND_SIGNATURE | WIN_MSG_GRID_WRITE))
        || (type == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_SESSION_ENDED))
      ) {
        gridMessage = message;
      } else if ((type & ~WIN_MSG_TYPE_MASK) == WIN_COMMAND_SIGNATURE) {
        uint8_t request = (uint8_t) type;
        if ((request == WIN_MSG_HELLO) || (request == WIN_MSG_GOODBYE)
          || (request == WIN_MSG_CLIENT_ENDED)
        ) {
          callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Client",
            "wmClientMessage", state);
        } else {
          callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Window",
            "wmWindowMessage", state);
        }
      } else {
        processMessageSetDone(message);
        if (processMessageWaiting(message) == false) {
          processMessageRelease(message);
        }
        if (type == (GFX_COMMAND_SIGNATURE | GFX_MSG_DISPLAY_REVOKED)) {
          return true;
        }
      }
    }
    if (state->inputWaiting) {
      input(state, NULL);
    }
    if ((gridMessage != NULL) || state->gridsResized || state->gridsDirty) {
      state->message = gridMessage;
      callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Grid", "wmGrid", state);
    }
    if (!state->deferring && frameNeeded(state)) {
      paint(state);
      if ((state->times != NULL) && events
        && ((grab == WM_GRAB_MOVE) || (grab == WM_GRAB_RESIZE))
      ) {
        timeFrame(state, grab - WM_GRAB_MOVE, &start);
      } else if ((state->times != NULL) && state->echoKey
        && state->gridPainted
      ) {
        state->echoKey = false;
        timeFrame(state, WM_TIMED_ECHO, &state->keySent);
      }
      state->gridPainted = false;
    }
    if ((state->numOutbox > 0) || postsDone(state)
      || ((message == NULL) && state->eventsWaiting)
    ) {
      callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Client", "wmClientFlush",
        state);
    }
  }
}

int main(int argc, char **argv) {
  WmState *state = (WmState*) calloc(1, sizeof(WmState));
  if (state == NULL) {
    fputs("sysnook: out of memory\n", stderr);
    return 1;
  }
  state->argc = argc;
  state->argv = argv;
  state->operation = WM_START_BEGIN;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Start", "wmStart", state);
  if (state->displayOpen && (state->exitStatus == 0)) {
    state->paintAll = true;
    GfxError status = paint(state);
    if (status == GFX_OK) {
      callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Client", "wmClientStart",
        state);
      state->revoked = run(state);
      state->ending = true;
      callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Client", "wmClientEnd",
        state);
    } else {
      state->exitStatus = WM_EXIT_PAINT;
    }
  }
  state->operation = WM_START_END;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Start", "wmStart", state);

  int exitStatus = state->exitStatus;
  if (exitStatus == WM_EXIT_HELP) {
    exitStatus = 0;
  }
  free(state);
  return exitStatus;
}
