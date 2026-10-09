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

/// @file main.c
///
/// @brief Command loop, addressing, and printing for the `ed` line editor.

#include "ed.h"

/// @var edErrorMessages
///
/// @brief The text of each EdError, printed by the h and H commands.
static const char *const edErrorMessages[NUM_ED_ERRORS] = {
  "",
  "invalid address",
  "unknown command",
  "invalid command suffix",
  "no current filename",
  "cannot open input file",
  "cannot open output file",
  "cannot write file",
  "out of memory",
  "warning: buffer modified",
  "invalid mark",
  "invalid destination",
  "scratch file error",
};

/// @fn static bool edReadCommand(EdState *state)
///
/// @brief Read a command line from standard input into state->commandBuffer,
/// without its newline.
///
/// @param state The EdState of the running `ed`.
///
/// @return Returns true if a command was read, false at the end of input.
static bool edReadCommand(EdState *state) {
  int length = 0;
  while (fgets(&state->commandBuffer[length],
    state->commandBufferSize - length, stdin) != NULL
  ) {
    length += (int) strlen(&state->commandBuffer[length]);
    if ((length > 0) && (state->commandBuffer[length - 1] == '\n')) {
      state->commandBuffer[length - 1] = '\0';
      return true;
    }

    if (length + 1 >= state->commandBufferSize) {
      char *commandBuffer = NULL;
      if (getFreeMemory()
        >= (size_t) state->commandBufferSize + ED_BUFFER_SIZE
          + ED_MEMORY_RESERVE
      ) {
        commandBuffer = (char*) realloc(state->commandBuffer,
          (size_t) state->commandBufferSize + ED_BUFFER_SIZE);
      }
      if (commandBuffer == NULL) {
        // Discard the rest of the line so it isn't run as a command.
        while ((fgets(state->textBuffer, ED_BUFFER_SIZE, stdin) != NULL)
          && (strchr(state->textBuffer, '\n') == NULL));
        state->commandBuffer[0] = '\0';
        state->error = ED_ERROR_MEMORY;
        return true;
      }
      state->commandBuffer = commandBuffer;
      state->commandBufferSize += ED_BUFFER_SIZE;
    }
  }

  state->commandBuffer[length] = '\0';
  return length > 0;
}

/// @fn static int edParseNumber(char **text)
///
/// @brief Parse a decimal number.
///
/// @param text A pointer to the text to parse, advanced past the number.
///
/// @return Returns the number, or -1 if the text doesn't start with a digit.
static int edParseNumber(char **text) {
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

/// @fn static bool edParseAddress(EdState *state, char **text, int *line)
///
/// @brief Parse one address:  a base of ".", "$", a number, or a mark,
/// followed by any number of "+n" and "-n" offsets.
///
/// @param state The EdState of the running `ed`.
/// @param text A pointer to the text to parse, advanced past the address.
/// @param line Set to the line number the address refers to.
///
/// @return Returns true if an address was parsed, false if the text doesn't
/// start with one or it is invalid, in which case state->error is set.
static bool edParseAddress(EdState *state, char **text, int *line) {
  char *start = *text;
  int number = edParseNumber(text);
  if (number >= 0) {
    *line = number;
  } else if (**text == '.') {
    *line = state->current;
    (*text)++;
  } else if (**text == '$') {
    *line = state->numLines;
    (*text)++;
  } else if (**text == '\'') {
    char mark = (*text)[1];
    if ((mark < 'a') || (mark > 'z') || (state->marks == NULL)) {
      state->error = ED_ERROR_MARK;
      return false;
    }
    *text += 2;
    *line = 0;
    for (int ii = 0; ii < state->numLines; ii++) {
      if (state->lines[ii] == state->marks[mark - 'a']) {
        *line = ii + 1;
        break;
      }
    }
    if (*line == 0) {
      state->error = ED_ERROR_MARK;
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
    int offset = edParseNumber(text);
    *line += sign * ((offset < 0) ? 1 : offset);
  }

  if ((*line < 0) || (*line > state->numLines)) {
    *text = start;
    state->error = ED_ERROR_ADDRESS;
    return false;
  }

  return true;
}

/// @fn static char* edParseCommandLine(EdState *state)
///
/// @brief Parse the addresses and command character of the command line in
/// state->commandBuffer into state.
///
/// @param state The EdState of the running `ed`.
///
/// @return Returns a pointer to the text after the command character, or
/// NULL with state->error set if the addresses are invalid.
static char* edParseCommandLine(EdState *state) {
  char *text = state->commandBuffer;
  int line = 0;
  bool haveAddress = edParseAddress(state, &text, &line);
  if (state->error != ED_ERROR_NONE) {
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
    if (!edParseAddress(state, &text, &state->addr2)) {
      if (state->error != ED_ERROR_NONE) {
        return NULL;
      }
      state->addr2 = state->numLines;
    }
    state->numAddresses = 2;
  } else if (haveAddress) {
    state->addr1 = state->addr2 = line;
    state->numAddresses = 1;
  }

  if (state->addr1 > state->addr2) {
    state->error = ED_ERROR_ADDRESS;
    return NULL;
  }

  state->command = *text;
  if (*text != '\0') {
    text++;
  }
  return text;
}

/// @fn static bool edPrintLines(EdState *state, bool numbered)
///
/// @brief Print the lines from state->addr1 to state->addr2 and make the last
/// one the current line.
///
/// @param state The EdState of the running `ed`.
/// @param numbered Whether to print each line's number before it.
///
/// @return Returns true on success, false with state->error set on failure.
static bool edPrintLines(EdState *state, bool numbered) {
  if (state->addr1 < 1) {
    state->error = ED_ERROR_ADDRESS;
    return false;
  }

  for (int line = state->addr1; line <= state->addr2; line++) {
    if (numbered) {
      printf("%d\t", line);
    }
    if (edCopyLine(state, line, stdout, true) < 0) {
      return false;
    }
  }

  state->current = state->addr2;
  return true;
}

/// @fn static void edQuit(EdState *state)
///
/// @brief Run the q or Q command:  remove the scratch file and exit, unless
/// the command is q and the buffer has unsaved changes it hasn't been warned
/// about.  This is in the main overlay so that quitting never depends on
/// loading another one.
///
/// @param state The EdState of the running `ed`.
///
/// @return This function returns no value.
static void edQuit(EdState *state) {
  if ((state->command == 'q') && state->modified
    && (state->warnedCommand != 'q')
  ) {
    state->warnedCommand = 'q';
    state->error = ED_ERROR_MODIFIED;
    return;
  }

  fclose(state->scratch);
  remove(state->scratchPath);
  state->scratch = NULL;
  state->quit = true;
}

/// @fn static void edCallOverlay(EdState *state, const char *overlay,
///   const char *function)
///
/// @brief Run a function in another of `ed`'s overlays.
///
/// @param state The EdState of the running `ed`.
/// @param overlay The name of the overlay.
/// @param function The name of the function to run.
///
/// @return This function returns no value.  If the overlay can't be loaded,
/// state->error is set.
static void edCallOverlay(
  EdState *state, const char *overlay, const char *function
) {
  if (callOverlayFunction(OVERLAY_SAME_NAMESPACE, overlay, function, state)
    == NULL
  ) {
    state->error = ED_ERROR_MEMORY;
  }
}

/// @fn static void edRunCommand(EdState *state)
///
/// @brief Run the command line in state->commandBuffer.
///
/// @param state The EdState of the running `ed`.
///
/// @return This function returns no value.  Errors are left in state->error.
static void edRunCommand(EdState *state) {
  char *text = edParseCommandLine(state);
  if (text == NULL) {
    return;
  }

  while (*text == ' ') {
    text++;
  }
  state->params = (*text != '\0') ? text : NULL;

  switch (state->command) {
    case '\0':
      if (state->numAddresses == 0) {
        state->addr1 = state->addr2 = state->current + 1;
        if (state->addr2 > state->numLines) {
          state->error = ED_ERROR_ADDRESS;
          break;
        }
      }
      state->addr1 = state->addr2;
      edPrintLines(state, false);
      break;

    case 'p':
    case 'n':
      if (state->params != NULL) {
        state->error = ED_ERROR_SUFFIX;
        break;
      }
      edPrintLines(state, state->command == 'n');
      break;

    case '=':
      printf("%d\n",
        (state->numAddresses == 0) ? state->numLines : state->addr2);
      break;

    case 'P':
      state->prompt = !state->prompt;
      break;

    case 'H':
      state->verbose = !state->verbose;
      // fall through
    case 'h':
      if (state->lastError != ED_ERROR_NONE) {
        printf("%s\n", edErrorMessages[state->lastError]);
      }
      break;

    case 'f':
      if (state->params != NULL) {
        char *filename = (char*) realloc(
          state->filename, strlen(state->params) + 1);
        if (filename == NULL) {
          state->error = ED_ERROR_MEMORY;
          break;
        }
        state->filename = strcpy(filename, state->params);
      }
      if (state->filename == NULL) {
        state->error = ED_ERROR_NO_FILENAME;
        break;
      }
      printf("%s\n", state->filename);
      break;

    case 'k':
      if ((state->params == NULL) || (state->params[0] < 'a')
        || (state->params[0] > 'z') || (state->params[1] != '\0')
      ) {
        state->error = ED_ERROR_MARK;
        break;
      }
      if (state->addr2 < 1) {
        state->error = ED_ERROR_ADDRESS;
        break;
      }
      if (state->marks == NULL) {
        state->marks = (uint32_t*) calloc(ED_NUM_MARKS, sizeof(uint32_t));
        if (state->marks == NULL) {
          state->error = ED_ERROR_MEMORY;
          break;
        }
      }
      state->marks[state->params[0] - 'a'] = state->lines[state->addr2 - 1];
      break;

    case 'm':
    case 't': {
      char *destinationText = state->params;
      if ((destinationText == NULL)
        || !edParseAddress(state, &destinationText, &state->destination)
        || (*destinationText != '\0')
      ) {
        if (state->error == ED_ERROR_NONE) {
          state->error = ED_ERROR_DESTINATION;
        }
        break;
      }
    }
      // fall through
    case 'a':
    case 'c':
    case 'd':
    case 'i':
    case 'j':
      edCallOverlay(state, "Edit", "edEdit");
      break;

    case 'e':
    case 'E':
    case 'r':
    case 'w':
    case 'W':
      edCallOverlay(state, "File", "edFile");
      if ((state->error != ED_ERROR_NONE) || (state->command != 'q')) {
        break;
      }
      // fall through
    case 'q':
    case 'Q':
      if ((state->numAddresses != 0) || (state->params != NULL)) {
        state->error = ED_ERROR_SUFFIX;
        break;
      }
      edQuit(state);
      break;

    default:
      state->error = ED_ERROR_COMMAND;
      break;
  }
}

int main(int argc, char **argv) {
  EdState *state = (EdState*) calloc(1, sizeof(EdState));
  if (state != NULL) {
    state->commandBufferSize = ED_BUFFER_SIZE;
    state->commandBuffer = (char*) malloc(ED_BUFFER_SIZE);
    state->textBuffer = (char*) malloc(ED_BUFFER_SIZE);
  }
  if ((state == NULL) || (state->commandBuffer == NULL)
    || (state->textBuffer == NULL)
  ) {
    fputs("ed: out of memory\n", stderr);
    if (state != NULL) {
      free(state->textBuffer);
      free(state->commandBuffer);
      free(state);
    }
    return 1;
  }

  state->params = (argc > 1) ? argv[1] : NULL;
  edCallOverlay(state, "File", "edStart");
  if ((state->scratch == NULL) && (state->error == ED_ERROR_MEMORY)) {
    fputs("ed: out of memory\n", stderr);
  }

  while ((state->scratch != NULL) && !state->quit) {
    if (state->error != ED_ERROR_NONE) {
      state->lastError = state->error;
      state->error = ED_ERROR_NONE;
      fputs("?\n", stdout);
      if (state->verbose) {
        printf("%s\n", edErrorMessages[state->lastError]);
      }
    }

    if (state->prompt) {
      fputs("*", stdout);
    }
    if (!edReadCommand(state)) {
      state->command = 'Q';
      edQuit(state);
      break;
    }
    if (state->error == ED_ERROR_NONE) {
      edRunCommand(state);
    }

    // A refused q or e only lets the same command through right after it.
    if (state->error != ED_ERROR_MODIFIED) {
      state->warnedCommand = '\0';
    }
  }

  int returnValue = state->quit ? 0 : 1;
  free(state->marks);
  free(state->filename);
  free(state->lines);
  free(state->textBuffer);
  free(state->commandBuffer);
  free(state);
  return returnValue;
}
