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
/// @brief term:  a terminal window.  The window server opens a console
/// session behind the window's text grid and sends the session the keys
/// typed in it; term only draws what the session prints.

#include "ConsoleOutput.h"
#include "Term.h"

/// @def TERM_LITERAL_LENGTH(literal)
///
/// @brief The number of characters in a string literal, its terminator left
/// out.
#define TERM_LITERAL_LENGTH(literal) (sizeof(literal) - 1)

/// @def TERM_OPTION_SIZE
///
/// @brief The start of the option that sets the terminal's size.
#define TERM_OPTION_SIZE "--size="

/// @def TERM_OPTION_AT
///
/// @brief The start of the option that places the terminal's window.
#define TERM_OPTION_AT "--at="

/// @fn static void printUsage(FILE *stream)
///
/// @brief Print term's usage.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: term [options]\n"
    "\n"
    "Open a terminal window running your shell, until the shell exits or the\n"
    "window is closed.\n"
    "\n"
    "Options:\n"
    "  --size=COLUMNSxROWS  The terminal's size; 80x24 by default.\n"
    "  --at=X,Y  Where the window goes.\n"
    "  --plain  No border or title bar.\n"
    "  --tee  Also write what the session prints to stderr.\n"
    "  --help  Show this help and exit.\n",
    stream);
}

/// @fn static void report(Term *term, const char *message, int rc)
///
/// @brief Have the Log overlay log an error.
///
/// @param term The terminal.
/// @param message What failed.
/// @param rc The negative errno value it failed with, or 0.
///
/// @return This function returns no value.
static void report(Term *term, const char *message, int rc) {
  snprintf(term->message, sizeof(term->message), "%s%s%s", message,
    (rc < 0) ? ": " : "", (rc < 0) ? strerror(-rc) : "");
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Log", "termLog", term);
}

/// @fn static bool parseOptions(Term *term, int argc, char **argv,
///   GfxRect *frame, uint8_t *flags, long *size)
///
/// @brief Read term's options.
///
/// @param term The terminal, whose tee is set for --tee.
/// @param argc The number of arguments.
/// @param argv The arguments.
/// @param frame Where to store the window's place, if given.
/// @param flags Where to clear WIN_FLAG_DECORATED for --plain.
/// @param columns Where to store the number of columns.
/// @param rows Where to store the number of rows.
///
/// @return Returns true if every argument is a valid option.
static bool parseOptions(Term *term, int argc, char **argv,
  GfxRect *frame, uint8_t *flags, long *columns, long *rows
) {
  for (int ii = 1; ii < argc; ii++) {
    char *end = NULL;
    if (strncmp(argv[ii], TERM_OPTION_SIZE,
      TERM_LITERAL_LENGTH(TERM_OPTION_SIZE)) == 0
    ) {
      *columns = strtol(&argv[ii][TERM_LITERAL_LENGTH(TERM_OPTION_SIZE)],
        &end, 10);
      if ((*end != 'x') || (*columns <= 0)
        || (*columns > WIN_GRID_MAX_COLUMNS)
      ) {
        return false;
      }
      *rows = strtol(end + 1, &end, 10);
      if ((*end != '\0') || (*rows <= 0) || (*rows > WIN_GRID_MAX_ROWS)) {
        return false;
      }
    } else if (strncmp(argv[ii], TERM_OPTION_AT,
      TERM_LITERAL_LENGTH(TERM_OPTION_AT)) == 0
    ) {
      frame->x = (int16_t) strtol(
        &argv[ii][TERM_LITERAL_LENGTH(TERM_OPTION_AT)], &end, 10);
      if (*end != ',') {
        return false;
      }
      frame->y = (int16_t) strtol(end + 1, &end, 10);
      if (*end != '\0') {
        return false;
      }
    } else if (strcmp(argv[ii], "--plain") == 0) {
      *flags &= ~WIN_FLAG_DECORATED;
    } else if (strcmp(argv[ii], "--tee") == 0) {
      term->tee = true;
    } else {
      return false;
    }
  }
  return true;
}

/// @fn static bool handleEvent(Term *term, const WinEvent *event)
///
/// @brief Handle an event from the window server.
///
/// @param term The terminal.
/// @param event The event.
///
/// @return Returns false once term should end, true otherwise.
static bool handleEvent(Term *term, const WinEvent *event) {
  TermParser *parser = &term->parser;
  if ((event->type == WIN_EVENT_RESIZED) && (event->widget == TERM_GRID)) {
    parser->columns = event->columns;
    parser->rows = event->rows;
    if (parser->row >= parser->rows) {
      parser->row = parser->rows - 1;
    }
    if (parser->column >= parser->columns) {
      parser->column = parser->columns - 1;
    }
  } else if (event->type == WIN_EVENT_KEY) {
    report(term, "a key came as an event", 0);
  } else if ((event->type == WIN_EVENT_CLOSE_REQUESTED)
    || (event->type == WIN_EVENT_SESSION_ENDED)
    || (event->type == WIN_EVENT_DISCONNECTED)
  ) {
    return false;
  }
  return true;
}

/// @fn static void run(Term *term)
///
/// @brief Draw what the session prints and handle the window server's
/// events until the session ends, the window is closed, or the window server
/// goes away.
///
/// @param term The terminal.
///
/// @return This function returns no value.
static void run(Term *term) {
  WinSession *session = &term->session;
  while (true) {
    while (session->nextEvent < session->numEvents) {
      if (!handleEvent(term, &session->events[session->nextEvent++])) {
        return;
      }
    }
    if (!session->connected) {
      return;
    }
    ProcessMessage *message = processMessageQueueWait(NULL);
    if ((message == NULL) || winHandleMessage(session, message)) {
      continue;
    }
    const char *text = consoleOutputText(message);
    if (text != NULL) {
      term->text = text;
      callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Parse", "termParse", term);
      if (term->tee) {
        strncpy(term->teeText, text, CONSOLE_BUFFER_SIZE);
      }
      // stderr goes through the console, which may be waiting for this
      // message, so the copy is written once the message is done.
      consoleOutputDoneTo(message, term->consolePid);
      if (term->tee) {
        fputs(term->teeText, stderr);
      }
      continue;
    }
    bool ended = (processMessageType(message)
      == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_SESSION_ENDED));
    processMessageSetDone(message);
    if (processMessageWaiting(message) == false) {
      processMessageRelease(message);
    }
    if (ended) {
      return;
    }
  }
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }
  GfxRect frame = { TERM_DEFAULT_LEFT, TERM_DEFAULT_TOP, 0, 0 };
  uint8_t flags = WIN_FLAG_DECORATED | WIN_FLAG_RESIZABLE
    | WIN_FLAG_IN_TASKBAR;
  long columns = TERM_DEFAULT_COLUMNS;
  long rows = TERM_DEFAULT_ROWS;
  Term *term = (Term*) calloc(1, sizeof(Term));
  if (term == NULL) {
    fputs("term: out of memory\n", stderr);
    return 1;
  }
  if (!parseOptions(term, argc, argv, &frame, &flags, &columns, &rows)) {
    printUsage(stderr);
    free(term);
    return 1;
  }

  WinSession *session = &term->session;
  findSystemProcess(CONSOLE_PROCESS_NAME, &term->consolePid);
  WinError status = winConnect(session);
  if (status != WIN_OK) {
    report(term, (status == WIN_ERR_NOT_FOUND) ? "no window server"
      : "can't connect to the window server", 0);
    free(term);
    return 1;
  }
  const WinHelloArgs *hello = &session->hello;
  GfxRect grid = { 0, 0, (int16_t) (columns * hello->cellWidth),
    (int16_t) (rows * hello->cellHeight) };
  frame.w = grid.w;
  frame.h = grid.h;
  if ((flags & WIN_FLAG_DECORATED) != 0) {
    frame.w += 2 * hello->borderWidth;
    frame.h += hello->titleHeight + 2 * hello->borderWidth;
  }
  uint8_t record[WIN_RECORD_HEADER_SIZE];
  winGridRecord(record, TERM_GRID, WIN_WIDGET_SESSION, grid);
  // The window server may not be shared a pointer into an overlay.
  char title[] = "Terminal";
  term->window = winCreateWindowWith(session, title, frame,
    WIN_LAYER_NORMAL, flags, record, sizeof(record));
  if (term->window < 0) {
    report(term, "can't open a session", term->window);
    winDisconnect(session);
    free(term);
    return 1;
  }

  TermParser *parser = &term->parser;
  parser->columns = (uint8_t) columns;
  parser->rows = (uint8_t) rows;
  parser->attribute = TERM_DEFAULT_ATTRIBUTE;
  parser->put = -1;
  run(term);
  // The window goes when term ends.  A one-way destroy posted just before
  // exiting can be dropped unread, and its arguments with it.
  if (session->connected) {
    winDisconnect(session);
  }
  free(term);
  return 0;
}
