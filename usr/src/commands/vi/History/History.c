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

/// @file History.c
///
/// @brief Undoing and redoing changes in `vi`.

#include "vi.h"

/// @struct ViSpan
///
/// @brief The lines a change group touches, numbered as they are after it.
///
/// @param first The first line, or the largest int before any is known.
/// @param last The last line, which is first - 1 if lines were only deleted.
/// @param added The number of lines the group adds, less those it deletes.
typedef struct ViSpan {
  int first;
  int last;
  int added;
} ViSpan;

/// @fn static void viWidenSpan(ViSpan *span, int line, bool insert)
///
/// @brief Widen a ViSpan by a line inserted or deleted after the changes it
/// already holds.
///
/// @param span The ViSpan.
/// @param line The number of the line inserted or deleted.
/// @param insert Whether the line is inserted.
///
/// @return This function returns no value.
static void viWidenSpan(ViSpan *span, int line, bool insert) {
  if (insert) {
    if (line <= span->first) {
      span->first = line;
    }
    span->last = (line <= span->last) ? span->last + 1 : line;
    span->added++;
  } else {
    if (line < span->first) {
      span->first = line;
    }
    span->last = (line <= span->last) ? span->last - 1 : line - 1;
    span->added--;
  }
}

/// @fn static int viSpanChange(ViState *state, ViSpan *span)
///
/// @brief Find the lines the change that u or r is about to undo or redo
/// touches, by reading its records from a copy of the undo history's
/// position.
///
/// @param state The ViState of the running `vi`.
/// @param span Set to the lines the change touches.
///
/// @return Returns the current line from before the change.
static int viSpanChange(ViState *state, ViSpan *span) {
  TextStore *store = &state->ex.store;
  TextStoreChain cursor = state->ex.history;
  bool undo = (state->ex.command == 'u');
  ExChange change;
  int line = 0;
  span->first = (int) (~0u >> 1);
  span->last = 0;
  span->added = 0;
  if (!undo && textstoreChainNext(store, &cursor, &change)) {
    line = (int) change.line;
  }
  while (undo
    ? textstoreChainPrevious(store, &cursor, &change)
    : textstoreChainNext(store, &cursor, &change)
  ) {
    if (change.kind == EX_CHANGE_GROUP) {
      line = undo ? (int) change.line : line;
      break;
    }
    viWidenSpan(span, (int) change.line,
      (change.kind == EX_CHANGE_INSERT) != undo);
  }
  return line;
}

/// @fn void* viRun(void *args)
///
/// @brief Undo (state->ex.command u) or redo (r) a change.  The lines it
/// touches replace the rows they took up, from the first of them or the line
/// that was current before the change, whichever is higher, which becomes
/// current.  If that line is past the end, or the buffer was or becomes
/// empty, the screen is redrawn instead.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  ViSpan span;
  int first = viSpanChange(state, &span);
  if (span.first < first) {
    first = span.first;
  }
  if (first < 1) {
    first = 1;
  }
  int row = viRowOf(state, first);
  int oldRows = viRowsOfLines(state, first, span.last - span.added, row);
  bool wasEmpty = (state->ex.store.numLines == 0);
  if (callOverlayFunction(EX_LIBRARY_PATH, "Undo", "exUndo", &state->ex)
    == NULL
  ) {
    state->ex.store.error = EX_ERROR_MEMORY;
  }
  if (state->ex.store.error != EX_ERROR_NONE) {
    return args;
  }

  int numLines = (state->ex.store.numLines > 0) ? state->ex.store.numLines : 1;
  int line = (state->ex.current > 0) ? state->ex.current : 1;
  line = (line > numLines) ? numLines : line;
  if (!wasEmpty && (first <= state->ex.store.numLines)
    && (state->top <= numLines)
  ) {
    viLoadLine(state, first);
    state->index = (first == line)
      ? state->ex.column : (int16_t) viFirstNonBlank(state);
    viRequestRows(state, first, row, VI_REQUEST_SHOW, oldRows,
      (span.last >= first) ? span.last - first + 1 : 0);
    return args;
  }
  if (state->top > numLines) {
    state->top = 1;
  }
  if (viLoadLine(state, line)) {
    state->index = state->ex.column;
    viRequest(state, line, viRowOf(state, line), VI_REQUEST_REDRAW);
  }
  return args;
}
