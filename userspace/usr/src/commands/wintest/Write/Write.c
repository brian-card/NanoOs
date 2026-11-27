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



/// @file Write.c
///
/// @brief wintest's text grid writes:  each command sends a window's grid
/// one operation, or a burst of them.

#include "Wintest.h"

/// @fn static int write(Wintest *wintest, const char *window,
///   const uint8_t *operation, uint16_t size)
///
/// @brief Send a window's text grid one operation.
///
/// @param wintest The context.
/// @param window The window's number.
/// @param operation The operation.
/// @param size Its size.
///
/// @return Returns 0 on success, or a negative errno value.
static int write(Wintest *wintest, const char *window,
  const uint8_t *operation, uint16_t size
) {
  return winGridWrite(&wintest->session, (uint8_t) strtol(window, NULL, 10),
    GRID_WIDGET, operation, size);
}

/// @def WINTEST_BURST_TEXT_SIZE
///
/// @brief The room for the text of each write burst makes, its terminator
/// included.
#define WINTEST_BURST_TEXT_SIZE 16

/// @def WINTEST_GRID_FIELDS(first, size)
///
/// @brief The number of a grid operation's one-byte fields from its first one
/// to its end.
#define WINTEST_GRID_FIELDS(first, size) ((size) - (first))

/// @def WINTEST_MAX_GRID_FIELDS
///
/// @brief The most fields any grid operation wintest writes is given, which
/// WIN_GRID_CLEAR has.
#define WINTEST_MAX_GRID_FIELDS \
  WINTEST_GRID_FIELDS(WIN_GRID_CLEAR_TOP, WIN_GRID_CLEAR_SIZE)

/// @fn static int burst(Wintest *wintest, const char *window, int count)
///
/// @brief Write a text grid as fast as the window server takes it:  count
/// writes, each putting "write N" at the start of the first row.
///
/// @param wintest The context.
/// @param window The window's number.
/// @param count The number of writes.
///
/// @return Returns 0 on success, or the first failure's negative errno value.
static int burst(Wintest *wintest, const char *window, int count) {
  for (int ii = 0; ii < count; ii++) {
    uint8_t operation[WIN_GRID_PUT_SIZE(WINTEST_BURST_TEXT_SIZE)];
    int length = snprintf((char*) &operation[WIN_GRID_PUT_TEXT],
      WINTEST_BURST_TEXT_SIZE, "write %d", ii);
    operation[WIN_GRID_TAG] = WIN_GRID_PUT;
    operation[WIN_GRID_PUT_ROW] = 0;
    operation[WIN_GRID_PUT_COLUMN] = 0;
    operation[WIN_GRID_PUT_ATTRIBUTE]
      = WIN_ATTRIBUTE(WIN_ANSI_BRIGHT_WHITE, WIN_ANSI_BLACK);
    operation[WIN_GRID_PUT_LENGTH] = (uint8_t) length;
    int rc = write(wintest, window, operation,
      (uint16_t) WIN_GRID_PUT_SIZE(length));
    if (rc < 0) {
      return rc;
    }
  }
  return 0;
}

/// @fn void* wintestWrite(void *args)
///
/// @brief Carry out the text grid write at the context's index, and move the
/// index past its arguments:  put N ROW,COL,ATTR TEXT,
/// clear N TOP,LEFT,BOTTOM,RIGHT,ATTR, scroll N TOP,BOTTOM,COUNT,ATTR,
/// cursor N ROW,COL,SHOWN, reports N ON or burst N COUNT.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer, with handled cleared if the
/// command isn't one of these.
void* wintestWrite(void *args) {
  Wintest *wintest = (Wintest*) args;
  int argc = wintest->argc;
  char **argv = wintest->argv;
  int ii = wintest->index;
  const char *command = argv[ii++];
  const char *first = (ii < argc) ? argv[ii] : NULL;
  const char *second = (ii + 1 < argc) ? argv[ii + 1] : NULL;
  uint8_t operation[WIN_GRID_PUT_SIZE(UINT8_MAX)];
  long values[WINTEST_MAX_GRID_FIELDS];
  int count = 0;
  int rc = 0;
  wintest->handled = false;
  if (second == NULL) {
    return args;
  } else if ((strcmp(command, "put") == 0) && (ii + 2 < argc)
    && parseNumbers(second, values,
      WINTEST_GRID_FIELDS(WIN_GRID_PUT_ROW, WIN_GRID_PUT_LENGTH))
  ) {
    size_t length = strlen(argv[ii + 2]);
    if (length > UINT8_MAX) {
      length = UINT8_MAX;
    }
    operation[WIN_GRID_TAG] = WIN_GRID_PUT;
    operation[WIN_GRID_PUT_LENGTH] = (uint8_t) length;
    memcpy(&operation[WIN_GRID_PUT_TEXT], argv[ii + 2], length);
    count = WINTEST_GRID_FIELDS(WIN_GRID_PUT_ROW, WIN_GRID_PUT_LENGTH);
    rc = (int) WIN_GRID_PUT_SIZE(length);
    ii++;
  } else if ((strcmp(command, "clear") == 0)
    && parseNumbers(second, values,
      WINTEST_GRID_FIELDS(WIN_GRID_CLEAR_TOP, WIN_GRID_CLEAR_SIZE))
  ) {
    operation[WIN_GRID_TAG] = WIN_GRID_CLEAR;
    count = WINTEST_GRID_FIELDS(WIN_GRID_CLEAR_TOP, WIN_GRID_CLEAR_SIZE);
    rc = WIN_GRID_CLEAR_SIZE;
  } else if ((strcmp(command, "scroll") == 0)
    && parseNumbers(second, values,
      WINTEST_GRID_FIELDS(WIN_GRID_SCROLL_TOP, WIN_GRID_SCROLL_SIZE))
  ) {
    operation[WIN_GRID_TAG] = WIN_GRID_SCROLL;
    count = WINTEST_GRID_FIELDS(WIN_GRID_SCROLL_TOP, WIN_GRID_SCROLL_SIZE);
    rc = WIN_GRID_SCROLL_SIZE;
  } else if ((strcmp(command, "cursor") == 0)
    && parseNumbers(second, values,
      WINTEST_GRID_FIELDS(WIN_GRID_CURSOR_ROW, WIN_GRID_CURSOR_SIZE))
  ) {
    operation[WIN_GRID_TAG] = WIN_GRID_CURSOR;
    count = WINTEST_GRID_FIELDS(WIN_GRID_CURSOR_ROW, WIN_GRID_CURSOR_SIZE);
    rc = WIN_GRID_CURSOR_SIZE;
  } else if ((strcmp(command, "reports") == 0)
    && parseNumbers(second, values,
      WINTEST_GRID_FIELDS(WIN_GRID_REPORTS_ON, WIN_GRID_REPORTS_SIZE))
  ) {
    operation[WIN_GRID_TAG] = WIN_GRID_REPORTS;
    count = WINTEST_GRID_FIELDS(WIN_GRID_REPORTS_ON, WIN_GRID_REPORTS_SIZE);
    rc = WIN_GRID_REPORTS_SIZE;
  } else if (strcmp(command, "burst") == 0) {
    rc = burst(wintest, first, (int) strtol(second, NULL, 10));
  } else {
    return args;
  }
  wintest->handled = true;
  for (int jj = 0; jj < count; jj++) {
    operation[WIN_GRID_TAG + 1 + jj] = (uint8_t) values[jj];
  }
  if (count > 0) {
    rc = write(wintest, first, operation, (uint16_t) rc);
  }
  if (rc < 0) {
    printf("wintest: %s %s: %s\n", command, first, strerror(-rc));
  }
  wintest->index = ii + 2;
  return args;
}
