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
/// @brief The `vi` work done rarely enough to be kept out of the main
/// overlay:  loading the file at start-up, the ex command line, and error
/// messages.

#include "vi.h"

/// @var viErrorMessages
///
/// @brief The text of each ExError.
static const char *const viErrorMessages[NUM_EX_ERRORS] = {
  "",
  "Scratch file error",
  "Out of memory",
  "Cannot write file",
  "Invalid address",
  "Not an editor command",
  "Trailing characters",
  "No file name",
  "Cannot open file",
  "Cannot open file for writing",
  "No write since last change (add ! to override)",
  "Mark not set",
  "Invalid destination",
};

/// @fn static void viShowError(ViState *state)
///
/// @brief Show the message for state->ex.store.error, if any, on the status
/// line and clear the error.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static void viShowError(ViState *state) {
  uint8_t error = state->ex.store.error;
  if (error != EX_ERROR_NONE) {
    viStatus(state, (error < NUM_EX_ERRORS) ? viErrorMessages[error] : "Error");
    state->ex.store.error = EX_ERROR_NONE;
  }
}

/// @fn static void viFileStatus(ViState *state, const char *name,
///   int numLines, const char *suffix)
///
/// @brief Show a file's name, its number of lines, and the last number of
/// bytes read or written on the status line.
///
/// @param state The ViState of the running `vi`.
/// @param name The file's name.
/// @param numLines The number of lines.
/// @param suffix The text to end the message with.
///
/// @return This function returns no value.
static void viFileStatus(
  ViState *state, const char *name, int numLines, const char *suffix
) {
  screenMoveTo(state->rows, 1);
  printf("\"%s\" %dL, %ldC%s", name, numLines, state->ex.byteCount, suffix);
  screenClearToEndOfLine();
}

/// @fn static void viRunCommand(ViState *state, char *text)
///
/// @brief Run an ex command line:  :w, :wq, :q, or :q!.
///
/// @param state The ViState of the running `vi`.
/// @param text The command line, without the colon.
///
/// @return This function returns no value.
static void viRunCommand(ViState *state, char *text) {
  ExState *ex = &state->ex;
  char *params = exParseCommandLine(ex, text);
  if (params == NULL) {
    viShowError(state);
    return;
  }
  while (*params == ' ') {
    params++;
  }
  ex->params = (*params != '\0') ? params : NULL;

  if (ex->command == 'w') {
    if (callOverlayFunction(EX_LIBRARY_PATH, "Write", "exWrite", ex) == NULL) {
      ex->store.error = EX_ERROR_MEMORY;
    }
    if (ex->store.error != EX_ERROR_NONE) {
      viShowError(state);
      return;
    }
    viFileStatus(state, (ex->params != NULL) ? ex->params : ex->filename,
      ex->addr2 - ex->addr1 + 1, " written");
    state->quit = (ex->command == 'q');
  } else if (ex->command == 'q') {
    if ((ex->params != NULL) && (strcmp(ex->params, "!") != 0)) {
      ex->store.error = EX_ERROR_SUFFIX;
      viShowError(state);
    } else if ((ex->params == NULL) && ex->modified) {
      ex->store.error = EX_ERROR_MODIFIED;
      viShowError(state);
    } else {
      state->quit = true;
    }
  } else {
    ex->store.error = EX_ERROR_COMMAND;
    viShowError(state);
  }
}

/// @fn static void viCommandLine(ViState *state)
///
/// @brief Read an ex command line on the status line and run it.  The current
/// line's buffer holds the command, so the line is reloaded after.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static void viCommandLine(ViState *state) {
  char *text = state->text;
  int length = 0;
  viStatus(state, ":");
  while (length >= 0) {
    int key = viReadKey(state);
    if ((key == '\r') || (key == '\n')) {
      break;
    } else if ((key == VI_KEY_ESCAPE) || (key == VI_KEY_EOF)) {
      length = -1;
    } else if ((key == '\b') || (key == 0x7f)) {
      length--;
      fputs("\b \b", stdout);
    } else if ((key >= ' ') && (key < 0x7f) && (length < VI_LINE_MAX - 1)) {
      text[length] = (char) key;
      fwrite(&text[length++], 1, 1, stdout);
    }
  }

  if (length < 0) {
    viStatus(state, "");
  } else {
    text[length] = '\0';
    viRunCommand(state, text);
  }
  if (!state->quit) {
    viLoadLine(state, state->line);
  }
}

/// @fn void* viStart(void *args)
///
/// @brief Put the terminal in raw mode with resize reports on, get its size,
/// create the text file, read in the file named on the command line, clear
/// the screen, and show the file's name and size, or that it is new, on the
/// status line.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///   Its ex.params member is the file named on the command line, or NULL, and
///   its terminal member is where to save the terminal's settings.
///
/// @return Returns the args pointer provided.  If the text file can't be
/// created, state->ex.store.text is left NULL.
void* viStart(void *args) {
  ViState *state = (ViState*) args;
  if (tcgetattr(STDIN_FILENO, state->terminal) == 0) {
    struct termios raw = *state->terminal;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_lflag |= RESIZEREPORTS;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  } else {
    state->terminal = NULL;
  }
  screenGetSize(&state->rows, &state->columns);

  if (callOverlayFunction(EX_LIBRARY_PATH, "File", "exStart", &state->ex)
    == NULL
  ) {
    fputs("vi: out of memory\n", stderr);
  }
  if (state->ex.store.text == NULL) {
    return args;
  }

  screenClear();
  if (state->ex.store.error == EX_ERROR_OPEN_INPUT) {
    state->ex.store.error = EX_ERROR_NONE;
    screenMoveTo(state->rows, 1);
    printf("\"%s\" [New File]", state->ex.filename);
  } else if (state->ex.filename != NULL) {
    viFileStatus(state, state->ex.filename, state->ex.store.numLines, "");
  }
  viShowError(state);
  return args;
}

/// @fn void* viCommand(void *args)
///
/// @brief Read and run an ex command line.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.
void* viCommand(void *args) {
  viCommandLine((ViState*) args);
  return args;
}

/// @fn void* viReport(void *args)
///
/// @brief Show the message for state->ex.store.error on the status line and
/// clear the error.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.
void* viReport(void *args) {
  viShowError((ViState*) args);
  return args;
}

/// @fn void* viFinish(void *args)
///
/// @brief Clear the status line, restore the terminal's settings, and remove
/// the text file.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.
void* viFinish(void *args) {
  ViState *state = (ViState*) args;
  screenMoveTo(state->rows, 1);
  screenClearToEndOfLine();
  if (state->terminal != NULL) {
    tcsetattr(STDIN_FILENO, TCSANOW, state->terminal);
  }
  if (state->ex.store.text != NULL) {
    textstoreClose(&state->ex.store);
  }
  return args;
}
