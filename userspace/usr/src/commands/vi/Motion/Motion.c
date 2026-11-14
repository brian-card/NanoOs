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

/// @file Motion.c
///
/// @brief The motions of the `vi` screen editor:  finding the line and index
/// a motion reaches, and moving the cursor there or leaving it for an
/// operator.

#include "vi.h"

/// @fn static int viClass(char c)
///
/// @brief Get the class of a character for word motions.
///
/// @param c The character.
///
/// @return Returns 0 for a blank, 1 for a letter, digit, underscore, or byte
/// past ASCII, and 2 for any other character.
static int viClass(char c) {
  unsigned char u = (unsigned char) c;
  if ((u == ' ') || (u == '\t')) {
    return 0;
  }
  return (((u >= 'a') && (u <= 'z')) || ((u >= 'A') && (u <= 'Z'))
    || ((u >= '0') && (u <= '9')) || (u == '_') || (u >= 0x80)) ? 1 : 2;
}

/// @fn static int viAt(ViState *state, int at)
///
/// @brief Get the character at an index of the current line, moving the
/// window onto a long line to hold it.
///
/// @param state The ViState of the running `vi`.
/// @param at The index in the whole line.
///
/// @return Returns the character, or -1 if the index is outside the line.
static int viAt(ViState *state, int at) {
  if (at < 0) {
    return -1;
  } else if (state->truncated && ((at < state->windowStart)
      || (at >= state->windowStart + state->length))
  ) {
    int start = at - (VI_WINDOW_SIZE / 2);
    state->windowStart = (int16_t) ((start > 0) ? start : 0);
    viLoadWindow(state);
  }
  at -= state->windowStart;
  return (at < state->length) ? (unsigned char) state->text[at] : -1;
}

/// @fn static int viEnd(ViState *state)
///
/// @brief Get the length of the current line, moving the window onto a long
/// line to its end.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns the length.
static int viEnd(ViState *state) {
  viAt(state, INT16_MAX);
  return state->windowStart + state->length;
}

/// @fn static int viFirst(ViState *state)
///
/// @brief Find the first character of the current line that isn't a space or
/// a tab, in the whole of a long line.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns its index, or the line's length if there is none.
static int viFirst(ViState *state) {
  int at = 0;
  int c;
  while (((c = viAt(state, at)) == ' ') || (c == '\t')) {
    at++;
  }
  return at;
}

/// @fn static bool viWordStep(ViState *state, int *line, int *index,
///   char motion)
///
/// @brief Move one word with w (to the next word's start), e (to the end of
/// the word), or b (to the start of the word), crossing lines.  Lines are
/// loaded as the current line as they are reached.
///
/// @param state The ViState of the running `vi`.
/// @param line The line, updated.
/// @param index The index in the whole line, updated.
/// @param motion w, e, or b.
///
/// @return Returns true if it moved, false if it was already at the end of
/// the buffer for w and e or the start for b.
static bool viWordStep(ViState *state, int *line, int *index, char motion) {
  int numLines = state->ex.store.numLines;
  int at = *index;
  int startLine = *line;
  int c;
  if (motion == 'b') {
    at--;
    while (((c = viAt(state, at)) < 0) || (viClass((char) c) == 0)) {
      if (at >= 0) {
        at--;
      } else if (*line == 1) {
        at = 0;
        break;
      } else {
        viLoadLine(state, --*line);
        at = viEnd(state) - 1;
        if (at < 0) {
          at = 0;
          break;
        }
      }
    }
    int class = viClass((char) viAt(state, at));
    while ((at > 0) && (class != 0)
      && (viClass((char) viAt(state, at - 1)) == class)
    ) {
      at--;
    }
  } else {
    c = viAt(state, at);
    int class = (c >= 0) ? viClass((char) c) : 0;
    if (motion == 'e') {
      at++;
      class = 0;
    }
    while ((class != 0) && ((c = viAt(state, at)) >= 0)
      && (viClass((char) c) == class)
    ) {
      at++;
    }
    while (((c = viAt(state, at)) < 0) || (viClass((char) c) == 0)) {
      if (c >= 0) {
        at++;
      } else if (*line >= numLines) {
        // The failed viAt left the window at the line's end.
        at = state->windowStart + state->length - 1;
        at = (at > 0) ? at : 0;
        break;
      } else {
        viLoadLine(state, ++*line);
        at = 0;
        if ((state->length == 0) && (motion == 'w')) {
          break;
        }
      }
    }
    if ((motion == 'e') && ((c = viAt(state, at)) >= 0)) {
      class = viClass((char) c);
      while (((c = viAt(state, at + 1)) >= 0)
        && (viClass((char) c) == class)
      ) {
        at++;
      }
    }
  }
  bool moved = (*line != startLine) || (at != *index);
  *index = at;
  return moved;
}

/// @fn static int viFind(ViState *state, int line, const char *pattern,
///   int from, bool forward)
///
/// @brief Find text in a line in the store, reading it in pieces that overlap
/// by the text's length so that a line of any length is searched whole.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line.
/// @param pattern The text.
/// @param from The index to search from.
/// @param forward Whether to find the first match at or after from, rather
///   than the last one before it.
///
/// @return Returns the match's index, or -1 if there is none.
static int viFind(
  ViState *state, int line, const char *pattern, int from, bool forward
) {
  TextStore *store = &state->ex.store;
  int keep = (int) strlen(pattern) - 1;
  int base = 0;
  int kept = 0;
  int found = -1;
  uint32_t offset = textstoreLineOffset(store, line);
  if ((store->error != EX_ERROR_NONE)
    || (fseek(store->text, (long) offset, SEEK_SET) != 0)
  ) {
    return -1;
  }
  while (fgets(&state->buffer[kept], TEXTSTORE_BUFFER_SIZE - kept,
    store->text) != NULL
  ) {
    char *end = strchr(state->buffer, '\n');
    if (end != NULL) {
      *end = '\0';
    }
    for (char *match = state->buffer;
      (match = strstr(match, pattern)) != NULL; match++
    ) {
      int at = base + (int) (match - state->buffer);
      if (at >= from) {
        return forward ? at : found;
      }
      found = at;
    }
    if (end != NULL) {
      break;
    }
    int got = (int) strlen(state->buffer);
    kept = (keep < 0) ? 0 : ((keep < got) ? keep : got);
    memmove(state->buffer, &state->buffer[got - kept], (size_t) kept);
    base += got - kept;
  }
  return forward ? -1 : found;
}

/// @fn static bool viSearch(ViState *state, int *line, int *index)
///
/// @brief Find the next match of the last search pattern, in the pattern's
/// direction for n or the other one for N, wrapping around the buffer.
///
/// @param state The ViState of the running `vi`.
/// @param line The line to start from, updated.
/// @param index The index in the whole line to start from, updated.
///
/// @return Returns true if a match was found, false with
/// state->ex.store.error set otherwise.
static bool viSearch(ViState *state, int *line, int *index) {
  char pattern[VI_PATTERN_SIZE];
  if (!textstoreReadHeader(&state->ex.store, VI_PATTERN_POSITION, pattern,
      sizeof(pattern))
    || (pattern[0] == '\0')
  ) {
    state->ex.store.error = VI_ERROR_NO_PATTERN;
    return false;
  }
  bool forward = (pattern[0] == '/') == (state->motion == 'n');
  int numLines = state->ex.store.numLines;
  int from = *index + (forward ? 1 : 0);
  for (int ii = 0; (ii <= numLines) && (numLines > 0)
    && (state->ex.store.error == EX_ERROR_NONE); ii++
  ) {
    int found = viFind(state, *line, &pattern[1], from, forward);
    if (found >= 0) {
      *index = found;
      return true;
    }
    *line = forward ? ((*line % numLines) + 1)
      : ((*line > 1) ? *line - 1 : numLines);
    from = forward ? 0 : INT16_MAX;
  }
  if (state->ex.store.error == EX_ERROR_NONE) {
    state->ex.store.error = VI_ERROR_NOT_FOUND;
  }
  return false;
}

/// @fn static int viLastVisible(ViState *state, int top)
///
/// @brief Find the last line shown whole on a screen with a line at the top.
///
/// @param state The ViState of the running `vi`.
/// @param top The number of the line at the top.
///
/// @return Returns the line's number.
static int viLastVisible(ViState *state, int top) {
  int row = 0;
  int line = top;
  int numLines = state->ex.store.numLines;
  while (line <= numLines) {
    row += viRowsFor(state, viReadLine(state, line, false));
    if (row > state->rows - 1) {
      break;
    }
    line++;
  }
  return (line > top) ? line - 1 : top;
}

/// @fn void* viRun(void *args)
///
/// @brief Find where state->motion reaches from the cursor, state->count
/// times.  For an operator, put the line in state->ex.addr2, the index in
/// state->targetIndex, and whether the range is whole lines in
/// state->linewise, past the end for motions that include their last
/// character.  The indexes of the cursor and the target are then from the
/// start of the window the cursor was in, and may be outside it on a long
/// line.  With no operator, move the cursor there, moving the window onto a
/// long line to hold it.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  If the motion can't be made,
/// state->ex.store.error is set.
void* viRun(void *args) {
  ViState *state = (ViState*) args;
  int count = (state->count > 0) ? state->count : 1;
  int numLines = (state->ex.store.numLines > 0) ? state->ex.store.numLines : 1;
  int startLine = state->line;
  int line = startLine;
  int base = state->windowStart;
  int cursor = base + state->index;
  int index = cursor;
  bool linewise = false;
  bool inclusive = false;
  int row = 0;
  char motion = state->motion;

  switch (motion) {
    case 'h':
      index -= count;
      break;
    case 'l':
      index += count;
      break;
    case '0':
      index = 0;
      break;
    case '^':
      index = viFirst(state);
      break;
    case '$':
      line += count - 1;
      inclusive = true;
      index = INT16_MAX;
      break;
    case 'j':
    case '+':
    case '_':
      line += (motion == '_') ? count - 1 : count;
      linewise = true;
      break;
    case 'k':
    case '-':
      line -= count;
      linewise = true;
      break;
    case 'G':
    case 'g':
      line = (state->count > 0) ? count : ((motion == 'G') ? numLines : 1);
      linewise = true;
      break;
    case 'H':
    case 'L':
    case 'M': {
      int last = viLastVisible(state, state->top);
      line = (motion == 'H') ? state->top + count - 1
        : ((motion == 'L') ? last - count + 1
          : state->top + ((last - state->top) / 2));
      line = (line < state->top) ? state->top : ((line > last) ? last : line);
      linewise = true;
      break;
    }
    case '\006':
    case '\002':
      if (state->operator != '\0') {
        state->ex.store.error = VI_ERROR_MOTION;
        return args;
      }
      line = state->top;
      for (int ii = 0; ii < count; ii++) {
        line = (motion == '\006') ? viLastVisible(state, line) : line - 1;
      }
      row = (motion == '\006') ? -1 : state->rows;
      linewise = true;
      break;
    case '\'': {
      char mark = state->argument;
      uint32_t offset = 0;
      if ((mark < 'a') || (mark > 'z')
        || ((state->ex.marks & (((uint32_t) 1) << (mark - 'a'))) == 0)
        || !textstoreReadHeader(&state->ex.store, EX_MARK_POSITION(mark),
          &offset, sizeof(offset))
        || ((line = textstoreFindLine(&state->ex.store, offset)) == 0)
      ) {
        state->ex.store.error = EX_ERROR_MARK;
        return args;
      }
      linewise = true;
      break;
    }
    case 'n':
    case 'N':
      if (!viSearch(state, &line, &index)) {
        return args;
      }
      break;
    default:
      for (int ii = 0; ii < count; ii++) {
        if (!viWordStep(state, &line, &index, motion)) {
          break;
        }
      }
      inclusive = (motion == 'e');
      break;
  }

  if ((line < 1) || (line > numLines)) {
    if ((motion != 'j') && (motion != 'k')
      && (motion != '\006') && (motion != '\002')
    ) {
      line = (line < 1) ? 1 : numLines;
    } else if (state->operator == '\0') {
      line = (line < 1) ? 1 : numLines;
      if (line == startLine) {
        state->ex.store.error = VI_ERROR_MOTION;
        return args;
      }
    } else {
      state->ex.store.error = VI_ERROR_MOTION;
      return args;
    }
  }
  if (line != state->line) {
    viLoadLine(state, line);
  }
  if (linewise && (motion != 'j') && (motion != 'k')) {
    index = viFirst(state);
  }
  index = (index < 0) ? 0 : ((index > INT16_MAX) ? INT16_MAX : index);
  if (viAt(state, index) < 0) {
    index = state->windowStart + state->length;
  }

  if (state->operator != '\0') {
    if (inclusive && (index < state->windowStart + state->length)) {
      index++;
    }
    state->ex.addr2 = line;
    state->linewise = linewise;
    if ((state->line != startLine) || (state->windowStart != base)) {
      viLoadLine(state, startLine);
      state->windowStart = (int16_t) base;
      if (base > 0) {
        viLoadWindow(state);
      }
    }
    state->index = (int16_t) (cursor - base);
    state->targetIndex = (int16_t) (index - base);
    return args;
  }

  if (row == 0) {
    row = viRowOf(state, line);
  }
  state->index = (int16_t) (index - state->windowStart);
  viRequest(state, line, row, VI_REQUEST_MOVE);
  return args;
}
