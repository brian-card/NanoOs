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

/// @file Normal.c
///
/// @brief Normal-mode commands of the `vi` screen editor other than the
/// commonest movements:  reading a count, an operator and its motion, and the
/// character some commands take, choosing the overlays that do the work, and
/// recording a change for . to repeat.  The main overlay runs each overlay
/// chosen and then this one again, so that no overlay call is nested inside
/// another one.

#include "vi.h"

/// @fn static void viRecordKey(ViDot *dot, int key)
///
/// @brief Add a key to the command being recorded for . to repeat.  A key
/// that isn't a byte, or one too many, makes the command one . can't repeat.
///
/// @param dot The command being recorded.
/// @param key The key.
///
/// @return This function returns no value.
static void viRecordKey(ViDot *dot, int key) {
  if ((key < 0) || (key >= VI_KEY_NONE) || (dot->numKeys >= VI_DOT_KEYS)) {
    dot->numKeys = UINT8_MAX;
    return;
  }
  dot->keys[dot->numKeys++] = (char) key;
}

/// @fn static int viNextKey(ViState *state, ViDot *dot)
///
/// @brief Read the next key of a command and record it, or, when . is
/// repeating the command, take the next key recorded.
///
/// @param state The ViState of the running `vi`.
/// @param dot The command being recorded or repeated.
///
/// @return Returns the key, or Escape after the last key repeated.
static int viNextKey(ViState *state, ViDot *dot) {
  if (state->repeating) {
    return (dot->next < dot->numKeys)
      ? (unsigned char) dot->keys[dot->next++] : VI_KEY_ESCAPE;
  }
  int key = viReadKey(state);
  viRecordKey(dot, key);
  return key;
}

/// @fn static int viReadCount(ViState *state, ViDot *dot, int key,
///   int16_t *count)
///
/// @brief Read the digits of a count.  The digits aren't recorded for . to
/// repeat:  the count is kept apart from the keys.
///
/// @param state The ViState of the running `vi`.
/// @param dot The command being recorded or repeated.  key is the last key
///   recorded in it.
/// @param key The key read already.
/// @param count The count, which the digits are added to.
///
/// @return Returns the first key after the count.
static int viReadCount(ViState *state, ViDot *dot, int key, int16_t *count) {
  bool digits = false;
  while (((key >= '1') && (key <= '9')) || ((*count > 0) && (key == '0'))) {
    if (*count < 1000) {
      *count = (int16_t) ((*count * 10) + (key - '0'));
    }
    key = viReadKey(state);
    digits = true;
  }
  if (digits) {
    dot->numKeys--;
    viRecordKey(dot, key);
  }
  return key;
}

/// @fn static int viMotionKey(int key)
///
/// @brief Map a key to the motion it is, if any.
///
/// @param key The key.
///
/// @return Returns the motion's key, which is the key itself for most
/// motions, or 0 if the key isn't a motion.
static int viMotionKey(int key) {
  switch (key) {
    case VI_KEY_LEFT:
    case '\b':
      return 'h';
    case VI_KEY_RIGHT:
    case ' ':
      return 'l';
    case VI_KEY_DOWN:
      return 'j';
    case VI_KEY_UP:
      return 'k';
    case '\r':
    case '\n':
      return '+';
  }
  return ((key > 0) && (strchr("hjkl0^$wbeGgHML+-'nN\006\002", key) != NULL))
    ? key : 0;
}

/// @fn static bool viReadMotion(ViState *state, ViDot *dot, int key)
///
/// @brief Take a key as a motion, reading the second g of gg or the mark
/// after a quote.
///
/// @param state The ViState of the running `vi`.
/// @param dot The command being recorded or repeated.
/// @param key The key.
///
/// @return Returns true if the key is a motion, false otherwise.
static bool viReadMotion(ViState *state, ViDot *dot, int key) {
  key = viMotionKey(key);
  if ((key == 0) || ((key == 'g') && (viNextKey(state, dot) != 'g'))) {
    return false;
  }
  if (key == '\'') {
    state->argument = (char) viNextKey(state, dot);
  }
  state->motion = (char) key;
  return true;
}

/// @fn static void viReadDot(ViState *state, size_t position, ViDot *dot)
///
/// @brief Read a ViDot from the block file's header area.  A ViDot that
/// can't be read is one with no keys.
///
/// @param state The ViState of the running `vi`.
/// @param position The ViDot's position in the header area.
/// @param dot Set to the ViDot.
///
/// @return This function returns no value.
static void viReadDot(ViState *state, size_t position, ViDot *dot) {
  if (!textstoreReadHeader(&state->ex.store, position, dot, sizeof(*dot))) {
    dot->numKeys = 0;
    dot->textLength = 0;
  }
}

/// @fn static void viKeepInsertedText(ViState *state)
///
/// @brief Keep the text insert mode inserted for . to repeat, or make the
/// command one . can't repeat if the text isn't simply what was inserted on
/// one line, or is too long to keep.
///
/// @param state The ViState of the running `vi`, with insert mode just ended.
///
/// @return This function returns no value.
static void viKeepInsertedText(ViState *state) {
  TextStore *store = &state->ex.store;
  ViDot dot;
  viReadDot(state, VI_DOT_PENDING_POSITION, &dot);
  int length = state->targetIndex - state->insertStart;
  if (!state->exact || (state->insertStart == VI_NO_INSERT_START)
    || (length > (int) VI_DOT_TEXT_SIZE)
  ) {
    dot.numKeys = 0;
  } else if (state->truncated) {
    // The main overlay has put the window back, so the text is in the store.
    uint32_t offset = textstoreLineOffset(store, state->line)
      + (uint32_t) (state->windowStart + state->insertStart);
    if ((store->error == EX_ERROR_NONE)
      && (fseek(store->blocks, (long) VI_DOT_TEXT_POSITION, SEEK_SET) == 0)
      && (textstoreCopyRange(store, offset, (uint32_t) length, store->blocks)
        >= 0)
    ) {
      dot.textLength = (uint8_t) length;
    }
  } else if ((length == 0)
    || textstoreWriteHeader(store, VI_DOT_TEXT_POSITION,
      &state->text[state->insertStart], (size_t) length)
  ) {
    dot.textLength = (uint8_t) length;
  }
  textstoreWriteHeader(store, VI_DOT_PENDING_POSITION, &dot, sizeof(dot));
}

/// @fn static void viStartInsert(ViState *state, char command,
///   bool afterStep)
///
/// @brief Make insert mode the next step, and remember whether it replaces, so
/// that it carries on the same way after Return.  When . is repeating a change
/// and insert mode follows another step, the Insert overlay is given the
/// length of the text to insert again; viBeginCommand gives it when insert
/// mode is the first step.  I and A on a window onto a long line first move
/// the window to the line's start or end.
///
/// @param state The ViState of the running `vi`.
/// @param command The command that starts insert mode:  i, a, I, A, or R.
/// @param afterStep Whether insert mode follows another step.
///
/// @return This function returns no value.
static void viStartInsert(ViState *state, char command, bool afterStep) {
  if (state->repeating && afterStep) {
    ViDot dot;
    viReadDot(state, VI_DOT_PENDING_POSITION, &dot);
    state->targetIndex = dot.textLength;
  }
  state->ex.command = command;
  state->argument = (command == 'R') ? 'R' : 'i';
  state->step = (state->truncated && ((command == 'I') || (command == 'A')))
    ? VI_STEP_WINDOW : VI_STEP_INSERT;
}

/// @fn static void viOperate(ViState *state, ViDot *dot, char operator,
///   int key)
///
/// @brief Read the motion of an operator and make the motion the next step.
/// A doubled operator, such as dd, works on whole lines.
///
/// @param state The ViState of the running `vi`.
/// @param dot The command being recorded or repeated.
/// @param operator The operator:  d, c, or y.
/// @param key The key after the operator.
///
/// @return This function returns no value.
static void viOperate(ViState *state, ViDot *dot, char operator, int key) {
  int16_t count = 0;
  key = viReadCount(state, dot, key, &count);
  if (count > 0) {
    state->count = (int16_t) (((state->count > 0) ? state->count : 1) * count);
  }
  if ((operator == 'c') && (key == 'w')
    && (state->text[state->index] != '\0')
    && (state->text[state->index] != ' ') && (state->text[state->index] != '\t')
  ) {
    key = 'e';
  }
  if (key == operator) {
    state->motion = '_';
  } else if (!viReadMotion(state, dot, key)) {
    return;
  }
  state->operator = operator;
  state->step = VI_STEP_MOTION;
}

/// @fn static void viParse(ViState *state, ViDot *dot, int key)
///
/// @brief Read a normal-mode command from the key after its count and choose
/// its first step.
///
/// @param state The ViState of the running `vi`.
/// @param dot The command being recorded or repeated.
/// @param key The key.
///
/// @return This function returns no value.
static void viParse(ViState *state, ViDot *dot, int key) {
  switch (key) {
    case 'd':
    case 'c':
    case 'y':
      viOperate(state, dot, (char) key, viNextKey(state, dot));
      break;

    case 'D':
    case 'C':
      viOperate(state, dot, (char) (key + ('a' - 'A')), '$');
      break;

    case 'Y':
      viOperate(state, dot, 'y', 'y');
      break;

    case 'S':
      viOperate(state, dot, 'c', 'c');
      break;

    case 'x':
    case 'X':
      viOperate(state, dot, 'd', (key == 'x') ? 'l' : 'h');
      break;

    case 'r':
    case 'm':
      state->argument = (char) viNextKey(state, dot);
      state->ex.command = (char) key;
      state->step = VI_STEP_CHANGE;
      break;

    case 'J':
      state->ex.command = (char) key;
      state->step = VI_STEP_RANGE;
      break;

    case '~':
      state->ex.command = (char) key;
      state->targetIndex = (int16_t)
        (state->index + ((state->count > 0) ? state->count : 1));
      state->step = VI_STEP_WINDOW;
      break;

    case 'p':
    case 'P':
      state->ex.command = (char) key;
      state->step = (state->yankKind == VI_YANK_TEXT)
        ? VI_STEP_CHANGE : VI_STEP_PUT;
      break;

    case 'o':
    case 'O':
      state->ex.command = (char) key;
      state->step = VI_STEP_PUT;
      break;

    case 'i':
    case 'a':
    case 'I':
    case 'A':
    case 'R':
      viStartInsert(state, (char) key, false);
      break;

    case 'Z':
      if (viReadKey(state) != 'Z') {
        break;
      }
      key = 'x';
      // fall through
    case ':':
    case '/':
    case '?':
      state->ex.command = (char) key;
      state->motion = ((key == '/') || (key == '?')) ? 'n' : '\0';
      state->step = VI_STEP_COMMAND;
      break;

    case 'u':
    case 0x12:
      state->ex.command = (key == 'u') ? 'u' : 'r';
      state->step = VI_STEP_HISTORY;
      break;
  }
}

/// @fn static void viContinue(ViState *state)
///
/// @brief Choose the step that follows the one just run, if any.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static void viContinue(ViState *state) {
  uint8_t step = state->step;
  char command = state->ex.command;
  state->step = VI_STEP_NONE;
  if (state->ex.store.error != EX_ERROR_NONE) {
    return;
  }

  if (step == VI_STEP_MOTION) {
    if (state->operator == '\0') {
      return;
    } else if (state->truncated && !state->linewise) {
      state->ex.command = state->operator;
      state->step = VI_STEP_RANGE;
    } else {
      state->step = VI_STEP_OPERATOR;
    }
  } else if ((step == VI_STEP_OPERATOR)
    || ((step == VI_STEP_RANGE) && (command == 'c'))
  ) {
    if (state->operator == 'c') {
      viStartInsert(state, 'i', true);
    }
  } else if (step == VI_STEP_PUT) {
    if ((command == 'o') || (command == 'O')) {
      viStartInsert(state, 'i', true);
    } else if (command == '\n') {
      viStartInsert(state, state->argument, true);
    }
  } else if (step == VI_STEP_INSERT) {
    if (command == '\n') {
      state->exact = false;
      state->step = state->truncated ? VI_STEP_WINDOW : VI_STEP_PUT;
    } else if (command == VI_COMMAND_WINDOW) {
      state->step = VI_STEP_WINDOW;
    } else if (command == VI_COMMAND_REPEAT) {
      state->step = VI_STEP_CHANGE;
    } else if (command == VI_KEY_ESCAPE) {
      if (!state->repeating) {
        viKeepInsertedText(state);
      }
      state->step = VI_STEP_PUT;
    }
  } else if (step == VI_STEP_WINDOW) {
    if (command == '\n') {
      viStartInsert(state, state->argument, true);
    } else if (command != '~') {
      state->step = VI_STEP_INSERT;
    }
  } else if (step == VI_STEP_COMMAND) {
    if ((state->motion == 'n') && !state->quit) {
      state->step = VI_STEP_MOTION;
    }
  }
}

/// @fn static void viEndCommand(ViState *state)
///
/// @brief Make the command just run the one . repeats, with its count, if it
/// changed the buffer.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static void viEndCommand(ViState *state) {
  if (state->beginChange) {
    return;
  }
  ViDot dot;
  viReadDot(state, VI_DOT_PENDING_POSITION, &dot);
  if ((dot.numKeys > VI_DOT_KEYS) || !state->exact) {
    dot.numKeys = 0;
  }
  dot.count = state->count;
  textstoreWriteHeader(&state->ex.store, VI_DOT_POSITION, &dot, sizeof(dot));
}

/// @fn static void viBeginCommand(ViState *state)
///
/// @brief Read a normal-mode command, whose first key is state->ex.command,
/// and choose its first step.  A command that might change the buffer is
/// recorded in the header area, for . to repeat if it does.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static void viBeginCommand(ViState *state) {
  ViDot dot;
  dot.numKeys = 0;
  dot.textLength = 0;
  dot.next = 0;
  state->repeating = false;
  state->exact = true;
  state->count = 0;
  int key = (unsigned char) state->ex.command;
  viRecordKey(&dot, key);
  key = viReadCount(state, &dot, key, &state->count);
  state->beginChange = true;
  state->ex.column = state->index;
  state->operator = '\0';

  if (key == '.') {
    int16_t count = state->count;
    viReadDot(state, VI_DOT_POSITION, &dot);
    if ((dot.numKeys == 0) || (dot.numKeys > VI_DOT_KEYS)) {
      return;
    }
    state->count = (count > 0) ? count : dot.count;
    dot.next = 0;
    state->repeating = true;
    key = viNextKey(state, &dot);
  }

  if (viReadMotion(state, &dot, key)) {
    state->step = VI_STEP_MOTION;
    return;
  }
  viParse(state, &dot, key);
  if (state->repeating && (state->step == VI_STEP_INSERT)) {
    state->targetIndex = dot.textLength;
  }
  if (state->step != VI_STEP_NONE) {
    textstoreWriteHeader(&state->ex.store, VI_DOT_PENDING_POSITION, &dot,
      sizeof(dot));
  }
}

/// @fn void* viNormal(void *args)
///
/// @brief Run the next part of a normal-mode command:  read it if
/// state->step is VI_STEP_NONE, or else choose what follows that step, and,
/// when nothing does, end the command.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  state->step is the next step,
/// or VI_STEP_NONE if the command is done.  Errors are left in
/// state->ex.store.error.
void* viNormal(void *args) {
  ViState *state = (ViState*) args;
  if (state->step == VI_STEP_NONE) {
    viBeginCommand(state);
  } else {
    viContinue(state);
  }
  if (state->step == VI_STEP_NONE) {
    viEndCommand(state);
  }
  return args;
}
