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
/// @brief The commands of the `ed` line editor that change lines:  a, c, d,
/// i, j, m, and t.

#include "ed.h"

/// @fn static void edDeleteLines(EdState *state, int first, int last)
///
/// @brief Remove a range of lines from the buffer.
///
/// @param state The EdState of the running `ed`.
/// @param first The number of the first line to remove.
/// @param last The number of the last line to remove.
///
/// @return This function returns no value.
static void edDeleteLines(EdState *state, int first, int last) {
  memmove(&state->lines[first - 1], &state->lines[last],
    (size_t) (state->numLines - last) * sizeof(uint32_t));
  state->numLines -= last - first + 1;
}

/// @fn static void edReverse(uint32_t *lines, int count)
///
/// @brief Reverse the order of a run of line-table entries.
///
/// @param lines The first entry of the run.
/// @param count The number of entries in the run.
///
/// @return This function returns no value.
static void edReverse(uint32_t *lines, int count) {
  for (int ii = 0, jj = count - 1; ii < jj; ii++, jj--) {
    uint32_t swap = lines[ii];
    lines[ii] = lines[jj];
    lines[jj] = swap;
  }
}

/// @fn static void edMoveLines(EdState *state)
///
/// @brief Move the lines from state->addr1 to state->addr2 to after
/// state->destination.  Moving them is a rotation of the line table, done by
/// three reversals so that it needs no extra memory.
///
/// @param state The EdState of the running `ed`.
///
/// @return This function returns no value.
static void edMoveLines(EdState *state) {
  int count = state->addr2 - state->addr1 + 1;
  if ((state->destination >= state->addr1)
    && (state->destination < state->addr2)
  ) {
    state->error = ED_ERROR_DESTINATION;
    return;
  }

  int first = state->addr1 - 1;
  int end = state->addr2;
  if (state->destination < state->addr1) {
    first = state->destination;
    state->current = state->destination + count;
  } else {
    end = state->destination;
    state->current = state->destination;
  }
  edReverse(&state->lines[first], end - first);
  if (state->destination < state->addr1) {
    edReverse(&state->lines[first], count);
    edReverse(&state->lines[first + count], end - first - count);
  } else {
    edReverse(&state->lines[first], end - first - count);
    edReverse(&state->lines[end - count], count);
  }
  state->modified = true;
}

/// @fn static void edCopyLines(EdState *state)
///
/// @brief Copy the lines from state->addr1 to state->addr2 to after
/// state->destination.  Lines are never changed in place, so a copy shares
/// its text with the original.
///
/// @param state The EdState of the running `ed`.
///
/// @return This function returns no value.
static void edCopyLines(EdState *state) {
  int count = state->addr2 - state->addr1 + 1;
  for (int ii = 0; ii < count; ii++) {
    int source = state->addr1 - 1 + ii;
    if (source >= state->destination) {
      source += ii;
    }
    if (!edInsertLine(state, state->destination + ii,
      state->lines[source])
    ) {
      return;
    }
  }

  state->current = state->destination + count;
}

/// @fn static void edJoinLines(EdState *state)
///
/// @brief Replace the lines from state->addr1 to state->addr2 with one line
/// holding their text.
///
/// @param state The EdState of the running `ed`.
///
/// @return This function returns no value.
static void edJoinLines(EdState *state) {
  uint32_t offset = state->scratchEnd;
  for (int line = state->addr1; line <= state->addr2; line++) {
    if (edCopyLine(state, line, state->scratch, line == state->addr2) < 0) {
      return;
    }
  }

  edDeleteLines(state, state->addr1 + 1, state->addr2);
  state->lines[state->addr1 - 1] = offset;
  state->current = state->addr1;
  state->modified = true;
}

/// @fn void* edEdit(void *args)
///
/// @brief Run one of the commands that change lines.
///
/// @param args A pointer to the EdState of the running `ed`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in the
/// EdState's error member.
void* edEdit(void *args) {
  EdState *state = (EdState*) args;
  char command = state->command;

  if (state->numAddresses == 0) {
    if (command == 'j') {
      state->addr2 = state->addr1 + 1;
    }
  }
  if ((state->addr1 < 1)
    && ((command == 'c') || (command == 'd') || (command == 'j')
      || (command == 'm') || (command == 't'))
  ) {
    state->error = ED_ERROR_ADDRESS;
    return args;
  }
  if (state->addr2 > state->numLines) {
    state->error = ED_ERROR_ADDRESS;
    return args;
  }
  if ((state->params != NULL)
    && (command != 'm') && (command != 't')
  ) {
    state->error = ED_ERROR_SUFFIX;
    return args;
  }

  switch (command) {
    case 'm':
      edMoveLines(state);
      break;

    case 't':
      edCopyLines(state);
      break;

    case 'j':
      if (state->addr1 < state->addr2) {
        edJoinLines(state);
      }
      break;

    case 'd':
    case 'c':
      edDeleteLines(state, state->addr1, state->addr2);
      state->modified = true;
      state->current = (state->addr1 <= state->numLines)
        ? state->addr1 : state->numLines;
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
      long bytesRead = 0;
      int last = edReadLines(state, stdin, after, true, &bytesRead);
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
