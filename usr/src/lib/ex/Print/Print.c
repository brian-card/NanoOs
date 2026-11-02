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

/// @file Print.c
///
/// @brief The editing commands that print lines:  p, n, and a command with
/// only an address, or nothing, which prints one line.  They are in an
/// overlay of their own so that the code that finds a line in the block file
/// stays out of the overlays that run the command loops.

#include "Ex.h"

/// @fn void* exPrint(void *args)
///
/// @brief Print the lines from state->addr1 to state->addr2, numbered for the
/// n command, and make the last one the current line.  With no command and no
/// address, print the line after the current line.
///
/// @param args A pointer to the ExState of the running editor, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in the
/// ExState's store.error member.
void* exPrint(void *args) {
  ExState *state = (ExState*) args;

  if (state->command == '\0') {
    if (state->numAddresses == 0) {
      state->addr2 = state->current + 1;
      if (state->addr2 > state->store.numLines) {
        state->store.error = EX_ERROR_ADDRESS;
        return args;
      }
    }
    state->addr1 = state->addr2;
  } else if (state->params != NULL) {
    state->store.error = EX_ERROR_SUFFIX;
    return args;
  }
  if (state->addr1 < 1) {
    state->store.error = EX_ERROR_ADDRESS;
    return args;
  }

  for (int line = state->addr1; line <= state->addr2; line++) {
    if (state->command == 'n') {
      printf("%d\t", line);
    }
    if (textstoreCopyLine(&state->store, line, stdout, true) < 0) {
      return args;
    }
  }

  state->current = state->addr2;
  return args;
}
