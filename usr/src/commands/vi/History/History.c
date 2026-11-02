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

/// @var viHistoryMessages
///
/// @brief The messages for running out of changes to undo or redo.
static const char *const viHistoryMessages[] = {
  "Already at oldest change",
  "Already at newest change",
};

/// @fn void* viUndo(void *args)
///
/// @brief Undo (state->ex.command u) or redo (r) a change and redraw the
/// screen, keeping the top line if the cursor's line can be shown with it.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.
void* viUndo(void *args) {
  ViState *state = (ViState*) args;
  if (callOverlayFunction(EX_LIBRARY_PATH, "Undo", "exUndo", &state->ex)
    == NULL
  ) {
    state->ex.store.error = EX_ERROR_MEMORY;
  }
  uint8_t error = state->ex.store.error;
  if (error != EX_ERROR_NONE) {
    if ((error == EX_ERROR_OLDEST) || (error == EX_ERROR_NEWEST)) {
      viStatus(state, viHistoryMessages[error - EX_ERROR_OLDEST]);
      state->ex.store.error = EX_ERROR_NONE;
    }
    return args;
  }

  int line = (state->ex.current > 0) ? state->ex.current : 1;
  int row = -1;
  if (line >= state->top) {
    row = 0;
    for (int ii = state->top; (ii < line) && (row < state->rows); ii++) {
      row += viRowsFor(state, viReadLine(state, ii, false));
    }
  }
  state->index = state->ex.column;
  if (viShowLine(state, line, row, false)) {
    viDrawFrom(state, 0, state->top);
  }
  viStatus(state, "");
  return args;
}
