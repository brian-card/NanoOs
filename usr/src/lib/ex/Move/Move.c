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

/// @file Move.c
///
/// @brief The editing commands that move or copy lines or mark them:  k, m,
/// and t.

#include "Ex.h"

/// @fn static void exMoveLines(ExState *state)
///
/// @brief Move the lines from state->addr1 to state->addr2 to after
/// state->destination.
///
/// @param state The ExState of the running editor.
///
/// @return This function returns no value.
static void exMoveLines(ExState *state) {
  if ((state->destination >= state->addr1)
    && (state->destination < state->addr2)
  ) {
    state->store.error = EX_ERROR_DESTINATION;
    return;
  }

  textstoreMoveLines(&state->store,
    state->addr1, state->addr2, state->destination);
  state->current = (state->destination < state->addr1)
    ? state->destination + (state->addr2 - state->addr1 + 1)
    : state->destination;
  state->modified = true;
}

/// @fn static void exCopyLines(ExState *state)
///
/// @brief Copy the lines from state->addr1 to state->addr2 to after
/// state->destination.  Lines are never changed in place, so a copy shares
/// its text with the original.
///
/// @param state The ExState of the running editor.
///
/// @return This function returns no value.
static void exCopyLines(ExState *state) {
  int count = state->addr2 - state->addr1 + 1;
  for (int ii = 0; ii < count; ii++) {
    int source = state->addr1 + ii;
    if (source > state->destination) {
      source += ii;
    }
    if (!textstoreInsertLine(&state->store, state->destination + ii,
      textstoreLineOffset(&state->store, source))
    ) {
      if (ii > 0) {
        state->modified = true;
      }
      return;
    }
  }

  state->modified = true;
  state->current = state->destination + count;
}

/// @fn static void exMarkLine(ExState *state)
///
/// @brief Mark the line state->addr2 with the letter in state->params:  the k
/// command.
///
/// @param state The ExState of the running editor.
///
/// @return This function returns no value.
static void exMarkLine(ExState *state) {
  if ((state->params == NULL) || (state->params[0] < 'a')
    || (state->params[0] > 'z') || (state->params[1] != '\0')
  ) {
    state->store.error = EX_ERROR_MARK;
    return;
  }
  if (state->addr2 < 1) {
    state->store.error = EX_ERROR_ADDRESS;
    return;
  }

  uint32_t offset = textstoreLineOffset(&state->store, state->addr2);
  if ((state->store.error == EX_ERROR_NONE)
    && textstoreWriteHeader(&state->store, EX_MARK_POSITION(state->params[0]),
      &offset, sizeof(offset))
  ) {
    state->marks |= ((uint32_t) 1) << (state->params[0] - 'a');
  }
}

/// @fn void* exMove(void *args)
///
/// @brief Run one of the commands that move, copy, or mark lines.
///
/// @param args A pointer to the ExState of the running editor, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in the
/// ExState's store.error member.
void* exMove(void *args) {
  ExState *state = (ExState*) args;

  if (state->command == 'k') {
    exMarkLine(state);
    return args;
  }
  if ((state->addr1 < 1) || (state->addr2 > state->store.numLines)) {
    state->store.error = EX_ERROR_ADDRESS;
    return args;
  }

  if (state->command == 'm') {
    exMoveLines(state);
  } else {
    exCopyLines(state);
  }
  return args;
}
