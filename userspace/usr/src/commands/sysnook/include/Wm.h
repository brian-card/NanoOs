///////////////////////////////////////////////////////////////////////////////
///
/// @file              Wm.h
///
/// @brief             What the window manager's overlays share:  its state,
///                    its windows, and the layout of the screen.
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

#ifndef WM_H
#define WM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ExecutiveProcesses.h"
#include "../../../src/kernel/Graphics.h"

/// @def WM_MAX_WINDOWS
///
/// @brief The number of windows the window manager draws.
#define WM_MAX_WINDOWS 3

/// @def WM_TITLE_HEIGHT
///
/// @brief The height of a window's title bar, in pixels.
#define WM_TITLE_HEIGHT 12

/// @def WM_GRIP_SIZE
///
/// @brief The width and height of the square at a window's bottom-right
/// corner that resizes it.
#define WM_GRIP_SIZE 8

/// @def WM_MIN_WIDTH
///
/// @brief The narrowest a window can be made.
#define WM_MIN_WIDTH 64

/// @def WM_MIN_HEIGHT
///
/// @brief The shortest a window can be made:  its title bar and 16 pixels of
/// body.
#define WM_MIN_HEIGHT (WM_TITLE_HEIGHT + 16)

/// @def WM_KEEP_TITLE
///
/// @brief How much of a moved window's title bar stays on the screen.
#define WM_KEEP_TITLE 32

/// @def WM_CELL_SIZE
///
/// @brief The width and height of a cell of the full window.
#define WM_CELL_SIZE 20

/// @def WM_CELL_COLUMNS
///
/// @brief The number of cells in each row of the full window.
#define WM_CELL_COLUMNS 16

/// @def WM_CELL_ROWS
///
/// @brief The most rows of cells the full window holds.
#define WM_CELL_ROWS 12

/// @def WM_MAX_CELLS
///
/// @brief The most cells the full window holds, which fill its body.
#define WM_MAX_CELLS (WM_CELL_COLUMNS * WM_CELL_ROWS)

/// @def WM_MAX_DAMAGE
///
/// @brief The most rectangles a frame repaints.  A resize, the worst case,
/// needs 11.
#define WM_MAX_DAMAGE 12

/// @def WM_TIMED_FRAMES
///
/// @brief The number of frames of each kind whose time --timing keeps.
#define WM_TIMED_FRAMES 64

/// @def WM_CTRL_Q
///
/// @brief The character Ctrl+Q types, which quits.
#define WM_CTRL_Q 17

/// @enum WmGrab
///
/// @brief What a press on a window started.
typedef enum WmGrab {
  WM_GRAB_NONE = 0,
  WM_GRAB_MOVE,
  WM_GRAB_RESIZE,
} WmGrab;

/// @enum WmPaintOperation
///
/// @brief What a call to the Paint overlay does.
typedef enum WmPaintOperation {
  WM_PAINT_START = 0,
  WM_PAINT_FRAME,
  WM_PAINT_END,
} WmPaintOperation;

/// @struct WmWindow
///
/// @brief One window.
///
/// @param frame Where the window is, in screen coordinates, including the
///   title bar.
/// @param shown Where the screen shows the window, which a frame changes to
///   frame.
/// @param body The colour of the body.
/// @param number The window's number, which its title shows.
/// @param cells The number of cells drawn in the body; 0 for a plain window.
typedef struct WmWindow {
  GfxRect  frame;
  GfxRect  shown;
  GfxColor body;
  uint8_t  number;
  uint8_t  cells;
} WmWindow;

/// @struct WmTimes
///
/// @brief The times --timing keeps for one kind of frame.
///
/// @param count The number of frames of the kind.
/// @param ms The milliseconds each of the first WM_TIMED_FRAMES took.
typedef struct WmTimes {
  uint16_t count;
  uint16_t ms[WM_TIMED_FRAMES];
} WmTimes;

/// @struct WmState
///
/// @brief Everything the window manager knows.  It's on the heap, because a
/// process has only 1 KB of stack on the Agon.  The bytes come first and the
/// arrays last, because ez80-clang can't address a multi-byte field at an
/// offset of 126 or 127.
///
/// @param numWindows The number of windows.
/// @param grab What the press that is held down started.
/// @param numDamage The number of rectangles in damage.
/// @param buttons The pointer's buttons after the last event.
/// @param operation The WmPaintOperation of the next Paint call.
/// @param status The GfxError of the last Paint call.
/// @param paintAll Whether the next frame repaints the whole screen.
/// @param paintTop Whether the next frame repaints the top window, which has
///   just been raised.
/// @param liveMove Whether a moved window follows the pointer, rather than
///   an outline.
/// @param liveResize Whether a resized window follows the pointer, rather
///   than an outline.
/// @param contentByCall Whether the full window's cells are painted by
///   calling preloads rather than by sending their records.
/// @param quit Whether Ctrl+Q has been pressed.
/// @param cmd The command buffer, 2 x 512 B.
/// @param times The frame times of moves and of resizes, or NULL without
///   --timing.
/// @param offset The pointer's offset from the grabbed frame's top-left
///   corner for a move, or from its bottom-right corner for a resize.
/// @param outline The outline the pointer has dragged, or an empty rectangle.
/// @param outlineShown The outline on the screen, or an empty rectangle.
/// @param display The display.
/// @param windows The windows, back to front.
/// @param damage The rectangles a frame repaints.
/// @param rows The preloaded rows of cells, or GFX_INVALID_HANDLE.
typedef struct WmState {
  uint8_t     numWindows;
  uint8_t     grab;
  uint8_t     numDamage;
  uint8_t     buttons;
  uint8_t     operation;
  uint8_t     status;
  bool        paintAll;
  bool        paintTop;
  bool        liveMove;
  bool        liveResize;
  bool        contentByCall;
  bool        quit;
  GfxCmdBuf  *cmd;
  WmTimes    *times;
  GfxPoint    offset;
  GfxRect     outline;
  GfxRect     outlineShown;
  GfxDisplay  display;
  WmWindow    windows[WM_MAX_WINDOWS];
  GfxRect     damage[WM_MAX_DAMAGE];
  GfxHandle   rows[WM_CELL_ROWS];
} WmState;

/// @fn static inline WmWindow* wmTop(WmState *state)
///
/// @brief Get the window in front of the others.
///
/// @param state The window manager's state.
///
/// @return Returns the top window.
static inline WmWindow* wmTop(WmState *state) {
  return &state->windows[state->numWindows - 1];
}

#endif // WM_H
