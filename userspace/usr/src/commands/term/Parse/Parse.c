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



/// @file Parse.c
///
/// @brief term's parser:  a session's output, turned into text grid
/// operations so that the grid shows what vdp.drv's console would for the
/// same bytes.

#include "Term.h"

/// @enum TermEscape
///
/// @brief Where the parser is in an escape sequence, as vdp.drv's
/// vdpTranslateEscape tracks it.
typedef enum TermEscape {
  TERM_ESCAPE_NONE = 0,
  TERM_ESCAPE_STARTED,
  TERM_ESCAPE_CSI,
  TERM_ESCAPE_CSI_PRIVATE,
  TERM_ESCAPE_CSI_IGNORED,
} TermEscape;

/// @def TERM_UNCHANGED
///
/// @brief A TermParser color that the sequence being read doesn't set.
#define TERM_UNCHANGED 0xFF

/// @def TERM_RESIZE_REPORT_MODE
///
/// @brief The private mode that turns resize reports on and off.
#define TERM_RESIZE_REPORT_MODE 2048

/// @fn static void flush(Term *term)
///
/// @brief Send the grid the operations collected, and wait until the window
/// server has carried them out.
///
/// @param term The terminal.
///
/// @return This function returns no value.
static void flush(Term *term) {
  TermParser *parser = &term->parser;
  if (parser->used > 0) {
    term->written = winGridWrite(&term->session, (uint8_t) term->window,
      TERM_GRID, parser->ops, parser->used);
  }
  parser->used = 0;
  parser->put = -1;
}

/// @fn static uint8_t* reserve(Term *term, uint16_t size)
///
/// @brief Make room for an operation that isn't a PUT, sending what's
/// collected first if it doesn't fit.
///
/// @param term The terminal.
/// @param size The operation's size.
///
/// @return Returns where to write the operation.
static uint8_t* reserve(Term *term, uint16_t size) {
  TermParser *parser = &term->parser;
  if (parser->used + size > TERM_OPS_SIZE) {
    flush(term);
  }
  uint8_t *op = &parser->ops[parser->used];
  parser->used += size;
  parser->put = -1;
  return op;
}

/// @fn static void lineFeed(Term *term)
///
/// @brief Move the cursor down a row, scrolling the grid up a row in the
/// current attribute at the bottom.
///
/// @param term The terminal.
///
/// @return This function returns no value.
static void lineFeed(Term *term) {
  TermParser *parser = &term->parser;
  if (parser->row + 1 < parser->rows) {
    parser->row++;
    return;
  }
  uint8_t *op = reserve(term, WIN_GRID_SCROLL_SIZE);
  op[WIN_GRID_TAG] = WIN_GRID_SCROLL;
  op[WIN_GRID_SCROLL_TOP] = 0;
  op[WIN_GRID_SCROLL_BOTTOM] = parser->rows - 1;
  op[WIN_GRID_SCROLL_COUNT] = 1;
  op[WIN_GRID_SCROLL_ATTRIBUTE] = parser->attribute;
}

/// @fn static void draw(Term *term, uint8_t c)
///
/// @brief Draw a character at the cursor and move the cursor past it, as the
/// VDP does with scroll protection on:  the last column wraps at once,
/// except on the bottom row, where the wrap waits for the next character.
///
/// @param term The terminal.
/// @param c The character.
///
/// @return This function returns no value.
static void draw(Term *term, uint8_t c) {
  TermParser *parser = &term->parser;
  if (parser->column >= parser->columns) {
    parser->column = 0;
    lineFeed(term);
  }
  int16_t put = parser->put;
  uint8_t *op = NULL;
  if (put >= 0) {
    op = &parser->ops[put];
  }
  if ((op == NULL) || (op[WIN_GRID_PUT_ROW] != parser->row)
    || (op[WIN_GRID_PUT_COLUMN] + op[WIN_GRID_PUT_LENGTH] != parser->column)
    || (op[WIN_GRID_PUT_ATTRIBUTE] != parser->attribute)
    || (op[WIN_GRID_PUT_LENGTH] == UINT8_MAX)
    || (parser->used == TERM_OPS_SIZE)
  ) {
    if (parser->used + WIN_GRID_PUT_SIZE(1) > TERM_OPS_SIZE) {
      flush(term);
    }
    put = (int16_t) parser->used;
    op = &parser->ops[put];
    op[WIN_GRID_TAG] = WIN_GRID_PUT;
    op[WIN_GRID_PUT_ROW] = parser->row;
    op[WIN_GRID_PUT_COLUMN] = parser->column;
    op[WIN_GRID_PUT_ATTRIBUTE] = parser->attribute;
    op[WIN_GRID_PUT_LENGTH] = 0;
    parser->used += WIN_GRID_PUT_SIZE(0);
    parser->put = put;
  }
  parser->ops[parser->used++] = c;
  op[WIN_GRID_PUT_LENGTH]++;
  parser->column++;
  if ((parser->column >= parser->columns)
    && (parser->row + 1 < parser->rows)
  ) {
    parser->column = 0;
    parser->row++;
  }
}

/// @fn static uint8_t cursorIndex(uint8_t parameter, uint8_t limit)
///
/// @brief Convert a one-based cursor position parameter, where 0 means 1, to
/// a zero-based index less than a limit.
///
/// @param parameter The parameter.
/// @param limit The number of rows or columns.
///
/// @return Returns the index.
static uint8_t cursorIndex(uint8_t parameter, uint8_t limit) {
  uint8_t index = (parameter > 1) ? (uint8_t) (parameter - 1) : 0;
  return (index < limit) ? index : (uint8_t) (limit - 1);
}

/// @fn static void endParameter(TermParser *parser)
///
/// @brief Finish reading a control sequence parameter, applying it to the
/// colors the sequence selects as a Select Graphic Rendition parameter, as
/// vdp.drv's vdpEndParameter does.
///
/// @param parser The parser.
///
/// @return This function returns no value.
static void endParameter(TermParser *parser) {
  uint8_t parameter = parser->parameter;
  if (parameter > parser->maxParameter) {
    parser->maxParameter = parameter;
  }
  if (parser->numParameters == 0) {
    parser->firstParameter = parameter;
  } else if (parser->numParameters == 1) {
    parser->secondParameter = parameter;
  }
  parser->numParameters++;
  parser->parameter = 0;

  if (parameter == ANSI_SGR_RESET) {
    parser->foreground = WIN_ATTRIBUTE_FOREGROUND(TERM_DEFAULT_ATTRIBUTE);
    parser->background = WIN_ATTRIBUTE_BACKGROUND(TERM_DEFAULT_ATTRIBUTE);
  } else if (parameter == ANSI_SGR_FOREGROUND_DEFAULT) {
    parser->foreground = WIN_ATTRIBUTE_FOREGROUND(TERM_DEFAULT_ATTRIBUTE);
  } else if (parameter == ANSI_SGR_BACKGROUND_DEFAULT) {
    parser->background = WIN_ATTRIBUTE_BACKGROUND(TERM_DEFAULT_ATTRIBUTE);
  } else {
    uint8_t bright = 0;
    uint8_t *color = &parser->foreground;
    if (parameter >= ANSI_SGR_BRIGHT_FOREGROUND) {
      parameter -= ANSI_SGR_BRIGHT_FOREGROUND - ANSI_SGR_FOREGROUND;
      bright = ANSI_COLORS_PER_SET;
    }
    if (parameter >= ANSI_SGR_BACKGROUND) {
      parameter -= ANSI_SGR_BACKGROUND - ANSI_SGR_FOREGROUND;
      color = &parser->background;
    }
    parameter -= ANSI_SGR_FOREGROUND;
    if (parameter < ANSI_COLORS_PER_SET) {
      *color = parameter + bright;
    }
  }
}

/// @fn static void finishSequence(Term *term, uint8_t c)
///
/// @brief Carry out a control sequence that has ended:  set colors, clear
/// the grid, move the cursor, erase to the end of the line, or insert or
/// delete rows.  Every other sequence does nothing.
///
/// @param term The terminal.
/// @param c The sequence's final byte.
///
/// @return This function returns no value.
static void finishSequence(Term *term, uint8_t c) {
  TermParser *parser = &term->parser;
  endParameter(parser);
  if (c == 'm') {
    if (parser->foreground != TERM_UNCHANGED) {
      parser->attribute = WIN_ATTRIBUTE(parser->foreground,
        WIN_ATTRIBUTE_BACKGROUND(parser->attribute));
    }
    if (parser->background != TERM_UNCHANGED) {
      parser->attribute = WIN_ATTRIBUTE(
        WIN_ATTRIBUTE_FOREGROUND(parser->attribute), parser->background);
    }
  } else if ((c == 'J') && (parser->maxParameter == ANSI_ERASE_ALL)) {
    uint8_t *op = reserve(term, WIN_GRID_CLEAR_SIZE);
    op[WIN_GRID_TAG] = WIN_GRID_CLEAR;
    op[WIN_GRID_CLEAR_TOP] = 0;
    op[WIN_GRID_CLEAR_LEFT] = 0;
    op[WIN_GRID_CLEAR_BOTTOM] = parser->rows - 1;
    op[WIN_GRID_CLEAR_RIGHT] = parser->columns - 1;
    op[WIN_GRID_CLEAR_ATTRIBUTE] = parser->attribute;
    parser->row = 0;
    parser->column = 0;
  } else if ((c == 'H') || (c == 'f')) {
    parser->row = cursorIndex(parser->firstParameter, parser->rows);
    parser->column = cursorIndex(parser->secondParameter, parser->columns);
  } else if ((c == 'K') && (parser->maxParameter == 0)
    && (parser->column < parser->columns)
  ) {
    uint8_t *op = reserve(term, WIN_GRID_CLEAR_SIZE);
    op[WIN_GRID_TAG] = WIN_GRID_CLEAR;
    op[WIN_GRID_CLEAR_TOP] = parser->row;
    op[WIN_GRID_CLEAR_LEFT] = parser->column;
    op[WIN_GRID_CLEAR_BOTTOM] = parser->row;
    op[WIN_GRID_CLEAR_RIGHT] = parser->columns - 1;
    op[WIN_GRID_CLEAR_ATTRIBUTE] = parser->attribute;
  } else if ((c == 'L') || (c == 'M')) {
    int8_t count = (int8_t) (cursorIndex(parser->firstParameter,
      parser->rows - parser->row) + 1);
    uint8_t *op = reserve(term, WIN_GRID_SCROLL_SIZE);
    op[WIN_GRID_TAG] = WIN_GRID_SCROLL;
    op[WIN_GRID_SCROLL_TOP] = parser->row;
    op[WIN_GRID_SCROLL_BOTTOM] = parser->rows - 1;
    op[WIN_GRID_SCROLL_COUNT] = (uint8_t) ((c == 'L') ? -count : count);
    op[WIN_GRID_SCROLL_ATTRIBUTE] = parser->attribute;
    parser->column = 0;
  }
}

/// @fn static void escape(Term *term, uint8_t c)
///
/// @brief Advance an escape sequence by one byte, as vdp.drv's
/// vdpTranslateEscape does.
///
/// @param term The terminal.
/// @param c The byte.
///
/// @return This function returns no value.
static void escape(Term *term, uint8_t c) {
  TermParser *parser = &term->parser;
  if (c == '\033') {
    parser->escape = TERM_ESCAPE_STARTED;
  } else if (parser->escape == TERM_ESCAPE_STARTED) {
    if (c == '[') {
      parser->escape = TERM_ESCAPE_CSI;
      parser->parameter = 0;
      parser->maxParameter = 0;
      parser->numParameters = 0;
      parser->secondParameter = 0;
      parser->foreground = TERM_UNCHANGED;
      parser->background = TERM_UNCHANGED;
    } else if ((c < ANSI_INTERMEDIATE_FIRST) || (c > ANSI_INTERMEDIATE_LAST)) {
      parser->escape = TERM_ESCAPE_NONE;
    }
  } else if (parser->escape == TERM_ESCAPE_CSI_PRIVATE) {
    if ((c >= '0') && (c <= '9')) {
      parser->privateMode = (parser->privateMode < TERM_MODE_DIGITS_LIMIT)
        ? (uint16_t) ((parser->privateMode * 10) + (c - '0')) : UINT16_MAX;
    } else if ((c >= ANSI_FINAL_FIRST) && (c <= ANSI_FINAL_LAST)) {
      if ((parser->privateMode == TERM_RESIZE_REPORT_MODE)
        && ((c == 'h') || (c == 'l'))
      ) {
        parser->reports = (c == 'h');
        uint8_t *op = reserve(term, WIN_GRID_REPORTS_SIZE);
        op[WIN_GRID_TAG] = WIN_GRID_REPORTS;
        op[WIN_GRID_REPORTS_ON] = parser->reports;
      }
      parser->escape = TERM_ESCAPE_NONE;
    }
  } else if ((c >= '0') && (c <= '9')) {
    unsigned int parameter = (parser->parameter * 10u) + (c - '0');
    parser->parameter
      = (parameter > UINT8_MAX) ? UINT8_MAX : (uint8_t) parameter;
  } else if (c == ';') {
    endParameter(parser);
  } else if ((c == '?') && (parser->escape == TERM_ESCAPE_CSI)) {
    parser->escape = TERM_ESCAPE_CSI_PRIVATE;
    parser->privateMode = 0;
  } else if ((c >= ANSI_INTERMEDIATE_FIRST) && (c <= ANSI_PARAMETER_LAST)) {
    parser->escape = TERM_ESCAPE_CSI_IGNORED;
  } else {
    if ((parser->escape == TERM_ESCAPE_CSI) && (c >= ANSI_FINAL_FIRST)
      && (c <= ANSI_FINAL_LAST)
    ) {
      finishSequence(term, c);
    }
    parser->escape = TERM_ESCAPE_NONE;
  }
}

/// @fn static void parse(Term *term, const uint8_t *text)
///
/// @brief Turn a string of a session's output into grid operations, as
/// vdp.drv's vdpTranslateText turns it into VDU bytes and the VDP carries
/// them out:  a newline is a carriage return and a line feed, a tab is
/// spaces to the next multiple of TERM_TAB_WIDTH columns, a backspace moves
/// back and up to the end of the row above at the start of a row, a bell
/// draws nothing, escape sequences are vdpTranslateEscape's, and every other
/// byte is drawn as its glyph.
///
/// @param term The terminal.
/// @param text The string.
///
/// @return This function returns no value.
static void parse(Term *term, const uint8_t *text) {
  TermParser *parser = &term->parser;
  for (; *text != '\0'; text++) {
    uint8_t c = *text;
    if ((c == '\033') || (parser->escape != TERM_ESCAPE_NONE)) {
      escape(term, c);
    } else if (c == '\n') {
      parser->column = 0;
      lineFeed(term);
    } else if (c == '\r') {
      parser->column = 0;
    } else if (c == '\t') {
      for (uint8_t count = TERM_TAB_WIDTH - (parser->column % TERM_TAB_WIDTH);
        count > 0; count--
      ) {
        draw(term, ' ');
      }
    } else if (c == '\b') {
      if (parser->column > 0) {
        parser->column--;
      } else if (parser->row > 0) {
        parser->row--;
        parser->column = parser->columns - 1;
      }
    } else if (c != '\a') {
      draw(term, c);
    }
  }
}

/// @fn void* termParse(void *args)
///
/// @brief Draw the terminal's text in its grid, then put the grid's cursor
/// where the parser's is, if it moved, and send what's collected.
///
/// @param args A pointer to the Term, whose text is set.
///
/// @return Always returns the provided pointer.
void* termParse(void *args) {
  Term *term = (Term*) args;
  TermParser *parser = &term->parser;
  parse(term, (const uint8_t*) term->text);
  uint8_t column = parser->column;
  if (column >= parser->columns) {
    column = parser->columns - 1;
  }
  if ((parser->cursorRow != parser->row)
    || (parser->cursorColumn != column)
  ) {
    uint8_t *op = reserve(term, WIN_GRID_CURSOR_SIZE);
    op[WIN_GRID_TAG] = WIN_GRID_CURSOR;
    op[WIN_GRID_CURSOR_ROW] = parser->row;
    op[WIN_GRID_CURSOR_COLUMN] = column;
    op[WIN_GRID_CURSOR_SHOWN] = 1;
    parser->cursorRow = parser->row;
    parser->cursorColumn = column;
  }
  flush(term);
  return args;
}
