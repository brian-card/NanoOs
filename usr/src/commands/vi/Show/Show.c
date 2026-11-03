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

/// @fn void* viRun(void *args)
///
/// @brief Show the lines a VI_REQUEST_SHOW or VI_REQUEST_SPLIT asks for, as
/// viRequestRows describes, or scroll and redraw the screen if the first of
/// them doesn't fit where it was asked to be shown.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  int line = state->requestLine;
  int row = state->requestRow;
  int oldRows = state->ex.addr1;
  int numLines = state->ex.destination;
  int textRows = state->rows - 1;
  int top = state->top;
  if (state->request == VI_REQUEST_SPLIT) {
    viDrawLine(state, state->line, state->lineRow);
  }
  state->request = VI_REQUEST_NONE;
  if (!viShowLine(state, line, row, false) || (state->top != top)) {
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
