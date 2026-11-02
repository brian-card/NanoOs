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

/// @file Write.c
///
/// @brief The editing commands that write files:  w and W.

#include "Ex.h"

/// @fn static void exWriteFile(ExState *state)
///
/// @brief Write lines to a file:  the w and W commands.  The w command
/// writes to a temporary file that replaces the target only once it is
/// complete, so a failed write leaves the target as it was.
///
/// @param state The ExState of the running editor.
///
/// @return This function returns no value.
static void exWriteFile(ExState *state) {
  bool quitAfter = false;
  if ((state->command == 'w') && (state->params != NULL)
    && (state->params[0] == 'q')
    && ((state->params[1] == '\0') || (state->params[1] == ' '))
  ) {
    quitAfter = true;
    state->params = (state->params[1] == ' ') ? &state->params[2] : NULL;
  }

  if (state->numAddresses == 0) {
    state->addr1 = 1;
    state->addr2 = state->store.numLines;
  } else if (state->addr1 < 1) {
    state->store.error = EX_ERROR_ADDRESS;
    return;
  }
  const char *name = exFilename(state);
  if (name == NULL) {
    return;
  }

  char *tempName = NULL;
  FILE *out = NULL;
  if (state->command == 'W') {
    out = fopen(name, "a");
  } else {
    tempName = (char*) malloc(strlen(name) + 2);
    if (tempName == NULL) {
      state->store.error = EX_ERROR_MEMORY;
      return;
    }
    strcat(strcpy(tempName, name), "~");
    out = fopen(tempName, "w");
  }
  if (out == NULL) {
    free(tempName);
    state->store.error = EX_ERROR_OPEN_OUTPUT;
    return;
  }

  long bytesWritten = 0;
  for (int line = state->addr1;
    (line <= state->addr2) && (state->store.error == EX_ERROR_NONE); line++
  ) {
    long copied = textstoreCopyLine(&state->store, line, out, true);
    if (copied >= 0) {
      bytesWritten += copied;
    }
  }
  fclose(out);

  if (tempName != NULL) {
    if ((state->store.error == EX_ERROR_NONE)
      && (rename(tempName, name) != 0)
    ) {
      state->store.error = EX_ERROR_WRITE;
    }
    if (state->store.error != EX_ERROR_NONE) {
      remove(tempName);
    }
    free(tempName);
  }
  if (state->store.error != EX_ERROR_NONE) {
    return;
  }

  printf("%ld\n", bytesWritten);
  if ((state->addr1 == 1) && (state->addr2 == state->store.numLines)) {
    state->modified = false;
  }
  if (quitAfter) {
    state->command = 'q';
    state->params = NULL;
    state->numAddresses = 0;
  }
}

/// @fn void* exWrite(void *args)
///
/// @brief Run one of the commands that write files.  After a successful wq,
/// the ExState's command member is changed to q.
///
/// @param args A pointer to the ExState of the running editor, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in the
/// ExState's store.error member.
void* exWrite(void *args) {
  exWriteFile((ExState*) args);
  return args;
}
