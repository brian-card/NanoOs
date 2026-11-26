///////////////////////////////////////////////////////////////////////////////
///
/// @file              Wintest.h
///
/// @brief             What wintest's overlays share:  the session, and what a
///                    call to the Script or Print overlay works on.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////


#ifndef WINTEST_H
#define WINTEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NanoOsUtils.h"
#include "../../../src/kernel/Window.h"

/// @struct Wintest
///
/// @brief What wintest's overlays share.
///
/// @param closes Whether a window asked to close is destroyed.
/// @param more Whether Print's caller should go on reading events.
/// @param handled Whether the Grid overlay knew the command.
/// @param argc The number of arguments.
/// @param index The index in argv of the command the Grid overlay carries
///   out, and after it, of the next command.
/// @param waitMs How long the Print overlay's wintestEvents prints events
///   for, in milliseconds, or 0 for until the window server goes away.
/// @param rc The negative errno value Print reports, for an error.
/// @param argv The arguments.
/// @param command The command Print reports an error of, or NULL to print
///   event instead.
/// @param window The window the command named, or NULL.
/// @param event The event Print prints.
/// @param session The connection.
typedef struct Wintest {
  bool        closes;
  bool        more;
  bool        handled;
  int         argc;
  int         index;
  long        waitMs;
  int         rc;
  char      **argv;
  const char *command;
  const char *window;
  WinEvent    event;
  WinSession  session;
} Wintest;

/// @def GRID_WIDGET
///
/// @brief The identifier wintest gives its windows' text grids.
#define GRID_WIDGET 1

/// @def WINTEST_COUNT(array)
///
/// @brief The number of elements of an array.
#define WINTEST_COUNT(array) (sizeof(array) / sizeof((array)[0]))

/// @def WINTEST_LITERAL_LENGTH(literal)
///
/// @brief The number of characters in a string literal, its terminator left
/// out.
#define WINTEST_LITERAL_LENGTH(literal) (sizeof(literal) - 1)

/// @def WINTEST_SCRIPT_START
///
/// @brief The index of a script's first word in the arguments of wintest
/// run, after "wintest" and "run".
#define WINTEST_SCRIPT_START 2

/// @def WINTEST_MS_PER_SECOND
///
/// @brief The number of milliseconds in a second.
#define WINTEST_MS_PER_SECOND 1000

/// @def WINTEST_NS_PER_MS
///
/// @brief The number of nanoseconds in a millisecond.
#define WINTEST_NS_PER_MS 1000000L

/// @def WINTEST_NS_PER_SECOND
///
/// @brief The number of nanoseconds in a second.
#define WINTEST_NS_PER_SECOND 1000000000L

/// @fn static inline bool parseNumbers(const char *text, long *values,
///   int count)
///
/// @brief Parse numbers separated by commas, in any base strtol knows.
///
/// @param text The text.
/// @param values Where to store the numbers.
/// @param count How many numbers there must be.
///
/// @return Returns true if the text is that many numbers, false otherwise.
static inline bool parseNumbers(const char *text, long *values, int count) {
  for (int ii = 0; ii < count; ii++) {
    char *end = NULL;
    values[ii] = strtol(text, &end, 0);
    if ((end == text) || ((ii < count - 1) && (*end != ','))
      || ((ii == count - 1) && (*end != '\0'))
    ) {
      return false;
    }
    text = end + 1;
  }
  return true;
}

#endif // WINTEST_H
