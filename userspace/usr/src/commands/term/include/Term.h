///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.24.2026
///
/// @file              Term.h
///
/// @brief             What term's overlays share:  the terminal's state and
///                    the parser's.
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

#ifndef TERM_H
#define TERM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NanoOsUtils.h"
#include "../../../src/kernel/Window.h"

/// @def TERM_GRID
///
/// @brief The identifier of the terminal window's text grid.
#define TERM_GRID 1

/// @def TERM_OPS_SIZE
///
/// @brief The size of the buffer the parser collects grid operations in
/// before it sends them.
#define TERM_OPS_SIZE 256

/// @def TERM_DEFAULT_ATTRIBUTE
///
/// @brief The attribute text starts with and SGR 0 goes back to:  bright
/// white on black, as on the VDP console.
#define TERM_DEFAULT_ATTRIBUTE \
  WIN_ATTRIBUTE(WIN_ANSI_BRIGHT_WHITE, WIN_ANSI_BLACK)

/// @def TERM_DEFAULT_COLUMNS
///
/// @brief The terminal's columns unless --size says otherwise.
#define TERM_DEFAULT_COLUMNS 80

/// @def TERM_DEFAULT_ROWS
///
/// @brief The terminal's rows unless --size says otherwise.
#define TERM_DEFAULT_ROWS 24

/// @def TERM_DEFAULT_LEFT
///
/// @brief The left edge, in pixels, of the terminal's window unless --at
/// says otherwise.
#define TERM_DEFAULT_LEFT 24

/// @def TERM_DEFAULT_TOP
///
/// @brief The top edge, in pixels, of the terminal's window unless --at says
/// otherwise.
#define TERM_DEFAULT_TOP 24

/// @def TERM_TAB_WIDTH
///
/// @brief The columns between tab stops.
#define TERM_TAB_WIDTH 8

/// @def ANSI_ERASE_ALL
///
/// @brief The ED (erase in display) parameter that erases the whole display.
#define ANSI_ERASE_ALL 2

/// @def ANSI_INTERMEDIATE_FIRST
///
/// @brief The first byte that can follow ESC before an escape sequence's
/// final byte.
#define ANSI_INTERMEDIATE_FIRST 0x20

/// @def ANSI_INTERMEDIATE_LAST
///
/// @brief The last byte that can follow ESC before an escape sequence's
/// final byte.
#define ANSI_INTERMEDIATE_LAST 0x2f

/// @def ANSI_PARAMETER_LAST
///
/// @brief The last byte that can come before a control sequence's final
/// byte, from ANSI_INTERMEDIATE_FIRST.
#define ANSI_PARAMETER_LAST 0x3f

/// @def ANSI_FINAL_FIRST
///
/// @brief The first byte that ends a control sequence.
#define ANSI_FINAL_FIRST 0x40

/// @def ANSI_FINAL_LAST
///
/// @brief The last byte that ends a control sequence.
#define ANSI_FINAL_LAST 0x7e

/// @def TERM_MODE_DIGITS_LIMIT
///
/// @brief The private mode number past which more digits make it
/// UINT16_MAX, a mode nothing matches, instead of overflowing.
#define TERM_MODE_DIGITS_LIMIT 1000

/// @def TERM_LOG_LINE
///
/// @brief The longest message term logs.
#define TERM_LOG_LINE 96

/// @struct TermParser
///
/// @brief What the parser of a session's output keeps between strings:  the
/// cursor, the attribute, and the escape sequence it is in.  It keeps no copy
/// of the grid, which the window server holds.
///
/// @param columns The grid's columns.
/// @param rows The grid's rows.
/// @param row The cursor's row.
/// @param column The cursor's column, which is columns while a wrap at the
///   end of the bottom row waits for the next character.
/// @param attribute The attribute characters are drawn in.
/// @param escape The TermEscape state.
/// @param parameter The parameter being read.
/// @param maxParameter The largest parameter of the sequence.
/// @param numParameters The number of parameters read.
/// @param firstParameter The first parameter.
/// @param secondParameter The second parameter.
/// @param foreground The foreground the sequence selects, or 0xFF.
/// @param background The background the sequence selects, or 0xFF.
/// @param reports Whether the program asked for resize reports.
/// @param cursorRow The cursor row last sent the grid.
/// @param cursorColumn The cursor column last sent the grid.
/// @param privateMode The number of a private mode sequence.
/// @param put The index in ops of the PUT characters are added to, or -1.
/// @param used The bytes of ops in use.
/// @param ops The grid operations not yet sent.
typedef struct TermParser {
  uint8_t  columns;
  uint8_t  rows;
  uint8_t  row;
  uint8_t  column;
  uint8_t  attribute;
  uint8_t  escape;
  uint8_t  parameter;
  uint8_t  maxParameter;
  uint8_t  numParameters;
  uint8_t  firstParameter;
  uint8_t  secondParameter;
  uint8_t  foreground;
  uint8_t  background;
  bool     reports;
  uint8_t  cursorRow;
  uint8_t  cursorColumn;
  uint16_t privateMode;
  int16_t  put;
  uint16_t used;
  uint8_t  ops[TERM_OPS_SIZE];
} TermParser;

/// @struct Term
///
/// @brief What term's overlays share.
///
/// @param window The terminal's window.
/// @param consolePid The console's ProcessId.
/// @param written The rc of the last grid write, 0 or a negative errno.
/// @param text The text the Parse overlay parses next.
/// @param tee Whether what the session prints is also written to stderr,
///   which, unlike stdout, isn't buffered.
/// @param message The message the Log overlay logs.
/// @param teeText A copy of the text being written to stderr.
/// @param session The connection to the window server.
/// @param parser The parser.
typedef struct Term {
  int         window;
  ProcessId   consolePid;
  int         written;
  const char *text;
  bool        tee;
  char        message[TERM_LOG_LINE];
  char        teeText[CONSOLE_BUFFER_SIZE + 1];
  WinSession  session;
  TermParser  parser;
} Term;

#endif // TERM_H
