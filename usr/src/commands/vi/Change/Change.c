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
/// it:  r, p and P for yanked text, m, and . repeating an insertion that
/// doesn't fit in the line buffer.  The last two build the line from ranges
/// of the text file, so that they work on a line of any length.

#include "vi.h"

/// @fn static void viSplice(ViState *state, int at, int skip,
///   const ViYank *text, int count)
///
/// @brief Put copies of text from the text file into the current line at an
/// index, in place of some of the line's characters, and leave the cursor on
/// the last character put.
///
/// @param state The ViState of the running `vi`.
/// @param at The index in the whole line.
/// @param skip The number of the line's characters to replace.
/// @param text The text's offset and length.
/// @param count The number of copies.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viSplice(
  ViState *state, int at, int skip, const ViYank *text, int count
) {
  TextStore *store = &state->ex.store;
  uint32_t old = textstoreLineOffset(store, state->line);
  uint32_t offset = store->textEnd;
  int oldRows = viRowsFor(state, state->width);
  if ((store->error != EX_ERROR_NONE) || !viBeginChange(state)
    || !viCopyLine(state, old, 0, at)
  ) {
    return;
  }
  for (int ii = 0; ii < count; ii++) {
    if (!viCopyLine(state, text->offset, 0, (int) text->length)) {
      return;
    }
  }
  if (!viCopyLine(state, old, at + skip, INT16_MAX)
    || !viReplaceLine(state, offset)
  ) {
    return;
  }
  viPlaceWindow(state, at + ((int) text->length * count) - 1);
  viRequestRows(state, state->line, state->lineRow, VI_REQUEST_SHOW,
    oldRows, 1);
}

/// @fn static void viPut(ViState *state, bool after, int count)
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
static void viPut(ViState *state, bool after, int count) {
  TextStoreChain cursor = state->yank;
  cursor.block = cursor.first;
  cursor.index = 0;
  ViYank yank;
  if (!textstoreChainNext(&state->ex.store, &cursor, &yank)) {
    return;
  }
  int at = state->windowStart + state->index
    + ((after && (state->length > 0)) ? 1 : 0);
  if ((state->line <= state->ex.store.numLines) || viAddEmptyLine(state, 0)) {
    viSplice(state, at, 0, &yank, count);
  }
}

/// @fn static void viRepeat(ViState *state)
///
/// @brief Insert the text the last change inserted at the cursor, or type it
/// over the line for R, for . to repeat it when insert mode can't do so in
/// the line buffer.  state->targetIndex is its length.  The text is copied
/// to the text file first, to be put like yanked text.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viRepeat(ViState *state) {
  TextStore *store = &state->ex.store;
  int length = state->targetIndex;
  ViYank text = { store->textEnd, length, 0 };
  // The line is in the store, so text is free to hold the inserted text.
  state->text[length] = '\0';
  if (textstoreReadHeader(store, VI_DOT_TEXT_POSITION, state->text,
      (size_t) length)
    && textstoreAppendText(store, state->text)
  ) {
    viSplice(state, state->windowStart + state->index,
      (state->argument == 'R') ? length : 0, &text, 1);
  }
}

/// @fn void* viRun(void *args)
///
/// @brief Run r, p, P, or m, as state->ex.command says, state->count times
/// for all but m, or repeat an insertion for . if it is VI_COMMAND_REPEAT.
/// The current line is committed before returning.
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
  } else if (state->ex.command == VI_COMMAND_REPEAT) {
    viRepeat(state);
  } else if ((state->ex.command == 'p') || (state->ex.command == 'P')) {
    viPut(state, state->ex.command == 'p', count);
  } else if ((state->index + count > state->length)
    || ((unsigned char) argument < ' ')
  ) {
    store->error = VI_ERROR_MOTION;
  } else {
    int oldRows = viRowsFor(state, state->width);
    memset(&state->text[state->index], argument, (size_t) count);
    state->index = (int16_t) (state->index + count - 1);
    state->changed = true;
    viCommitLine(state);
    viRequestRows(state, state->line, state->lineRow, VI_REQUEST_SHOW,
      oldRows, 1);
  }
  return args;
}
