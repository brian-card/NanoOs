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




/// @file Draw.c
///
/// @brief wintest's commands for canvases:  canvas records, cells, which
/// draws in a canvas, rawcanvas, which sends records as they are, and
/// face.

#include "Wintest.h"

/// @fn static void report(const char *command, const char *window, int rc)
///
/// @brief Print the result of a widget command:  "ok", or its error.
///
/// @param command The command.
/// @param window The window it named.
/// @param rc 0, or a negative errno value.
///
/// @return This function returns no value.
static void report(const char *command, const char *window, int rc) {
  printf("wintest: %s %s: %s\n", command, window,
    (rc < 0) ? strerror(-rc) : "ok");
}

/// @fn static int canvas(Wintest *wintest, int ii)
///
/// @brief Add a canvas record to the template:  canvas ID X,Y,W,H, then
/// options.
///
/// @param wintest The context.
/// @param ii The index of the command's word.
///
/// @return Returns the index of the word after the command, or -1 if it is
/// malformed.
static int canvas(Wintest *wintest, int ii) {
  char **argv = wintest->argv;
  GfxRect rect;
  if ((ii + 2 >= wintest->argc) || !parseRect(argv[ii + 2], &rect)) {
    return -1;
  }
  uint8_t flags = 0;
  uint8_t anchor = 0;
  int next = options(wintest, ii + 3, &flags, &anchor);
  WinTemplate *widgets = pending(wintest);
  if ((widgets == NULL) || !winAddCanvas(widgets,
    (uint8_t) strtol(argv[ii + 1], NULL, 0), flags, anchor, rect)
  ) {
    printf("wintest: canvas %s: template full\n", argv[ii + 1]);
  }
  return next;
}

/// @def WINTEST_CELL_SIZE
///
/// @brief The width and height of a cell that cells draws.
#define WINTEST_CELL_SIZE 20

/// @def WINTEST_CELL_COLUMNS
///
/// @brief The number of cells in each row that cells draws.
#define WINTEST_CELL_COLUMNS 16

/// @def WINTEST_TEXT_ROW_EVERY
///
/// @brief How many rows of cells there are for each one with text across it.
#define WINTEST_TEXT_ROW_EVERY 3

/// @def WINTEST_ROW_TEXT_TOP
///
/// @brief How far below the top of its row a row's text is.
#define WINTEST_ROW_TEXT_TOP 6

/// @var _cellFills
///
/// @brief The colors cells fills its cells with, in turn along each row and
/// down each column.
static const GfxColor _cellFills[] = {
  CGA_GREEN, CGA_RED, CGA_MAGENTA, CGA_BROWN,
};

/// @var _rowText
///
/// @brief The text across every third row of cells.
static const char _rowText[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcd";

/// @fn static int cells(Wintest *wintest, uint8_t window, uint8_t widget,
///   int count)
///
/// @brief Draw rows of colored, outlined cells in a canvas, with text
/// across every third row, as graphics 6's demonstration window did:  fills
/// of one color together, then the outlines of whole rows as lines, so
/// that 192 cells fit a canvas.
///
/// @param wintest The context.
/// @param window The window.
/// @param widget The canvas.
/// @param count The number of cells.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static int cells(Wintest *wintest, uint8_t window, uint8_t widget,
  int count
) {
  uint8_t *buffer = (uint8_t*) malloc(WIN_CANVAS_MAX);
  if (buffer == NULL) {
    return -ENOMEM;
  }
  WinCanvas canvas;
  winCanvasBegin(&canvas, buffer, WIN_CANVAS_MAX);
  int numFills = (int) WINTEST_COUNT(_cellFills);
  for (int fill = 0; fill < numFills; fill++) {
    winCanvasColor(&canvas, _cellFills[fill]);
    for (int cell = 0; cell < count; cell++) {
      int row = cell / WINTEST_CELL_COLUMNS;
      int column = cell % WINTEST_CELL_COLUMNS;
      if ((row + column) % numFills == fill) {
        GfxRect rect = { (int16_t) (column * WINTEST_CELL_SIZE),
          (int16_t) (row * WINTEST_CELL_SIZE), WINTEST_CELL_SIZE,
          WINTEST_CELL_SIZE };
        winCanvasRect(&canvas, rect, true);
      }
    }
  }
  winCanvasColor(&canvas, CGA_BLACK);
  int fullRows = count / WINTEST_CELL_COLUMNS;
  int16_t right = WINTEST_CELL_COLUMNS * WINTEST_CELL_SIZE - 1;
  int16_t bottom = (int16_t) (fullRows * WINTEST_CELL_SIZE - 1);
  for (int edge = 0; edge < 2 * fullRows; edge++) {
    int16_t at = (int16_t) ((edge / 2) * WINTEST_CELL_SIZE
      + (edge % 2) * (WINTEST_CELL_SIZE - 1));
    GfxPoint from = { 0, at };
    GfxPoint to = { right, at };
    winCanvasLine(&canvas, from, to);
  }
  for (int edge = 0; (fullRows > 0) && (edge < 2 * WINTEST_CELL_COLUMNS);
    edge++
  ) {
    int16_t at = (int16_t) ((edge / 2) * WINTEST_CELL_SIZE
      + (edge % 2) * (WINTEST_CELL_SIZE - 1));
    GfxPoint from = { at, 0 };
    GfxPoint to = { at, bottom };
    winCanvasLine(&canvas, from, to);
  }
  for (int column = 0; column < count % WINTEST_CELL_COLUMNS; column++) {
    GfxRect rect = { (int16_t) (column * WINTEST_CELL_SIZE),
      (int16_t) (fullRows * WINTEST_CELL_SIZE), WINTEST_CELL_SIZE,
      WINTEST_CELL_SIZE };
    winCanvasRect(&canvas, rect, false);
  }
  winCanvasColor(&canvas, CGA_WHITE);
  int rows = (count + WINTEST_CELL_COLUMNS - 1) / WINTEST_CELL_COLUMNS;
  for (int row = 0; row < rows; row += WINTEST_TEXT_ROW_EVERY) {
    GfxPoint at = { 0,
      (int16_t) (row * WINTEST_CELL_SIZE + WINTEST_ROW_TEXT_TOP) };
    winCanvasText(&canvas, at, _rowText);
  }
  int rc = winSetCanvas(&wintest->session, window, widget, &canvas);
  free(buffer);
  return rc;
}

/// @fn static int rawCanvas(Wintest *wintest, uint8_t window,
///   uint8_t widget, const char *hex)
///
/// @brief Send a canvas the records written as hexadecimal bytes, as they
/// are.
///
/// @param wintest The context.
/// @param window The window.
/// @param widget The canvas.
/// @param hex The records' bytes, two hexadecimal digits each.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static int rawCanvas(Wintest *wintest, uint8_t window, uint8_t widget,
  const char *hex
) {
  size_t size = strlen(hex) / 2;
  uint8_t *buffer = (uint8_t*) malloc(size + 1);
  if (buffer == NULL) {
    return -ENOMEM;
  }
  for (size_t ii = 0; ii < size; ii++) {
    char pair[3] = { hex[2 * ii], hex[2 * ii + 1], '\0' };
    buffer[ii] = (uint8_t) strtol(pair, NULL, 16);
  }
  WinCanvas canvas;
  winCanvasBegin(&canvas, buffer, (uint16_t) size);
  canvas.size = (uint16_t) size;
  int rc = winSetCanvas(&wintest->session, window, widget, &canvas);
  free(buffer);
  return rc;
}

/// @fn void* wintestDraw(void *args)
///
/// @brief Carry out canvas ID X,Y,W,H, cells N ID COUNT, rawcanvas N ID HEX
/// or face N COLOR at the context's index, and move the index past it.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer, with handled cleared if the
/// command is malformed.
void* wintestDraw(void *args) {
  Wintest *wintest = (Wintest*) args;
  char **argv = wintest->argv;
  int ii = wintest->index;
  const char *command = argv[ii];
  int next = -1;
  if (strcmp(command, "canvas") == 0) {
    next = canvas(wintest, ii);
  } else if ((strcmp(command, "face") == 0) && (ii + 2 < wintest->argc)) {
    report(command, argv[ii + 1], winSetFace(&wintest->session,
      windowWord(wintest, argv[ii + 1]),
      (GfxColor) strtoll(argv[ii + 2], NULL, 0)));
    next = ii + 3;
  } else if ((strcmp(command, "cells") == 0) && (ii + 3 < wintest->argc)) {
    report(command, argv[ii + 1], cells(wintest,
      windowWord(wintest, argv[ii + 1]),
      (uint8_t) strtol(argv[ii + 2], NULL, 0),
      (int) strtol(argv[ii + 3], NULL, 0)));
    next = ii + 4;
  } else if ((strcmp(command, "rawcanvas") == 0)
    && (ii + 3 < wintest->argc)
  ) {
    report(command, argv[ii + 1], rawCanvas(wintest,
      windowWord(wintest, argv[ii + 1]),
      (uint8_t) strtol(argv[ii + 2], NULL, 0), argv[ii + 3]));
    next = ii + 4;
  }
  wintest->handled = (next >= 0);
  if (wintest->handled) {
    wintest->index = next;
  }
  return args;
}
