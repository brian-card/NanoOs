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


/// @file Start.c
///
/// @brief The window server's start and end:  its options, the display, and
/// the frame times it prints.
/// Entered once as it starts and once as it ends.

#include "Wm.h"

/// @def WM_LITERAL_LENGTH(literal)
///
/// @brief The number of characters in a string literal, its terminator left
/// out.
#define WM_LITERAL_LENGTH(literal) (sizeof(literal) - 1)

/// @def WM_OPTION_MOVE
///
/// @brief The start of the option that picks how a window is moved.
#define WM_OPTION_MOVE "--move="

/// @def WM_OPTION_RESIZE
///
/// @brief The start of the option that picks how a window is resized.
#define WM_OPTION_RESIZE "--resize="

/// @def WM_OPTION_BUDGET
///
/// @brief The start of the option that sets each client's heap budget.
#define WM_OPTION_BUDGET "--budget="

/// @def WM_DISPLAY_WIDTH
///
/// @brief The width, in pixels, of the display sysnook asks for.
#define WM_DISPLAY_WIDTH 640

/// @def WM_DISPLAY_HEIGHT
///
/// @brief The height, in pixels, of the display sysnook asks for.
#define WM_DISPLAY_HEIGHT 480

/// @def WM_CMD_BUF_SIZE
///
/// @brief The size of the command buffer frames are recorded in.
#define WM_CMD_BUF_SIZE 512

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

/// @fn static bool parseOptions(WmState *state)
///
/// @brief Set the state's modes from the command line.
///
/// @param state The window manager's state.
///
/// @return Returns true if every argument is a valid option, false otherwise.
static bool parseOptions(WmState *state) {
  for (int ii = 1; ii < state->argc; ii++) {
    const char *arg = state->argv[ii];
    bool valid = true;
    if (strncmp(arg, WM_OPTION_MOVE, WM_LITERAL_LENGTH(WM_OPTION_MOVE)) == 0) {
      valid = parseMode(&arg[WM_LITERAL_LENGTH(WM_OPTION_MOVE)], "live",
        "outline", &state->liveMove);
    } else if (strncmp(arg, WM_OPTION_RESIZE,
      WM_LITERAL_LENGTH(WM_OPTION_RESIZE)) == 0
    ) {
      valid = parseMode(&arg[WM_LITERAL_LENGTH(WM_OPTION_RESIZE)], "live",
        "outline", &state->liveResize);
    } else if (strncmp(arg, WM_OPTION_BUDGET,
      WM_LITERAL_LENGTH(WM_OPTION_BUDGET)) == 0
    ) {
      char *end = NULL;
      long budget = strtol(&arg[WM_LITERAL_LENGTH(WM_OPTION_BUDGET)], &end,
        10);
      valid = (*end == '\0') && (budget > 0) && (budget <= UINT16_MAX);
      state->budget = (uint16_t) budget;
    } else if (strcmp(arg, "--finish") == 0) {
      state->finishFrames = true;
    } else if (strcmp(arg, "--timing") == 0) {
      if (state->times == NULL) {
        state->times = (WmTimes*) calloc(WM_TIMED_KINDS, sizeof(WmTimes));
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

/// @fn static void wmBegin(WmState *state)
///
/// @brief Parse the options, take the display, and make the command
/// buffer.  The exit status goes in
/// the state, and is 0 when the window server should run.  --help prints the
/// usage, and so leaves the display alone.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void wmBegin(WmState *state) {
  state->exitStatus = 0;
  if ((state->argc > 1) && (strcmp(state->argv[1], "--help") == 0)) {
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Usage", "wmUsage", stdout);
    state->exitStatus = WM_EXIT_HELP;
    return;
  }
  state->budget = WM_DEFAULT_BUDGET;
  if (!parseOptions(state)) {
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Usage", "wmUsage", stderr);
    state->exitStatus = 1;
    return;
  }
  GfxError status = gfxOpenDisplay(&state->display, GFX_DRIVER_PROCESS_NAME,
    WM_DISPLAY_WIDTH, WM_DISPLAY_HEIGHT);
  if (status == GFX_ERR_DISPLAY_BUSY) {
    fputs("sysnook: display busy\n", stderr);
    state->exitStatus = 1;
  } else if (status != GFX_OK) {
    fprintf(stderr, "sysnook: %s\n", gfxErrorString(status));
    state->exitStatus = 1;
  } else {
    state->displayOpen = true;
    state->windows = (WmWindow*) calloc(WM_MAX_WINDOWS, sizeof(WmWindow));
    state->nextId = 1;
    state->workArea.w = (int16_t) state->display.width;
    state->workArea.h = (int16_t) state->display.height;
    timespec_get(&state->started, TIME_UTC);
    state->cmd = gfxCmdBufCreate(&state->display, WM_CMD_BUF_SIZE);
    if ((state->windows == NULL) || (state->cmd == NULL)) {
      fputs("sysnook: out of memory\n", stderr);
      state->exitStatus = 1;
      return;
    }
    gfxCmdBufBegin(state->cmd, 0);
  }
}

/// @fn static void wmFinish(WmState *state)
///
/// @brief Print why the first frame failed if it did, destroy the command
/// buffer, and give the display back unless it was taken back.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void wmFinish(WmState *state) {
  if (state->exitStatus == WM_EXIT_PAINT) {
    fprintf(stderr, "sysnook: %s\n",
      gfxErrorString((GfxError) state->status));
    state->exitStatus = 1;
  }
  state->cmd = gfxCmdBufDestroy(state->cmd);
  if (state->displayOpen && !state->revoked) {
    gfxCloseDisplay(&state->display);
  }
  for (uint8_t ii = 0; ii < state->numWindows; ii++) {
    free(state->windows[ii].title);
  }
  free(state->windows);
  state->windows = NULL;
  state->numWindows = 0;
  free(state->times);
  state->times = NULL;
}

/// @fn void* wmStart(void *args)
///
/// @brief Carry out the state's operation:  WM_START_BEGIN starts the
/// window server and WM_START_END ends it.
///
/// @param args A pointer to the WmState.
///
/// @return Always returns the provided pointer.
void* wmStart(void *args) {
  WmState *state = (WmState*) args;
  if (state->operation == WM_START_BEGIN) {
    wmBegin(state);
  } else {
    wmFinish(state);
  }
  return args;
}
