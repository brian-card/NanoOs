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

/// @file Show.c
///
/// @brief The `vi` drawing of lines that replaced others:  the rows below
/// them move by the difference in rows, so that only the new lines, and any
/// rows that move up from below the screen, are drawn.

#include "vi.h"

/// @fn static void viFillBottom(ViState *state, int row, int line,
///   int blankRow)
///
/// @brief Draw the lines that move up into the blank rows above the status
/// line after rows are deleted, starting from the first line that reaches
/// them.
///
/// @param state The ViState of the running `vi`.
/// @param row The screen row a line starts on, at or above blankRow.
/// @param line The number of that line.
/// @param blankRow The first blank row.
///
/// @return This function returns no value.
static void viFillBottom(ViState *state, int row, int line, int blankRow) {
  while (row < blankRow) {
    int width = viReadLine(state, line, false);
    if ((width < 0) || (row + viRowsFor(state, width) > blankRow)) {
      break;
    }
    row += viRowsFor(state, width);
    line++;
  }
  viDrawFrom(state, row, line);
}

/// @fn static void viScrollTo(ViState *state, int line, int row,
///   bool keepRows)
///
/// @brief Scroll so that the current line, which doesn't fit at a screen
/// row, is the top line if the row is above the screen, or else the bottom
/// one.  If asked to keep the rows on the screen, a scroll of at most half
/// the screen moves them and draws only the rows that come into view; any
/// other scroll redraws the screen.  A row below the screen that is known,
/// as viRowOf's is not past the screen, is reached by dropping lines from the
/// top, which reads only those lines rather than every line on the screen.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the current line.
/// @param row The screen row it didn't fit at, or the number of rows for a
///   line below the screen whose row isn't known.
/// @param keepRows Whether the rows on the screen show the buffer's lines
///   from the top line down to the current line.
///
/// @return This function returns no value.
static void viScrollTo(ViState *state, int line, int row, bool keepRows) {
  int textRows = state->rows - 1;
  int lineRows = viRowsFor(state, state->width);
  int oldTop = state->top;
  int top = oldTop;
  int used = row + lineRows;
  int distance = 0;
  if ((row >= 0) && (row < state->rows)) {
    while ((used > textRows) && (top < line)) {
      int rows = viRowsFor(state, viReadLine(state, top, false));
      used -= rows;
      distance += rows;
      top++;
    }
  } else {
    top = line;
    used = lineRows;
    while ((row >= 0) && (top > 1)) {
      int width = viReadLine(state, top - 1, false);
      if ((width < 0) || (used + viRowsFor(state, width) > textRows)) {
        break;
      }
      used += viRowsFor(state, width);
      top--;
    }
    int first = (top < oldTop) ? top : oldTop;
    int last = (top < oldTop) ? oldTop : top;
    for (int ii = first; (ii < last) && (distance <= textRows / 2); ii++) {
      distance += viRowsFor(state,
        (ii == line) ? state->width : viReadLine(state, ii, false));
    }
  }
  state->top = top;
  state->lineRow = used - lineRows;

  if (!keepRows || (distance > textRows / 2) || ((top < oldTop) != (row < 0))) {
    viDrawFrom(state, 0, top);
  } else if (top < oldTop) {
    viShiftRows(state, 0, distance);
    for (row = 0; row < distance; line++) {
      row += viDrawLine(state, line, row);
    }
  } else {
    viShiftRows(state, 0, -distance);
    row = state->lineRow;
    while (row > textRows - distance) {
      line--;
      row -= viRowsFor(state, viReadLine(state, line, false));
    }
    viDrawFrom(state, row, line);
  }
}

/// @fn void* viRun(void *args)
///
/// @brief Show what a request asks for:  the lines a VI_REQUEST_SHOW or
/// VI_REQUEST_SPLIT asks for, as viRequestRows describes, a line a
/// VI_REQUEST_MOVE moves to, or the whole screen for VI_REQUEST_REDRAW, and
/// scroll if the line doesn't fit where it was asked to be shown.  Only the
/// rows below a line can have changed when it is shown at the bottom, and
/// none can when no lines replace others, so only then does scrolling keep
/// the rows on the screen.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  int line = state->requestLine;
  int row = state->requestRow;
  uint8_t request = state->request;
  bool replaces = (request == VI_REQUEST_SHOW)
    || (request == VI_REQUEST_SPLIT);
  int oldRows = replaces ? state->ex.addr1 : 0;
  int numLines = replaces ? state->ex.destination : 0;
  int textRows = state->rows - 1;
  if (request == VI_REQUEST_SPLIT) {
    viDrawLine(state, state->line, state->lineRow);
  }
  state->request = VI_REQUEST_NONE;
  if (!viShowLine(state, line, row, VI_SHOW_FIT)) {
    if (state->ex.store.error == EX_ERROR_NONE) {
      viScrollTo(state, line, row, (request != VI_REQUEST_REDRAW)
        && ((row >= 0) || !replaces));
    }
    return args;
  } else if (request == VI_REQUEST_REDRAW) {
    viDrawFrom(state, 0, state->top);
    return args;
  }

  int newRows = 0;
  for (int ii = 0; (ii < numLines) && (row + newRows < textRows); ii++) {
    newRows += viRowsFor(state,
      (ii == 0) ? state->width : viReadLine(state, line + ii, false));
  }
  viShiftRows(state, row + ((oldRows < newRows) ? oldRows : newRows),
    newRows - oldRows);
  int end = row + newRows;
  while ((row < end) && (row < textRows)) {
    row += viDrawLine(state, line, row);
    line++;
  }
  if (newRows < oldRows) {
    viFillBottom(state, row, line, textRows - (oldRows - newRows));
  }
  return args;
}
