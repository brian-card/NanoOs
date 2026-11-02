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

/// @file vi.h
///
/// @brief State and helpers shared by the overlays of the `vi` command:
/// loading and committing the current line, drawing, moving between lines,
/// and reading keys.  They are inline so that each overlay that draws can use
/// them without an overlay call.

#ifndef VI_H
#define VI_H

#include "Ex.h"
#include "Screen.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def VI_LINE_MAX
///
/// @brief The size, in bytes, of the buffer holding the current line.  A line
/// longer than this can be shown but not changed within the line.
#define VI_LINE_MAX 120

/// @def VI_PENDING_SIZE
///
/// @brief The most input bytes kept for later when one read holds more than
/// one key.
#define VI_PENDING_SIZE 8

/// @def VI_TAB_STOP
///
/// @brief The number of columns between tab stops.
#define VI_TAB_STOP 8

/// @enum ViKey
///
/// @brief The keys viReadKey returns that aren't a byte of input.
typedef enum ViKey {
  VI_KEY_EOF = -1,
  VI_KEY_ESCAPE = 0x1b,
  VI_KEY_NONE = 0x100,
  VI_KEY_UP,
  VI_KEY_DOWN,
  VI_KEY_RIGHT,
  VI_KEY_LEFT,
  VI_KEY_RESIZE,
} ViKey;

/// @struct ViState
///
/// @brief Everything `vi` keeps.  It is the only memory `vi` allocates.
///
/// @param ex The ExState of the editing commands.
/// @param rows The number of rows on the screen.  The last is the status line.
/// @param columns The number of columns on the screen.
/// @param top The number of the line at the top of the screen.
/// @param line The number of the current line.  With no lines, it is 1.
/// @param lineRow The screen row, from 0, that the current line starts on.
/// @param index The index in text of the character under the cursor.
/// @param length The number of characters in text.
/// @param width The number of columns the whole current line takes up.
/// @param truncated Whether the current line is longer than text holds.
/// @param changed Whether text differs from the line in the store.
/// @param haveYank Whether a line has been yanked.
/// @param quit Whether `vi` is to exit.
/// @param yank The text-file offset of the yanked line.
/// @param numPending The number of bytes in pending.
/// @param pending Input read with an earlier key, not yet used.
/// @param text The current line, NUL-terminated, without its newline.
/// @param buffer The buffer the TextStore and key reading move text through.
/// @param terminal The terminal settings to restore on exit, or NULL if stdin
///   isn't a terminal.
typedef struct ViState {
  ExState  ex;
  int      rows;
  int      columns;
  int      top;
  int      line;
  int      lineRow;
  int      index;
  int      length;
  int      width;
  bool     truncated;
  bool     changed;
  bool     haveYank;
  bool     quit;
  uint32_t yank;
  uint8_t  numPending;
  char     pending[VI_PENDING_SIZE];
  char     text[VI_LINE_MAX];
  char     buffer[TEXTSTORE_BUFFER_SIZE];
  struct termios *terminal;
} ViState;

/// @struct ViDraw
///
/// @brief Where drawing a line has reached.
///
/// @param row The screen row being drawn, from 0.
/// @param column The screen column being drawn, from 0.
/// @param lastRow The last screen row that may be drawn.
/// @param lineColumn The column within the whole line, for tab stops.
typedef struct ViDraw {
  int row;
  int column;
  int lastRow;
  int lineColumn;
} ViDraw;

/// @fn static inline int viCharWidth(unsigned char c, int column)
///
/// @brief Get the number of columns a character takes up:  a tab reaches the
/// next tab stop, and other control characters are shown as ^ and a letter.
///
/// @param c The character.
/// @param column The column within the line the character starts at.
///
/// @return Returns the number of columns.
static inline int viCharWidth(unsigned char c, int column) {
  if (c == '\t') {
    return VI_TAB_STOP - (column % VI_TAB_STOP);
  }
  return ((c < ' ') || (c == 0x7f)) ? 2 : 1;
}

/// @fn static inline int viRowsFor(ViState *state, int width)
///
/// @brief Get the number of screen rows a line of a width takes up.
///
/// @param state The ViState of the running `vi`.
/// @param width The line's width in columns.
///
/// @return Returns the number of rows, at least 1.
static inline int viRowsFor(ViState *state, int width) {
  return (width == 0) ? 1 : ((width + state->columns - 1) / state->columns);
}

/// @fn static inline void viStatus(ViState *state, const char *message)
///
/// @brief Show a message on the status line.
///
/// @param state The ViState of the running `vi`.
/// @param message The message.
///
/// @return This function returns no value.
static inline void viStatus(ViState *state, const char *message) {
  screenMoveTo(state->rows, 1);
  fputs(message, stdout);
  screenClearToEndOfLine();
}

/// @fn static inline void viMeasure(ViState *state)
///
/// @brief Recompute the width of the current line after a change to text.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static inline void viMeasure(ViState *state) {
  state->width = 0;
  for (int ii = 0; ii < state->length; ii++) {
    state->width += viCharWidth((unsigned char) state->text[ii], state->width);
  }
}

/// @fn static inline int viReadLine(ViState *state, int line, bool keep)
///
/// @brief Measure a line from the store, keeping its text as the current
/// line's if asked to.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line.
/// @param keep Whether to put the line's text in state->text.
///
/// @return Returns the line's width on success, -1 with state->ex.store.error
/// set on failure.
static inline int viReadLine(ViState *state, int line, bool keep) {
  TextStore *store = &state->ex.store;
  int width = 0;
  if (keep) {
    state->length = 0;
    state->truncated = false;
  }
  if (line <= store->numLines) {
    uint32_t offset = textstoreLineOffset(store, line);
    if ((store->error != TEXTSTORE_ERROR_NONE)
      || (fseek(store->text, (long) offset, SEEK_SET) != 0)
    ) {
      store->error = TEXTSTORE_ERROR_IO;
      return -1;
    }
    bool ended = false;
    while (!ended
      && (fgets(state->buffer, TEXTSTORE_BUFFER_SIZE, store->text) != NULL)
    ) {
      int length = (int) strlen(state->buffer);
      ended = (length > 0) && (state->buffer[length - 1] == '\n');
      if (ended) {
        length--;
      }
      for (int ii = 0; ii < length; ii++) {
        width += viCharWidth((unsigned char) state->buffer[ii], width);
        if (!keep) {
          continue;
        } else if (state->length < VI_LINE_MAX - 1) {
          state->text[state->length++] = state->buffer[ii];
        } else {
          state->truncated = true;
        }
      }
    }
  }
  if (keep) {
    state->text[state->length] = '\0';
    state->width = width;
  }
  return width;
}

/// @fn static inline bool viLoadLine(ViState *state, int line)
///
/// @brief Make a line the current line.  The cursor keeps its index, which
/// may be past the line's end.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static inline bool viLoadLine(ViState *state, int line) {
  state->line = line;
  state->ex.current = line;
  state->changed = false;
  return viReadLine(state, line, true) >= 0;
}

/// @fn static inline bool viCommitLine(ViState *state)
///
/// @brief Put the current line's text in the store if it has changed.  The
/// line with no lines in the buffer becomes line 1.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static inline bool viCommitLine(ViState *state) {
  TextStore *store = &state->ex.store;
  if (!state->changed) {
    return true;
  }
  uint32_t offset = store->textEnd;
  if (!textstoreAppendText(store, state->text)
    || !textstoreAppendText(store, "\n")
  ) {
    return false;
  }
  if (state->line > store->numLines) {
    textstoreInsertLine(store, store->numLines, offset);
  } else {
    textstoreSetLineOffset(store, state->line, offset);
  }
  if (store->error != TEXTSTORE_ERROR_NONE) {
    return false;
  }
  state->ex.modified = true;
  state->changed = false;
  return true;
}

/// @fn static inline bool viAddEmptyLine(ViState *state, int after)
///
/// @brief Insert an empty line into the store.
///
/// @param state The ViState of the running `vi`.
/// @param after The number of the line to insert it after, 0 for the start.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static inline bool viAddEmptyLine(ViState *state, int after) {
  TextStore *store = &state->ex.store;
  uint32_t offset = store->textEnd;
  if (!textstoreAppendText(store, "\n")
    || !textstoreInsertLine(store, after, offset)
  ) {
    return false;
  }
  state->ex.modified = true;
  return true;
}

/// @fn static inline void viDrawCells(ViState *state, ViDraw *draw,
///   const char *cells, int count)
///
/// @brief Write characters that take a column each, wrapping to the next row
/// at the last column and stopping after the last row.
///
/// @param state The ViState of the running `vi`.
/// @param draw Where drawing has reached.
/// @param cells The characters.
/// @param count The number of characters.
///
/// @return This function returns no value.
static inline void viDrawCells(
  ViState *state, ViDraw *draw, const char *cells, int count
) {
  draw->lineColumn += count;
  while ((count > 0) && (draw->row <= draw->lastRow)) {
    if (draw->column == state->columns) {
      draw->row++;
      draw->column = 0;
      if (draw->row > draw->lastRow) {
        return;
      }
      screenMoveTo(draw->row + 1, 1);
    }
    int length = state->columns - draw->column;
    if (length > count) {
      length = count;
    }
    fwrite(cells, 1, (size_t) length, stdout);
    draw->column += length;
    cells += length;
    count -= length;
  }
}

/// @fn static inline void viDrawText(ViState *state, ViDraw *draw,
///   const char *text, int length)
///
/// @brief Draw part of a line, expanding tabs and showing other control
/// characters as ^ and a letter.
///
/// @param state The ViState of the running `vi`.
/// @param draw Where drawing has reached.
/// @param text The characters.
/// @param length The number of characters.
///
/// @return This function returns no value.
static inline void viDrawText(
  ViState *state, ViDraw *draw, const char *text, int length
) {
  int ii = 0;
  while (ii < length) {
    unsigned char c = (unsigned char) text[ii];
    if (c == '\t') {
      viDrawCells(state, draw, "        ",
        viCharWidth(c, draw->lineColumn));
      ii++;
    } else if ((c < ' ') || (c == 0x7f)) {
      char cells[2] = { '^', (char) (c ^ 0x40) };
      viDrawCells(state, draw, cells, 2);
      ii++;
    } else {
      int jj = ii;
      while ((jj < length) && ((unsigned char) text[jj] >= ' ')
        && ((unsigned char) text[jj] != 0x7f)
      ) {
        jj++;
      }
      viDrawCells(state, draw, &text[ii], jj - ii);
      ii = jj;
    }
  }
}

/// @fn static inline int viDrawLine(ViState *state, int line, int row)
///
/// @brief Draw a line, or ~ past the last line, starting at a screen row.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line.
/// @param row The screen row, from 0.
///
/// @return Returns the number of rows drawn.
static inline int viDrawLine(ViState *state, int line, int row) {
  TextStore *store = &state->ex.store;
  ViDraw draw = { row, 0, state->rows - 2, 0 };
  screenMoveTo(row + 1, 1);
  if ((line == state->line) && !state->truncated) {
    viDrawText(state, &draw, state->text, state->length);
  } else if (line > store->numLines) {
    viDrawCells(state, &draw, "~", 1);
  } else {
    uint32_t offset = textstoreLineOffset(store, line);
    bool ended = (store->error != TEXTSTORE_ERROR_NONE)
      || (fseek(store->text, (long) offset, SEEK_SET) != 0);
    while (!ended
      && (fgets(state->buffer, TEXTSTORE_BUFFER_SIZE, store->text) != NULL)
    ) {
      int length = (int) strlen(state->buffer);
      ended = (length > 0) && (state->buffer[length - 1] == '\n');
      viDrawText(state, &draw, state->buffer, ended ? length - 1 : length);
    }
  }
  if ((draw.row <= draw.lastRow) && (draw.column < state->columns)) {
    screenClearToEndOfLine();
  }
  return draw.row - row + 1;
}

/// @fn static inline void viDrawFrom(ViState *state, int row, int line)
///
/// @brief Draw lines from a screen row to the last row above the status line.
///
/// @param state The ViState of the running `vi`.
/// @param row The screen row, from 0.
/// @param line The number of the line to draw there.
///
/// @return This function returns no value.
static inline void viDrawFrom(ViState *state, int row, int line) {
  while (row < state->rows - 1) {
    row += viDrawLine(state, line, row);
    line++;
  }
}

/// @fn static inline void viPlaceCursor(ViState *state)
///
/// @brief Move the cursor to the character at state->index.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static inline void viPlaceCursor(ViState *state) {
  int column = 0;
  for (int ii = 0; ii < state->index; ii++) {
    column += viCharWidth((unsigned char) state->text[ii], column);
  }
  int row = state->lineRow + (column / state->columns);
  if (row > state->rows - 2) {
    row = state->rows - 2;
  }
  screenMoveTo(row + 1, (column % state->columns) + 1);
}

/// @fn static inline bool viShowLine(ViState *state, int line, int row,
///   bool redraw)
///
/// @brief Make a line the current line and show it at a screen row if it
/// fits there, or else scroll so that it is the top line, if it is above the
/// screen, or the bottom one.  The current line's changed text is kept.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line.
/// @param row The screen row to show it at, or -1 to put it at the top.
/// @param redraw Whether to redraw from the line down even if it fits.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static inline bool viShowLine(ViState *state, int line, int row, bool redraw) {
  int textRows = state->rows - 1;
  if (((line != state->line) || !state->changed) && !viLoadLine(state, line)) {
    return false;
  }
  int lineRows = viRowsFor(state, state->width);
  if ((row >= 0) && (row + lineRows <= textRows)) {
    state->lineRow = row;
    if (redraw) {
      viDrawFrom(state, row, line);
    }
    return true;
  }

  if (row < 0) {
    state->top = line;
    state->lineRow = 0;
  } else {
    int used = lineRows;
    state->top = line;
    while (state->top > 1) {
      int width = viReadLine(state, state->top - 1, false);
      if ((width < 0) || (used + viRowsFor(state, width) > textRows)) {
        break;
      }
      used += viRowsFor(state, width);
      state->top--;
    }
    state->lineRow = used - lineRows;
  }
  viDrawFrom(state, 0, state->top);
  return true;
}

/// @fn static inline void viRedrawCurrent(ViState *state, int oldRows)
///
/// @brief Redraw the current line after its text changed, and the lines
/// below it if it now takes up a different number of rows.
///
/// @param state The ViState of the running `vi`.
/// @param oldRows The number of rows the line took up before the change.
///
/// @return This function returns no value.
static inline void viRedrawCurrent(ViState *state, int oldRows) {
  viMeasure(state);
  int lineRows = viRowsFor(state, state->width);
  if (state->lineRow + lineRows > state->rows - 1) {
    viShowLine(state, state->line, state->rows, true);
  } else if (lineRows != oldRows) {
    viDrawFrom(state, state->lineRow, state->line);
  } else {
    viDrawLine(state, state->line, state->lineRow);
  }
}

/// @fn static inline int viReadKey(ViState *state)
///
/// @brief Read a key:  a byte of input, or an arrow key or resize report sent
/// as an escape sequence.  Escape followed by O and anything but an arrow
/// key's letter is taken as those keys typed quickly, since an SS3 sequence
/// is only ever an arrow key here.  Input read along with the key is kept for
/// the next call.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns the byte, a ViKey, or VI_KEY_EOF at the end of input.  For
/// VI_KEY_RESIZE, state->rows and state->columns are set.
static inline int viReadKey(ViState *state) {
  char *input = state->buffer;
  int length = state->numPending;
  if (length > 0) {
    memcpy(input, state->pending, (size_t) length);
    state->numPending = 0;
  } else {
    length = (int) fread(input, 1, TEXTSTORE_BUFFER_SIZE - 1, stdin);
    if (length <= 0) {
      return VI_KEY_EOF;
    }
  }
  input[length] = '\0';

  int key = (unsigned char) input[0];
  int used = 1;
  if ((key == VI_KEY_ESCAPE) && (length > 2)
    && ((input[1] == '[')
      || ((input[1] == 'O') && (input[2] >= 'A') && (input[2] <= 'D')))
  ) {
    used = 2;
    while ((used < length)
      && ((input[used] < 0x40) || (input[used] > 0x7e))
    ) {
      used++;
    }
    char final = input[used++];
    key = VI_KEY_NONE;
    if ((final >= 'A') && (final <= 'D')) {
      key = VI_KEY_UP + (final - 'A');
    } else if (screenParseResize(input, &state->rows, &state->columns)) {
      key = VI_KEY_RESIZE;
    }
  }

  if (used < length) {
    int remaining = length - used;
    if (remaining > VI_PENDING_SIZE) {
      remaining = VI_PENDING_SIZE;
    }
    memcpy(state->pending, &input[used], (size_t) remaining);
    state->numPending = (uint8_t) remaining;
  }
  return key;
}

#ifdef __cplusplus
}
#endif

#endif // VI_H
