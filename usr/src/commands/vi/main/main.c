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
/// commit it before they return, which keeps the code that does so out of
/// this overlay.

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

/// @fn static void viCallOverlay(ViState *state, const char *overlay,
///   const char *function)
///
/// @brief Run a function in another of `vi`'s overlays, then show what it
/// asked to be shown:  lines that replaced others by the Show overlay, and a
/// move or a redraw here.
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
  uint8_t request = state->request;
  if ((request == VI_REQUEST_SHOW) || (request == VI_REQUEST_SPLIT)) {
    viRunOverlay(state, "Show", "viRun");
  } else if ((request != VI_REQUEST_NONE)
    && viShowLine(state, state->requestLine, state->requestRow, false)
    && (request == VI_REQUEST_REDRAW)
  ) {
    viDrawFrom(state, 0, state->top);
  }
}

/// @var viStepOverlays
///
/// @brief The overlay of each ViStep after VI_STEP_NONE.  Each one's function
/// is viRun.
static const char *const viStepOverlays[] = {
  "Motion", "Operator", "Change", "Put", "Insert", "Command", "History",
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
  switch (key) {
    case 'h':
    case VI_KEY_LEFT:
      if (state->index > 0) {
        state->index--;
      }
      break;

    case 'l':
    case VI_KEY_RIGHT:
      if (state->index + 1 < state->length) {
        state->index++;
      }
      break;

    case 'j':
    case VI_KEY_DOWN:
      if (state->line < state->ex.store.numLines) {
        viShowLine(state, state->line + 1,
          state->lineRow + viRowsFor(state, state->width), false);
      }
      break;

    case 'k':
    case VI_KEY_UP:
      if (state->line > 1) {
        int width = viReadLine(state, state->line - 1, false);
        if (width >= 0) {
          viShowLine(state, state->line - 1,
            state->lineRow - viRowsFor(state, width), false);
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
      viShowLine(state, state->line, -1, true);
      break;

    case 0x0c:
      screenClear();
      viDrawFrom(state, 0, state->top);
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
    viShowLine(state, 1, 0, true);
  }

  while (!state->quit) {
    if ((state->index >= state->length) && (state->index > 0)) {
      state->index = (state->length > 0) ? state->length - 1 : 0;
    }
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
