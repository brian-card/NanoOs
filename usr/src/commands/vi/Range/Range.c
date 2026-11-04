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

/// @file Range.c
///
/// @brief The `vi` commands that build the current line from ranges of the
/// text file, so that they work on a line of any length:  J, and d, c, and y
/// within a line too long for the line buffer.

#include "vi.h"

/// @fn static void viOperate(ViState *state)
///
/// @brief Run state->operator over the range from the cursor to the line in
/// state->ex.addr2 and the index in state->targetIndex, both from the start
/// of the window, on a line too long for the line buffer.  A range that the
/// motion took to another line ends at this line's end or start.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viOperate(ViState *state) {
  TextStore *store = &state->ex.store;
  int start = state->windowStart + state->index;
  int end = state->windowStart + state->targetIndex;
  if (state->ex.addr2 > state->line) {
    end = INT16_MAX;
  } else if (state->ex.addr2 < state->line) {
    end = start;
    start = 0;
  } else if (end < start) {
    end = start;
    start = state->windowStart + state->targetIndex;
  }
  uint32_t old = textstoreLineOffset(store, state->line);
  ViYank yank = { store->textEnd, 0, 0 };
  long length = (store->error != EX_ERROR_NONE) ? -1
    : textstoreCopyRange(store, old + (uint32_t) start,
      (uint32_t) (end - start), store->text);
  if (length <= 0) {
    return;
  }
  yank.length = (int32_t) length;
  viClearYank(state);
  if (!textstoreAppendText(store, "\n")
    || !textstoreChainAppend(store, &state->yank, &yank)
  ) {
    return;
  }
  state->yankKind = VI_YANK_TEXT;
  int oldRows = viRowsFor(state, state->width);
  uint32_t offset = store->textEnd;
  if ((state->operator != 'y')
    && (!viBeginChange(state)
      || !viCopyLine(state, old, 0, start)
      || !viCopyLine(state, old, start + (int) length, INT16_MAX)
      || !viReplaceLine(state, offset))
  ) {
    return;
  }
  viPlaceWindow(state, start);
  if (state->operator != 'y') {
    viRequestRows(state, state->line, state->lineRow, VI_REQUEST_SHOW,
      oldRows, 1);
  }
}

/// @fn static bool viJoin(ViState *state)
///
/// @brief Append the line after the current one to it, without its leading
/// blanks and after a space unless either side is empty, then delete it.
/// state->index is set to the index in the whole line where they meet.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static bool viJoin(ViState *state) {
  TextStore *store = &state->ex.store;
  int next = state->line + 1;
  if (next > store->numLines) {
    store->error = VI_ERROR_MOTION;
    return false;
  }
  uint32_t second = textstoreLineOffset(store, next);
  if ((store->error != EX_ERROR_NONE)
    || (fseek(store->text, (long) second, SEEK_SET) != 0)
  ) {
    store->error = EX_ERROR_SCRATCH;
    return false;
  }
  int blanks = 0;
  bool empty = true;
  while (empty
    && (fgets(state->buffer, TEXTSTORE_BUFFER_SIZE, store->text) != NULL)
  ) {
    char *c = state->buffer;
    while ((*c == ' ') || (*c == '\t')) {
      c++;
    }
    blanks += (int) (c - state->buffer);
    if (*c != '\0') {
      empty = (*c == '\n');
      break;
    }
  }
  uint32_t first = textstoreLineOffset(store, state->line);
  uint32_t offset = store->textEnd;
  long length = (!viBeginChange(state)) ? -1
    : textstoreCopyRange(store, first, UINT32_MAX, store->text);
  if ((length < 0)
    || (((length > 0) && !empty) && !textstoreAppendText(store, " "))
    || !viCopyLine(state, second, blanks, INT16_MAX)
    || !viReplaceLine(state, offset)
    || !exDeleteLines(&state->ex, next, next)
  ) {
    return false;
  }
  state->index = (int16_t) length;
  return true;
}

/// @fn void* viRun(void *args)
///
/// @brief Run J, state->count times, if state->ex.command is J, or else d,
/// c, or y within a line too long for the line buffer.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  if (state->ex.command != 'J') {
    viOperate(state);
    return args;
  }
  int count = (state->count > 2) ? state->count - 1 : 1;
  int oldRows = viRowsFor(state, state->width);
  state->index = (int16_t) (state->windowStart + state->index);
  for (int ii = 0; ii < count; ii++) {
    int width = viReadLine(state, state->line + 1, false);
    if (!viJoin(state)) {
      break;
    }
    oldRows += viRowsFor(state, width);
  }
  viPlaceWindow(state, state->index);
  viRequestRows(state, state->line, state->lineRow, VI_REQUEST_SHOW,
    oldRows, 1);
  return args;
}
