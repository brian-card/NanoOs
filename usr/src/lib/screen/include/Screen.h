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

/// @file Screen.h
///
/// @brief Drawing on a terminal with the NanoOs terminal subset of ANSI
/// escape sequences, and learning its size.  The functions are inline so that
/// every overlay that draws can use them without an overlay call each.

#ifndef SCREEN_H
#define SCREEN_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @def SCREEN_DEFAULT_ROWS
///
/// @brief The number of rows assumed when nothing gives the size.
#define SCREEN_DEFAULT_ROWS 24

/// @def SCREEN_DEFAULT_COLUMNS
///
/// @brief The number of columns assumed when nothing gives the size.
#define SCREEN_DEFAULT_COLUMNS 80

/// @fn static inline void screenMoveTo(int row, int column)
///
/// @brief Move the cursor.
///
/// @param row The row, from 1.
/// @param column The column, from 1.
///
/// @return This function returns no value.
static inline void screenMoveTo(int row, int column) {
  printf("\033[%d;%dH", row, column);
}

/// @fn static inline void screenClearToEndOfLine(void)
///
/// @brief Erase from the cursor to the end of its row.
///
/// @return This function returns no value.
static inline void screenClearToEndOfLine(void) {
  fputs("\033[K", stdout);
}

/// @fn static inline void screenInsertLines(int count)
///
/// @brief Insert blank rows at the cursor's row, moving the rows from there
/// down and losing those pushed off the bottom.
///
/// @param count The number of rows.
///
/// @return This function returns no value.
static inline void screenInsertLines(int count) {
  printf("\033[%dL", count);
}

/// @fn static inline void screenDeleteLines(int count)
///
/// @brief Delete rows from the cursor's row down, moving the rows below up
/// and leaving blank rows at the bottom.
///
/// @param count The number of rows.
///
/// @return This function returns no value.
static inline void screenDeleteLines(int count) {
  printf("\033[%dM", count);
}

/// @fn static inline void screenClear(void)
///
/// @brief Erase the screen and move the cursor to its top left corner.
///
/// @return This function returns no value.
static inline void screenClear(void) {
  fputs("\033[2J\033[H", stdout);
}

/// @fn static inline void screenGetSize(int *rows, int *columns)
///
/// @brief Get the terminal's size:  from LINES and COLUMNS if they're set,
/// else from tcgetwinsize, else SCREEN_DEFAULT_ROWS by
/// SCREEN_DEFAULT_COLUMNS.
///
/// @param rows Set to the number of rows.
/// @param columns Set to the number of columns.
///
/// @return This function returns no value.
static inline void screenGetSize(int *rows, int *columns) {
  struct winsize winsize;
  if (tcgetwinsize(STDIN_FILENO, &winsize) == 0) {
    *rows = winsize.ws_row;
    *columns = winsize.ws_col;
  } else {
    *rows = SCREEN_DEFAULT_ROWS;
    *columns = SCREEN_DEFAULT_COLUMNS;
  }

  const char *value = getenv("LINES");
  if (value != NULL) {
    *rows = (int) strtol(value, NULL, 10);
  }
  value = getenv("COLUMNS");
  if (value != NULL) {
    *columns = (int) strtol(value, NULL, 10);
  }
}

/// @fn static inline bool screenParseResize(const char *sequence,
///   int *rows, int *columns)
///
/// @brief Recognize a resize report, ESC[48;rows;columns;height;widtht,
/// among the escape sequences read from the terminal.
///
/// @param sequence The escape sequence, which needn't be NUL-terminated past
///   its final byte.
/// @param rows Set to the number of rows if it is a resize report.
/// @param columns Set to the number of columns if it is a resize report.
///
/// @return Returns true if the sequence is a resize report, false otherwise.
static inline bool screenParseResize(
  const char *sequence, int *rows, int *columns
) {
  if ((sequence[0] != '\033') || (sequence[1] != '[') || (sequence[2] != '4')
    || (sequence[3] != '8') || (sequence[4] != ';')
  ) {
    return false;
  }
  char *end = NULL;
  long newRows = strtol(&sequence[5], &end, 10);
  if (*end != ';') {
    return false;
  }
  long newColumns = strtol(end + 1, &end, 10);
  if ((newRows < 2) || (newColumns < 2)) {
    return false;
  }
  *rows = (int) newRows;
  *columns = (int) newColumns;
  return true;
}

#ifdef __cplusplus
}
#endif

#endif // SCREEN_H
