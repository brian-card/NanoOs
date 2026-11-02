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

/// @file Put.c
///
/// @brief The `vi` work that adds a line:  p and P put a copy of the yanked
/// line, o and O open an empty one for insert mode to fill, and Return in
/// insert mode splits the current line.  The current line is also committed
/// here when insert mode ends.

#include "vi.h"

/// @fn static void viPutLine(ViState *state, bool after, bool empty)
///
/// @brief Put a copy of the yanked line, or an empty line, after the current
/// line or before it, and make the new line current.
///
/// @param state The ViState of the running `vi`.
/// @param after Whether to put it after the current line.
/// @param empty Whether to put an empty line rather than the yanked one.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viPutLine(ViState *state, bool after, bool empty) {
  TextStore *store = &state->ex.store;
  if ((!empty && !state->haveYank)
    || ((store->numLines == 0) && !viAddEmptyLine(state, 0))
  ) {
    return;
  }
  int line = after ? state->line : state->line - 1;
  if (empty) {
    if (!viAddEmptyLine(state, line)) {
      return;
    }
  } else {
    uint32_t offset = store->textEnd;
    if ((textstoreCopyText(store, state->yank, store->text, true) < 0)
      || !textstoreInsertLine(store, line, offset)
    ) {
      return;
    }
    state->ex.modified = true;
  }
  int row = after
    ? state->lineRow + viRowsFor(state, state->width) : state->lineRow;
  state->index = 0;
  viShowLine(state, line + 1, row, true);
}

/// @fn static bool viSplitLine(ViState *state)
///
/// @brief Commit the current line up to the cursor, make the text after the
/// cursor a new line below it, and make that line current.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static bool viSplitLine(ViState *state) {
  TextStore *store = &state->ex.store;
  uint32_t offset = store->textEnd;
  if (!textstoreAppendText(store, &state->text[state->index])
    || !textstoreAppendText(store, "\n")
  ) {
    return false;
  }
  state->text[state->index] = '\0';
  state->length = state->index;
  state->changed = true;
  if (!viCommitLine(state)
    || !textstoreInsertLine(store, state->line, offset)
  ) {
    return false;
  }
  viMeasure(state);
  viDrawLine(state, state->line, state->lineRow);
  state->index = 0;
  return viShowLine(state, state->line + 1,
    state->lineRow + viRowsFor(state, state->width), true);
}

/// @fn void* viPut(void *args)
///
/// @brief Run p, P, o, or O, or, for the key that ended insert mode, commit
/// the current line and split it for Return.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viPut(void *args) {
  ViState *state = (ViState*) args;
  char command = state->ex.command;
  if (command == '\n') {
    viSplitLine(state);
    return args;
  } else if (command == VI_KEY_ESCAPE) {
    viCommitLine(state);
    return args;
  }
  viPutLine(state, (command == 'p') || (command == 'o'),
    (command == 'o') || (command == 'O'));
  return args;
}
