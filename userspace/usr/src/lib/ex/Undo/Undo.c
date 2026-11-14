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

/// @file Undo.c
///
/// @brief Undoing and redoing changes from the undo history.

#include "Ex.h"

/// @fn static void exApply(ExState *state, ExChange *change, bool undo)
///
/// @brief Make the change a record describes, or its opposite.
///
/// @param state The ExState of the running editor.
/// @param change The record.
/// @param undo Whether to make the opposite change.
///
/// @return This function returns no value.  On failure, state->store.error
/// is set.
static void exApply(ExState *state, ExChange *change, bool undo) {
  if ((change->kind == EX_CHANGE_INSERT) != undo) {
    textstoreInsertLine(&state->store, change->line - 1, change->offset);
  } else {
    textstoreDeleteLines(&state->store, change->line, change->line);
  }
}

/// @fn void* exUndo(void *args)
///
/// @brief Undo the change before the undo history's position (state->command
/// u) or redo the one after it (state->command r).  The current line and
/// column become the ones from before the change.
///
/// @param args A pointer to the ExState of the running editor, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in the
/// ExState's store.error member.
void* exUndo(void *args) {
  ExState *state = (ExState*) args;
  TextStore *store = &state->store;
  TextStoreChain *history = &state->history;
  ExChange change;
  ExChange group;

  if (state->command == 'u') {
    group.kind = EX_CHANGE_INSERT;
    while ((group.kind != EX_CHANGE_GROUP)
      && textstoreChainPrevious(store, history, &group)
    ) {
      if (group.kind != EX_CHANGE_GROUP) {
        exApply(state, &group, true);
      }
    }
    if ((group.kind != EX_CHANGE_GROUP) && (store->error == EX_ERROR_NONE)) {
      store->error = EX_ERROR_OLDEST;
    }
  } else if (!textstoreChainNext(store, history, &group)) {
    if (store->error == EX_ERROR_NONE) {
      store->error = EX_ERROR_NEWEST;
    }
  } else {
    while (textstoreChainNext(store, history, &change)) {
      if (change.kind == EX_CHANGE_GROUP) {
        textstoreChainPrevious(store, history, &change);
        break;
      }
      exApply(state, &change, false);
    }
  }
  if (store->error != EX_ERROR_NONE) {
    return args;
  }

  state->current = (group.line < store->numLines)
    ? (int) group.line : store->numLines;
  state->column = (int16_t) group.column;
  state->modified = true;
  return args;
}
