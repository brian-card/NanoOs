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
/// commands that read or write files:  e, E, r, w, and W.

#include <sys/stat.h>
#include <unistd.h>
#include "Ex.h"

/// @fn static bool exSetFilename(ExState *state, const char *name)
///
/// @brief Make a name the current filename.
///
/// @param state The ExState of the running `ed`.
/// @param name The name to use.
///
/// @return Returns true on success, false with state->store.error set on failure.
static bool exSetFilename(ExState *state, const char *name) {
  char *filename = (char*) realloc(state->filename, strlen(name) + 1);
  if (filename == NULL) {
    state->store.error = EX_ERROR_MEMORY;
    return false;
  }
  state->filename = strcpy(filename, name);
  return true;
}

/// @fn static const char* exFilename(ExState *state)
///
/// @brief Get the filename a command operates on:  the one given with it,
/// which becomes the current filename if there isn't one yet, or else the
/// current filename.
///
/// @param state The ExState of the running `ed`.
///
/// @return Returns the filename, or NULL with state->store.error set if there is
/// none.
static const char* exFilename(ExState *state) {
  if (state->params == NULL) {
    if (state->filename == NULL) {
      state->store.error = EX_ERROR_NO_FILENAME;
    }
    return state->filename;
  }

  if ((state->filename == NULL) && !exSetFilename(state, state->params)) {
    return NULL;
  }
  return state->params;
}

/// @fn static int exReadFile(ExState *state, FILE *in, int after)
///
/// @brief Read a file into the buffer, print the number of bytes read, and
/// close the file.  If the whole file can't be read, none of it is kept.
///
/// @param state The ExState of the running `ed`.
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
  printf("%ld\n", bytesRead);
  return last;
}

/// @fn static void exEditFile(ExState *state)
///
/// @brief Replace the buffer with the contents of a file:  the e and E
/// commands.
///
/// @param state The ExState of the running `ed`.
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
  free(state->marks);
  state->marks = NULL;

  if (exReadFile(state, in, 0) < 0) {
    // A later w must not replace the file with an empty buffer.
    free(state->filename);
    state->filename = NULL;
  }
  state->current = state->store.numLines;
  state->modified = false;
}

/// @fn static void exWriteFile(ExState *state)
///
/// @brief Write lines to a file:  the w and W commands.  The w command
/// writes to a temporary file that replaces the target only once it is
/// complete, so a failed write leaves the target as it was.
///
/// @param state The ExState of the running `ed`.
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
    if ((state->store.error == EX_ERROR_NONE) && (rename(tempName, name) != 0)) {
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

  if (!textstoreOpen(&state->store, state->name)) {
    fprintf(stderr, "%s: cannot create %s\n", state->name, state->store.path);
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
/// @brief Run one of the commands that read or write files.  After a
/// successful wq, the ExState's command member is changed to q.
///
/// @param args A pointer to the ExState of the running `ed`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in the
/// ExState's error member.
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
      if (last > after) {
        state->current = last;
      }
      return args;
    }

    case 'w':
    case 'W':
      exWriteFile(state);
      break;
  }

  return args;
}
