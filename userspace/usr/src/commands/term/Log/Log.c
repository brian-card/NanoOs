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



/// @file Log.c
///
/// @brief term's log:  each error goes to $HOME/.log/term.log, since a term
/// started from the taskbar has no stderr, and to stderr, which a term
/// started from a shell has.

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "Term.h"

/// @def TERM_LOG_LIMIT
///
/// @brief The size past which the log is renamed to .old before the next
/// entry, so that it never takes more than twice this.
#define TERM_LOG_LIMIT 4096

/// @def TERM_LOG_PATH_SIZE
///
/// @brief The size of the buffer the log's path is made in.
#define TERM_LOG_PATH_SIZE 80

/// @def TERM_LOG_DIRECTORY
///
/// @brief The directory, under $HOME, that the log is kept in.
#define TERM_LOG_DIRECTORY "/.log"

/// @def TERM_LOG_FILE
///
/// @brief The log's name, in TERM_LOG_DIRECTORY.
#define TERM_LOG_FILE "/term.log"

/// @def TERM_LOG_OLD_SUFFIX
///
/// @brief What the log's name gains when it's set aside for being too long.
#define TERM_LOG_OLD_SUFFIX ".old"

/// @def TERM_LOG_PREFIX_SIZE
///
/// @brief The room a log line needs besides its message:  the time, the
/// process ID, the spaces after them, the newline, and the terminator.
#define TERM_LOG_PREFIX_SIZE 24

/// @fn static void logLine(const char *line)
///
/// @brief Append a line to $HOME/.log/term.log, making $HOME/.log if it
/// isn't there.  A log that can't be written is left alone.
///
/// @param line The line, with its newline.
///
/// @return This function returns no value.
static void logLine(const char *line) {
  const char *home = getenv("HOME");
  char path[TERM_LOG_PATH_SIZE];
  char old[TERM_LOG_PATH_SIZE + sizeof(TERM_LOG_OLD_SUFFIX) - 1];
  // The directory's path leaves room for the file's name to be added.
  size_t room = sizeof(path) - (sizeof(TERM_LOG_FILE) - 1);
  if ((home == NULL)
    || (snprintf(path, room, "%s" TERM_LOG_DIRECTORY, home) >= (int) room)
  ) {
    return;
  }
  mkdir(path, 0700);
  strncat(path, TERM_LOG_FILE, sizeof(path) - strlen(path) - 1);
  FILE *file = fopen(path, "a");
  if (file == NULL) {
    return;
  }
  fseek(file, 0, SEEK_END);
  if (ftell(file) > TERM_LOG_LIMIT) {
    fclose(file);
    snprintf(old, sizeof(old), "%s" TERM_LOG_OLD_SUFFIX, path);
    remove(old);
    rename(path, old);
    file = fopen(path, "a");
    if (file == NULL) {
      return;
    }
  }
  fputs(line, file);
  fclose(file);
}

/// @fn void* termLog(void *args)
///
/// @brief Log the terminal's message:  the time, term's process ID, and the
/// message, as one line of the log, and the message on stderr.
///
/// @param args A pointer to the Term, whose message is set.
///
/// @return Always returns the provided pointer.
void* termLog(void *args) {
  Term *term = (Term*) args;
  char line[TERM_LOG_LINE + TERM_LOG_PREFIX_SIZE];
  struct timespec now;
  timespec_get(&now, TIME_UTC);
  snprintf(line, sizeof(line), "%lu %d %s\n", (unsigned long) now.tv_sec,
    (int) getpid(), term->message);
  logLine(line);
  fprintf(stderr, "term: %s\n", term->message);
  return args;
}
