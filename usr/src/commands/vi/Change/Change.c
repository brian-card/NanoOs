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

/// @file Change.c
///
/// @brief The `vi` commands that delete and yank text:  x, dd, and yy.

#include "vi.h"

/// @fn static void viDeleteLine(ViState *state)
///
/// @brief Yank and delete the current line:  dd.  The next line becomes
/// current, or the one before when the last line is deleted.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viDeleteLine(ViState *state) {
  TextStore *store = &state->ex.store;
  int line = state->line;
  if ((line > store->numLines) || !viCommitLine(state)) {
    return;
  }
  state->yank = textstoreLineOffset(store, line);
  textstoreDeleteLines(store, line, line);
  if (store->error != TEXTSTORE_ERROR_NONE) {
    return;
  }
  state->haveYank = true;
  state->ex.modified = true;

  int row = state->lineRow;
  if ((line > store->numLines) && (line > 1)) {
    line--;
    int width = viReadLine(state, line, false);
    if (width < 0) {
      return;
    }
    row -= viRowsFor(state, width);
  }
  state->index = 0;
  viShowLine(state, line, row, true);
}

/// @fn void* viChange(void *args)
///
/// @brief Run x, dd, or yy.  For dd and yy, state->ex.command is d or y.  The
/// current line is committed before returning.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viChange(void *args) {
  ViState *state = (ViState*) args;
  switch (state->ex.command) {
    case 'x': {
      if (state->truncated || (state->length == 0)) {
        break;
      }
      int oldRows = viRowsFor(state, state->width);
      memmove(&state->text[state->index], &state->text[state->index + 1],
        (size_t) (state->length - state->index));
      state->length--;
      state->changed = true;
      viRedrawCurrent(state, oldRows);
      viCommitLine(state);
      break;
    }

    case 'd':
      viDeleteLine(state);
      break;

    case 'y':
      if ((state->line <= state->ex.store.numLines) && viCommitLine(state)) {
        state->yank = textstoreLineOffset(&state->ex.store, state->line);
        state->haveYank = true;
      }
      break;
  }
  return args;
}
