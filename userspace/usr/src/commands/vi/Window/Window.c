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

/// @file Window.c
///
/// @brief The `vi` work on a line too long for the line buffer, which holds a
/// window onto it:  putting a changed window back between the line's text
/// before and after it, moving the window, and splitting the line.  ~ is
/// here too, since its count can run past the end of a window.

#include "vi.h"

/// @fn static void viCenterWindow(ViState *state)
///
/// @brief Read the current line again with the cursor in the middle of the
/// window.  The indexes insert mode started and ended at move with it.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viCenterWindow(ViState *state) {
  int index = state->windowStart + state->index;
  int oldStart = state->windowStart;
  int start = index - (VI_WINDOW_SIZE / 2);
  state->windowStart = (int16_t) ((start > 0) ? start : 0);
  if (!viLoadWindow(state)) {
    return;
  }
  state->index = (int16_t) (index - state->windowStart);
  state->targetIndex = (int16_t)
    (state->targetIndex - (state->windowStart - oldStart));
  if (state->insertStart != VI_NO_INSERT_START) {
    state->insertStart = (int16_t)
      (state->insertStart - (state->windowStart - oldStart));
  }
}

/// @fn static bool viCommitWindow(ViState *state)
///
/// @brief Put the current line in the store if it has changed:  for a window
/// onto a longer line, the line's text before the window, the window, and the
/// line's text after it, copied within the text file.  The window is then
/// read again with the cursor in its middle, since the cursor may have gone
/// past the end of the window the line is read with.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static bool viCommitWindow(ViState *state) {
  TextStore *store = &state->ex.store;
  if (!state->truncated || !state->changed) {
    if (!viCommitLine(state)) {
      return false;
    }
    viCenterWindow(state);
    return store->error == EX_ERROR_NONE;
  }
  uint32_t old = textstoreLineOffset(store, state->line);
  uint32_t offset = store->textEnd;
  if ((store->error != EX_ERROR_NONE)
    || !viBeginChange(state)
    || (textstoreCopyRange(store, old, (uint32_t) state->windowStart,
      store->text) < 0)
    || !textstoreAppendText(store, state->text)
    || (textstoreCopyRange(store, old + (uint32_t) state->windowStart
      + (uint32_t) state->windowLength, UINT32_MAX, store->text) < 0)
    || !textstoreAppendText(store, "\n")
    || !exSetLineOffset(&state->ex, state->line, offset)
  ) {
    return false;
  }
  state->ex.modified = true;
  viCenterWindow(state);
  return store->error == EX_ERROR_NONE;
}

/// @fn void* viCommit(void *args)
///
/// @brief Put a changed window onto the current line in the store.  The main
/// overlay runs this when another overlay leaves one changed.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viCommit(void *args) {
  viCommitWindow((ViState*) args);
  return args;
}

/// @fn static void viSplitWindow(ViState *state)
///
/// @brief Split the current line at the cursor for Return in insert mode,
/// copying both parts within the text file, and make the second part current.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viSplitWindow(ViState *state) {
  TextStore *store = &state->ex.store;
  int oldRows = viRowsFor(state, state->width);
  if (!viCommitWindow(state)) {
    return;
  }
  uint32_t old = textstoreLineOffset(store, state->line);
  uint32_t at = (uint32_t) (state->windowStart + state->index);
  uint32_t second = store->textEnd;
  if ((store->error != EX_ERROR_NONE)
    || !viBeginChange(state)
    || (textstoreCopyRange(store, old + at, UINT32_MAX, store->text) < 0)
    || !textstoreAppendText(store, "\n")
  ) {
    return;
  }
  uint32_t first = store->textEnd;
  if ((textstoreCopyRange(store, old, at, store->text) < 0)
    || !textstoreAppendText(store, "\n")
    || !exSetLineOffset(&state->ex, state->line, first)
    || !exInsertLine(&state->ex, state->line, second)
  ) {
    return;
  }
  state->ex.modified = true;
  state->windowStart = 0;
  if (!viLoadWindow(state)) {
    return;
  }
  int rows = viRowsFor(state, state->width);
  state->index = 0;
  viRequestRows(state, state->line + 1, state->lineRow + rows,
    VI_REQUEST_SPLIT, oldRows - rows, 1);
}

/// @fn static void viToggleCase(ViState *state)
///
/// @brief Reverse the case of the letters from the cursor to
/// state->targetIndex or the line's end, putting each window back and moving
/// on as the letters run past it, and leave the cursor after them.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viToggleCase(ViState *state) {
  int oldRows = viRowsFor(state, state->width);
  while (true) {
    int end = (state->targetIndex < state->length)
      ? state->targetIndex : state->length;
    if (state->index >= end) {
      break;
    }
    for (char *c = &state->text[state->index]; c < &state->text[end]; c++) {
      char lower = (char) (*c | 0x20);
      if ((lower >= 'a') && (lower <= 'z')) {
        *c = (char) (*c ^ 0x20);
      }
    }
    state->index = (int16_t) end;
    state->changed = true;
    if (!viCommitWindow(state)) {
      return;
    }
  }
  viRequestRows(state, state->line, state->lineRow, VI_REQUEST_SHOW,
    oldRows, 1);
}

/// @fn void* viRun(void *args)
///
/// @brief Split the current line for Return in insert mode (state->ex.command
/// is a newline), move the window to the line's start for I or its end for
/// A, reverse the case of letters for ~, or else, when the cursor is at the
/// start of a window after the line's start or the line buffer is full, put
/// the line back and move the window to have the cursor in its middle.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  char command = state->ex.command;
  if (command == '\n') {
    viSplitWindow(state);
  } else if (command == '~') {
    viToggleCase(state);
  } else if ((command == 'I') || (command == 'A')) {
    state->windowStart = (command == 'A') ? INT16_MAX : 0;
    viLoadWindow(state);
  } else if ((state->length >= VI_LINE_MAX - 1)
    || ((state->index == 0) && (state->windowStart > 0))
  ) {
    viCommitWindow(state);
  }
  return args;
}
