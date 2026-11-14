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

/// @file Ex.h
///
/// @brief State, errors, and address parsing shared by the line-editing
/// commands of `ed` and `vi`.  The parsing functions are inline because every
/// command line needs them, and an overlay call per command would be slow.

#ifndef EX_H
#define EX_H

#include "TextStore.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def EX_LIBRARY_PATH
///
/// @brief The directory holding the overlays of the ex library.
#define EX_LIBRARY_PATH "/usr/lib/ex"

/// @def EX_MARK_POSITION
///
/// @brief The position in the TextStore's header area of the text-file offset
/// of a marked line.
#define EX_MARK_POSITION(mark) (((mark) - 'a') * sizeof(uint32_t))

/// @enum ExError
///
/// @brief The errors the editing commands report.  The TextStoreErrors come
/// first so that both kinds share the TextStore's error member.
typedef enum ExError {
  EX_ERROR_NONE = TEXTSTORE_ERROR_NONE,
  EX_ERROR_SCRATCH = TEXTSTORE_ERROR_IO,
  EX_ERROR_MEMORY = TEXTSTORE_ERROR_MEMORY,
  EX_ERROR_WRITE = TEXTSTORE_ERROR_WRITE,
  EX_ERROR_ADDRESS = NUM_TEXTSTORE_ERRORS,
  EX_ERROR_COMMAND,
  EX_ERROR_SUFFIX,
  EX_ERROR_NO_FILENAME,
  EX_ERROR_OPEN_INPUT,
  EX_ERROR_OPEN_OUTPUT,
  EX_ERROR_MODIFIED,
  EX_ERROR_MARK,
  EX_ERROR_DESTINATION,
  EX_ERROR_OLDEST,
  EX_ERROR_NEWEST,
  NUM_EX_ERRORS
} ExError;

/// @enum ExChangeKind
///
/// @brief The kinds of record in the undo history.
typedef enum ExChangeKind {
  EX_CHANGE_GROUP,
  EX_CHANGE_INSERT,
  EX_CHANGE_DELETE,
} ExChangeKind;

/// @struct ExChange
///
/// @brief A record of the undo history.  Each change to the buffer is a group
/// record followed by the line insertions and deletions it made, in order.
///
/// @param kind The ExChangeKind of the record.
/// @param unused Padding.
/// @param column For a group, the cursor's column before the change.
/// @param line For a group, the current line before the change; otherwise
///   the number the line had when it was inserted or deleted.
/// @param offset The text-file offset of the line inserted or deleted.
typedef struct ExChange {
  uint8_t  kind;
  uint8_t  unused;
  uint16_t column;
  int32_t  line;
  uint32_t offset;
} ExChange;

/// @struct ExState
///
/// @brief Everything the editing commands keep between commands.  Overlays
/// keep no state of their own, so all of it lives here.  The ExError of the
/// command being run is store.error.
///
/// @param store The TextStore holding the buffer's lines.
/// @param name The name of the program running the commands, which names its
///   text file and starts its messages.  It is copied here rather than
///   pointed to because a string in the caller's overlay is gone while an ex
///   overlay runs.
/// @param current The current line number.
/// @param addr1 The first address of the command being run.
/// @param addr2 The second address of the command being run.
/// @param numAddresses The number of addresses given with the command.
/// @param destination The destination address of an m or t command.
/// @param command The command character of the command being run.
/// @param params The text after the command character, or NULL if none.
/// @param filename The current filename, or NULL if none.
/// @param marks A bit for each mark, 'a' in bit 0, set once the mark is.  The
///   text-file offset of each marked line is in the store's header area, at
///   EX_MARK_POSITION.
/// @param modified Whether the buffer has changed since it was last written.
/// @param warnedCommand The command, q or e, that was just refused because
///   the buffer was modified, or '\0' if none.
/// @param quiet Whether to leave out the byte count that reading and writing
///   a file print.
/// @param byteCount The number of bytes the last file read or written held.
/// @param history The undo history:  a chain of ExChange records whose
///   position is between the changes undone and the ones not.
/// @param column The cursor's column, recorded with each change for an
///   editor with a cursor, and set by undo.
/// @param undone Whether the last command was an undo, for ed's u, which
///   redoes after an undo.
typedef struct ExState {
  TextStore       store;
  char            name[4];
  int             current;
  int             addr1;
  int             addr2;
  int             numAddresses;
  int             destination;
  char            command;
  char           *params;
  char           *filename;
  uint32_t        marks;
  bool            modified;
  char            warnedCommand;
  bool            quiet;
  long            byteCount;
  TextStoreChain  history;
  int16_t         column;
  bool            undone;
} ExState;

/// @fn static inline bool exRecord(ExState *state, uint8_t kind, int line,
///   uint32_t offset)
///
/// @brief Add a record to the undo history, dropping any changes undone.
///
/// @param state The ExState of the running editor.
/// @param kind The ExChangeKind of the record.
/// @param line The record's line.
/// @param offset The record's offset.
///
/// @return Returns true on success, false with state->store.error set on
/// failure.
static inline bool exRecord(
  ExState *state, uint8_t kind, int line, uint32_t offset
) {
  ExChange change;
  change.kind = kind;
  change.unused = 0;
  change.column = (uint16_t) state->column;
  change.line = line;
  change.offset = offset;
  return textstoreChainAppend(&state->store, &state->history, &change);
}

/// @fn static inline bool exBeginChange(ExState *state)
///
/// @brief Start a change to the buffer in the undo history, with the current
/// line and column as they are before it.
///
/// @param state The ExState of the running editor.
///
/// @return Returns true on success, false with state->store.error set on
/// failure.
static inline bool exBeginChange(ExState *state) {
  state->undone = false;
  return exRecord(state, EX_CHANGE_GROUP, state->current, 0);
}

/// @fn static inline bool exRecordLines(ExState *state, int first, int last)
///
/// @brief Record in the undo history lines that were just inserted.
///
/// @param state The ExState of the running editor.
/// @param first The number of the first line inserted.
/// @param last The number of the last line inserted.
///
/// @return Returns true on success, false with state->store.error set on
/// failure.
static inline bool exRecordLines(ExState *state, int first, int last) {
  for (int line = first; line <= last; line++) {
    uint32_t offset = textstoreLineOffset(&state->store, line);
    if ((state->store.error != EX_ERROR_NONE)
      || !exRecord(state, EX_CHANGE_INSERT, line, offset)
    ) {
      return false;
    }
  }
  return true;
}

/// @fn static inline bool exInsertLine(ExState *state, int after,
///   uint32_t offset)
///
/// @brief Insert a line and record it in the undo history.
///
/// @param state The ExState of the running editor.
/// @param after The number of the line to insert after, 0 for the start.
/// @param offset The text-file offset of the line's text.
///
/// @return Returns true on success, false with state->store.error set on
/// failure.
static inline bool exInsertLine(ExState *state, int after, uint32_t offset) {
  return textstoreInsertLine(&state->store, after, offset)
    && exRecord(state, EX_CHANGE_INSERT, after + 1, offset);
}

/// @fn static inline bool exDeleteLines(ExState *state, int first, int last)
///
/// @brief Record a range of lines in the undo history and remove them.
///
/// @param state The ExState of the running editor.
/// @param first The number of the first line to remove.
/// @param last The number of the last line to remove.
///
/// @return Returns true on success, false with state->store.error set on
/// failure.
static inline bool exDeleteLines(ExState *state, int first, int last) {
  for (int line = first; line <= last; line++) {
    uint32_t offset = textstoreLineOffset(&state->store, line);
    // Each line is the first of the range by the time it is removed.
    if ((state->store.error != EX_ERROR_NONE)
      || !exRecord(state, EX_CHANGE_DELETE, first, offset)
    ) {
      return false;
    }
  }
  textstoreDeleteLines(&state->store, first, last);
  return state->store.error == EX_ERROR_NONE;
}

/// @fn static inline bool exSetLineOffset(ExState *state, int line,
///   uint32_t offset)
///
/// @brief Give a line new text, recorded in the undo history as the old
/// line's deletion and the new one's insertion.
///
/// @param state The ExState of the running editor.
/// @param line The number of the line.
/// @param offset The text-file offset of the line's new text.
///
/// @return Returns true on success, false with state->store.error set on
/// failure.
static inline bool exSetLineOffset(ExState *state, int line, uint32_t offset) {
  uint32_t old = textstoreLineOffset(&state->store, line);
  if ((state->store.error != EX_ERROR_NONE)
    || !exRecord(state, EX_CHANGE_DELETE, line, old)
    || !exRecord(state, EX_CHANGE_INSERT, line, offset)
  ) {
    return false;
  }
  textstoreSetLineOffset(&state->store, line, offset);
  return state->store.error == EX_ERROR_NONE;
}

/// @fn static inline bool exSetFilename(ExState *state, const char *name)
///
/// @brief Make a name the current filename.
///
/// @param state The ExState of the running editor.
/// @param name The name to use.
///
/// @return Returns true on success, false with state->store.error set on
/// failure.
static inline bool exSetFilename(ExState *state, const char *name) {
  char *filename = (char*) realloc(state->filename, strlen(name) + 1);
  if (filename == NULL) {
    state->store.error = EX_ERROR_MEMORY;
    return false;
  }
  state->filename = strcpy(filename, name);
  return true;
}

/// @fn static inline const char* exFilename(ExState *state)
///
/// @brief Get the filename a command operates on:  the one given with it,
/// which becomes the current filename if there isn't one yet, or else the
/// current filename.
///
/// @param state The ExState of the running editor.
///
/// @return Returns the filename, or NULL with state->store.error set if there
/// is none.
static inline const char* exFilename(ExState *state) {
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

/// @fn static inline int exParseNumber(char **text)
///
/// @brief Parse a decimal number.
///
/// @param text A pointer to the text to parse, advanced past the number.
///
/// @return Returns the number, or -1 if the text doesn't start with a digit.
static inline int exParseNumber(char **text) {
  if ((**text < '0') || (**text > '9')) {
    return -1;
  }

  int number = 0;
  while ((**text >= '0') && (**text <= '9')) {
    number = (number * 10) + (**text - '0');
    (*text)++;
  }

  return number;
}

/// @fn static inline bool exParseAddress(ExState *state, char **text,
///   int *line)
///
/// @brief Parse one address:  a base of ".", "$", a number, or a mark,
/// followed by any number of "+n" and "-n" offsets.
///
/// @param state The ExState of the running editor.
/// @param text A pointer to the text to parse, advanced past the address.
/// @param line Set to the line number the address refers to.
///
/// @return Returns true if an address was parsed, false if the text doesn't
/// start with one or it is invalid, in which case state->store.error is set.
static inline bool exParseAddress(ExState *state, char **text, int *line) {
  char *start = *text;
  int number = exParseNumber(text);
  if (number >= 0) {
    *line = number;
  } else if (**text == '.') {
    *line = state->current;
    (*text)++;
  } else if (**text == '$') {
    *line = state->store.numLines;
    (*text)++;
  } else if (**text == '\'') {
    char mark = (*text)[1];
    uint32_t offset = 0;
    if ((mark < 'a') || (mark > 'z')
      || ((state->marks & (((uint32_t) 1) << (mark - 'a'))) == 0)
      || !textstoreReadHeader(&state->store, EX_MARK_POSITION(mark),
        &offset, sizeof(offset))
    ) {
      state->store.error = EX_ERROR_MARK;
      return false;
    }
    *text += 2;
    *line = textstoreFindLine(&state->store, offset);
    if (*line == 0) {
      state->store.error = EX_ERROR_MARK;
      return false;
    }
  } else if ((**text == '+') || (**text == '-')) {
    *line = state->current;
  } else {
    return false;
  }

  while ((**text == '+') || (**text == '-')) {
    int sign = (**text == '+') ? 1 : -1;
    (*text)++;
    int offset = exParseNumber(text);
    *line += sign * ((offset < 0) ? 1 : offset);
  }

  if ((*line < 0) || (*line > state->store.numLines)) {
    *text = start;
    state->store.error = EX_ERROR_ADDRESS;
    return false;
  }

  return true;
}

/// @fn static inline char* exParseCommandLine(ExState *state, char *text)
///
/// @brief Parse the addresses and command character of a command line into
/// state.
///
/// @param state The ExState of the running editor.
/// @param text The command line.
///
/// @return Returns a pointer to the text after the command character, or
/// NULL with state->store.error set if the addresses are invalid.
static inline char* exParseCommandLine(ExState *state, char *text) {
  int line = 0;
  bool haveAddress = exParseAddress(state, &text, &line);
  if (state->store.error != EX_ERROR_NONE) {
    return NULL;
  }

  state->numAddresses = 0;
  state->addr1 = state->addr2 = state->current;
  if ((*text == ',') || (*text == ';')) {
    char separator = *text++;
    if (!haveAddress) {
      line = (separator == ',') ? 1 : state->current;
    }
    if (separator == ';') {
      state->current = line;
    }
    state->addr1 = line;
    if (!exParseAddress(state, &text, &state->addr2)) {
      if (state->store.error != EX_ERROR_NONE) {
        return NULL;
      }
      state->addr2 = state->store.numLines;
    }
    state->numAddresses = 2;
  } else if (haveAddress) {
    state->addr1 = state->addr2 = line;
    state->numAddresses = 1;
  }

  if (state->addr1 > state->addr2) {
    state->store.error = EX_ERROR_ADDRESS;
    return NULL;
  }

  state->command = *text;
  if (*text != '\0') {
    text++;
  }
  return text;
}

#ifdef __cplusplus
}
#endif

#endif // EX_H
