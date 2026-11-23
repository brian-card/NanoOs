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
/// @brief Nook's window server:  it takes the display, draws a desktop and
/// three windows, moves and resizes them with the mouse, and quits on Ctrl+Q
/// or when the display is taken back.

#include "Wm.h"

/// @fn static void printUsage(FILE *stream)
///
/// @brief Print the usage summary for sysnook.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: sysnook [options]\n"
    "\n"
    "Take the display and manage windows on it until Ctrl+Q.\n"
    "\n"
    "Options:\n"
    "  --move=live|outline  How a moved window follows the pointer; outline by\n"
    "    default.\n"
    "  --resize=live|outline  How a resized window follows the pointer;\n"
    "    outline by default.\n"
    "  --content=records|call  How the full window's cells are painted.\n"
    "  --cells=N  The number of cells in the full window, up to 192.\n"
    "  --timing  Print how long frames took when it ends.\n"
    "  --help  Show this help and exit.\n",
    stream);
}

/// @fn static bool parseMode(const char *value, const char *enabled,
///   const char *disabled, bool *result)
///
/// @brief Parse the value of an option that is one of two words.
///
/// @param value The option's value.
/// @param enabled The word that sets the result to true.
/// @param disabled The word that sets the result to false.
/// @param result Where to store the result.
///
/// @return Returns true if the value is one of the words, false otherwise.
static bool parseMode(const char *value, const char *enabled,
  const char *disabled, bool *result
) {
  if (strcmp(value, enabled) == 0) {
    *result = true;
  } else if (strcmp(value, disabled) == 0) {
    *result = false;
  } else {
    return false;
  }
  return true;
}

/// @fn static bool parseOptions(WmState *state, int argc, char **argv,
///   int *cells)
///
/// @brief Set the state's modes from the command line.
///
/// @param state The window manager's state.
/// @param argc The number of arguments.
/// @param argv The arguments.
/// @param cells Where to store the number of cells of the full window.
///
/// @return Returns true if every argument is a valid option, false otherwise.
static bool parseOptions(WmState *state, int argc, char **argv, int *cells) {
  for (int ii = 1; ii < argc; ii++) {
    const char *arg = argv[ii];
    bool valid = true;
    if (strncmp(arg, "--move=", 7) == 0) {
      valid = parseMode(&arg[7], "live", "outline", &state->liveMove);
    } else if (strncmp(arg, "--resize=", 9) == 0) {
      valid = parseMode(&arg[9], "live", "outline", &state->liveResize);
    } else if (strncmp(arg, "--content=", 10) == 0) {
      valid = parseMode(&arg[10], "call", "records", &state->contentByCall);
    } else if (strncmp(arg, "--cells=", 8) == 0) {
      *cells = (int) strtol(&arg[8], NULL, 10);
      valid = (*cells >= 0) && (*cells <= WM_MAX_CELLS);
    } else if (strcmp(arg, "--timing") == 0) {
      if (state->times == NULL) {
        state->times = (WmTimes*) calloc(2, sizeof(WmTimes));
      }
      valid = (state->times != NULL);
    } else {
      valid = false;
    }
    if (!valid) {
      return false;
    }
  }
  return true;
}

/// @fn static void layOut(WmState *state, int cells)
///
/// @brief Place the three windows:  two plain ones, and the full one.
///
/// @param state The window manager's state.
/// @param cells The number of cells of the full window.
///
/// @return This function returns no value.
static void layOut(WmState *state, int cells) {
  static const GfxRect frames[WM_MAX_WINDOWS] = {
    { 24, 24, 200, 150 },
    { 260, 40, 180, 130 },
    { 300, 200, WM_CELL_COLUMNS * WM_CELL_SIZE,
      WM_TITLE_HEIGHT + (WM_CELL_ROWS * WM_CELL_SIZE) },
  };
  static const GfxColor bodies[WM_MAX_WINDOWS] = {
    CGA_LIGHT_GRAY, CGA_YELLOW, CGA_WHITE,
  };
  for (uint8_t ii = 0; ii < WM_MAX_WINDOWS; ii++) {
    WmWindow *window = &state->windows[ii];
    window->frame = frames[ii];
    window->shown = frames[ii];
    window->body = bodies[ii];
    window->number = ii;
  }
  state->windows[WM_MAX_WINDOWS - 1].cells = (uint8_t) cells;
  state->numWindows = WM_MAX_WINDOWS;
  state->paintAll = true;
}

/// @fn static bool contains(const GfxRect *rect, int16_t pointX,
///   int16_t pointY)
///
/// @brief Determine whether a point is inside a rectangle.
///
/// @param rect The rectangle.
/// @param pointX The point's column.
/// @param pointY The point's row.
///
/// @return Returns true if it is, false otherwise.
static bool contains(const GfxRect *rect, int16_t pointX, int16_t pointY) {
  return (pointX >= rect->x) && (pointX < rect->x + rect->w)
    && (pointY >= rect->y) && (pointY < rect->y + rect->h);
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

/// @fn static void press(WmState *state, int16_t pointX, int16_t pointY)
///
/// @brief Raise the window a press lands on, and start a move if it's on
/// the title bar or a resize if it's on the grip.
///
/// @param state The window manager's state.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return This function returns no value.
static void press(WmState *state, int16_t pointX, int16_t pointY) {
  int index = state->numWindows - 1;
  while ((index >= 0)
    && !contains(&state->windows[index].frame, pointX, pointY)
  ) {
    index--;
  }
  if (index < 0) {
    return;
  }
  if (index != state->numWindows - 1) {
    WmWindow raised = state->windows[index];
    memmove(&state->windows[index], &state->windows[index + 1],
      sizeof(WmWindow) * (size_t) (state->numWindows - 1 - index));
    *wmTop(state) = raised;
    state->paintTop = true;
  }

  const GfxRect *frame = &wmTop(state)->frame;
  int16_t right = frame->x + frame->w;
  int16_t bottom = frame->y + frame->h;
  if (pointY < frame->y + WM_TITLE_HEIGHT) {
    state->grab = WM_GRAB_MOVE;
    state->offset.x = pointX - frame->x;
    state->offset.y = pointY - frame->y;
  } else if ((pointX >= right - WM_GRIP_SIZE)
    && (pointY >= bottom - WM_GRIP_SIZE)
  ) {
    state->grab = WM_GRAB_RESIZE;
    state->offset.x = right - pointX;
    state->offset.y = bottom - pointY;
  }
  bool live = state->liveResize;
  if (state->grab == WM_GRAB_MOVE) {
    live = state->liveMove;
  }
  if ((state->grab != WM_GRAB_NONE) && !live) {
    state->outline = *frame;
  }
}

/// @fn static void drag(WmState *state, int16_t pointX, int16_t pointY)
///
/// @brief Move or resize the grabbed window, or its outline, to follow the
/// pointer, within the limits.
///
/// @param state The window manager's state.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return This function returns no value.
static void drag(WmState *state, int16_t pointX, int16_t pointY) {
  WmWindow *window = wmTop(state);
  GfxRect target = window->frame;
  int16_t width = (int16_t) state->display.width;
  int16_t height = (int16_t) state->display.height;
  bool live = state->liveResize;
  if (state->grab == WM_GRAB_MOVE) {
    target.x = clamp(pointX - state->offset.x, WM_KEEP_TITLE - target.w,
      width - WM_KEEP_TITLE);
    target.y = clamp(pointY - state->offset.y, 0, height - WM_TITLE_HEIGHT);
    live = state->liveMove;
  } else {
    target.w = clamp(pointX + state->offset.x - target.x, WM_MIN_WIDTH,
      width - target.x);
    target.h = clamp(pointY + state->offset.y - target.y, WM_MIN_HEIGHT,
      height - target.y);
  }
  if (live) {
    window->frame = target;
  } else {
    state->outline = target;
  }
}

/// @fn static void release(WmState *state)
///
/// @brief End a move or resize, putting the window where its outline is if
/// an outline followed the pointer.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void release(WmState *state) {
  if (state->outline.w != 0) {
    wmTop(state)->frame = state->outline;
    memset(&state->outline, 0, sizeof(state->outline));
  }
  state->grab = WM_GRAB_NONE;
}

/// @fn static void applyEvents(WmState *state,
///   const GfxInputEventsArgs *events)
///
/// @brief Apply a message's input events to the state.
///
/// @param state The window manager's state.
/// @param events The events.
///
/// @return This function returns no value.
static void applyEvents(WmState *state, const GfxInputEventsArgs *events) {
  for (uint8_t ii = 0; ii < events->count; ii++) {
    const GfxEvent *event = &events->events[ii];
    if (event->type == GFX_EVENT_KEY) {
      if ((event->down != 0) && (event->ascii == WM_CTRL_Q)) {
        state->quit = true;
      }
      continue;
    } else if (event->type != GFX_EVENT_POINTER) {
      continue;
    }
    bool wasDown = (state->buttons & 1) != 0;
    bool down = (event->buttons & 1) != 0;
    state->buttons = event->buttons;
    if (down && !wasDown) {
      press(state, event->x, event->y);
    } else if (state->grab != WM_GRAB_NONE) {
      drag(state, event->x, event->y);
      if (!down) {
        release(state);
      }
    }
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
  const WmWindow *window = wmTop(state);
  return state->paintTop
    || (memcmp(&window->frame, &window->shown, sizeof(GfxRect)) != 0)
    || (memcmp(&state->outline, &state->outlineShown, sizeof(GfxRect)) != 0);
}

/// @fn static GfxError paint(WmState *state, uint8_t operation)
///
/// @brief Have the Paint overlay carry out an operation.
///
/// @param state The window manager's state.
/// @param operation The WmPaintOperation.
///
/// @return Returns the operation's status.
static GfxError paint(WmState *state, uint8_t operation) {
  state->operation = operation;
  state->status = GFX_ERR_DEVICE_LOST;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Paint", "wmPaint", state);
  return (GfxError) state->status;
}

/// @fn static void timeFrame(WmState *state, uint8_t grab,
///   const struct timespec *start)
///
/// @brief Wait until a frame is drawn and keep how long it took since its
/// events arrived.
///
/// @param state The window manager's state.
/// @param grab The WmGrab the frame belongs to.
/// @param start When the frame's events arrived.
///
/// @return This function returns no value.
static void timeFrame(WmState *state, uint8_t grab,
  const struct timespec *start
) {
  gfxFinish(&state->display);
  struct timespec now;
  timespec_get(&now, TIME_UTC);
  WmTimes *times = &state->times[grab - WM_GRAB_MOVE];
  if (times->count < WM_TIMED_FRAMES) {
    times->ms[times->count] = (uint16_t) (((now.tv_sec - start->tv_sec)
      * 1000L) + ((now.tv_nsec - start->tv_nsec) / 1000000L));
  }
  times->count++;
}

/// @fn static void printTimes(const char *kind, WmTimes *times)
///
/// @brief Print the number of frames of a kind, and the median and longest
/// time of the first WM_TIMED_FRAMES of them.
///
/// @param kind The kind of frame.
/// @param times The frames' times, which are sorted.
///
/// @return This function returns no value.
static void printTimes(const char *kind, WmTimes *times) {
  uint16_t kept = times->count;
  if (kept > WM_TIMED_FRAMES) {
    kept = WM_TIMED_FRAMES;
  }
  for (uint16_t ii = 1; ii < kept; ii++) {
    uint16_t value = times->ms[ii];
    uint16_t jj = ii;
    while ((jj > 0) && (times->ms[jj - 1] > value)) {
      times->ms[jj] = times->ms[jj - 1];
      jj--;
    }
    times->ms[jj] = value;
  }
  unsigned int median = 0;
  unsigned int longest = 0;
  if (kept > 0) {
    median = times->ms[kept / 2];
    longest = times->ms[kept - 1];
  }
  printf("sysnook: %s frames %u median %u ms max %u ms\n", kind,
    (unsigned int) times->count, median, longest);
}

/// @fn static bool run(WmState *state)
///
/// @brief Apply input events and paint what they change until Ctrl+Q or
/// until the display is taken back.
///
/// @param state The window manager's state.
///
/// @return Returns true if the display was taken back, false on Ctrl+Q.
static bool run(WmState *state) {
  while (true) {
    ProcessMessage *message = processMessageQueueWait(NULL);
    if (message == NULL) {
      continue;
    }
    if (gfxIsInputEvents(message)) {
      struct timespec start;
      uint8_t grab = state->grab;
      if (state->times != NULL) {
        timespec_get(&start, TIME_UTC);
      }
      applyEvents(state, gfxInputEvents(message));
      gfxInputEventsDone(message);
      if (state->quit) {
        return false;
      }
      if (frameNeeded(state)) {
        paint(state, WM_PAINT_FRAME);
        if ((state->times != NULL) && (grab != WM_GRAB_NONE)) {
          timeFrame(state, grab, &start);
        }
      }
      continue;
    }

    int64_t type = processMessageType(message);
    processMessageSetDone(message);
    if (processMessageWaiting(message) == false) {
      processMessageRelease(message);
    }
    if (type == (GFX_COMMAND_SIGNATURE | GFX_MSG_DISPLAY_REVOKED)) {
      return true;
    }
  }
}

int main(int argc, char **argv) {
  if ((argc > 1) && (strcmp(argv[1], "--help") == 0)) {
    printUsage(stdout);
    return 0;
  }
  WmState *state = (WmState*) calloc(1, sizeof(WmState));
  if (state == NULL) {
    fputs("sysnook: out of memory\n", stderr);
    return 1;
  }
  int cells = WM_MAX_CELLS;
  if (!parseOptions(state, argc, argv, &cells)) {
    printUsage(stderr);
    free(state->times);
    free(state);
    return 1;
  }

  int exitStatus = 0;
  GfxError status = gfxOpenDisplay(&state->display, GFX_DRIVER_PROCESS_NAME,
    640, 480);
  if (status == GFX_ERR_DISPLAY_BUSY) {
    fputs("sysnook: display busy\n", stderr);
    exitStatus = 1;
  } else if (status != GFX_OK) {
    fprintf(stderr, "sysnook: %s\n", gfxErrorString(status));
    exitStatus = 1;
  } else {
    layOut(state, cells);
    status = paint(state, WM_PAINT_START);
    bool revoked = false;
    if (status == GFX_OK) {
      revoked = run(state);
    } else {
      fprintf(stderr, "sysnook: %s\n", gfxErrorString(status));
      exitStatus = 1;
    }
    paint(state, WM_PAINT_END);
    if (!revoked) {
      gfxCloseDisplay(&state->display);
    }
  }

  if ((state->times != NULL) && (exitStatus == 0)) {
    printTimes("move", &state->times[0]);
    printTimes("resize", &state->times[1]);
  }
  free(state->times);
  free(state);
  return exitStatus;
}
