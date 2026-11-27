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



/// @file Grid.c
///
/// @brief wintest's text grid commands:  making a window whose body is a
/// text grid here, and handing writes to the Write overlay and tries to send
/// a session input to the Inject overlay.

#include "Wintest.h"

/// @def WINTEST_GRID_RECORDS
///
/// @brief The number of grid records in the template of a window made by
/// grid ... twice.
#define WINTEST_GRID_RECORDS 2

/// @fn static int createGrid(Wintest *wintest, const char *at,
///   const char *size, const char *kind)
///
/// @brief Make a window whose body is one text grid of a number of columns
/// and rows, or whose template is two such grids bound to sessions.
///
/// @param wintest The context.
/// @param at X,Y:  where the window goes.
/// @param size COLUMNSxROWS.
/// @param kind "session" for a grid bound to a session, "twice" for two of
///   them, or anything else for a grid bound to none.
///
/// @return Returns the window, or a negative errno value.
static int createGrid(Wintest *wintest, const char *at, const char *size,
  const char *kind
) {
  long place[2];
  char *end = NULL;
  long columns = strtol(size, &end, 10);
  long rows = (*end == 'x') ? strtol(end + 1, NULL, 10) : 0;

  if ((!parseNumbers(at, place, (int) WINTEST_COUNT(place))) || (columns <= 0)
    || (rows <= 0)
  ) {
    return -EINVAL;
  }

  const WinHelloArgs *hello = &wintest->session.hello;
  GfxRect grid = { 0, 0, (int16_t) (columns * hello->cellWidth),
    (int16_t) (rows * hello->cellHeight) };
  GfxRect frame = { (int16_t) place[0], (int16_t) place[1],
    (int16_t) (grid.w + (2 * hello->borderWidth)),
    (int16_t) (grid.h + hello->titleHeight + (2 * hello->borderWidth)) };
  uint8_t records[WINTEST_GRID_RECORDS * WIN_RECORD_HEADER_SIZE];
  bool twice = (strcmp(kind, "twice") == 0);
  uint8_t flags = (twice || (strcmp(kind, "session") == 0))
    ? WIN_WIDGET_SESSION : 0;

  winGridRecord(records, GRID_WIDGET, flags, grid);
  winGridRecord(&records[WIN_RECORD_HEADER_SIZE], GRID_WIDGET + 1, flags,
    grid);
  // The window server may not be shared a pointer into an overlay.
  char title[] = "Grid";

  return winCreateWindowWith(&wintest->session, title, frame,
    WIN_LAYER_NORMAL, WIN_FLAG_DECORATED | WIN_FLAG_RESIZABLE
      | WIN_FLAG_IN_TASKBAR, records,
    (uint16_t) (twice ? sizeof(records) : WIN_RECORD_HEADER_SIZE));
}

/// @fn void* wintestGrid(void *args)
///
/// @brief Carry out the text grid command at the context's index, and move
/// the index past its arguments:  grid X,Y COLSxROWS [session|twice] here,
/// and inject in the Inject overlay.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer, with handled cleared if the
/// command isn't one of wintest's text grid commands.
void* wintestGrid(void *args) {
  Wintest *wintest = (Wintest*) args;
  int ii = wintest->index;
  const char *command = wintest->argv[ii];

  wintest->handled = true;

  if ((strcmp(command, "grid") == 0) && ((ii + 2) < wintest->argc)) {
    const char *kind = ((ii + 3) < wintest->argc) ? wintest->argv[ii + 3] : "";
    bool session = (strcmp(kind, "session") == 0)
      || (strcmp(kind, "twice") == 0);
    int rc = createGrid(wintest, wintest->argv[ii + 1],
      wintest->argv[ii + 2], kind);

    if (rc >= 0) {
      printf("wintest: window %d\n", rc);
      wintest->last = (uint8_t) rc;
    } else {
      printf("wintest: grid: %s\n", strerror(-rc));
    }

    wintest->index = ii + (session ? 4 : 3);
  } else if (strcmp(command, "inject") == 0) {
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Inject", "wintestInject",
      wintest);
    wintest->index = ii + 1;
  } else {
    wintest->handled = false;
  }

  return args;
}
