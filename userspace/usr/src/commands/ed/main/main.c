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

#include "Ex.h"
#include <stdio.h>
#include "NanoOsUtils.h"

/// @def ED_BUFFER_SIZE
///
/// @brief The size, in bytes, that the command buffer starts at and grows by.
#define ED_BUFFER_SIZE 96

/// @def ED_MEMORY_RESERVE
///
/// @brief The number of bytes of dynamic memory that `ed` leaves free for the
/// rest of the system when it grows its command buffer.
#define ED_MEMORY_RESERVE 256

/// @struct EdState
///
/// @brief Everything `ed` keeps between commands.
///
/// @param ex The ExState of the editing commands.
/// @param commandBuffer The buffer a command line is read into.
/// @param commandBufferSize The size, in bytes, of commandBuffer.
/// @param lastError The ExError of the last command that failed.
/// @param prompt Whether to print a prompt before reading a command.
/// @param verbose Whether to print the text of each error.
/// @param quit Whether `ed` is to exit.
typedef struct EdState {
  ExState  ex;
  char    *commandBuffer;
  int      commandBufferSize;
  uint8_t  lastError;
  bool     prompt;
  bool     verbose;
  bool     quit;
} EdState;

/// @var edErrorMessages
///
/// @brief The text of each ExError, printed by the h and H commands.
static const char *const edErrorMessages[NUM_EX_ERRORS] = {
  "",
  "scratch file error",
  "out of memory",
  "cannot write file",
  "invalid address",
  "unknown command",
  "invalid command suffix",
  "no current filename",
  "cannot open input file",
  "cannot open output file",
  "warning: buffer modified",
  "invalid mark",
  "invalid destination",
  "nothing to undo",
  "nothing to undo",
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
        while ((fgets(state->ex.store.buffer, ED_BUFFER_SIZE, stdin) != NULL)
          && (strchr(state->ex.store.buffer, '\n') == NULL));
        state->commandBuffer[0] = '\0';
        state->ex.store.error = EX_ERROR_MEMORY;
        return true;
      }
      state->commandBuffer = commandBuffer;
      state->commandBufferSize += ED_BUFFER_SIZE;
    }
  }

  state->commandBuffer[length] = '\0';
  return length > 0;
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
  if ((state->ex.command == 'q') && state->ex.modified
    && (state->ex.warnedCommand != 'q')
  ) {
    state->ex.warnedCommand = 'q';
    state->ex.store.error = EX_ERROR_MODIFIED;
    return;
  }

  textstoreClose(&state->ex.store);
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
/// state->ex.store.error is set.
static void edCallOverlay(
  EdState *state, const char *overlay, const char *function
) {
  if (callOverlayFunction(EX_LIBRARY_PATH, overlay, function, &state->ex)
    == NULL
  ) {
    state->ex.store.error = EX_ERROR_MEMORY;
  }
}

/// @fn static void edRunCommand(EdState *state)
///
/// @brief Run the command line in state->commandBuffer.
///
/// @param state The EdState of the running `ed`.
///
/// @return This function returns no value.  Errors are left in
/// state->ex.store.error.
static void edRunCommand(EdState *state) {
  char *text = exParseCommandLine(&state->ex, state->commandBuffer);
  if (text == NULL) {
    return;
  }

  while (*text == ' ') {
    text++;
  }
  state->ex.params = (*text != '\0') ? text : NULL;

  switch (state->ex.command) {
    case '\0':
    case 'p':
    case 'n':
      edCallOverlay(state, "Print", "exPrint");
      break;

    case '=':
      printf("%d\n", (state->ex.numAddresses == 0)
        ? state->ex.store.numLines : state->ex.addr2);
      break;

    case 'P':
      state->prompt = !state->prompt;
      break;

    case 'H':
      state->verbose = !state->verbose;
      // fall through
    case 'h':
      if (state->lastError != EX_ERROR_NONE) {
        printf("%s\n", edErrorMessages[state->lastError]);
      }
      break;

    case 'f':
      if (state->ex.params != NULL) {
        char *filename = (char*) realloc(
          state->ex.filename, strlen(state->ex.params) + 1);
        if (filename == NULL) {
          state->ex.store.error = EX_ERROR_MEMORY;
          break;
        }
        state->ex.filename = strcpy(filename, state->ex.params);
      }
      if (state->ex.filename == NULL) {
        state->ex.store.error = EX_ERROR_NO_FILENAME;
        break;
      }
      printf("%s\n", state->ex.filename);
      break;

    case 'm':
    case 't': {
      char *destinationText = state->ex.params;
      if ((destinationText == NULL)
        || !exParseAddress(&state->ex, &destinationText, &state->ex.destination)
        || (*destinationText != '\0')
      ) {
        if (state->ex.store.error == EX_ERROR_NONE) {
          state->ex.store.error = EX_ERROR_DESTINATION;
        }
        break;
      }
    }
      // fall through
    case 'k':
      edCallOverlay(state, "Move", "exMove");
      break;

    case 'a':
    case 'c':
    case 'd':
    case 'i':
    case 'j':
      edCallOverlay(state, "Edit", "exEdit");
      break;

    case 'e':
    case 'E':
    case 'r':
      edCallOverlay(state, "File", "exFile");
      break;

    case 'u':
      if ((state->ex.numAddresses != 0) || (state->ex.params != NULL)) {
        state->ex.store.error = EX_ERROR_SUFFIX;
        break;
      }
      // A u right after a u redoes what it undid.
      state->ex.command = state->ex.undone ? 'r' : 'u';
      edCallOverlay(state, "Undo", "exUndo");
      state->ex.undone = (state->ex.store.error == EX_ERROR_NONE)
        && !state->ex.undone;
      break;

    case 'w':
    case 'W':
      edCallOverlay(state, "Write", "exWrite");
      if ((state->ex.store.error != EX_ERROR_NONE)
        || (state->ex.command != 'q')
      ) {
        break;
      }
      // fall through
    case 'q':
    case 'Q':
      if ((state->ex.numAddresses != 0) || (state->ex.params != NULL)) {
        state->ex.store.error = EX_ERROR_SUFFIX;
        break;
      }
      edQuit(state);
      break;

    default:
      state->ex.store.error = EX_ERROR_COMMAND;
      break;
  }
}

/// @fn static void printUsage(FILE *stream)
///
/// @brief Print the usage summary for ed.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: ed [file]\n"
    "\n"
    "Edit a text file a line at a time.  With a file, read it in to start.\n"
    "Commands have the form [address[,address]]command[parameters].\n"
    "\n"
    "Addresses:\n"
    "  .       current line           $       last line\n"
    "  n       line n                 'x      line marked x\n"
    "  +n, -n  n lines after or before the current line\n"
    "  ,       lines 1 through $      ;       current line through $\n"
    "Commands:\n"
    "  a, i      append after or insert before a line; end with \".\"\n"
    "  c         change lines         d         delete lines\n"
    "  j         join lines           m addr    move lines after addr\n"
    "  t addr    copy lines after addr\n"
    "  p, n      print lines, n with line numbers\n"
    "  =         print a line number  k x       mark a line as x\n"
    "  e file    edit another file (E discards unsaved changes)\n"
    "  r file    read a file in after a line\n"
    "  w file    write lines (W appends, wq writes and quits)\n"
    "  f file    set or show the file name\n"
    "  h, H      explain the last error (H turns explanations on or off)\n"
    "  P         turn the * prompt on or off\n"
    "  u         undo the last change, or redo it after a u\n"
    "  q, Q      quit (Q discards unsaved changes)\n"
    "  (newline) print the next line\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }
  EdState *state = (EdState*) calloc(1, sizeof(EdState));
  if (state != NULL) {
    state->commandBufferSize = ED_BUFFER_SIZE;
    state->commandBuffer = (char*) malloc(ED_BUFFER_SIZE);
    state->ex.store.buffer = (char*) malloc(TEXTSTORE_BUFFER_SIZE);
  }
  if ((state == NULL) || (state->commandBuffer == NULL)
    || (state->ex.store.buffer == NULL)
  ) {
    fputs("ed: out of memory\n", stderr);
    if (state != NULL) {
      free(state->ex.store.buffer);
      free(state->commandBuffer);
      free(state);
    }
    return 1;
  }

  strcpy(state->ex.name, "ed");
  state->ex.params = (argc > 1) ? argv[1] : NULL;
  edCallOverlay(state, "File", "exStart");
  if ((state->ex.store.text == NULL)
    && (state->ex.store.error == EX_ERROR_MEMORY)
  ) {
    fputs("ed: out of memory\n", stderr);
  }

  while ((state->ex.store.text != NULL) && !state->quit) {
    if (state->ex.store.error != EX_ERROR_NONE) {
      state->lastError = state->ex.store.error;
      state->ex.store.error = EX_ERROR_NONE;
      fputs("?\n", stdout);
      if (state->verbose) {
        printf("%s\n", edErrorMessages[state->lastError]);
      }
    }

    if (state->prompt) {
      fputs("*", stdout);
    }
    if (!edReadCommand(state)) {
      state->ex.command = 'Q';
      edQuit(state);
      break;
    }
    if (state->ex.store.error == EX_ERROR_NONE) {
      edRunCommand(state);
    }

    // A refused q or e only lets the same command through right after it.
    if (state->ex.store.error != EX_ERROR_MODIFIED) {
      state->ex.warnedCommand = '\0';
    }
  }

  int returnValue = state->quit ? 0 : 1;
  free(state->ex.filename);
  free(state->ex.store.buffer);
  free(state->commandBuffer);
  free(state);
  return returnValue;
}
