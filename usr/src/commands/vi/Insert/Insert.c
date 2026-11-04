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

/// @file Insert.c
///
/// @brief Insert mode of the `vi` screen editor:  i, a, I, and A.

#include "vi.h"

/// @fn static uint32_t viSuffixOffset(ViState *state)
///
/// @brief Get the text-file offset of the characters after the window onto
/// the current line.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns the offset.
static uint32_t viSuffixOffset(ViState *state) {
  return textstoreLineOffset(&state->ex.store, state->line)
    + (uint32_t) state->windowStart + (uint32_t) state->windowLength;
}

/// @fn static bool viHasSuffix(ViState *state)
///
/// @brief Find out whether the current line has characters after the window
/// onto it.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns true if it does, false if it doesn't.
static bool viHasSuffix(ViState *state) {
  FILE *text = state->ex.store.text;
  return (fseek(text, (long) viSuffixOffset(state), SEEK_SET) == 0)
    && (fgets(state->buffer, 2, text) != NULL) && (state->buffer[0] != '\n');
}

/// @fn static void viMeasureSuffix(ViState *state)
///
/// @brief Add the width of the characters after the window onto the current
/// line in the store to state->width.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static void viMeasureSuffix(ViState *state) {
  TextStore *store = &state->ex.store;
  int width = state->width;
  bool ended = (fseek(store->text, (long) viSuffixOffset(state), SEEK_SET)
    != 0);
  while (!ended
    && (fgets(state->buffer, TEXTSTORE_BUFFER_SIZE, store->text) != NULL)
  ) {
    for (char *c = state->buffer; (*c != '\0') && !ended; c++) {
      ended = (*c == '\n');
      if (!ended) {
        width += viCharWidth((unsigned char) *c, width);
      }
    }
  }
  state->width = (int16_t) ((width > INT16_MAX) ? INT16_MAX : width);
}

/// @fn static bool viRedrawCurrent(ViState *state, int oldRows)
///
/// @brief Redraw the current line from the start of the window onto it after
/// its text changed, or, if it now takes up a different number of rows, ask
/// the Show overlay to show it.  This is here rather than with the other
/// drawing in vi.h because only insert mode draws a window that has changed.
///
/// @param state The ViState of the running `vi`.
/// @param oldRows The number of rows the line took up before the change.
///
/// @return Returns true if the line was redrawn, false if showing it was
/// requested.
static bool viRedrawCurrent(ViState *state, int oldRows) {
  viMeasure(state);
  if (state->truncated) {
    viMeasureSuffix(state);
  }
  if (viRowsFor(state, state->width) != oldRows) {
    viRequestRows(state, state->line, state->lineRow, VI_REQUEST_SHOW,
      oldRows, 1);
    return false;
  }
  ViDraw draw = {
    state->lineRow + (state->prefixWidth / state->columns),
    state->prefixWidth % state->columns, state->rows - 2, state->prefixWidth
  };
  screenMoveTo(draw.row + 1, draw.column + 1);
  viDrawText(state, &draw, state->text, state->length);
  if (state->truncated) {
    viDrawFile(state, &draw, viSuffixOffset(state));
  }
  if ((draw.row <= draw.lastRow) && (draw.column < state->columns)) {
    screenClearToEndOfLine();
  }
  return true;
}

/// @fn static void viRepeatInsert(ViState *state, bool replace)
///
/// @brief Insert the text the last change inserted, or type it over the line
/// for R, for . to repeat it.  state->targetIndex is its length.
///
/// @param state The ViState of the running `vi`.
/// @param replace Whether to type the text over the line.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viRepeatInsert(ViState *state, bool replace) {
  int length = state->targetIndex;
  int oldRows = viRowsFor(state, state->width);
  int end = replace ? state->index + length : state->length + length;
  if (end < state->length) {
    end = state->length;
  }
  if (end > VI_LINE_MAX - 1) {
    state->ex.store.error = VI_ERROR_TOO_LONG;
    return;
  }
  if (!replace) {
    memmove(&state->text[state->index + length], &state->text[state->index],
      (size_t) (state->length - state->index + 1));
  }
  if ((length > 0)
    && !textstoreReadHeader(&state->ex.store, VI_DOT_TEXT_POSITION,
      &state->text[state->index], (size_t) length)
  ) {
    viRequest(state, state->line, state->lineRow, VI_REQUEST_MOVE);
    return;
  }
  state->index = (int16_t) (state->index + length);
  state->length = (int16_t) end;
  state->text[end] = '\0';
  state->changed = true;
  viRedrawCurrent(state, oldRows);
}

/// @fn void* viRun(void *args)
///
/// @brief Insert text typed until Escape or Return is pressed:  before the
/// cursor (i), after it (a), at the first non-blank (I), or at the end of the
/// line (A), or type it over the line from the cursor if state->argument is
/// R, where Backspace only moves back.  The changed line is left for the Put
/// overlay to commit, and to split for Return, so that this overlay has no
/// code that changes the store.  state->ex.command is set to the key that
/// ended insertion, and, for Escape, state->targetIndex to the index after
/// the last character inserted.  It is set to VI_COMMAND_WINDOW, with a
/// request to show the screen if one is needed, when the window onto the line
/// must move, the screen was resized, or the line now takes up a different
/// number of rows; insert mode carries on when run again with that command.  A key typed
/// after Ctrl-V is inserted as it is.  When . is repeating a change, its text
/// is inserted instead.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  char command = state->ex.command;
  bool replace = (state->argument == 'R');
  const char *mode = replace ? "-- REPLACE --" : "-- INSERT --";

  if ((command == 'a') && (state->length > 0)) {
    state->index++;
  } else if (command == 'A') {
    state->index = state->length;
  } else if (command == 'I') {
    state->index = (int16_t) viFirstNonBlank(state);
  }
  if (command != VI_COMMAND_WINDOW) {
    state->insertStart = state->index;
  }

  if (state->repeating) {
    viRepeatInsert(state, replace);
    state->ex.command = VI_KEY_ESCAPE;
  } else {
    viStatus(state, mode);
  }
  while (!state->repeating && (state->ex.store.error == EX_ERROR_NONE)) {
    if ((state->length >= VI_LINE_MAX - 1)
      || ((state->index == 0) && (state->windowStart > 0))
      || (replace && state->truncated && (state->index == state->length)
        && viHasSuffix(state))
    ) {
      state->ex.command = VI_COMMAND_WINDOW;
      return args;
    }
    viPlaceCursor(state);
    int key = viReadKey(state);
    bool literal = (key == 0x16);
    if (literal) {
      key = viReadKey(state);
    }
    int oldRows = viRowsFor(state, state->width);
    if ((key == VI_KEY_EOF) || ((key == VI_KEY_ESCAPE) && !literal)) {
      state->ex.command = VI_KEY_ESCAPE;
      break;
    } else if (key == VI_KEY_RESIZE) {
      // The old layout may not fit, so start over with this line at the top.
      screenClear();
      viRequest(state, state->line, -1, VI_REQUEST_REDRAW);
      state->ex.command = VI_COMMAND_WINDOW;
      return args;
    } else if (((key == '\r') || (key == '\n')) && !literal) {
      state->ex.command = '\n';
      return args;
    } else if (((key == '\b') || (key == 0x7f)) && !literal) {
      if (state->index == 0) {
        continue;
      } else if (replace || (state->index <= state->insertStart)) {
        state->insertStart = -1;
      }
      if (replace) {
        state->index--;
        continue;
      }
      memmove(&state->text[state->index - 1], &state->text[state->index],
        (size_t) (state->length - state->index + 1));
      state->index--;
      state->length--;
    } else if ((key > 0) && (key <= 0xff)
      && (literal || (key == '\t') || (key >= ' '))
    ) {
      if (replace && (state->index < state->length)) {
        state->text[state->index++] = (char) key;
      } else {
        memmove(&state->text[state->index + 1], &state->text[state->index],
          (size_t) (state->length - state->index + 1));
        state->text[state->index++] = (char) key;
        state->length++;
      }
    } else {
      continue;
    }
    state->changed = true;
    if (!viRedrawCurrent(state, oldRows)) {
      state->ex.command = VI_COMMAND_WINDOW;
      return args;
    }
  }

  state->targetIndex = state->index;
  if (state->index > 0) {
    state->index--;
  }
  viStatus(state, "");
  return args;
}
