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
/// @param last The window the script made last, which "." names, or 0.
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
/// @param widgets The template the Widget overlay's commands build, whose
///   buffer is on the heap once one has been used.
typedef struct Wintest {
  bool        closes;
  bool        more;
  bool        handled;
  uint8_t     last;
  int         argc;
  int         index;
  long        waitMs;
  int         rc;
  char      **argv;
  const char *command;
  const char *window;
  WinEvent    event;
  WinSession  session;
  WinTemplate widgets;
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

/// @def WINTEST_TEMPLATE_SIZE
///
/// @brief The size of the buffer the widget commands build a template in.
#define WINTEST_TEMPLATE_SIZE 1024

/// @def WINTEST_SPACE
///
/// @brief The character that stands for a space in a widget's text, since
/// the commands' words are separated by spaces.
#define WINTEST_SPACE '_'

/// @fn static char* spaced(const char *word)
///
/// @brief Copy a word, turning each WINTEST_SPACE into a space.
///
/// @param word The word.
///
/// @return Returns the copy, which the caller frees, or NULL if there was no
/// memory.
static inline char* spaced(const char *word) {
  size_t size = strlen(word) + 1;
  char *text = (char*) malloc(size);
  if (text != NULL) {
    memcpy(text, word, size);
  }
  for (char *at = text; (at != NULL) && (*at != '\0'); at++) {
    if (*at == WINTEST_SPACE) {
      *at = ' ';
    }
  }
  return text;
}

/// @fn static uint8_t windowWord(const Wintest *wintest, const char *word)
///
/// @brief Read a window's number:  ".", for the window the script made
/// last, or a number.
///
/// @param wintest The context.
/// @param word The word.
///
/// @return Returns the window's identifier.
static inline uint8_t windowWord(const Wintest *wintest, const char *word) {
  if (strcmp(word, ".") == 0) {
    return wintest->last;
  }
  return (uint8_t) strtol(word, NULL, 0);
}

/// @fn static char* listItems(const char *word, uint16_t *size)
///
/// @brief Turn a word of list items separated by commas into a list's
/// items, each ended by a NUL but the last, turning each WINTEST_SPACE into
/// a space.  A "-" is no items.
///
/// @param word The word.
/// @param size Where to store the bytes of the items.
///
/// @return Returns the items, which the caller frees, or NULL if there was
/// no memory.
static inline char* listItems(const char *word, uint16_t *size) {
  char *items = spaced((strcmp(word, "-") == 0) ? "" : word);
  *size = 0;
  for (char *at = items; (at != NULL) && (*at != '\0'); at++) {
    if (*at == ',') {
      *at = '\0';
    }
    (*size)++;
  }
  return items;
}

/// @fn static bool parseRect(const char *text, GfxRect *rect)
///
/// @brief Parse X,Y,W,H.
///
/// @param text The text.
/// @param rect Where to store the rectangle.
///
/// @return Returns true if the text is four numbers, false otherwise.
static inline bool parseRect(const char *text, GfxRect *rect) {
  long values[4];
  if (!parseNumbers(text, values, (int) WINTEST_COUNT(values))) {
    return false;
  }
  rect->x = (int16_t) values[0];
  rect->y = (int16_t) values[1];
  rect->w = (int16_t) values[2];
  rect->h = (int16_t) values[3];
  return true;
}

/// @fn static WinTemplate* pending(Wintest *wintest)
///
/// @brief Get the template the widget commands build, making its buffer the
/// first time.
///
/// @param wintest The context.
///
/// @return Returns the template, or NULL if there was no memory for it.
static inline WinTemplate* pending(Wintest *wintest) {
  if (wintest->widgets.buffer == NULL) {
    uint8_t *buffer = (uint8_t*) malloc(WINTEST_TEMPLATE_SIZE);
    if (buffer == NULL) {
      return NULL;
    }
    winTemplateBegin(&wintest->widgets, buffer, WINTEST_TEMPLATE_SIZE);
  }
  return &wintest->widgets;
}

/// @fn static inline uint8_t parseWidgetFlags(const char *names)
///
/// @brief Turn a list of widget flag names into WIN_WIDGET_* bits.
///
/// @param names The names, separated by commas.
///
/// @return Returns the bits.  A name it doesn't know adds none.
static inline uint8_t parseWidgetFlags(const char *names) {
  static const char *const known[] = {
    "disabled", "hidden", "default", "cancel", "password", "readonly",
  };
  static const uint8_t bits[] = {
    WIN_WIDGET_DISABLED, WIN_WIDGET_HIDDEN, WIN_WIDGET_DEFAULT,
    WIN_WIDGET_CANCEL, WIN_WIDGET_PASSWORD, WIN_WIDGET_READ_ONLY,
  };
  uint8_t flags = 0;
  for (size_t ii = 0; ii < WINTEST_COUNT(known); ii++) {
    const char *found = strstr(names, known[ii]);
    size_t length = strlen(known[ii]);
    if ((found != NULL) && ((found == names) || (found[-1] == ','))
      && ((found[length] == '\0') || (found[length] == ','))
    ) {
      flags |= bits[ii];
    }
  }
  return flags;
}

/// @fn static inline uint8_t parseAnchor(const char *letters)
///
/// @brief Turn anchor letters into WIN_ANCHOR_* bits:  l, t, r and b for
/// left, top, right and bottom.
///
/// @param letters The letters.
///
/// @return Returns the bits.
static inline uint8_t parseAnchor(const char *letters) {
  uint8_t anchor = 0;
  for (const char *at = letters; *at != '\0'; at++) {
    if (*at == 'l') {
      anchor |= WIN_ANCHOR_LEFT;
    } else if (*at == 't') {
      anchor |= WIN_ANCHOR_TOP;
    } else if (*at == 'r') {
      anchor |= WIN_ANCHOR_RIGHT;
    } else if (*at == 'b') {
      anchor |= WIN_ANCHOR_BOTTOM;
    }
  }
  return anchor;
}

/// @fn static inline int options(Wintest *wintest, int ii, uint8_t *flags,
///   uint8_t *anchor)
///
/// @brief Read the flags= and anchor= options that may follow a widget
/// command.
///
/// @param wintest The context.
/// @param ii The index of the first word after the command's own.
/// @param flags Where to store the WIN_WIDGET_* bits, 0 if none are given.
/// @param anchor Where to store the WIN_ANCHOR_* bits, left and top if none
///   are given.
///
/// @return Returns the index of the first word after the options.
static inline int options(Wintest *wintest, int ii, uint8_t *flags,
  uint8_t *anchor
) {
  *flags = 0;
  *anchor = WIN_ANCHOR_LEFT | WIN_ANCHOR_TOP;
  while (ii < wintest->argc) {
    const char *word = wintest->argv[ii];
    if (strncmp(word, "flags=", WINTEST_LITERAL_LENGTH("flags=")) == 0) {
      *flags = parseWidgetFlags(&word[WINTEST_LITERAL_LENGTH("flags=")]);
    } else if (strncmp(word, "anchor=", WINTEST_LITERAL_LENGTH("anchor="))
      == 0
    ) {
      *anchor = parseAnchor(&word[WINTEST_LITERAL_LENGTH("anchor=")]);
    } else {
      break;
    }
    ii++;
  }
  return ii;
}

#endif // WINTEST_H
