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
/// @brief The key loop and normal-mode movement of the `vi` screen editor.
/// The current line is never left changed here:  the overlays that change it
/// commit it before they return, or the Window overlay does for a window onto
/// a long line, which keeps the code that does so out of this overlay.

#include <stdio.h>
#include "vi.h"
#include "NanoOsUtils.h"

/// @fn static void viRunOverlay(ViState *state, const char *overlay,
///   const char *function)
///
/// @brief Run a function in another of `vi`'s overlays.
///
/// @param state The ViState of the running `vi`.
/// @param overlay The name of the overlay.
/// @param function The name of the function to run.
///
/// @return This function returns no value.  If the overlay can't be loaded,
/// state->ex.store.error is set.
static void viRunOverlay(
  ViState *state, const char *overlay, const char *function
) {
  if (callOverlayFunction(OVERLAY_SAME_NAMESPACE, overlay, function, state)
    == NULL
  ) {
    state->ex.store.error = EX_ERROR_MEMORY;
  }
}

/// @fn static void viShow(ViState *state, int line, int row,
///   uint8_t request)
///
/// @brief Have the Show overlay show a line at a screen row, which is all the
/// drawing of the screen this overlay asks for.  Keeping the drawing code
/// there leaves room here for the key loop.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line to make current.
/// @param row The screen row to show it at, or -1 for above the screen.
/// @param request VI_REQUEST_MOVE to scroll to it, or VI_REQUEST_REDRAW to
///   redraw the screen as well.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viShow(ViState *state, int line, int row, uint8_t request) {
  viRequest(state, line, row, request);
  viRunOverlay(state, "Show", "viRun");
}

/// @fn static void viMoveTo(ViState *state, int line, int row)
///
/// @brief Make a line the current line at a screen row, having the Show
/// overlay scroll if it doesn't fit there.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line.
/// @param row The screen row to show it at, or -1 for above the screen.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viMoveTo(ViState *state, int line, int row) {
  if (!viShowLine(state, line, row, VI_SHOW_FIT)
    && (state->ex.store.error == EX_ERROR_NONE)
  ) {
    viShow(state, line, row, VI_REQUEST_MOVE);
  }
}

/// @fn static void viCallOverlay(ViState *state, const char *overlay,
///   const char *function)
///
/// @brief Run a function in another of `vi`'s overlays, put a window onto a
/// long line that it left changed back in the store, then show what it asked
/// to be shown:  a move that doesn't scroll here, and anything else by the
/// Show overlay.
///
/// @param state The ViState of the running `vi`.
/// @param overlay The name of the overlay.
/// @param function The name of the function to run.
///
/// @return This function returns no value.  If an overlay can't be loaded,
/// state->ex.store.error is set.
static void viCallOverlay(
  ViState *state, const char *overlay, const char *function
) {
  state->request = VI_REQUEST_NONE;
  viRunOverlay(state, overlay, function);
  if (state->changed && state->truncated) {
    viRunOverlay(state, "Window", "viCommit");
  }
  uint8_t request = state->request;
  if (request == VI_REQUEST_MOVE) {
    viMoveTo(state, state->requestLine, state->requestRow);
  } else if (request != VI_REQUEST_NONE) {
    viRunOverlay(state, "Show", "viRun");
  }
}

/// @var viStepOverlays
///
/// @brief The overlay of each ViStep after VI_STEP_NONE.  Each one's function
/// is viRun.
static const char *const viStepOverlays[] = {
  "Motion", "Operator", "Change", "Put", "Insert", "Command", "History",
  "Window", "Range",
};

/// @fn static void viRunCommand(ViState *state, int key)
///
/// @brief Run a normal-mode command in the Normal overlay, and each overlay it
/// chooses as a step, until the command is done.
///
/// @param state The ViState of the running `vi`.
/// @param key The key that starts the command.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viRunCommand(ViState *state, int key) {
  state->ex.command = (char) key;
  state->step = VI_STEP_NONE;
  while (true) {
    viCallOverlay(state, "Normal", "viNormal");
    if ((state->step == VI_STEP_NONE)
      || (state->ex.store.error != EX_ERROR_NONE)
    ) {
      break;
    }
    viCallOverlay(state, viStepOverlays[state->step - 1], "viRun");
  }
}

/// @fn static void viRunKey(ViState *state, int key)
///
/// @brief Run a normal-mode command.  The commonest movements are run here;
/// every other command is run by the Normal overlay.
///
/// @param state The ViState of the running `vi`.
/// @param key The key that starts the command.
///
/// @return This function returns no value.
static void viRunKey(ViState *state, int key) {
  if ((key == VI_KEY_LEFT) || (key == VI_KEY_RIGHT)) {
    key = (key == VI_KEY_LEFT) ? 'h' : 'l';
  }
  if (state->truncated
    && (((key == 'h') && (state->index == 0))
      || ((key == 'l') && (state->index + 1 >= state->length))
      || (key == '0') || (key == '$'))
  ) {
    // The window onto the line must move, which the Motion overlay does.
    viRunCommand(state, key);
    key = VI_KEY_NONE;
  }
  switch (key) {
    case 'h':
      if (state->index > 0) {
        state->index--;
      }
      break;

    case 'l':
      if (state->index + 1 < state->length) {
        state->index++;
      }
      break;

    case 'j':
    case VI_KEY_DOWN:
      if (state->line < state->ex.store.numLines) {
        viMoveTo(state, state->line + 1,
          state->lineRow + viRowsFor(state, state->width));
      }
      break;

    case 'k':
    case VI_KEY_UP:
      if (state->line > 1) {
        int width = viReadLine(state, state->line - 1, false);
        if (width >= 0) {
          viMoveTo(state, state->line - 1,
            state->lineRow - viRowsFor(state, width));
        }
      }
      break;

    case '0':
      state->index = 0;
      break;

    case '$':
      state->index = state->length;
      break;

    case VI_KEY_RESIZE:
      // The old layout may not fit, so start over with this line at the top.
      screenClear();
      viShow(state, state->line, -1, VI_REQUEST_REDRAW);
      break;

    case 0x0c:
      screenClear();
      viShow(state, state->line, state->lineRow, VI_REQUEST_REDRAW);
      break;

    default:
      if (key < VI_KEY_NONE) {
        viRunCommand(state, key);
      }
      break;
  }
  if (state->ex.store.error != EX_ERROR_NONE) {
    viCallOverlay(state, "Session", "viReport");
  }
}

/// @fn static __attribute__((noinline)) void viRuler(ViState *state)
///
/// @brief Show the cursor's line and column at the right of the status line,
/// as vim's ruler does:  the line, the index of the character plus 1 (0 on an
/// empty line), and, if it differs, the last screen column the character
/// takes up.  It is kept out of line so that its buffer isn't on the stack
/// under the overlays main runs.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static __attribute__((noinline)) void viRuler(ViState *state) {
  int start = state->columns - 18;
  if (start < (state->columns + 1) / 2) {
    start = (state->columns + 1) / 2;
  }
  // The last column is left alone, since writing it can scroll the screen.
  int room = state->columns - 1 - start;
  if (room <= 0) {
    return;
  }
  int column = viCursorColumn(state);
  int screenColumn = column + ((state->index < state->length)
    ? viCharWidth((unsigned char) state->text[state->index], column) : 1);
  int index = (state->length > 0)
    ? state->windowStart + state->index + 1 : 0;
  char ruler[24];
  int length = snprintf(ruler, sizeof(ruler), "%d,%d",
    (state->ex.store.numLines > 0) ? state->line : 0, index);
  if (screenColumn != index) {
    snprintf(&ruler[length], sizeof(ruler) - (size_t) length, "-%d",
      screenColumn);
  }
  ruler[room] = '\0';
  screenMoveTo(state->rows, start + 1);
  fputs(ruler, stdout);
  screenClearToEndOfLine();
}

/// @fn static void printUsage(FILE *stream)
///
/// @brief Print the usage summary for vi.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: vi [file]\n"
    "\n"
    "Edit a file on the full screen.\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  } else if (argc > 2) {
    printUsage(stderr);
    return 1;
  }

  ViState *state = (ViState*) calloc(1, sizeof(ViState));
  if (state == NULL) {
    fputs("vi: out of memory\n", stderr);
    return 1;
  }
  state->ex.store.buffer = state->buffer;
  strcpy(state->ex.name, "vi");
  state->ex.quiet = true;
  state->ex.params = (argc > 1) ? argv[1] : NULL;

  viCallOverlay(state, "Session", "viStart");
  int returnValue = (state->ex.store.text == NULL) ? 1 : 0;
  state->quit = (returnValue != 0);
  state->top = 1;
  if (!state->quit) {
    viShow(state, 1, 0, VI_REQUEST_REDRAW);
  }

  while (!state->quit) {
    if ((state->index >= state->length) && (state->index > 0)) {
      state->index = (state->length > 0) ? state->length - 1 : 0;
    }
    viRuler(state);
    viPlaceCursor(state);
    int key = viReadKey(state);
    if (key == VI_KEY_EOF) {
      break;
    }
    viRunKey(state, key);
  }

  viCallOverlay(state, "Session", "viFinish");
  free(state->ex.filename);
  free(state);
  return returnValue;
}
