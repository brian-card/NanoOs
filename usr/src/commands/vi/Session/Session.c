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

/// @file Session.c
///
/// @brief The `vi` work done once or rarely enough to be kept out of the
/// other overlays:  start-up, error messages, and exit.

#include "vi.h"

/// @var viErrorMessages
///
/// @brief The text of each ExError and ViError.
static const char *const viErrorMessages[NUM_VI_ERRORS] = {
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
  "Already at oldest change",
  "Already at newest change",
  "",
  "Pattern not found",
  "No previous search pattern",
  "Line too long",
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
    viStatus(state, (error < NUM_VI_ERRORS) ? viErrorMessages[error] : "Error");
    state->ex.store.error = EX_ERROR_NONE;
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
  int rows = 0;
  int columns = 0;
  screenGetSize(&rows, &columns);
  state->rows = (int16_t) rows;
  state->columns = (int16_t) columns;
  textstoreChainInit(&state->yank);

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
