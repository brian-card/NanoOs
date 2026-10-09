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

/// @file ed.h
///
/// @brief State and helpers shared by the overlays of the `ed` command.

#ifndef ED_H
#define ED_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NanoOsUtils.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def ED_BUFFER_SIZE
///
/// @brief The size, in bytes, of the buffers that text is moved through.
#define ED_BUFFER_SIZE 96

/// @def ED_MEMORY_RESERVE
///
/// @brief The number of bytes of dynamic memory that `ed` leaves free for the
/// rest of the system when it grows its line table.
#define ED_MEMORY_RESERVE 256

/// @def ED_TABLE_GROWTH
///
/// @brief The number of lines the line table grows by at a time.
#define ED_TABLE_GROWTH 16

/// @def ED_NUM_MARKS
///
/// @brief The number of marks, 'a' through 'z'.
#define ED_NUM_MARKS 26

/// @enum EdError
///
/// @brief The errors `ed` reports.  Their text is in the main overlay.
typedef enum EdError {
  ED_ERROR_NONE,
  ED_ERROR_ADDRESS,
  ED_ERROR_COMMAND,
  ED_ERROR_SUFFIX,
  ED_ERROR_NO_FILENAME,
  ED_ERROR_OPEN_INPUT,
  ED_ERROR_OPEN_OUTPUT,
  ED_ERROR_WRITE,
  ED_ERROR_MEMORY,
  ED_ERROR_MODIFIED,
  ED_ERROR_MARK,
  ED_ERROR_DESTINATION,
  ED_ERROR_SCRATCH,
  NUM_ED_ERRORS
} EdError;

/// @struct EdState
///
/// @brief Everything `ed` keeps between commands.  Overlays keep no state of
/// their own, so all of it lives here.
///
/// @param scratch The scratch file that holds the text of every line.
/// @param scratchEnd The size, in bytes, of the scratch file.
/// @param lines The scratch-file offset of each line in the buffer.  Line n
///   is at index n - 1.
/// @param numLines The number of lines in the buffer.
/// @param capacity The number of entries allocated in lines.
/// @param current The current line number.
/// @param addr1 The first address of the command being run.
/// @param addr2 The second address of the command being run.
/// @param numAddresses The number of addresses given with the command.
/// @param destination The destination address of an m or t command.
/// @param command The command character of the command being run.
/// @param params The text after the command character, or NULL if none.
/// @param commandBuffer The buffer a command line is read into.
/// @param commandBufferSize The size, in bytes, of commandBuffer.
/// @param textBuffer The buffer text is moved through.
/// @param filename The current filename, or NULL if none.
/// @param marks The scratch-file offset of each marked line, or NULL if no
///   line has been marked.
/// @param error The EdError of the command being run.
/// @param lastError The EdError of the last command that failed.
/// @param modified Whether the buffer has changed since it was last written.
/// @param warnedCommand The command, q or e, that was just refused because
///   the buffer was modified, or '\0' if none.
/// @param prompt Whether to print a prompt before reading a command.
/// @param verbose Whether to print the text of each error.
/// @param quit Whether `ed` is to exit.
/// @param scratchPath The path of the scratch file.
typedef struct EdState {
  FILE     *scratch;
  uint32_t  scratchEnd;
  uint32_t *lines;
  int       numLines;
  int       capacity;
  int       current;
  int       addr1;
  int       addr2;
  int       numAddresses;
  int       destination;
  char      command;
  char     *params;
  char     *commandBuffer;
  int       commandBufferSize;
  char     *textBuffer;
  char     *filename;
  uint32_t *marks;
  uint8_t   error;
  uint8_t   lastError;
  bool      modified;
  char      warnedCommand;
  bool      prompt;
  bool      verbose;
  bool      quit;
  char      scratchPath[16];
} EdState;

/// @fn static inline bool edAppendText(EdState *state, const char *text)
///
/// @brief Append text to the end of the scratch file.
///
/// @param state The EdState of the running `ed`.
/// @param text The text to append.
///
/// @return Returns true on success, false with state->error set on failure.
static inline bool edAppendText(EdState *state, const char *text) {
  size_t length = strlen(text);
  if ((fseek(state->scratch, (long) state->scratchEnd, SEEK_SET) != 0)
    || (fwrite(text, 1, length, state->scratch) != length)
  ) {
    state->error = ED_ERROR_SCRATCH;
    return false;
  }

  state->scratchEnd += length;
  return true;
}

/// @fn static inline bool edInsertLine(EdState *state, int after,
///   uint32_t offset)
///
/// @brief Insert a line into the buffer, growing the line table if needed
/// while leaving ED_MEMORY_RESERVE bytes of dynamic memory free, and mark the
/// buffer modified.
///
/// @param state The EdState of the running `ed`.
/// @param after The number of the line to insert after, 0 for the start.
/// @param offset The scratch-file offset of the line's text.
///
/// @return Returns true on success, false with state->error set on failure.
static inline bool edInsertLine(EdState *state, int after, uint32_t offset) {
  if (state->numLines == state->capacity) {
    size_t newSize
      = (size_t) (state->capacity + ED_TABLE_GROWTH) * sizeof(uint32_t);
    uint32_t *lines = NULL;
    if (getFreeMemory() >= newSize + ED_MEMORY_RESERVE) {
      lines = (uint32_t*) realloc(state->lines, newSize);
    }
    if (lines == NULL) {
      state->error = ED_ERROR_MEMORY;
      return false;
    }
    state->lines = lines;
    state->capacity += ED_TABLE_GROWTH;
  }

  memmove(&state->lines[after + 1], &state->lines[after],
    (size_t) (state->numLines - after) * sizeof(uint32_t));
  state->lines[after] = offset;
  state->numLines++;
  state->modified = true;
  return true;
}

/// @fn static inline long edCopyLine(EdState *state, int line, FILE *out,
///   bool newline)
///
/// @brief Copy the text of a line to a stream, which may be the scratch file
/// itself.
///
/// @param state The EdState of the running `ed`.
/// @param line The number of the line to copy.
/// @param out The stream to copy the line to.
/// @param newline Whether to copy the line's terminating newline.
///
/// @return Returns the number of bytes copied on success, -1 with
/// state->error set on failure.
static inline long edCopyLine(
  EdState *state, int line, FILE *out, bool newline
) {
  uint32_t position = state->lines[line - 1];
  long copied = 0;
  bool lineEnded = false;
  while (!lineEnded) {
    if ((fseek(state->scratch, (long) position, SEEK_SET) != 0)
      || (fgets(state->textBuffer, ED_BUFFER_SIZE, state->scratch) == NULL)
    ) {
      state->error = ED_ERROR_SCRATCH;
      return -1;
    }

    size_t length = strlen(state->textBuffer);
    position += length;
    lineEnded = (length > 0) && (state->textBuffer[length - 1] == '\n');
    if (lineEnded && !newline) {
      state->textBuffer[--length] = '\0';
    }

    if (out == state->scratch) {
      if (!edAppendText(state, state->textBuffer)) {
        return -1;
      }
    } else if (fputs(state->textBuffer, out) == EOF) {
      state->error = ED_ERROR_WRITE;
      return -1;
    }
    copied += (long) length;
  }

  return copied;
}

/// @fn static inline int edReadLines(EdState *state, FILE *in, int after,
///   bool inputMode, long *bytesRead)
///
/// @brief Read lines from a stream into the buffer.
///
/// @param state The EdState of the running `ed`.
/// @param in The stream to read from.
/// @param after The number of the line to insert the lines after.
/// @param inputMode Whether a line holding only "." ends the input, as it
///   does for text typed after the a, c, and i commands.  On a failure in
///   input mode, the rest of the text is read and discarded so that none of
///   it is taken as commands.
/// @param bytesRead Incremented by the number of bytes read.
///
/// @return Returns the number of the last line inserted (after, if none
/// were) on success, -1 with state->error set on failure.
static inline int edReadLines(
  EdState *state, FILE *in, int after, bool inputMode, long *bytesRead
) {
  bool lineStart = true;
  while (fgets(state->textBuffer, ED_BUFFER_SIZE, in) == state->textBuffer) {
    if (inputMode && lineStart && (strcmp(state->textBuffer, ".\n") == 0)) {
      break;
    }

    size_t length = strlen(state->textBuffer);
    if (state->error == ED_ERROR_NONE) {
      if (lineStart && edInsertLine(state, after, state->scratchEnd)) {
        after++;
      }
      if (state->error == ED_ERROR_NONE) {
        edAppendText(state, state->textBuffer);
      }
      *bytesRead += (long) length;
      if ((state->error != ED_ERROR_NONE) && !inputMode) {
        return -1;
      }
    }
    lineStart = (length > 0) && (state->textBuffer[length - 1] == '\n');
  }
  if (state->error != ED_ERROR_NONE) {
    return -1;
  }

  if (!lineStart && !edAppendText(state, "\n")) {
    return -1;
  }

  return after;
}

#ifdef __cplusplus
}
#endif

#endif // ED_H
