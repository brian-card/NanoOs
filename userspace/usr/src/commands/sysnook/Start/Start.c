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
/// @brief The window server's start and end:  its options, the
/// demonstration windows, the display, and the frame times it prints.
/// Entered once as it starts and once as it ends.

#include "WmCells.h"

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

/// @def WM_OPTION_CONTENT
///
/// @brief The start of the option that picks how the cells are recorded.
#define WM_OPTION_CONTENT "--content="

/// @def WM_OPTION_CELLS
///
/// @brief The start of the option that sets the number of cells.
#define WM_OPTION_CELLS "--cells="

/// @def WM_DEMO_TITLE
///
/// @brief The title of a demo window, whose last character is replaced by
/// the window's number.
#define WM_DEMO_TITLE "Window 0"

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

/// @fn static void printUsage(FILE *stream)
///
/// @brief Print the usage summary for sysnook, as nook, the command that
/// runs it.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: nook [options]\n"
    "\n"
    "Start Nook on the display, and wait until it ends.  Ctrl+Q quits it.\n"
    "\n"
    "Options:\n"
    "  --move=live|outline  How a moved window follows the pointer; outline by\n"
    "    default.\n"
    "  --resize=live|outline  How a resized window follows the pointer;\n"
    "    outline by default.\n"
    "  --content=records|call  How the full window's cells are painted;\n"
    "    records by default.  Only with --demo.\n"
    "  --cells=N  The number of cells in the full window, up to 192.  Only\n"
    "    with --demo.\n"
    "  --timing  Print how long frames and echoes took when it ends.\n"
    "  --demo  Draw three demonstration windows of the window server's own.\n"
    "  --finish  Wait until each frame is drawn before going on.\n"
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

/// @fn static bool parseOptions(WmState *state, int *cells)
///
/// @brief Set the state's modes from the command line.
///
/// @param state The window manager's state.
/// @param cells Where to store the number of cells of the full window.
///
/// @return Returns true if every argument is a valid option, false otherwise.
static bool parseOptions(WmState *state, int *cells) {
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
    } else if (strncmp(arg, WM_OPTION_CONTENT,
      WM_LITERAL_LENGTH(WM_OPTION_CONTENT)) == 0
    ) {
      valid = parseMode(&arg[WM_LITERAL_LENGTH(WM_OPTION_CONTENT)], "call",
        "records", &state->contentByCall);
    } else if (strncmp(arg, WM_OPTION_CELLS,
      WM_LITERAL_LENGTH(WM_OPTION_CELLS)) == 0
    ) {
      *cells = (int) strtol(&arg[WM_LITERAL_LENGTH(WM_OPTION_CELLS)], NULL,
        10);
      valid = (*cells >= 0) && (*cells <= WM_MAX_CELLS);
    } else if (strcmp(arg, "--demo") == 0) {
      state->demo = true;
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

/// @fn static void layOut(WmState *state, int cells)
///
/// @brief Place the three demonstration windows:  two plain ones, and the
/// full one, which is in front and active.
///
/// @param state The window manager's state.
/// @param cells The number of cells of the full window.
///
/// @return This function returns no value.
static void layOut(WmState *state, int cells) {
  static const GfxRect frames[WM_DEMO_WINDOWS] = {
    { 24, 24, 200, 150 },
    { 260, 40, 180, 130 },
    { 300, 200, (WM_CELL_COLUMNS * WM_CELL_SIZE) + (2 * WM_BORDER),
      WM_FRAME_TOP + (WM_CELL_ROWS * WM_CELL_SIZE) + WM_BORDER },
  };
  static const GfxColor faces[WM_DEMO_WINDOWS] = {
    CGA_LIGHT_GRAY, CGA_YELLOW, CGA_WHITE,
  };
  for (uint8_t ii = 0; ii < WM_DEMO_WINDOWS; ii++) {
    WmWindow *window = &state->windows[ii];
    window->id = state->nextId++;
    window->layer = WIN_LAYER_NORMAL;
    window->flags = WIN_FLAG_DECORATED | WIN_FLAG_RESIZABLE
      | WIN_FLAG_IN_TASKBAR;
    window->frame = frames[ii];
    window->face = faces[ii];
    window->title = (char*) malloc(sizeof(WM_DEMO_TITLE));
    if (window->title != NULL) {
      strcpy(window->title, WM_DEMO_TITLE);
      window->title[WM_LITERAL_LENGTH(WM_DEMO_TITLE) - 1] = (char) ('0' + ii);
    }
  }
  state->windows[WM_DEMO_WINDOWS - 1].cells = (uint8_t) cells;
  state->numWindows = WM_DEMO_WINDOWS;
  state->active = state->windows[WM_DEMO_WINDOWS - 1].id;
  state->focus = state->active;
}

/// @fn static GfxError preloadRows(WmState *state)
///
/// @brief Preload each row of the full window's cells, one preload per row,
/// because a preload has to fit one 512-byte buffer.
///
/// @param state The window manager's state.
///
/// @return Returns the status of the preloads.
static GfxError preloadRows(WmState *state) {
  uint8_t cells = state->windows[WM_DEMO_WINDOWS - 1].cells;
  uint8_t rows = (cells + WM_CELL_COLUMNS - 1) / WM_CELL_COLUMNS;
  GfxPoint origin = { 0, 0 };
  GfxError status = GFX_OK;
  for (uint8_t row = 0; (row < rows) && (status == GFX_OK); row++) {
    gfxCmdBufBegin(state->cmd, GFX_CMDBUF_NO_FLUSH);
    wmRecordRow(state->cmd, row, cells, origin, NULL);
    status = gfxPreupload(state->cmd, &state->rows[row]);
  }
  gfxCmdBufBegin(state->cmd, 0);
  return status;
}

/// @fn static void wmBegin(WmState *state)
///
/// @brief Parse the options, take the display, make the command buffer, and
/// lay out and preload the demonstration windows.  The exit status goes in
/// the state, and is 0 when the window server should run.  --help prints the
/// usage, and so leaves the display alone.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
static void wmBegin(WmState *state) {
  state->exitStatus = 0;
  if ((state->argc > 1) && (strcmp(state->argv[1], "--help") == 0)) {
    printUsage(stdout);
    state->exitStatus = WM_EXIT_HELP;
    return;
  }
  int cells = WM_MAX_CELLS;
  if (!parseOptions(state, &cells)) {
    printUsage(stderr);
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
    if (state->demo) {
      layOut(state, cells);
      if (state->contentByCall) {
        status = preloadRows(state);
      }
    }
    if (status != GFX_OK) {
      fprintf(stderr, "sysnook: %s\n", gfxErrorString(status));
      state->exitStatus = 1;
    }
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
    fprintf(stderr, "sysnook: %s\n", gfxErrorString(state->status));
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
