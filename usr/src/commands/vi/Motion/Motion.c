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

/// @fn static bool viWordStep(ViState *state, int *line, int *index,
///   char motion)
///
/// @brief Move one word with w (to the next word's start), e (to the end of
/// the word), or b (to the start of the word), crossing lines.  Lines are
/// loaded as the current line as they are reached.
///
/// @param state The ViState of the running `vi`.
/// @param line The line, updated.
/// @param index The index, updated.
/// @param motion w, e, or b.
///
/// @return Returns true if it moved, false if it was already at the end of
/// the buffer for w and e or the start for b.
static bool viWordStep(ViState *state, int *line, int *index, char motion) {
  int numLines = state->ex.store.numLines;
  int at = *index;
  int startLine = *line;
  if (motion == 'b') {
    at--;
    while ((at < 0) || (viClass(state->text[at]) == 0)) {
      if (at >= 0) {
        at--;
      } else if (*line == 1) {
        at = 0;
        break;
      } else {
        viLoadLine(state, --*line);
        at = state->length - 1;
        if (at < 0) {
          at = 0;
          break;
        }
      }
    }
    int class = viClass(state->text[at]);
    while ((at > 0) && (class != 0) && (viClass(state->text[at - 1]) == class)) {
      at--;
    }
  } else {
    int class = (at < state->length) ? viClass(state->text[at]) : 0;
    if (motion == 'e') {
      at++;
      class = 0;
    }
    while ((at < state->length) && (class != 0)
      && (viClass(state->text[at]) == class)
    ) {
      at++;
    }
    while ((at >= state->length) || (viClass(state->text[at]) == 0)) {
      if (at < state->length) {
        at++;
      } else if (*line >= numLines) {
        at = (state->length > 0) ? state->length - 1 : 0;
        break;
      } else {
        viLoadLine(state, ++*line);
        at = 0;
        if ((state->length == 0) && (motion == 'w')) {
          break;
        }
      }
    }
    if ((motion == 'e') && (at < state->length)) {
      class = viClass(state->text[at]);
      while ((at + 1 < state->length)
        && (viClass(state->text[at + 1]) == class)
      ) {
        at++;
      }
    }
  }
  bool moved = (*line != startLine) || (at != *index);
  *index = at;
  return moved;
}

/// @fn static bool viSearch(ViState *state, int *line, int *index)
///
/// @brief Find the next match of the last search pattern, in the pattern's
/// direction for n or the other one for N, wrapping around the buffer.  Only
/// the part of each line the line buffer holds is searched.
///
/// @param state The ViState of the running `vi`.
/// @param line The line to start from, updated.
/// @param index The index to start from, updated.
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
  if (numLines == 0) {
    state->ex.store.error = VI_ERROR_NOT_FOUND;
    return false;
  }
  int at = *index;
  for (int ii = 0; ii <= numLines; ii++) {
    char *match = NULL;
    if (forward) {
      if (at < state->length) {
        match = strstr(&state->text[at + 1 - (ii > 0)], &pattern[1]);
      }
    } else {
      for (char *found = state->text;
        ((found = strstr(found, &pattern[1])) != NULL)
          && ((ii > 0) || (found - state->text < at));
        found++
      ) {
        match = found;
      }
    }
    if (match != NULL) {
      *index = (int) (match - state->text);
      return true;
    }
    *line = forward ? ((*line % numLines) + 1)
      : ((*line > 1) ? *line - 1 : numLines);
    viLoadLine(state, *line);
    at = 0;
  }
  state->ex.store.error = VI_ERROR_NOT_FOUND;
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

/// @fn void* viMotion(void *args)
///
/// @brief Find where state->motion reaches from the cursor, state->count
/// times.  For an operator, put the line in state->ex.addr2, the index in
/// state->targetIndex, and whether the range is whole lines in
/// state->linewise, past the end for motions that include their last
/// character.  With no operator, move the cursor there.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  If the motion can't be made,
/// state->ex.store.error is set.
void* viMotion(void *args) {
  ViState *state = (ViState*) args;
  int count = (state->count > 0) ? state->count : 1;
  int numLines = (state->ex.store.numLines > 0) ? state->ex.store.numLines : 1;
  int startLine = state->line;
  int line = startLine;
  int index = state->index;
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
      index = viFirstNonBlank(state);
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
        viLoadLine(state, startLine);
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
    index = viFirstNonBlank(state);
  }
  if (index > state->length) {
    index = state->length;
  }
  if (index < 0) {
    index = 0;
  }

  if (state->operator != '\0') {
    if (inclusive && (index < state->length)) {
      index++;
    }
    state->ex.addr2 = line;
    state->targetIndex = (int16_t) index;
    state->linewise = linewise;
    viLoadLine(state, startLine);
    return args;
  }

  if (row == 0) {
    viLoadLine(state, startLine);
    row = viRowOf(state, line);
  }
  state->index = (int16_t) index;
  viRequest(state, line, row, VI_REQUEST_MOVE);
  return args;
}
