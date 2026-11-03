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
/// @brief The `vi` commands that change the current line in place or mark
/// it:  r, J, p and P for yanked text, and m.

#include "vi.h"

/// @fn static bool viJoinLine(ViState *state)
///
/// @brief Append the line after the current one to it, without its leading
/// blanks and after a space unless either side is empty, then delete it.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static bool viJoinLine(ViState *state) {
  TextStore *store = &state->ex.store;
  int next = state->line + 1;
  if (next > store->numLines) {
    store->error = VI_ERROR_MOTION;
    return false;
  }
  uint32_t offset = textstoreLineOffset(store, next);
  if ((store->error != EX_ERROR_NONE)
    || (fseek(store->text, (long) offset, SEEK_SET) != 0)
  ) {
    store->error = EX_ERROR_SCRATCH;
    return false;
  }
  int length = state->length;
  bool blank = true;
  bool ended = false;
  while (!ended
    && (fgets(state->buffer, TEXTSTORE_BUFFER_SIZE, store->text) != NULL)
  ) {
    for (char *c = state->buffer; (*c != '\0') && !ended; c++) {
      ended = (*c == '\n');
      if (ended || (blank && ((*c == ' ') || (*c == '\t')))) {
        continue;
      }
      if (blank && (length > 0)) {
        state->text[length++] = ' ';
      }
      blank = false;
      if (length >= VI_LINE_MAX - 1) {
        state->text[state->length] = '\0';
        store->error = VI_ERROR_TOO_LONG;
        return false;
      }
      state->text[length++] = *c;
    }
  }
  state->index = (int16_t) state->length;
  state->text[length] = '\0';
  state->length = (int16_t) length;
  state->changed = true;
  return viCommitLine(state) && exDeleteLines(&state->ex, next, next);
}

/// @fn static void viPutText(ViState *state, bool after, int count)
///
/// @brief Put copies of the yanked text after the cursor or before it, and
/// leave the cursor on the last character put.
///
/// @param state The ViState of the running `vi`.
/// @param after Whether to put it after the cursor.
/// @param count The number of copies.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viPutText(ViState *state, bool after, int count) {
  TextStore *store = &state->ex.store;
  TextStoreChain cursor = state->yank;
  cursor.block = cursor.first;
  cursor.index = 0;
  ViYank yank;
  if (!textstoreChainNext(store, &cursor, &yank)) {
    return;
  }
  int length = (int) yank.length;
  int total = length * count;
  if (state->truncated || (state->length + total > VI_LINE_MAX - 1)) {
    store->error = VI_ERROR_TOO_LONG;
    return;
  }
  int at = state->index + ((after && (state->length > 0)) ? 1 : 0);
  memmove(&state->text[at + total], &state->text[at],
    (size_t) (state->length - at + 1));
  if ((fseek(store->text, (long) yank.offset, SEEK_SET) != 0)
    || (fread(&state->text[at], 1, (size_t) length, store->text)
      != (size_t) length)
  ) {
    store->error = EX_ERROR_SCRATCH;
    viLoadLine(state, state->line);
    return;
  }
  for (int ii = 1; ii < count; ii++) {
    memcpy(&state->text[at + (ii * length)], &state->text[at],
      (size_t) length);
  }
  state->length = (int16_t) (state->length + total);
  state->index = (int16_t) (at + total - 1);
  state->changed = true;
  if (viCommitLine(state)) {
    viRequest(state, state->line, state->lineRow, VI_REQUEST_SHOW);
  }
}

/// @fn void* viRun(void *args)
///
/// @brief Run r, J, p, P, or m, as state->ex.command says, state->count
/// times for all but m.  The current line is committed before returning.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  TextStore *store = &state->ex.store;
  int count = (state->count > 0) ? state->count : 1;
  char argument = state->argument;

  if (state->ex.command == 'm') {
    uint32_t offset = textstoreLineOffset(store, state->line);
    if ((argument < 'a') || (argument > 'z')
      || (state->line > store->numLines)
    ) {
      store->error = EX_ERROR_MARK;
    } else if ((store->error == EX_ERROR_NONE)
      && textstoreWriteHeader(store, EX_MARK_POSITION(argument),
        &offset, sizeof(offset))
    ) {
      state->ex.marks |= ((uint32_t) 1) << (argument - 'a');
    }
    return args;
  } else if (state->truncated) {
    store->error = VI_ERROR_TOO_LONG;
    return args;
  }

  if ((state->ex.command == 'p') || (state->ex.command == 'P')) {
    viPutText(state, state->ex.command == 'p', count);
    return args;
  } else if (state->ex.command == 'r') {
    if ((state->index + count > state->length)
      || ((unsigned char) argument < ' ')
    ) {
      store->error = VI_ERROR_MOTION;
      return args;
    }
    memset(&state->text[state->index], argument, (size_t) count);
    state->index = (int16_t) (state->index + count - 1);
    state->changed = true;
    viCommitLine(state);
  } else {
    count = (count > 2) ? count - 1 : 1;
    for (int ii = 0; (ii < count) && viJoinLine(state); ii++) {
    }
  }
  viRequest(state, state->line, state->lineRow, VI_REQUEST_SHOW);
  return args;
}
