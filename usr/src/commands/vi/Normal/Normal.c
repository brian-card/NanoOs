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
/// character some commands take, running the overlay that does the work, and
/// recording a change for . to repeat.

#include "vi.h"

/// @fn static void viRun(ViState *state, const char *overlay,
///   const char *function)
///
/// @brief Run a function in another of `vi`'s overlays.
///
/// @param state The ViState of the running `vi`.
/// @param overlay The name of the overlay.
/// @param function The name of the function to run.
///
/// @return This function returns no value.  If the overlay can't be loaded,
/// state->ex.store.error is set.
static void viRun(ViState *state, const char *overlay, const char *function) {
  if (callOverlayFunction(OVERLAY_SAME_NAMESPACE, overlay, function, state)
    == NULL
  ) {
    state->ex.store.error = EX_ERROR_MEMORY;
  }
}

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

/// @fn static void viKeepInsertedText(ViState *state, ViDot *dot, bool exact)
///
/// @brief Keep the text insert mode inserted for . to repeat, or make the
/// command one . can't repeat if the text isn't simply what was inserted on
/// one line, or is too long to keep.
///
/// @param state The ViState of the running `vi`, with insert mode just ended.
/// @param dot The command being recorded.
/// @param exact Whether insert mode stayed on one line.
///
/// @return This function returns no value.
static void viKeepInsertedText(ViState *state, ViDot *dot, bool exact) {
  int length = state->targetIndex - state->insertStart;
  if (!exact || (state->insertStart < 0) || (length > (int) VI_DOT_TEXT_SIZE)) {
    dot->numKeys = 0;
  } else if ((length == 0)
    || textstoreWriteHeader(&state->ex.store, VI_DOT_TEXT_POSITION,
      &state->text[state->insertStart], (size_t) length)
  ) {
    dot->textLength = (uint8_t) length;
  }
}

/// @fn static void viInsertLoop(ViState *state, ViDot *dot, char command)
///
/// @brief Run insert mode, splitting the line at each Return, until Escape.
///
/// @param state The ViState of the running `vi`.
/// @param dot The command being recorded or repeated.
/// @param command The command that starts insert mode:  i, a, I, or A.
///
/// @return This function returns no value.
static void viInsertLoop(ViState *state, ViDot *dot, char command) {
  bool exact = true;
  state->ex.command = command;
  state->targetIndex = dot->textLength;
  while (state->ex.store.error == EX_ERROR_NONE) {
    viRun(state, "Insert", "viInsert");
    char key = state->ex.command;
    if ((key != '\n') && (key != VI_KEY_ESCAPE)) {
      break;
    } else if (key == '\n') {
      exact = false;
    } else if (!state->repeating) {
      viKeepInsertedText(state, dot, exact);
    }
    viRun(state, "Put", "viPut");
    if (key != '\n') {
      break;
    }
    state->ex.command = 'i';
  }
}

/// @fn static void viOperate(ViState *state, ViDot *dot, char operator,
///   int key)
///
/// @brief Run an operator over the range of a motion.  A doubled operator,
/// such as dd, works on whole lines.
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
  viRun(state, "Motion", "viMotion");
  if (state->ex.store.error == EX_ERROR_NONE) {
    viRun(state, "Operator", "viOperator");
  }
  if ((operator == 'c') && (state->ex.store.error == EX_ERROR_NONE)) {
    viInsertLoop(state, dot, 'i');
  }
}

/// @fn static void viDispatch(ViState *state, ViDot *dot, int key)
///
/// @brief Run a normal-mode command from the key after its count.
///
/// @param state The ViState of the running `vi`.
/// @param dot The command being recorded or repeated.
/// @param key The key.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void viDispatch(ViState *state, ViDot *dot, int key) {
  if (viReadMotion(state, dot, key)) {
    viRun(state, "Motion", "viMotion");
    return;
  }

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
      // fall through
    case 'J':
      state->ex.command = (char) key;
      viRun(state, "Change", "viChange");
      break;

    case 'p':
    case 'P':
      if (state->yankKind == VI_YANK_TEXT) {
        state->ex.command = (char) key;
        viRun(state, "Change", "viChange");
        break;
      }
      // fall through
    case 'o':
    case 'O':
      state->ex.command = (char) key;
      viRun(state, "Put", "viPut");
      if ((key == 'o') || (key == 'O')) {
        viInsertLoop(state, dot, 'i');
      }
      break;

    case 'i':
    case 'a':
    case 'I':
    case 'A':
      viInsertLoop(state, dot, (char) key);
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
      viRun(state, "Command", "viCommand");
      if ((key == '/') || (key == '?')) {
        if ((state->ex.store.error == EX_ERROR_NONE) && !state->quit) {
          state->motion = 'n';
          viRun(state, "Motion", "viMotion");
        }
      }
      break;

    case 'u':
    case 0x12:
      state->ex.command = (key == 'u') ? 'u' : 'r';
      viRun(state, "History", "viUndo");
      break;
  }
}

/// @fn void* viNormal(void *args)
///
/// @brief Run a normal-mode command.  state->ex.command is its first key.  A
/// command that changes the buffer is recorded for . to repeat.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viNormal(void *args) {
  ViState *state = (ViState*) args;
  TextStore *store = &state->ex.store;
  ViDot dot;
  dot.numKeys = 0;
  dot.textLength = 0;
  dot.next = 0;
  state->repeating = false;
  state->count = 0;
  int key = (unsigned char) state->ex.command;
  viRecordKey(&dot, key);
  key = viReadCount(state, &dot, key, &state->count);
  state->beginChange = true;
  state->ex.column = state->index;
  state->operator = '\0';

  if (key == '.') {
    int16_t count = state->count;
    if (!textstoreReadHeader(store, VI_DOT_POSITION, &dot, sizeof(dot))
      || (dot.numKeys == 0) || (dot.numKeys > VI_DOT_KEYS)
    ) {
      return args;
    }
    state->count = (count > 0) ? count : dot.count;
    dot.next = 0;
    state->repeating = true;
    key = viNextKey(state, &dot);
  }

  viDispatch(state, &dot, key);
  if (!state->beginChange) {
    if (dot.numKeys > VI_DOT_KEYS) {
      dot.numKeys = 0;
    }
    dot.count = state->count;
    textstoreWriteHeader(store, VI_DOT_POSITION, &dot, sizeof(dot));
  }
  return args;
}
