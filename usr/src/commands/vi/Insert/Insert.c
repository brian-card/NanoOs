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
    viLoadLine(state, state->line);
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
/// line (A), or type it over the line from the cursor (R), where Backspace
/// only moves back.  The changed line is left for the Put overlay to commit, and to
/// split for Return, so that this overlay has no code that changes the store.
/// state->ex.command is set to the key that ended insertion, and, for
/// Escape, state->targetIndex to the index after the last character
/// inserted.  A key typed after Ctrl-V is inserted as it is.  When . is
/// repeating a change, its text is inserted instead.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  char command = state->ex.command;
  bool replace = (command == 'R');
  const char *mode = replace ? "-- REPLACE --" : "-- INSERT --";

  if (state->truncated) {
    viStatus(state, "Line too long");
    return args;
  } else if ((command == 'a') && (state->length > 0)) {
    state->index++;
  } else if (command == 'A') {
    state->index = state->length;
  } else if (command == 'I') {
    state->index = (int16_t) viFirstNonBlank(state);
  }
  state->insertStart = state->index;

  if (state->repeating) {
    viRepeatInsert(state, replace);
    state->ex.command = VI_KEY_ESCAPE;
  } else {
    viStatus(state, mode);
  }
  while (!state->repeating && (state->ex.store.error == EX_ERROR_NONE)) {
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
      viShowLine(state, state->line, -1, VI_SHOW_REDRAW);
      viStatus(state, mode);
      continue;
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
      } else if (state->length >= VI_LINE_MAX - 1) {
        viStatus(state, "Line too long");
        continue;
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
    viRedrawCurrent(state, oldRows);
  }

  state->targetIndex = state->index;
  if (state->index > 0) {
    state->index--;
  }
  viStatus(state, "");
  return args;
}
