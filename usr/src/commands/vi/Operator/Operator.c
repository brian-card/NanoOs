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

/// @file Operator.c
///
/// @brief The operators of the `vi` screen editor:  d, c, and y over the
/// range a motion reaches.  A range of whole lines is yanked as the lines'
/// offsets; a range within a line is yanked as a copy of its text.

#include "vi.h"

/// @fn static bool viYankLines(ViState *state, int first, int last)
///
/// @brief Make a range of lines the yank.
///
/// @param state The ViState of the running `vi`.
/// @param first The number of the first line.
/// @param last The number of the last line.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static bool viYankLines(ViState *state, int first, int last) {
  viClearYank(state);
  ViYank yank = { 0, 0, 0 };
  for (int line = first; line <= last; line++) {
    yank.offset = textstoreLineOffset(&state->ex.store, line);
    if ((state->ex.store.error != EX_ERROR_NONE)
      || !textstoreChainAppend(&state->ex.store, &state->yank, &yank)
    ) {
      return false;
    }
  }
  state->yankKind = VI_YANK_LINES;
  return true;
}

/// @fn static bool viYankText(ViState *state, int start, int end)
///
/// @brief Make text of the current line the yank, as a copy appended to the
/// text file.
///
/// @param state The ViState of the running `vi`.
/// @param start The index of the first character.
/// @param end The index past the last character.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static bool viYankText(ViState *state, int start, int end) {
  TextStore *store = &state->ex.store;
  viClearYank(state);
  ViYank yank = { store->textEnd, end - start, 0 };
  char saved = state->text[end];
  state->text[end] = '\0';
  bool ok = textstoreAppendText(store, &state->text[start]);
  state->text[end] = saved;
  if (!ok || !textstoreAppendText(store, "\n")
    || !textstoreChainAppend(store, &state->yank, &yank)
  ) {
    return false;
  }
  state->yankKind = VI_YANK_TEXT;
  return true;
}

/// @fn static void viOperateOnLines(ViState *state)
///
/// @brief Run the operator over whole lines:  yank them, then delete them
/// (d) or replace them with an empty line to insert into (c).
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viOperateOnLines(ViState *state) {
  TextStore *store = &state->ex.store;
  int first = state->line;
  int last = state->ex.addr2;
  if (last < first) {
    first = last;
    last = state->line;
  }
  if ((store->numLines == 0) || !viYankLines(state, first, last)) {
    return;
  }
  int row = viRowOf(state, first);
  if (state->operator == 'y') {
    if (first != state->line) {
      viRequest(state, first, row, VI_REQUEST_MOVE);
    }
    return;
  }

  if (!viBeginChange(state)) {
    return;
  }
  int oldRows = viRowsOfLines(state, first, last, row);
  if (state->operator == 'd') {
    if (exDeleteLines(&state->ex, first, last)) {
      state->ex.modified = true;
      viRequestDeleted(state, first, row, oldRows);
    }
    return;
  }
  if ((last > first) && !exDeleteLines(&state->ex, first + 1, last)) {
    return;
  }
  viLoadLine(state, first);
  state->length = 0;
  state->text[0] = '\0';
  state->changed = true;
  state->truncated = false;
  state->index = 0;
  if (viCommitLine(state)) {
    viRequestRows(state, first, row, VI_REQUEST_SHOW, oldRows, 1);
  }
}

/// @fn void* viRun(void *args)
///
/// @brief Run state->operator over the range from the cursor to the line in
/// state->ex.addr2 and the index in state->targetIndex.  A range within a
/// line that the motion took to another line ends at that line's end or
/// start.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  if (state->linewise) {
    viOperateOnLines(state);
    return args;
  }

  int start = state->index;
  int end = state->targetIndex;
  if (state->ex.addr2 > state->line) {
    end = state->length;
  } else if (state->ex.addr2 < state->line) {
    end = start;
    start = 0;
  } else if (end < start) {
    end = start;
    start = state->targetIndex;
  }
  if (start == end) {
    return args;
  } else if (state->truncated && (state->ex.addr2 != state->line)
    && (state->operator != 'y')
  ) {
    state->ex.store.error = VI_ERROR_TOO_LONG;
    return args;
  }
  if (!viYankText(state, start, end)) {
    return args;
  }
  state->index = (int16_t) start;
  if (state->operator == 'y') {
    return args;
  }
  memmove(&state->text[start], &state->text[end],
    (size_t) (state->length - end + 1));
  state->length = (int16_t) (state->length - (end - start));
  state->changed = true;
  if (viCommitLine(state)) {
    viRequestRows(state, state->line, state->lineRow, VI_REQUEST_SHOW,
      viRowsFor(state, state->width), 1);
  }
  return args;
}
