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
  NUM_EX_ERRORS
} ExError;

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
typedef struct ExState {
  TextStore   store;
  char        name[4];
  int         current;
  int         addr1;
  int         addr2;
  int         numAddresses;
  int         destination;
  char        command;
  char       *params;
  char       *filename;
  uint32_t    marks;
  bool        modified;
  char        warnedCommand;
  bool        quiet;
  long        byteCount;
} ExState;

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
