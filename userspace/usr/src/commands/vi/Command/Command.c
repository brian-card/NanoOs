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

/// @file Command.c
///
/// @brief The ex command line and search prompts of the `vi` screen editor.

#include "vi.h"

/// @fn static void viCallEx(ViState *state, const char *overlay,
///   const char *function)
///
/// @brief Run a function in an overlay of the ex library.
///
/// @param state The ViState of the running `vi`.
/// @param overlay The name of the overlay.
/// @param function The name of the function to run.
///
/// @return This function returns no value.  If the overlay can't be loaded,
/// state->ex.store.error is set.
static void viCallEx(ViState *state, const char *overlay,
  const char *function
) {
  if (callOverlayFunction(EX_LIBRARY_PATH, overlay, function, &state->ex)
    == NULL
  ) {
    state->ex.store.error = EX_ERROR_MEMORY;
  }
}

/// @fn static void viWrite(ViState *state)
///
/// @brief Run :w, :wq, or :x, and show what was written.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viWrite(ViState *state) {
  ExState *ex = &state->ex;
  bool quit = (ex->command == 'x');
  if (quit && !ex->modified && (ex->params == NULL)) {
    state->quit = true;
    return;
  }
  ex->command = 'w';
  viCallEx(state, "Write", "exWrite");
  if (ex->store.error != EX_ERROR_NONE) {
    return;
  }
  viFileStatus(state, (ex->params != NULL) ? ex->params : ex->filename,
    ex->addr2 - ex->addr1 + 1, " written");
  state->quit = quit || (ex->command == 'q');
}

/// @fn static void viRunCommand(ViState *state, char *text)
///
/// @brief Run an ex command line.  A command that changes the buffer asks
/// for the screen to be redrawn.
///
/// @param state The ViState of the running `vi`.
/// @param text The command line, without the colon.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viRunCommand(ViState *state, char *text) {
  ExState *ex = &state->ex;
  char *params = exParseCommandLine(ex, text);
  if (params == NULL) {
    return;
  }
  bool force = (*params == '!');
  if (force) {
    params++;
  }
  while (*params == ' ') {
    params++;
  }
  ex->params = (*params != '\0') ? params : NULL;

  int line = 0;
  switch (ex->command) {
    case '\0':
      if (ex->numAddresses > 0) {
        line = (ex->addr2 > 0) ? ex->addr2 : 1;
      }
      break;

    case 'w':
    case 'x':
      viWrite(state);
      break;

    case 'q':
      if (ex->params != NULL) {
        ex->store.error = EX_ERROR_SUFFIX;
      } else if (ex->modified && !force) {
        ex->store.error = EX_ERROR_MODIFIED;
      } else {
        state->quit = true;
      }
      break;

    case 'e':
      if (ex->modified && !force) {
        ex->store.error = EX_ERROR_MODIFIED;
        break;
      }
      ex->command = 'E';
      viCallEx(state, "File", "exFile");
      if ((ex->store.error != EX_ERROR_NO_FILENAME)
        && (ex->store.error != EX_ERROR_OPEN_INPUT)
      ) {
        textstoreChainInit(&state->yank);
        state->yankKind = VI_YANK_NONE;
        state->top = 1;
        line = 1;
      }
      if ((ex->store.error == EX_ERROR_NONE) && (ex->filename != NULL)) {
        viFileStatus(state, ex->filename, ex->store.numLines, "");
      }
      break;

    case 'r':
      if (ex->numAddresses == 0) {
        ex->numAddresses = 1;
      }
      viCallEx(state, "File", "exFile");
      line = (ex->current > 0) ? ex->current : 1;
      break;

    case 'm':
    case 't': {
      char *destination = ex->params;
      if ((destination == NULL)
        || !exParseAddress(ex, &destination, &ex->destination)
        || (*destination != '\0')
      ) {
        if (ex->store.error == EX_ERROR_NONE) {
          ex->store.error = EX_ERROR_DESTINATION;
        }
        break;
      }
    }
      // fall through
    case 'k':
      viCallEx(state, "Move", "exMove");
      line = (ex->current > 0) ? ex->current : 1;
      break;

    case 'd': {
      int first = ex->addr1;
      int row = viRowOf(state, first);
      int oldRows = viRowsOfLines(state, first, ex->addr2, row);
      viCallEx(state, "Edit", "exEdit");
      if (ex->store.error == EX_ERROR_NONE) {
        viRequestDeleted(state, first, row, oldRows);
      } else {
        line = (ex->current > 0) ? ex->current : 1;
      }
      break;
    }

    case 'j':
      viCallEx(state, "Edit", "exEdit");
      line = (ex->current > 0) ? ex->current : 1;
      break;

    default:
      ex->store.error = EX_ERROR_COMMAND;
      break;
  }

  if ((line > 0) && (ex->store.text != NULL)) {
    int numLines = (ex->store.numLines > 0) ? ex->store.numLines : 1;
    line = (line > numLines) ? numLines : line;
    if (state->top > numLines) {
      state->top = 1;
    }
    if (viLoadLine(state, line)) {
      state->index = (int16_t) viFirstNonBlank(state);
      viRequest(state, line, viRowOf(state, line), VI_REQUEST_REDRAW);
    }
  }
}

/// @fn static void viSetPattern(ViState *state, char *text, int length)
///
/// @brief Make what was typed at a search prompt the last search pattern, or
/// search for the last one again in the prompt's direction if nothing was.
///
/// @param state The ViState of the running `vi`.
/// @param text The prompt character, / or ?, and what was typed after it.
/// @param length The number of characters typed after the prompt.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viSetPattern(ViState *state, char *text, int length) {
  TextStore *store = &state->ex.store;
  if (length == 0) {
    char direction = text[0];
    if (!textstoreReadHeader(store, VI_PATTERN_POSITION, text,
        VI_PATTERN_SIZE)
    ) {
      return;
    } else if (text[0] == '\0') {
      store->error = VI_ERROR_NO_PATTERN;
      return;
    }
    text[0] = direction;
  } else if (length > VI_PATTERN_SIZE - 2) {
    text[VI_PATTERN_SIZE - 1] = '\0';
  }
  textstoreWriteHeader(store, VI_PATTERN_POSITION, text, VI_PATTERN_SIZE);
}

/// @fn static void viCommandLine(ViState *state)
///
/// @brief Read an ex command line or a search pattern on the status line,
/// after the prompt in state->ex.command, and run it or store it.  The
/// current line's buffer holds what is typed, so the line is reloaded after.
/// A prompt left with Escape, or by erasing it, sets VI_ERROR_MOTION so that
/// no search follows.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viCommandLine(ViState *state) {
  char *text = state->text;
  int length = 0;
  text[0] = state->ex.command;
  text[1] = '\0';
  viStatus(state, text);
  while (length >= 0) {
    int key = viReadKey(state);
    if ((key == '\r') || (key == '\n')) {
      break;
    } else if ((key == VI_KEY_ESCAPE) || (key == VI_KEY_EOF)) {
      length = -1;
    } else if ((key == '\b') || (key == 0x7f)) {
      length--;
      fputs("\b \b", stdout);
    } else if ((key >= ' ') && (key < 0x7f) && (length < VI_LINE_MAX - 2)) {
      text[++length] = (char) key;
      fwrite(&text[length], 1, 1, stdout);
    }
  }

  if (length < 0) {
    viStatus(state, "");
    state->ex.store.error = VI_ERROR_MOTION;
  } else {
    text[length + 1] = '\0';
    if (text[0] == ':') {
      viStatus(state, "");
      viRunCommand(state, &text[1]);
    } else {
      viSetPattern(state, text, length);
    }
  }
  if (!state->quit && (state->request == VI_REQUEST_NONE)) {
    viLoadLine(state, state->line);
  }
}

/// @fn void* viRun(void *args)
///
/// @brief Read and run an ex command line or a search prompt, or, for ZZ,
/// which state->ex.command gives as x, write the file if it has changed and
/// quit.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  state->request = VI_REQUEST_NONE;
  if (state->ex.command == 'x') {
    state->ex.params = NULL;
    state->ex.numAddresses = 0;
    viWrite(state);
  } else {
    viCommandLine(state);
  }
  return args;
}
