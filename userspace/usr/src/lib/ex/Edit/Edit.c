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

/// @file Edit.c
///
/// @brief The editing commands that add, change, or remove lines:  a, c, d,
/// i, and j.

#include "Ex.h"

/// @fn static void exJoinLines(ExState *state)
///
/// @brief Replace the lines from state->addr1 to state->addr2 with one line
/// holding their text.
///
/// @param state The ExState of the running editor.
///
/// @return This function returns no value.
static void exJoinLines(ExState *state) {
  uint32_t offset = state->store.textEnd;
  for (int line = state->addr1; line <= state->addr2; line++) {
    if (textstoreCopyLine(&state->store, line, state->store.text,
      line == state->addr2) < 0
    ) {
      return;
    }
  }

  if (!exBeginChange(state)
    || !exDeleteLines(state, state->addr1 + 1, state->addr2)
    || !exSetLineOffset(state, state->addr1, offset)
  ) {
    return;
  }
  state->current = state->addr1;
  state->modified = true;
}

/// @fn void* exEdit(void *args)
///
/// @brief Run one of the commands that add, change, or remove lines.
///
/// @param args A pointer to the ExState of the running editor, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in the
/// ExState's store.error member.
void* exEdit(void *args) {
  ExState *state = (ExState*) args;
  char command = state->command;

  if (state->numAddresses == 0) {
    if (command == 'j') {
      state->addr2 = state->addr1 + 1;
    }
  }
  if ((state->addr1 < 1)
    && ((command == 'c') || (command == 'd') || (command == 'j'))
  ) {
    state->store.error = EX_ERROR_ADDRESS;
    return args;
  }
  if (state->addr2 > state->store.numLines) {
    state->store.error = EX_ERROR_ADDRESS;
    return args;
  }
  if (state->params != NULL) {
    state->store.error = EX_ERROR_SUFFIX;
    return args;
  }

  switch (command) {
    case 'j':
      if (state->addr1 < state->addr2) {
        exJoinLines(state);
      }
      break;

    case 'd':
    case 'c':
      if (!exBeginChange(state)
        || !exDeleteLines(state, state->addr1, state->addr2)
      ) {
        break;
      }
      state->modified = true;
      state->current = (state->addr1 <= state->store.numLines)
        ? state->addr1 : state->store.numLines;
      if (command == 'd') {
        break;
      }
      state->addr2 = state->addr1 - 1;
      // fall through
    case 'a':
    case 'i': {
      int after = state->addr2;
      if ((command == 'i') && (after > 0)) {
        after--;
      }
      int numLines = state->store.numLines;
      long bytesRead = 0;
      int last = textstoreReadLines(&state->store, stdin, after, true,
        &bytesRead);
      if (state->store.numLines > numLines) {
        state->modified = true;
        if (((command != 'c') && !exBeginChange(state))
          || !exRecordLines(state, after + 1,
            after + (state->store.numLines - numLines))
        ) {
          break;
        }
      }
      if (last > after) {
        state->current = last;
      } else if (command != 'c') {
        state->current = state->addr2;
      }
      break;
    }
  }

  return args;
}
