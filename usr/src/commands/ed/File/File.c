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
/// @brief Start-up and the file commands of the `ed` line editor:  e, E, r,
/// w, and W.

#include <sys/stat.h>
#include <unistd.h>
#include "ed.h"

/// @fn static bool edSetFilename(EdState *state, const char *name)
///
/// @brief Make a name the current filename.
///
/// @param state The EdState of the running `ed`.
/// @param name The name to use.
///
/// @return Returns true on success, false with state->error set on failure.
static bool edSetFilename(EdState *state, const char *name) {
  char *filename = (char*) realloc(state->filename, strlen(name) + 1);
  if (filename == NULL) {
    state->error = ED_ERROR_MEMORY;
    return false;
  }
  state->filename = strcpy(filename, name);
  return true;
}

/// @fn static const char* edFilename(EdState *state)
///
/// @brief Get the filename a command operates on:  the one given with it,
/// which becomes the current filename if there isn't one yet, or else the
/// current filename.
///
/// @param state The EdState of the running `ed`.
///
/// @return Returns the filename, or NULL with state->error set if there is
/// none.
static const char* edFilename(EdState *state) {
  if (state->params == NULL) {
    if (state->filename == NULL) {
      state->error = ED_ERROR_NO_FILENAME;
    }
    return state->filename;
  }

  if ((state->filename == NULL) && !edSetFilename(state, state->params)) {
    return NULL;
  }
  return state->params;
}

/// @fn static int edReadFile(EdState *state, FILE *in, int after)
///
/// @brief Read a file into the buffer, print the number of bytes read, and
/// close the file.  If the whole file can't be read, none of it is kept.
///
/// @param state The EdState of the running `ed`.
/// @param in The open file to read.
/// @param after The number of the line to insert the file's lines after.
///
/// @return Returns the number of the last line read in, or -1 with
/// state->error set on failure.
static int edReadFile(EdState *state, FILE *in, int after) {
  int numLines = state->numLines;
  bool modified = state->modified;
  long bytesRead = 0;
  int last = edReadLines(state, in, after, false, &bytesRead);
  fclose(in);
  if (last < 0) {
    int inserted = state->numLines - numLines;
    memmove(&state->lines[after], &state->lines[after + inserted],
      (size_t) (numLines - after) * sizeof(uint32_t));
    state->numLines = numLines;
    state->modified = modified;
    return -1;
  }

  printf("%ld\n", bytesRead);
  return last;
}

/// @fn static void edEditFile(EdState *state)
///
/// @brief Replace the buffer with the contents of a file:  the e and E
/// commands.
///
/// @param state The EdState of the running `ed`.
///
/// @return This function returns no value.
static void edEditFile(EdState *state) {
  if ((state->command == 'e') && state->modified
    && (state->warnedCommand != 'e')
  ) {
    state->warnedCommand = 'e';
    state->error = ED_ERROR_MODIFIED;
    return;
  }

  const char *name = (state->params != NULL)
    ? state->params : state->filename;
  if (name == NULL) {
    state->error = ED_ERROR_NO_FILENAME;
    return;
  }
  FILE *in = fopen(name, "r");
  if (in == NULL) {
    state->error = ED_ERROR_OPEN_INPUT;
    return;
  }
  if ((state->params != NULL) && !edSetFilename(state, state->params)) {
    fclose(in);
    return;
  }

  // Reopening the scratch file empties it, so every old line is gone.
  fclose(state->scratch);
  state->scratch = fopen(state->scratchPath, "w+");
  if (state->scratch == NULL) {
    fclose(in);
    state->error = ED_ERROR_SCRATCH;
    return;
  }
  state->scratchEnd = 0;
  state->numLines = 0;
  free(state->marks);
  state->marks = NULL;

  if (edReadFile(state, in, 0) < 0) {
    // A later w must not replace the file with an empty buffer.
    free(state->filename);
    state->filename = NULL;
  }
  state->current = state->numLines;
  state->modified = false;
}

/// @fn static void edWriteFile(EdState *state)
///
/// @brief Write lines to a file:  the w and W commands.  The w command
/// writes to a temporary file that replaces the target only once it is
/// complete, so a failed write leaves the target as it was.
///
/// @param state The EdState of the running `ed`.
///
/// @return This function returns no value.
static void edWriteFile(EdState *state) {
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
    state->addr2 = state->numLines;
  } else if (state->addr1 < 1) {
    state->error = ED_ERROR_ADDRESS;
    return;
  }
  const char *name = edFilename(state);
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
      state->error = ED_ERROR_MEMORY;
      return;
    }
    strcat(strcpy(tempName, name), "~");
    out = fopen(tempName, "w");
  }
  if (out == NULL) {
    free(tempName);
    state->error = ED_ERROR_OPEN_OUTPUT;
    return;
  }

  long bytesWritten = 0;
  for (int line = state->addr1;
    (line <= state->addr2) && (state->error == ED_ERROR_NONE); line++
  ) {
    long copied = edCopyLine(state, line, out, true);
    if (copied >= 0) {
      bytesWritten += copied;
    }
  }
  fclose(out);

  if (tempName != NULL) {
    if ((state->error == ED_ERROR_NONE) && (rename(tempName, name) != 0)) {
      state->error = ED_ERROR_WRITE;
    }
    if (state->error != ED_ERROR_NONE) {
      remove(tempName);
    }
    free(tempName);
  }
  if (state->error != ED_ERROR_NONE) {
    return;
  }

  printf("%ld\n", bytesWritten);
  if ((state->addr1 == 1) && (state->addr2 == state->numLines)) {
    state->modified = false;
  }
  if (quitAfter) {
    state->command = 'q';
    state->params = NULL;
    state->numAddresses = 0;
  }
}

/// @fn void* edStart(void *args)
///
/// @brief Create the scratch file and read in the file named on the command
/// line, if any.
///
/// @param args A pointer to the EdState of the running `ed`, cast to a void*.
///   Its params member is the file named on the command line, or NULL.
///
/// @return Returns the args pointer provided.  If the scratch file can't be
/// created, the EdState's scratch member is left NULL.
void* edStart(void *args) {
  EdState *state = (EdState*) args;

  // /tmp may already exist, so a failure here is only a problem if the
  // scratch file can't be created below.
  mkdir("/tmp", 0777);
  snprintf(state->scratchPath, sizeof(state->scratchPath),
    "/tmp/ed%d", getpid());
  state->scratch = fopen(state->scratchPath, "w+");
  if (state->scratch == NULL) {
    fprintf(stderr, "ed: cannot create %s\n", state->scratchPath);
    return args;
  }

  if (state->params != NULL) {
    if (!edSetFilename(state, state->params)) {
      return args;
    }
    FILE *in = fopen(state->filename, "r");
    if (in == NULL) {
      state->error = ED_ERROR_OPEN_INPUT;
      return args;
    }
    if (edReadFile(state, in, 0) < 0) {
      // A later w must not replace the file with an empty buffer.
      free(state->filename);
      state->filename = NULL;
    }
    state->current = state->numLines;
    state->modified = false;
  }

  return args;
}

/// @fn void* edFile(void *args)
///
/// @brief Run one of the commands that read or write files.  After a
/// successful wq, the EdState's command member is changed to q.
///
/// @param args A pointer to the EdState of the running `ed`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in the
/// EdState's error member.
void* edFile(void *args) {
  EdState *state = (EdState*) args;

  if ((state->numAddresses != 0)
    && ((state->command == 'e') || (state->command == 'E'))
  ) {
    state->error = ED_ERROR_ADDRESS;
    return args;
  }

  switch (state->command) {
    case 'e':
    case 'E':
      edEditFile(state);
      return args;

    case 'r': {
      int after = (state->numAddresses == 0)
        ? state->numLines : state->addr2;
      const char *name = edFilename(state);
      if (name == NULL) {
        return args;
      }
      FILE *in = fopen(name, "r");
      if (in == NULL) {
        state->error = ED_ERROR_OPEN_INPUT;
        return args;
      }
      int last = edReadFile(state, in, after);
      if (last > after) {
        state->current = last;
      }
      return args;
    }

    case 'w':
    case 'W':
      edWriteFile(state);
      break;
  }

  return args;
}
