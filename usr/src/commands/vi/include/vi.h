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
#define VI_LINE_MAX 112

/// @def VI_PENDING_SIZE
///
/// @brief The most input bytes kept for later when one read holds more than
/// one key:  the O and letter after an escape that isn't an arrow key.
#define VI_PENDING_SIZE 4

/// @def VI_PATTERN_POSITION
///
/// @brief The position in the block file's header area of the last search
/// pattern, after the marks the ex library keeps there.
#define VI_PATTERN_POSITION (EX_MARK_POSITION('z') + sizeof(uint32_t))

/// @def VI_PATTERN_SIZE
///
/// @brief The size, in bytes, of the search pattern, with its NUL.
#define VI_PATTERN_SIZE 32

/// @def VI_DOT_KEYS
///
/// @brief The most keys of a command, after its count, that . can repeat.
#define VI_DOT_KEYS 5

/// @struct ViDot
///
/// @brief The last change, kept in the block file's header area after the
/// search pattern for . to repeat.
///
/// @param count The command's count, or 0 if it had none.
/// @param numKeys The number of keys in keys, or 0 if there is no change to
///   repeat.
/// @param textLength The number of characters the command inserted.
/// @param next The index in keys of the next key to repeat.
/// @param keys The keys of the command, without its counts.
typedef struct ViDot {
  int16_t count;
  uint8_t numKeys;
  uint8_t textLength;
  uint8_t next;
  char    keys[VI_DOT_KEYS];
} ViDot;

/// @def VI_DOT_POSITION
///
/// @brief The position in the block file's header area of the ViDot.
#define VI_DOT_POSITION (VI_PATTERN_POSITION + VI_PATTERN_SIZE)

/// @def VI_DOT_PENDING_POSITION
///
/// @brief The position in the block file's header area of the ViDot of the
/// command being run, which becomes the one at VI_DOT_POSITION if the command
/// changes the buffer.
#define VI_DOT_PENDING_POSITION (VI_DOT_POSITION + sizeof(ViDot))

/// @def VI_DOT_TEXT_POSITION
///
/// @brief The position in the block file's header area of the text the last
/// change inserted.
#define VI_DOT_TEXT_POSITION (VI_DOT_PENDING_POSITION + sizeof(ViDot))

/// @def VI_DOT_TEXT_SIZE
///
/// @brief The most characters of inserted text that . can repeat.
#define VI_DOT_TEXT_SIZE (TEXTSTORE_HEADER_SIZE - VI_DOT_TEXT_POSITION)

/// @enum ViStep
///
/// @brief The overlay the main overlay runs next for a command the Normal
/// overlay is running, after which it runs the Normal overlay again.
typedef enum ViStep {
  VI_STEP_NONE,
  VI_STEP_MOTION,
  VI_STEP_OPERATOR,
  VI_STEP_CHANGE,
  VI_STEP_PUT,
  VI_STEP_INSERT,
  VI_STEP_COMMAND,
  VI_STEP_HISTORY,
} ViStep;

/// @enum ViYankKind
///
/// @brief What the yank chain holds.
typedef enum ViYankKind {
  VI_YANK_NONE,
  VI_YANK_LINES,
  VI_YANK_TEXT,
} ViYankKind;

/// @struct ViYank
///
/// @brief A record of the yank chain:  a yanked line, or the yanked text
/// within a line, kept in the text file.
///
/// @param offset The text-file offset of the line or text.
/// @param length For text within a line, its length.
/// @param unused Padding to the chain's record size.
typedef struct ViYank {
  uint32_t offset;
  int32_t  length;
  uint32_t unused;
} ViYank;

/// @def VI_TAB_STOP
///
/// @brief The number of columns between tab stops.
#define VI_TAB_STOP 8

/// @enum ViRequest
///
/// @brief What an overlay that changed the buffer asks to be shown once it
/// returns:  the main overlay moves to a line or redraws the screen, and the
/// Show overlay shows lines that replaced others.
typedef enum ViRequest {
  VI_REQUEST_NONE,
  VI_REQUEST_MOVE,
  VI_REQUEST_SHOW,
  VI_REQUEST_SPLIT,
  VI_REQUEST_REDRAW,
} ViRequest;

/// @enum ViError
///
/// @brief The errors of `vi`'s own, numbered after the ExErrors so that both
/// share the TextStore's error member.  VI_ERROR_MOTION has no message:  a
/// motion that can't move just doesn't.
typedef enum ViError {
  VI_ERROR_MOTION = NUM_EX_ERRORS,
  VI_ERROR_NOT_FOUND,
  VI_ERROR_NO_PATTERN,
  VI_ERROR_TOO_LONG,
  NUM_VI_ERRORS
} ViError;

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
/// @brief Everything `vi` keeps.  It is the only memory `vi` allocates.  Its
/// members are ordered by size so that no padding is needed between them.
///
/// @param ex The ExState of the editing commands.  vi uses its addr2 for the
///   line a motion reaches.
/// @param yank The yank chain of ViYank records.
/// @param savedModes The terminal's local modes (c_lflag) to restore on exit.
/// @param top The number of the line at the top of the screen.
/// @param line The number of the current line.  With no lines, it is 1.
/// @param width The number of columns the whole current line takes up.
/// @param requestLine The line to make current for a request.
/// @param rows The number of rows on the screen.  The last is the status line.
/// @param columns The number of columns on the screen.
/// @param lineRow The screen row, from 0, that the current line starts on.
/// @param index The index in text of the character under the cursor.
/// @param length The number of characters in text.
/// @param requestRow The screen row to show requestLine at.
/// @param width The number of columns the whole current line takes up, at
///   most INT16_MAX.
/// @param count The count typed before the command, or 0 if none was.
/// @param targetIndex The index in the line a motion reaches.  When insert
///   mode ends, the index after the last character inserted; when . repeats
///   an insertion, the number of characters to insert.
/// @param insertStart The index insert mode started inserting at, or -1 if
///   it erased characters from before there.
/// @param truncated Whether the current line is longer than text holds.
/// @param changed Whether text differs from the line in the store.
/// @param quit Whether `vi` is to exit.
/// @param beginChange Whether the command being run has yet to start a change
///   in the undo history.
/// @param repeating Whether . is repeating the command being run.
/// @param haveTerminal Whether stdin is a terminal, whose modes are saved in
///   savedModes.
/// @param exact Whether the insert mode of the command being run has stayed
///   on one line.
/// @param request The ViRequest:  show requestLine at requestRow, as
///   viRequest and viRequestRows describe.
/// @param yankKind The ViYankKind of the yank chain.
/// @param step The ViStep of the command being run.
/// @param operator The operator a motion is for, or '\0' to move the cursor.
/// @param motion The key of the motion.
/// @param argument The character typed after the command, for r, m, and ';
///   during insert mode, R if it replaces and i if it doesn't.
/// @param linewise Whether the motion's range is whole lines.
/// @param numPending The number of bytes in pending.
/// @param pending Input read with an earlier key, not yet used.
/// @param text The current line, NUL-terminated, without its newline.
/// @param buffer The buffer the TextStore and key reading move text through.
typedef struct ViState {
  ExState         ex;
  TextStoreChain  yank;
  tcflag_t        savedModes;
  int             top;
  int             line;
  int             requestLine;
  int16_t         rows;
  int16_t         columns;
  int16_t         lineRow;
  int16_t         index;
  int16_t         length;
  int16_t         requestRow;
  int16_t         width;
  int16_t         count;
  int16_t         targetIndex;
  int16_t         insertStart;
  bool            truncated;
  bool            changed;
  bool            quit;
  bool            beginChange;
  bool            repeating;
  bool            haveTerminal;
  bool            exact;
  uint8_t         request;
  uint8_t         yankKind;
  uint8_t         step;
  char            operator;
  char            motion;
  char            argument;
  bool            linewise;
  uint8_t         numPending;
  char            pending[VI_PENDING_SIZE];
  char            text[VI_LINE_MAX];
  char            buffer[TEXTSTORE_BUFFER_SIZE];
} ViState;

/// @def VI_SHOW_KEEP
///
/// @brief The viShowLine way of showing a line that draws nothing if it fits.
#define VI_SHOW_KEEP 0

/// @def VI_SHOW_REDRAW
///
/// @brief The viShowLine way of showing a line that redraws from it down.
#define VI_SHOW_REDRAW 1

/// @def VI_SHOW_FIT
///
/// @brief The viShowLine way of showing a line only if it fits.
#define VI_SHOW_FIT 2

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
/// @brief Get the number of screen rows a line of a width takes up.  It
/// subtracts rather than divides, since a division costs the SAMD21 a
/// 460-byte library routine in each overlay that has one.
///
/// @param state The ViState of the running `vi`.
/// @param width The line's width in columns.
///
/// @return Returns the number of rows, at least 1.
static inline int viRowsFor(ViState *state, int width) {
  int rows = 1;
  while (width > state->columns) {
    width -= state->columns;
    rows++;
  }
  return rows;
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

/// @fn static inline void viFileStatus(ViState *state, const char *name,
///   int numLines, const char *suffix)
///
/// @brief Show a file's name, its number of lines, and the last number of
/// bytes read or written on the status line.
///
/// @param state The ViState of the running `vi`.
/// @param name The file's name.
/// @param numLines The number of lines.
/// @param suffix The text to end the message with.
///
/// @return This function returns no value.
static inline void viFileStatus(
  ViState *state, const char *name, int numLines, const char *suffix
) {
  screenMoveTo(state->rows, 1);
  printf("\"%s\" %dL, %ldC%s", name, numLines, state->ex.byteCount, suffix);
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
/// line's if asked to.  An error already set is kept unless this fails.
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
    uint8_t error = store->error;
    store->error = TEXTSTORE_ERROR_NONE;
    uint32_t offset = textstoreLineOffset(store, line);
    if ((store->error != TEXTSTORE_ERROR_NONE)
      || (fseek(store->text, (long) offset, SEEK_SET) != 0)
    ) {
      store->error = TEXTSTORE_ERROR_IO;
      return -1;
    }
    store->error = error;
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
    state->width = (int16_t) ((width > INT16_MAX) ? INT16_MAX : width);
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

/// @fn static inline void viRequest(ViState *state, int line, int row,
///   uint8_t request)
///
/// @brief Ask the main overlay to show a line at a screen row.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line to make current.
/// @param row The screen row to show it at.
/// @param request VI_REQUEST_MOVE to redraw only if the screen scrolls, or
///   VI_REQUEST_REDRAW to redraw the whole screen.
///
/// @return This function returns no value.
static inline void viRequest(ViState *state, int line, int row,
  uint8_t request
) {
  state->requestLine = line;
  state->requestRow = (int16_t) row;
  state->request = request;
}

/// @fn static inline void viRequestRows(ViState *state, int line, int row,
///   uint8_t request, int oldRows, int numLines)
///
/// @brief Ask the Show overlay to show numLines lines from a line at a screen
/// row, in place of lines that took up oldRows rows there.  The counts are
/// kept in ex.addr1 and ex.destination, which only the Command overlay's
/// address parsing uses, since ViState has no room for them.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the first line, which is made current.
/// @param row The screen row it starts on.
/// @param request VI_REQUEST_SHOW, or VI_REQUEST_SPLIT to redraw the current
///   line first.
/// @param oldRows The number of rows the replaced lines took up.
/// @param numLines The number of lines that replace them.
///
/// @return This function returns no value.
static inline void viRequestRows(ViState *state, int line, int row,
  uint8_t request, int oldRows, int numLines
) {
  viRequest(state, line, row, request);
  state->ex.addr1 = oldRows;
  state->ex.destination = numLines;
}

/// @fn static inline int viFirstNonBlank(ViState *state)
///
/// @brief Find the first character of the current line that isn't a space or
/// a tab.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns its index, or the line's length if there is none.
static inline int viFirstNonBlank(ViState *state) {
  int index = 0;
  while ((state->text[index] == ' ') || (state->text[index] == '\t')) {
    index++;
  }
  return index;
}

/// @fn static inline void viClearYank(ViState *state)
///
/// @brief Empty the yank chain, putting its blocks on the free list.
///
/// @param state The ViState of the running `vi`.
///
/// @return This function returns no value.
static inline void viClearYank(ViState *state) {
  textstoreChainFreeAfter(&state->ex.store, &state->yank, TEXTSTORE_NO_BLOCK);
  state->yankKind = VI_YANK_NONE;
}

/// @fn static inline bool viBeginChange(ViState *state)
///
/// @brief Start a change in the undo history if the command being run hasn't
/// started one yet, so that all a command changes is undone together.
///
/// @param state The ViState of the running `vi`.
///
/// @return Returns true on success, false with state->ex.store.error set on
/// failure.
static inline bool viBeginChange(ViState *state) {
  if (!state->beginChange) {
    return true;
  }
  state->beginChange = false;
  return exBeginChange(&state->ex);
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
  if (!viBeginChange(state)
    || !textstoreAppendText(store, state->text)
    || !textstoreAppendText(store, "\n")
    || ((state->line > store->numLines)
      ? !exInsertLine(&state->ex, store->numLines, offset)
      : !exSetLineOffset(&state->ex, state->line, offset))
  ) {
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
  if (!viBeginChange(state)
    || !textstoreAppendText(store, "\n")
    || !exInsertLine(&state->ex, after, offset)
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

/// @fn static inline void viShiftRows(ViState *state, int row, int count)
///
/// @brief Move the rows from a screen row to the last row above the status
/// line down, inserting blank rows, or up, deleting rows and leaving blank
/// ones above the status line.  Each is a deletion and an insertion, so that
/// the status line stays where it is, written with one printf rather than
/// screenDeleteLines and screenInsertLines, which is smaller.
///
/// @param state The ViState of the running `vi`.
/// @param row The screen row, from 0.
/// @param count The number of rows to insert, or minus the number to delete.
///
/// @return This function returns no value.
static inline void viShiftRows(ViState *state, int row, int count) {
  int textRows = state->rows - 1;
  int rows = (count < 0) ? -count : count;
  if (rows > textRows - row) {
    rows = textRows - row;
  }
  if (rows <= 0) {
    return;
  }
  printf("\033[%d;1H\033[%dM\033[%d;1H\033[%dL",
    ((count > 0) ? textRows - rows : row) + 1, rows,
    ((count > 0) ? row : textRows - rows) + 1, rows);
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
///   uint8_t how)
///
/// @brief Make a line the current line and show it at a screen row if it
/// fits there, or else, unless asked not to, scroll so that it is the top
/// line, if it is above the screen, or the bottom one, and redraw the
/// screen.  The current line's changed text is kept.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line.
/// @param row The screen row to show it at, or -1 for above the screen.
/// @param how VI_SHOW_KEEP to draw nothing if it fits, VI_SHOW_REDRAW to
///   redraw from the line down even if it fits, or VI_SHOW_FIT to draw
///   nothing and not scroll if it doesn't fit.
///
/// @return Returns true if it is shown, false if VI_SHOW_FIT was given and it
/// doesn't fit, or false with state->ex.store.error set on failure.
static inline bool viShowLine(ViState *state, int line, int row, uint8_t how) {
  int textRows = state->rows - 1;
  if (((line != state->line) || !state->changed) && !viLoadLine(state, line)) {
    return false;
  }
  int lineRows = viRowsFor(state, state->width);
  if ((row >= 0) && (row + lineRows <= textRows)) {
    state->lineRow = row;
    if (how == VI_SHOW_REDRAW) {
      viDrawFrom(state, row, line);
    }
    return true;
  } else if (how == VI_SHOW_FIT) {
    return false;
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

/// @fn static inline int viRowOf(ViState *state, int line)
///
/// @brief Find the screen row a line starts on, from the line at the top.
///
/// @param state The ViState of the running `vi`.
/// @param line The number of the line.
///
/// @return Returns the row, -1 if the line is above the screen, or the number
/// of rows if it starts below the last row above the status line.
static inline int viRowOf(ViState *state, int line) {
  if (line < state->top) {
    return -1;
  }
  int row = 0;
  for (int ii = state->top; (ii < line) && (row < state->rows); ii++) {
    row += viRowsFor(state, (ii == state->line)
      ? state->width : viReadLine(state, ii, false));
  }
  return (row < state->rows - 1) ? row : state->rows;
}

/// @fn static inline int viRowsOfLines(ViState *state, int first, int last,
///   int row)
///
/// @brief Count the screen rows a range of lines takes up from a screen row,
/// stopping at the last row above the status line.
///
/// @param state The ViState of the running `vi`.
/// @param first The number of the first line.
/// @param last The number of the last line.
/// @param row The screen row the first line starts on, or -1 if it is above
///   the screen.
///
/// @return Returns the number of rows, 0 if the first line is above the
/// screen.
static inline int viRowsOfLines(ViState *state, int first, int last, int row) {
  int rows = 0;
  for (int line = first;
    (line <= last) && (row >= 0) && (row + rows < state->rows - 1); line++
  ) {
    rows += viRowsFor(state, (line == state->line)
      ? state->width : viReadLine(state, line, false));
  }
  return rows;
}

/// @fn static inline void viRequestDeleted(ViState *state, int first,
///   int row, int oldRows)
///
/// @brief After lines were deleted, make the line that took their place
/// current, or the new last line if they were the last ones, and ask the
/// Show overlay to show it.
///
/// @param state The ViState of the running `vi`.
/// @param first The number of the first line deleted.
/// @param row The screen row it started on.
/// @param oldRows The number of rows the deleted lines took up.
///
/// @return This function returns no value.
static inline void viRequestDeleted(
  ViState *state, int first, int row, int oldRows
) {
  TextStore *store = &state->ex.store;
  int numLines = 0;
  if (first > store->numLines) {
    numLines = 1;
    if (store->numLines > 0) {
      first = store->numLines;
      row = viRowOf(state, first);
      oldRows += viRowsFor(state, viReadLine(state, first, false));
    }
  }
  viLoadLine(state, first);
  state->index = (int16_t) viFirstNonBlank(state);
  viRequestRows(state, first, row, VI_REQUEST_SHOW, oldRows, numLines);
}

/// @fn static inline void viRedrawCurrent(ViState *state, int oldRows)
///
/// @brief Redraw the current line after its text changed.  If it now takes
/// up more rows, the lines below it move down; if fewer, they are redrawn.
///
/// @param state The ViState of the running `vi`.
/// @param oldRows The number of rows the line took up before the change.
///
/// @return This function returns no value.
static inline void viRedrawCurrent(ViState *state, int oldRows) {
  viMeasure(state);
  int lineRows = viRowsFor(state, state->width);
  if (state->lineRow + lineRows > state->rows - 1) {
    viShowLine(state, state->line, state->rows, VI_SHOW_REDRAW);
  } else if (lineRows < oldRows) {
    viDrawFrom(state, state->lineRow, state->line);
  } else {
    viShiftRows(state, state->lineRow + oldRows, lineRows - oldRows);
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
/// VI_KEY_RESIZE, state->rows and state->columns are set; a resize report of
/// the size already in use is VI_KEY_NONE.
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
    int rows = 0;
    int columns = 0;
    key = VI_KEY_NONE;
    if ((final >= 'A') && (final <= 'D')) {
      key = VI_KEY_UP + (final - 'A');
    } else if (screenParseResize(input, &rows, &columns)
      && ((rows != state->rows) || (columns != state->columns))
    ) {
      state->rows = (int16_t) rows;
      state->columns = (int16_t) columns;
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
