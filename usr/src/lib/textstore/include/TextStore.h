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

/// @file TextStore.h
///
/// @brief File-backed storage of lines of text:  an append-only text file
/// holding every line's text and a table of the offset of each line in it.
/// The functions are inline so that callers in any overlay can use them
/// without an overlay call per line.

#ifndef TEXT_STORE_H
#define TEXT_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "NanoOsUtils.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def TEXTSTORE_BUFFER_SIZE
///
/// @brief The size, in bytes, of the buffer text is moved through.
#define TEXTSTORE_BUFFER_SIZE 96

/// @def TEXTSTORE_MEMORY_RESERVE
///
/// @brief The number of bytes of dynamic memory left free for the rest of the
/// system when the line table grows.
#define TEXTSTORE_MEMORY_RESERVE 256

/// @def TEXTSTORE_TABLE_GROWTH
///
/// @brief The number of lines the line table grows by at a time.
#define TEXTSTORE_TABLE_GROWTH 16

/// @enum TextStoreError
///
/// @brief The errors a TextStore reports.  A client that keeps its own errors
/// in a TextStore's error member numbers them from NUM_TEXTSTORE_ERRORS.
typedef enum TextStoreError {
  TEXTSTORE_ERROR_NONE,
  TEXTSTORE_ERROR_IO,
  TEXTSTORE_ERROR_MEMORY,
  TEXTSTORE_ERROR_WRITE,
  NUM_TEXTSTORE_ERRORS
} TextStoreError;

/// @struct TextStore
///
/// @brief The text file and line table of a sequence of lines.
///
/// @param text The text file that holds the text of every line.
/// @param textEnd The size, in bytes, of the text file.
/// @param lines The text-file offset of each line.  Line n is at index n - 1.
/// @param numLines The number of lines.
/// @param capacity The number of entries allocated in lines.
/// @param buffer A buffer of TEXTSTORE_BUFFER_SIZE bytes that text is moved
///   through, provided by the client.
/// @param error The TextStoreError of the operation that failed, or a code of
///   the client's own.
/// @param path The path of the text file.
typedef struct TextStore {
  FILE     *text;
  uint32_t  textEnd;
  uint32_t *lines;
  int       numLines;
  int       capacity;
  char     *buffer;
  uint8_t   error;
  char      path[16];
} TextStore;

/// @fn static inline bool textstoreOpen(TextStore *store, const char *prefix)
///
/// @brief Create an empty text file under /tmp, named with a prefix and the
/// process ID.
///
/// @param store The TextStore to open.
/// @param prefix The start of the text file's name.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreOpen(TextStore *store, const char *prefix) {
  // /tmp may already exist, so a failure here is only a problem if the text
  // file can't be created below.
  mkdir("/tmp", 0777);
  snprintf(store->path, sizeof(store->path), "/tmp/%s%d", prefix, getpid());
  store->text = fopen(store->path, "w+");
  if (store->text == NULL) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }
  return true;
}

/// @fn static inline bool textstoreReset(TextStore *store)
///
/// @brief Remove every line.
///
/// @param store The TextStore to empty.
///
/// @return Returns true on success, false with store->error set on failure,
/// in which case the store has no text file.
static inline bool textstoreReset(TextStore *store) {
  // Reopening the text file empties it, so every old line is gone.
  fclose(store->text);
  store->text = fopen(store->path, "w+");
  if (store->text == NULL) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }
  store->textEnd = 0;
  store->numLines = 0;
  return true;
}

/// @fn static inline void textstoreClose(TextStore *store)
///
/// @brief Close and remove the text file.
///
/// @param store The TextStore to close.
///
/// @return This function returns no value.
static inline void textstoreClose(TextStore *store) {
  fclose(store->text);
  remove(store->path);
  store->text = NULL;
}

/// @fn static inline uint32_t textstoreLineOffset(TextStore *store, int line)
///
/// @brief Get the text-file offset of a line.
///
/// @param store The TextStore holding the line.
/// @param line The number of the line.
///
/// @return Returns the offset.
static inline uint32_t textstoreLineOffset(TextStore *store, int line) {
  return store->lines[line - 1];
}

/// @fn static inline void textstoreSetLineOffset(TextStore *store, int line,
///   uint32_t offset)
///
/// @brief Make a line's text the text at a text-file offset.
///
/// @param store The TextStore holding the line.
/// @param line The number of the line.
/// @param offset The text-file offset of the line's new text.
///
/// @return This function returns no value.
static inline void textstoreSetLineOffset(
  TextStore *store, int line, uint32_t offset
) {
  store->lines[line - 1] = offset;
}

/// @fn static inline int textstoreFindLine(TextStore *store, uint32_t offset)
///
/// @brief Find the first line whose text is at a text-file offset.
///
/// @param store The TextStore to search.
/// @param offset The text-file offset.
///
/// @return Returns the number of the line, or 0 if no line has that offset.
static inline int textstoreFindLine(TextStore *store, uint32_t offset) {
  for (int ii = 0; ii < store->numLines; ii++) {
    if (store->lines[ii] == offset) {
      return ii + 1;
    }
  }
  return 0;
}

/// @fn static inline bool textstoreAppendText(TextStore *store,
///   const char *text)
///
/// @brief Append text to the end of the text file.
///
/// @param store The TextStore to append to.
/// @param text The text to append.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreAppendText(TextStore *store, const char *text) {
  size_t length = strlen(text);
  if ((fseek(store->text, (long) store->textEnd, SEEK_SET) != 0)
    || (fwrite(text, 1, length, store->text) != length)
  ) {
    store->error = TEXTSTORE_ERROR_IO;
    return false;
  }

  store->textEnd += length;
  return true;
}

/// @fn static inline bool textstoreInsertLine(TextStore *store, int after,
///   uint32_t offset)
///
/// @brief Insert a line, growing the line table if needed while leaving
/// TEXTSTORE_MEMORY_RESERVE bytes of dynamic memory free.
///
/// @param store The TextStore to insert into.
/// @param after The number of the line to insert after, 0 for the start.
/// @param offset The text-file offset of the line's text.
///
/// @return Returns true on success, false with store->error set on failure.
static inline bool textstoreInsertLine(
  TextStore *store, int after, uint32_t offset
) {
  if (store->numLines == store->capacity) {
    size_t newSize
      = (size_t) (store->capacity + TEXTSTORE_TABLE_GROWTH) * sizeof(uint32_t);
    uint32_t *lines = NULL;
    if (getFreeMemory() >= newSize + TEXTSTORE_MEMORY_RESERVE) {
      lines = (uint32_t*) realloc(store->lines, newSize);
    }
    if (lines == NULL) {
      store->error = TEXTSTORE_ERROR_MEMORY;
      return false;
    }
    store->lines = lines;
    store->capacity += TEXTSTORE_TABLE_GROWTH;
  }

  memmove(&store->lines[after + 1], &store->lines[after],
    (size_t) (store->numLines - after) * sizeof(uint32_t));
  store->lines[after] = offset;
  store->numLines++;
  return true;
}

/// @fn static inline void textstoreDeleteLines(TextStore *store, int first,
///   int last)
///
/// @brief Remove a range of lines.
///
/// @param store The TextStore to remove the lines from.
/// @param first The number of the first line to remove.
/// @param last The number of the last line to remove.
///
/// @return This function returns no value.
static inline void textstoreDeleteLines(TextStore *store, int first, int last) {
  memmove(&store->lines[first - 1], &store->lines[last],
    (size_t) (store->numLines - last) * sizeof(uint32_t));
  store->numLines -= last - first + 1;
}

/// @fn static inline void textstoreReverse(uint32_t *lines, int count)
///
/// @brief Reverse the order of a run of line-table entries.
///
/// @param lines The first entry of the run.
/// @param count The number of entries in the run.
///
/// @return This function returns no value.
static inline void textstoreReverse(uint32_t *lines, int count) {
  for (int ii = 0, jj = count - 1; ii < jj; ii++, jj--) {
    uint32_t swap = lines[ii];
    lines[ii] = lines[jj];
    lines[jj] = swap;
  }
}

/// @fn static inline void textstoreMoveLines(TextStore *store, int first,
///   int last, int destination)
///
/// @brief Move a range of lines to after another line, which must not be in
/// the range.  Moving them is a rotation of the line table, done by three
/// reversals so that it needs no extra memory.
///
/// @param store The TextStore holding the lines.
/// @param first The number of the first line to move.
/// @param last The number of the last line to move.
/// @param destination The number of the line to move them after, 0 for the
///   start.
///
/// @return This function returns no value.
static inline void textstoreMoveLines(
  TextStore *store, int first, int last, int destination
) {
  int count = last - first + 1;
  int start = first - 1;
  int end = last;
  if (destination < first) {
    start = destination;
  } else {
    end = destination;
  }
  textstoreReverse(&store->lines[start], end - start);
  if (destination < first) {
    textstoreReverse(&store->lines[start], count);
    textstoreReverse(&store->lines[start + count], end - start - count);
  } else {
    textstoreReverse(&store->lines[start], end - start - count);
    textstoreReverse(&store->lines[end - count], count);
  }
}

/// @fn static inline long textstoreCopyLine(TextStore *store, int line,
///   FILE *out, bool newline)
///
/// @brief Copy the text of a line to a stream, which may be the text file
/// itself.
///
/// @param store The TextStore holding the line.
/// @param line The number of the line to copy.
/// @param out The stream to copy the line to.
/// @param newline Whether to copy the line's terminating newline.
///
/// @return Returns the number of bytes copied on success, -1 with
/// store->error set on failure.
static inline long textstoreCopyLine(
  TextStore *store, int line, FILE *out, bool newline
) {
  uint32_t position = store->lines[line - 1];
  long copied = 0;
  bool lineEnded = false;
  while (!lineEnded) {
    if ((fseek(store->text, (long) position, SEEK_SET) != 0)
      || (fgets(store->buffer, TEXTSTORE_BUFFER_SIZE, store->text) == NULL)
    ) {
      store->error = TEXTSTORE_ERROR_IO;
      return -1;
    }

    size_t length = strlen(store->buffer);
    position += length;
    lineEnded = (length > 0) && (store->buffer[length - 1] == '\n');
    if (lineEnded && !newline) {
      store->buffer[--length] = '\0';
    }

    if (out == store->text) {
      if (!textstoreAppendText(store, store->buffer)) {
        return -1;
      }
    } else if (fputs(store->buffer, out) == EOF) {
      store->error = TEXTSTORE_ERROR_WRITE;
      return -1;
    }
    copied += (long) length;
  }

  return copied;
}

/// @fn static inline int textstoreReadLines(TextStore *store, FILE *in,
///   int after, bool inputMode, long *bytesRead)
///
/// @brief Read lines from a stream and insert them.
///
/// @param store The TextStore to insert the lines into.
/// @param in The stream to read from.
/// @param after The number of the line to insert the lines after.
/// @param inputMode Whether a line holding only "." ends the input, as it
///   does for text typed after the a, c, and i commands of ed.  On a failure
///   in input mode, the rest of the text is read and discarded so that none
///   of it is taken as commands.
/// @param bytesRead Incremented by the number of bytes read.
///
/// @return Returns the number of the last line inserted (after, if none
/// were) on success, -1 with store->error set on failure.
static inline int textstoreReadLines(
  TextStore *store, FILE *in, int after, bool inputMode, long *bytesRead
) {
  bool lineStart = true;
  while (fgets(store->buffer, TEXTSTORE_BUFFER_SIZE, in) == store->buffer) {
    if (inputMode && lineStart && (strcmp(store->buffer, ".\n") == 0)) {
      break;
    }

    size_t length = strlen(store->buffer);
    if (store->error == TEXTSTORE_ERROR_NONE) {
      if (lineStart && textstoreInsertLine(store, after, store->textEnd)) {
        after++;
      }
      if (store->error == TEXTSTORE_ERROR_NONE) {
        textstoreAppendText(store, store->buffer);
      }
      *bytesRead += (long) length;
      if ((store->error != TEXTSTORE_ERROR_NONE) && !inputMode) {
        return -1;
      }
    }
    lineStart = (length > 0) && (store->buffer[length - 1] == '\n');
  }
  if (store->error != TEXTSTORE_ERROR_NONE) {
    return -1;
  }

  if (!lineStart && !textstoreAppendText(store, "\n")) {
    return -1;
  }

  return after;
}

#ifdef __cplusplus
}
#endif

#endif // TEXT_STORE_H
