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

/// @file File.c
///
/// @brief Reading the file named on the command line, and the editing
/// commands that read files:  e, E, and r.

#include <sys/stat.h>
#include <unistd.h>
#include "Ex.h"

/// @fn static int exReadFile(ExState *state, FILE *in, int after)
///
/// @brief Read a file into the buffer, print the number of bytes read, and
/// close the file.  If the whole file can't be read, none of it is kept.
///
/// @param state The ExState of the running editor.
/// @param in The open file to read.
/// @param after The number of the line to insert the file's lines after.
///
/// @return Returns the number of the last line read in, or -1 with
/// state->store.error set on failure.
static int exReadFile(ExState *state, FILE *in, int after) {
  int numLines = state->store.numLines;
  long bytesRead = 0;
  int last = textstoreReadLines(&state->store, in, after, false, &bytesRead);
  fclose(in);
  if (last < 0) {
    int inserted = state->store.numLines - numLines;
    textstoreDeleteLines(&state->store, after + 1, after + inserted);
    return -1;
  }

  if (state->store.numLines > numLines) {
    state->modified = true;
  }
  state->byteCount = bytesRead;
  if (!state->quiet) {
    printf("%ld\n", bytesRead);
  }
  return last;
}

/// @fn static void exEditFile(ExState *state)
///
/// @brief Replace the buffer with the contents of a file:  the e and E
/// commands.
///
/// @param state The ExState of the running editor.
///
/// @return This function returns no value.
static void exEditFile(ExState *state) {
  if ((state->command == 'e') && state->modified
    && (state->warnedCommand != 'e')
  ) {
    state->warnedCommand = 'e';
    state->store.error = EX_ERROR_MODIFIED;
    return;
  }

  const char *name = (state->params != NULL)
    ? state->params : state->filename;
  if (name == NULL) {
    state->store.error = EX_ERROR_NO_FILENAME;
    return;
  }
  FILE *in = fopen(name, "r");
  if (in == NULL) {
    state->store.error = EX_ERROR_OPEN_INPUT;
    return;
  }
  if ((state->params != NULL) && !exSetFilename(state, state->params)) {
    fclose(in);
    return;
  }

  if (!textstoreReset(&state->store)) {
    fclose(in);
    return;
  }
  textstoreChainInit(&state->history);
  state->marks = 0;

  if (exReadFile(state, in, 0) < 0) {
    // A later w must not replace the file with an empty buffer.
    free(state->filename);
    state->filename = NULL;
  }
  state->current = state->store.numLines;
  state->modified = false;
}

/// @fn void* exStart(void *args)
///
/// @brief Create the text file and read in the file named on the command
/// line, if any.
///
/// @param args A pointer to the ExState of the running editor, cast to a
///   void*.  Its params member is the file named on the command line, or
///   NULL.
///
/// @return Returns the args pointer provided.  If the text file can't be
/// created, the ExState's store.text member is left NULL.
void* exStart(void *args) {
  ExState *state = (ExState*) args;

  textstoreChainInit(&state->history);
  if (!textstoreOpen(&state->store, state->name)) {
    fprintf(stderr, "%s: cannot create a temporary file\n", state->name);
    return args;
  }
  if (state->params == NULL) {
    return args;
  }

  if (!exSetFilename(state, state->params)) {
    return args;
  }
  FILE *in = fopen(state->filename, "r");
  if (in == NULL) {
    state->store.error = EX_ERROR_OPEN_INPUT;
    return args;
  }
  if (exReadFile(state, in, 0) < 0) {
    // A later w must not replace the file with an empty buffer.
    free(state->filename);
    state->filename = NULL;
  }
  state->current = state->store.numLines;
  state->modified = false;

  return args;
}

/// @fn void* exFile(void *args)
///
/// @brief Run one of the commands that read files.
///
/// @param args A pointer to the ExState of the running editor, cast to a
///   void*.
///
/// @return Returns the args pointer provided.  Errors are left in the
/// ExState's store.error member.
void* exFile(void *args) {
  ExState *state = (ExState*) args;

  if ((state->numAddresses != 0)
    && ((state->command == 'e') || (state->command == 'E'))
  ) {
    state->store.error = EX_ERROR_ADDRESS;
    return args;
  }

  switch (state->command) {
    case 'e':
    case 'E':
      exEditFile(state);
      return args;

    case 'r': {
      int after = (state->numAddresses == 0)
        ? state->store.numLines : state->addr2;
      const char *name = exFilename(state);
      if (name == NULL) {
        return args;
      }
      FILE *in = fopen(name, "r");
      if (in == NULL) {
        state->store.error = EX_ERROR_OPEN_INPUT;
        return args;
      }
      int last = exReadFile(state, in, after);
      if ((last > after)
        && exBeginChange(state) && exRecordLines(state, after + 1, last)
      ) {
        state->current = last;
      }
      return args;
    }

  }

  return args;
}
