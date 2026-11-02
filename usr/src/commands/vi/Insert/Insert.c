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

/// @fn void* viInsert(void *args)
///
/// @brief Insert text typed until Escape or Return is pressed:  before the
/// cursor (i), after it (a), at the first non-blank (I), or at the end of the
/// line (A).  The changed line is left for the Put overlay to commit, and to
/// split for Return, so that this overlay has no code that changes the store.
/// state->ex.command is set to the key that ended insertion.  A key typed
/// after Ctrl-V is inserted as it is.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viInsert(void *args) {
  ViState *state = (ViState*) args;
  char command = state->ex.command;
  viApplyRequest(state);

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

  viStatus(state, "-- INSERT --");
  while (state->ex.store.error == EX_ERROR_NONE) {
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
      viShowLine(state, state->line, -1, true);
      viStatus(state, "-- INSERT --");
      continue;
    } else if (((key == '\r') || (key == '\n')) && !literal) {
      state->ex.command = '\n';
      return args;
    } else if (((key == '\b') || (key == 0x7f)) && !literal) {
      if (state->index == 0) {
        continue;
      }
      memmove(&state->text[state->index - 1], &state->text[state->index],
        (size_t) (state->length - state->index + 1));
      state->index--;
      state->length--;
    } else if ((key > 0) && (key <= 0xff)
      && (literal || (key == '\t') || (key >= ' '))
    ) {
      if (state->length >= VI_LINE_MAX - 1) {
        viStatus(state, "Line too long");
        continue;
      }
      memmove(&state->text[state->index + 1], &state->text[state->index],
        (size_t) (state->length - state->index + 1));
      state->text[state->index++] = (char) key;
      state->length++;
    } else {
      continue;
    }
    state->changed = true;
    viRedrawCurrent(state, oldRows);
  }

  if (state->index > 0) {
    state->index--;
  }
  viStatus(state, "");
  return args;
}
