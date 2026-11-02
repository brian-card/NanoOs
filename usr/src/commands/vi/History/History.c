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

/// @fn void* viUndo(void *args)
///
/// @brief Undo (state->ex.command u) or redo (r) a change and ask for the
/// screen to be redrawn, keeping the top line if the cursor's line can be
/// shown with it.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viUndo(void *args) {
  ViState *state = (ViState*) args;
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
  if (state->top > numLines) {
    state->top = 1;
  }
  if (viLoadLine(state, line)) {
    state->index = state->ex.column;
    viRequest(state, line, viRowOf(state, line), VI_REQUEST_REDRAW);
  }
  return args;
}
