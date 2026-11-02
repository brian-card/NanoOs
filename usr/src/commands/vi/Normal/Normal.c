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
/// character some commands take, and running the overlay that does the work.

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

/// @fn static int viReadCount(ViState *state, int key, int16_t *count)
///
/// @brief Read the digits of a count.
///
/// @param state The ViState of the running `vi`.
/// @param key The key read already.
/// @param count The count, which the digits are added to.
///
/// @return Returns the first key after the count.
static int viReadCount(ViState *state, int key, int16_t *count) {
  while (((key >= '1') && (key <= '9')) || ((*count > 0) && (key == '0'))) {
    if (*count < 1000) {
      *count = (int16_t) ((*count * 10) + (key - '0'));
    }
    key = viReadKey(state);
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

/// @fn static bool viReadMotion(ViState *state, int key)
///
/// @brief Take a key as a motion, reading the second g of gg or the mark
/// after a quote.
///
/// @param state The ViState of the running `vi`.
/// @param key The key.
///
/// @return Returns true if the key is a motion, false otherwise.
static bool viReadMotion(ViState *state, int key) {
  key = viMotionKey(key);
  if ((key == 0) || ((key == 'g') && (viReadKey(state) != 'g'))) {
    return false;
  }
  if (key == '\'') {
    state->argument = (char) viReadKey(state);
  }
  state->motion = (char) key;
  return true;
}

/// @fn static void viInsertLoop(ViState *state, char command)
///
/// @brief Run insert mode, splitting the line at each Return, until Escape.
///
/// @param state The ViState of the running `vi`.
/// @param command The command that starts insert mode:  i, a, I, or A.
///
/// @return This function returns no value.
static void viInsertLoop(ViState *state, char command) {
  state->ex.command = command;
  while (state->ex.store.error == EX_ERROR_NONE) {
    viRun(state, "Insert", "viInsert");
    char key = state->ex.command;
    if ((key != '\n') && (key != VI_KEY_ESCAPE)) {
      break;
    }
    viRun(state, "Put", "viPut");
    if (key != '\n') {
      break;
    }
    state->ex.command = 'i';
  }
}

/// @fn static void viOperate(ViState *state, char operator, int key)
///
/// @brief Run an operator over the range of a motion.  A doubled operator,
/// such as dd, works on whole lines.
///
/// @param state The ViState of the running `vi`.
/// @param operator The operator:  d, c, or y.
/// @param key The key after the operator.
///
/// @return This function returns no value.
static void viOperate(ViState *state, char operator, int key) {
  int16_t count = 0;
  key = viReadCount(state, key, &count);
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
  } else if (!viReadMotion(state, key)) {
    return;
  }
  state->operator = operator;
  viRun(state, "Motion", "viMotion");
  if (state->ex.store.error == EX_ERROR_NONE) {
    viRun(state, "Operator", "viOperator");
  }
  if ((operator == 'c') && (state->ex.store.error == EX_ERROR_NONE)) {
    viInsertLoop(state, 'i');
  }
}

/// @fn void* viNormal(void *args)
///
/// @brief Run a normal-mode command.  state->ex.command is its first key.
///
/// @param args A pointer to the ViState of the running `vi`, cast to a void*.
///
/// @return Returns the args pointer provided.  Errors are left in
/// state->ex.store.error.
void* viNormal(void *args) {
  ViState *state = (ViState*) args;
  state->count = 0;
  int key = viReadCount(state, (unsigned char) state->ex.command,
    &state->count);
  state->beginChange = true;
  state->ex.column = state->index;
  state->operator = '\0';

  if (viReadMotion(state, key)) {
    viRun(state, "Motion", "viMotion");
    return args;
  }

  switch (key) {
    case 'd':
    case 'c':
    case 'y':
      viOperate(state, (char) key, viReadKey(state));
      break;

    case 'D':
    case 'C':
      viOperate(state, (char) (key + ('a' - 'A')), '$');
      break;

    case 'Y':
      viOperate(state, 'y', 'y');
      break;

    case 'S':
      viOperate(state, 'c', 'c');
      break;

    case 'x':
    case 'X':
      viOperate(state, 'd', (key == 'x') ? 'l' : 'h');
      break;

    case 'r':
    case 'm':
      state->argument = (char) viReadKey(state);
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
        viInsertLoop(state, 'i');
      }
      break;

    case 'i':
    case 'a':
    case 'I':
    case 'A':
      viInsertLoop(state, (char) key);
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
  return args;
}
