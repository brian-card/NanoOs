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
/// @brief The `vi` work that adds lines:  p and P put copies of yanked lines,
/// o and O open an empty line for insert mode to fill, and Return in insert
/// mode splits the current line.  The current line is also committed here
/// when insert mode ends.

#include "vi.h"

/// @fn static void viOpenLine(ViState *state, bool after)
///
/// @brief Add an empty line after the current line or before it, and make
/// the new line current.
///
/// @param state The ViState of the running `vi`.
/// @param after Whether to add it after the current line.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viOpenLine(ViState *state, bool after) {
  if ((state->ex.store.numLines == 0) && !viAddEmptyLine(state, 0)) {
    return;
  }
  int line = after ? state->line : state->line - 1;
  if (!viAddEmptyLine(state, line)) {
    return;
  }
  int row = after
    ? state->lineRow + viRowsFor(state, state->width) : state->lineRow;
  state->index = 0;
  viRequestRows(state, line + 1, row, VI_REQUEST_SHOW, 0, 1);
}

/// @fn static void viPutLines(ViState *state, bool after, int count)
///
/// @brief Put copies of the yanked lines after the current line or before
/// it, and make the first line put current.
///
/// @param state The ViState of the running `vi`.
/// @param after Whether to put them after the current line.
/// @param count The number of copies.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viPutLines(ViState *state, bool after, int count) {
  TextStore *store = &state->ex.store;
  if (!viBeginChange(state)
    || ((store->numLines == 0) && !viAddEmptyLine(state, 0))
  ) {
    return;
  }
  int line = after ? state->line : state->line - 1;
  int first = line + 1;
  for (int ii = 0; ii < count; ii++) {
    TextStoreChain cursor = state->yank;
    cursor.block = cursor.first;
    cursor.index = 0;
    ViYank yank;
    while (textstoreChainNext(store, &cursor, &yank)) {
      uint32_t offset = store->textEnd;
      if ((textstoreCopyText(store, yank.offset, store->text, true) < 0)
        || !exInsertLine(&state->ex, line, offset)
      ) {
        return;
      }
      line++;
    }
    if (store->error != EX_ERROR_NONE) {
      return;
    }
  }
  state->ex.modified = true;
  int row = after
    ? state->lineRow + viRowsFor(state, state->width) : state->lineRow;
  if (viLoadLine(state, first)) {
    state->index = (int16_t) viFirstNonBlank(state);
    viRequestRows(state, first, row, VI_REQUEST_SHOW, 0, line - first + 1);
  }
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
    || !exInsertLine(&state->ex, state->line, offset)
  ) {
    return false;
  }
  int oldRows = viRowsFor(state, state->width);
  viMeasure(state);
  int rows = viRowsFor(state, state->width);
  state->index = 0;
  viRequestRows(state, state->line + 1, state->lineRow + rows,
    VI_REQUEST_SPLIT, oldRows - rows, 1);
  return true;
}

/// @fn void* viRun(void *args)
///
/// @brief Run p or P for yanked lines, o, or O, or, for the key that ended insert mode, commit
/// the current line and split it for Return.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  char command = state->ex.command;
  if (command == '\n') {
    viSplitLine(state);
    return args;
  } else if (command == VI_KEY_ESCAPE) {
    viCommitLine(state);
    return args;
  }
  bool after = (command == 'p') || (command == 'o');
  int count = (state->count > 0) ? state->count : 1;
  if ((command == 'o') || (command == 'O')) {
    viOpenLine(state, after);
  } else if (state->yankKind == VI_YANK_LINES) {
    viPutLines(state, after, count);
  }
  return args;
}
