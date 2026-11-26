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
#include "../../../src/kernel/Console.h"
#include "../../../src/kernel/Graphics.h"
#include "../../../src/kernel/WindowProtocol.h"
#include "WmTheme.h"

/// @def WM_MAX_WINDOWS
///
/// @brief The most windows the window server keeps at once.
#define WM_MAX_WINDOWS 16

/// @def WM_DEMO_WINDOWS
///
/// @brief The number of demonstration windows --demo draws.
#define WM_DEMO_WINDOWS 3

/// @def WM_MAX_CLIENTS
///
/// @brief The most processes connected to the window server at once.
#define WM_MAX_CLIENTS 16

/// @def WM_MAX_PENDING
///
/// @brief The most events a client's queue holds while its last events
/// message is still unread.
#define WM_MAX_PENDING 16

/// @def WM_MAX_POSTS
///
/// @brief The most events messages the window server has waiting on clients
/// at once, so that clients that never read their queues can't take every
/// message the kernel has.
#define WM_MAX_POSTS 3

/// @def WM_OUTBOX
///
/// @brief The most events the overlays hand the Client overlay between two
/// of its calls.
#define WM_OUTBOX 8

/// @def WM_POLL_MS
///
/// @brief How long the window server waits for a message while a client has
/// events it hasn't been able to send.
#define WM_POLL_MS 50

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
/// @brief The most rectangles a frame repaints before it repaints the whole
/// screen instead.  A resize, the worst case for one window, needs 11.
#define WM_MAX_DAMAGE 16

/// @def WM_TIMED_FRAMES
///
/// @brief The number of frames of each kind whose time --timing keeps.
#define WM_TIMED_FRAMES 64

/// @def WM_GRID_HOLD
///
/// @brief The most bytes of keys a session's text grid holds while the
/// console is still reading the last ones it was sent.
#define WM_GRID_HOLD 32

/// @def WM_GRID_MARGINS
///
/// @brief The most parts of a window's body its text grid's cells leave
/// uncovered:  above, below, left of and right of them.
#define WM_GRID_MARGINS 4

/// @def WM_GRID_PLANES
///
/// @brief The number of planes of a text grid's cells:  its characters, then
/// their attributes.
#define WM_GRID_PLANES 2

/// @def WM_BLANK_ATTRIBUTE
///
/// @brief The attribute of the cells a new or resized grid adds:  bright
/// white on black, as the VDP console's text starts.
#define WM_BLANK_ATTRIBUTE WIN_ATTRIBUTE(WIN_ANSI_BRIGHT_WHITE, WIN_ANSI_BLACK)

/// @def WM_GRID_MAX_ROWS
///
/// @brief The most rows a text grid has, which its dirty bits cover.
#define WM_GRID_MAX_ROWS WIN_GRID_MAX_ROWS

/// @def WM_GRID_DIRTY_SHIFT
///
/// @brief How far a row's number shifts right to give the byte of a text
/// grid's dirty bits that holds the row's bit.
#define WM_GRID_DIRTY_SHIFT 3

/// @def WM_GRID_ROWS_PER_BYTE
///
/// @brief The number of rows whose dirty bits one byte holds.
#define WM_GRID_ROWS_PER_BYTE (1 << WM_GRID_DIRTY_SHIFT)

/// @def WM_GRID_DIRTY_BYTES
///
/// @brief The size of a text grid's dirty bits.
#define WM_GRID_DIRTY_BYTES (WM_GRID_MAX_ROWS / WM_GRID_ROWS_PER_BYTE)

/// @def WM_GRID_DIRTY_BYTE(row)
///
/// @brief The index of the byte of a text grid's dirty bits that holds a
/// row's bit.
#define WM_GRID_DIRTY_BYTE(row) ((row) >> WM_GRID_DIRTY_SHIFT)

/// @def WM_GRID_DIRTY_BIT(row)
///
/// @brief A row's bit within its byte of a text grid's dirty bits.
#define WM_GRID_DIRTY_BIT(row) \
  ((uint8_t) (1 << ((row) & (WM_GRID_ROWS_PER_BYTE - 1))))

/// @def WM_MAX_DEFERRED
///
/// @brief The most frames in a row the window server puts off while clients'
/// grid writes keep arriving.
#define WM_MAX_DEFERRED 8

/// @def WM_WRITE_WAIT_NS
///
/// @brief How long, in nanoseconds, the window server waits after a grid
/// write for the writer's next one before it paints.
#define WM_WRITE_WAIT_NS 2000000L

/// @def WM_NO_PORT
///
/// @brief The port of a text grid bound to no session.
#define WM_NO_PORT 0xFF

/// @def WM_SHARED
///
/// @brief Declare a helper more than one overlay calls, kept out of line so
/// that each overlay carries one copy rather than one per call.  `unused`
/// keeps an overlay that calls none of them warning-free.
#define WM_SHARED static __attribute__((noinline, unused))

/// @enum WmGrab
///
/// @brief What the press that is held down started.
///
/// @param WM_GRAB_NONE Nothing.
/// @param WM_GRAB_MOVE Moving a window by its title bar.
/// @param WM_GRAB_RESIZE Resizing a window by its edges.
/// @param WM_GRAB_BUTTON Pressing a caption button, which acts on release.
typedef enum WmGrab {
  WM_GRAB_NONE = 0,
  WM_GRAB_MOVE,
  WM_GRAB_RESIZE,
  WM_GRAB_BUTTON,
} WmGrab;

/// @enum WmButton
///
/// @brief A window's caption buttons, left to right.
///
/// @param WM_BUTTON_NONE No button.
/// @param WM_BUTTON_MINIMIZE Minimize.
/// @param WM_BUTTON_MAXIMIZE Maximize, or restore if maximized.
/// @param WM_BUTTON_CLOSE Ask the owner to close the window.
typedef enum WmButton {
  WM_BUTTON_NONE = 0,
  WM_BUTTON_MINIMIZE,
  WM_BUTTON_MAXIMIZE,
  WM_BUTTON_CLOSE,
} WmButton;

/// @def WM_EDGE_LEFT
///
/// @brief A resize moves the left edge.
#define WM_EDGE_LEFT 0x01

/// @def WM_EDGE_TOP
///
/// @brief A resize moves the top edge.
#define WM_EDGE_TOP 0x02

/// @def WM_EDGE_RIGHT
///
/// @brief A resize moves the right edge.
#define WM_EDGE_RIGHT 0x04

/// @def WM_EDGE_BOTTOM
///
/// @brief A resize moves the bottom edge.
#define WM_EDGE_BOTTOM 0x08

/// @enum WmStartOperation
///
/// @brief What a call to the Start overlay does.
typedef enum WmStartOperation {
  WM_START_BEGIN = 0,
  WM_START_END,
} WmStartOperation;

/// @def WM_EXIT_HELP
///
/// @brief The exit status the Start overlay sets after printing --help's
/// usage, which main turns into 0 without running the window server.
#define WM_EXIT_HELP 2

/// @def WM_EXIT_PAINT
///
/// @brief The exit status main sets when the first frame fails, whose
/// GfxError is the state's status, which the Start overlay prints and turns
/// into 1, so that main carries no error strings.
#define WM_EXIT_PAINT 3

/// @struct WmWindow
///
/// @brief One window.  The bytes come first, because ez80-clang can't
/// address a multi-byte field at an offset of 126 or 127.
///
/// @param id The window's identifier, unique across the window server; 0 is
///   none.
/// @param layer The WinLayer.
/// @param flags The WIN_FLAG_* bits.
/// @param state The WinState.
/// @param restoreState The WinState a minimized window goes back to.
/// @param cells The number of cells of a demonstration window's body.
/// @param owner The ProcessId of the client that owns it, or 0 for the
///   window server's own.
/// @param face The colour of the body.
/// @param frame Where the window is, in screen coordinates, including its
///   decorations.
/// @param restored The frame a maximized window goes back to.
/// @param title The title, on the heap, or NULL.
/// @param content The window's WmGrid, or NULL.
typedef struct WmWindow {
  uint8_t   id;
  uint8_t   layer;
  uint8_t   flags;
  uint8_t   state;
  uint8_t   restoreState;
  uint8_t   cells;
  ProcessId owner;
  GfxColor  face;
  GfxRect   frame;
  GfxRect   restored;
  char     *title;
  void     *content;
} WmWindow;

/// @struct WmGrid
///
/// @brief A text grid:  rows and columns of cells, each a character byte and
/// an attribute byte, the foreground ANSI colour in its low 4 bits and the
/// background in its high 4.
///
/// @param widget The widget's identifier.
/// @param flags The WIN_WIDGET_* bits of its record.
/// @param anchor The WIN_ANCHOR_* bits of its record.
/// @param columns The number of columns.
/// @param rows The number of rows.
/// @param cursorRow The cursor's row.
/// @param cursorColumn The cursor's column.
/// @param cursorShown Whether the cursor is shown.
/// @param reports Whether the session's program asked for resize reports.
/// @param reportWanted Whether a resize report is to be sent the session.
/// @param winsizeWanted Whether the session's size is to be sent the
///   console.
/// @param resized Whether the body changed size since the grid followed it.
/// @param numMargins The number of margins.
/// @param noBlit Whether the scrolls since the last frame can't be painted
///   by moving pixels.
/// @param scroll The rows the pixels of scrollTop to scrollBottom move up
///   in the next frame, or down if negative.
/// @param scrollTop The first row of the scroll.
/// @param scrollBottom The last row of the scroll.
/// @param port The console port of the grid's session, or WM_NO_PORT.
/// @param numHeld The number of bytes in held.
/// @param shell The ProcessId of the session's shell.
/// @param left How far right of the frame's left edge the grid starts.
/// @param top How far below the frame's top edge the grid starts.
/// @param rect The grid's place in the body when the window was made.
/// @param marginRight How far the grid's right edge is from the body's.
/// @param marginBottom How far the grid's bottom edge is from the body's.
/// @param margins The parts of the body the cells leave uncovered, relative
///   to the window's frame, which are painted the body's colour instead of
///   the whole body, so that no cell shows it between its old colours and
///   its new.
/// @param dirty A bit per row to repaint.
/// @param held Keys typed that the session hasn't been sent.
/// @param input The CONSOLE_INPUT_READY the console hasn't finished, or
///   NULL.
/// @param inputArgs The arguments of the input sent the session.
/// @param winsize The CONSOLE_SET_WINSIZE the console hasn't finished, or
///   NULL.
/// @param winsizeArgs The arguments of the size sent the console.
/// @param cells The characters, row by row, then the attributes.
typedef struct WmGrid {
  uint8_t                widget;
  uint8_t                flags;
  uint8_t                anchor;
  uint8_t                columns;
  uint8_t                rows;
  uint8_t                cursorRow;
  uint8_t                cursorColumn;
  bool                   cursorShown;
  bool                   reports;
  bool                   reportWanted;
  bool                   winsizeWanted;
  bool                   resized;
  uint8_t                numMargins;
  bool                   noBlit;
  int8_t                 scroll;
  uint8_t                scrollTop;
  uint8_t                scrollBottom;
  uint8_t                port;
  uint8_t                numHeld;
  ProcessId              shell;
  int16_t                left;
  int16_t                top;
  GfxRect                rect;
  int16_t                marginRight;
  int16_t                marginBottom;
  GfxRect                margins[WM_GRID_MARGINS];
  uint8_t                dirty[WM_GRID_DIRTY_BYTES];
  uint8_t                held[WM_GRID_HOLD];
  ProcessMessage        *input;
  ConsoleInputReadyArgs *inputArgs;
  ProcessMessage        *winsize;
  ConsoleWinsizeArgs    *winsizeArgs;
  uint8_t               *cells;
} WmGrid;

/// @struct WmClient
///
/// @brief A connected process.
///
/// @param pid The process's ProcessId.
/// @param numPending The number of entries in pending.
/// @param leaving Whether the process said goodbye, so that it gets no more
///   events.
/// @param message The WIN_MSG_EVENTS the process hasn't marked done, or NULL.
/// @param args The events message's arguments, a persistent round trip that
///   lives as long as the client.
/// @param pending The events waiting for the process to finish with the last
///   message.
typedef struct WmClient {
  ProcessId       pid;
  uint8_t         numPending;
  bool            leaving;
  ProcessMessage *message;
  WinEventsArgs  *args;
  WinEvent        pending[WM_MAX_PENDING];
} WmClient;

/// @struct WmOutgoing
///
/// @brief An event an overlay hands the Client overlay for a client.
///
/// @param pid The client's ProcessId.
/// @param event The event.
typedef struct WmOutgoing {
  ProcessId pid;
  WinEvent  event;
} WmOutgoing;

/// @enum WmTimed
///
/// @brief The kinds of frame --timing keeps the times of.
///
/// @param WM_TIMED_MOVE A frame of a move.
/// @param WM_TIMED_RESIZE A frame of a resize.
/// @param WM_TIMED_ECHO The first frame to repaint a text grid after a key
///   was sent a session.
/// @param WM_TIMED_KINDS The number of kinds.
typedef enum WmTimed {
  WM_TIMED_MOVE = 0,
  WM_TIMED_RESIZE,
  WM_TIMED_ECHO,
  WM_TIMED_KINDS,
} WmTimed;

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
/// @param grab The WmGrab of the press that is held down.
/// @param edges The WM_EDGE_* bits a resize moves.
/// @param numDamage The number of rectangles in damage.
/// @param buttons The pointer's buttons after the last event.
/// @param operation The operation of the next overlay call.
/// @param status The GfxError of the last Paint call.
/// @param paintAll Whether the next frame repaints the whole screen.
/// @param liveMove Whether a moved window follows the pointer, rather than
///   an outline.
/// @param liveResize Whether a resized window follows the pointer, rather
///   than an outline.
/// @param contentByCall Whether the full window's cells are painted by
///   calling preloads rather than by sending their records.
/// @param demo Whether --demo asked for the demonstration windows.
/// @param finishFrames Whether --finish asked for each frame to be drawn
///   before the next message is handled.
/// @param displayOpen Whether the display was opened.
/// @param revoked Whether the display was taken back.
/// @param copy Whether the next frame starts by copying copySource to
///   copyTarget:  the pixels of a moved window, or of a scrolled text grid.
/// @param restoreOnDrag Whether the window being moved is maximized, and is
///   restored under the pointer once the pointer moves.
/// @param numClients The number of entries in clients.
/// @param numOutbox The number of entries in outbox.
/// @param numPosts The number of events messages waiting on clients.
/// @param active The window drawn as active, or 0.
/// @param focus The window that gets the keys, or 0.
/// @param grabbed The window being moved or resized, or whose caption
///   button is held down, or 0.
/// @param hoverWindow The window whose caption button is under the pointer,
///   or 0.
/// @param hoverButton The WmButton under the pointer.
/// @param pressButton The WmButton held down.
/// @param pressInside Whether the pointer is on the caption button held
///   down.
/// @param lastPressWindow The window of the last press on a title bar, for
///   double-clicks.
/// @param nextId The identifier the next window is given, if it's free.
/// @param opWindow The window the Window overlay's wmWindowSetState
///   changes.
/// @param opState The WinState wmWindowSetState gives it.
/// @param opPid The client whose windows wmWindowRemoveOwned takes away.
/// @param eventsWaiting Whether a client has events it couldn't be sent yet.
/// @param exitStatus The exit status the Start overlay sets.
/// @param argc The number of command-line arguments.
/// @param argv The command-line arguments.
/// @param event The input event the Input overlay handles.
/// @param cmd The command buffer, 2 x 512 B.
/// @param times The frame times of each WmTimed, or NULL without --timing.
/// @param windows The windows, by layer and back to front within each.
/// @param lastPressTime When the last press on a title bar was.
/// @param lastPressAt Where the last press on a title bar was.
/// @param offset The pointer's offset from the grabbed frame's top-left
///   corner, or from the corner a resize moves.
/// @param copySource The pixels main asks Paint to copy.
/// @param copyTarget Where Paint copies copySource to.
/// @param outline The outline the pointer has dragged, or an empty rectangle.
/// @param outlineShown The outline on the screen, or an empty rectangle.
/// @param shown Where the screen shows the grabbed window.
/// @param grabStart The grabbed window's frame when the press grabbed it.
/// @param workArea The screen less the strips reserving windows take.
/// @param pad Keeps display's multi-byte fields off offsets 126 and 127.
/// @param display The display.
/// @param started When the window server started.
/// @param damage The rectangles the next frame repaints.
/// @param rows The preloaded rows of cells, or GFX_INVALID_HANDLE.
/// @param message The window protocol message the Client overlay handles.
/// @param clients The connected processes.
/// @param outbox The events the Client overlay sends next.
/// @param gridsResized Whether a text grid's body changed size.
/// @param inputWaiting Whether a session has keys, a size or a report to be
///   sent, or input the console hasn't finished.
/// @param gridsDirty Whether a text grid has rows to repaint.
/// @param deferred The number of frames put off in a row because messages
///   were waiting.
/// @param consolePid The console's ProcessId, once a session needed it.
/// @param deferring Whether the Grid overlay put off the frame because
///   another message came, which it does only while a text grid has rows to
///   repaint, so that it is called again and clears this.
/// @param next The message the Grid overlay took from the queue while it
///   waited, which main handles next, or NULL.
/// @param echoKey Whether --timing waits for the frame that echoes the key
///   sent at keySent.
/// @param gridPainted Whether the next frame repaints text grid rows.
/// @param keySent When the key echoKey waits on was sent.
typedef struct WmState {
  uint8_t         numWindows;
  uint8_t         grab;
  uint8_t         edges;
  uint8_t         numDamage;
  uint8_t         buttons;
  uint8_t         operation;
  uint8_t         status;
  bool            paintAll;
  bool            liveMove;
  bool            liveResize;
  bool            contentByCall;
  bool            demo;
  bool            finishFrames;
  bool            displayOpen;
  bool            revoked;
  bool            copy;
  bool            restoreOnDrag;
  uint8_t         numClients;
  uint8_t         numOutbox;
  uint8_t         numPosts;
  uint8_t         active;
  uint8_t         focus;
  uint8_t         grabbed;
  uint8_t         hoverWindow;
  uint8_t         hoverButton;
  uint8_t         pressButton;
  bool            pressInside;
  uint8_t         lastPressWindow;
  uint8_t         nextId;
  uint8_t         opWindow;
  uint8_t         opState;
  ProcessId       opPid;
  bool            eventsWaiting;
  int             exitStatus;
  int             argc;
  char          **argv;
  const GfxEvent *event;
  GfxCmdBuf      *cmd;
  WmTimes        *times;
  WmWindow       *windows;
  uint32_t        lastPressTime;
  GfxPoint        lastPressAt;
  GfxPoint        offset;
  GfxRect         copySource;
  GfxPoint        copyTarget;
  GfxRect         outline;
  GfxRect         outlineShown;
  GfxRect         shown;
  GfxRect         grabStart;
  GfxRect         workArea;
  uint8_t         pad;
  GfxDisplay      display;
  struct timespec started;
  GfxRect         damage[WM_MAX_DAMAGE];
  GfxHandle       rows[WM_CELL_ROWS];
  ProcessMessage *message;
  WmClient       *clients[WM_MAX_CLIENTS];
  WmOutgoing      outbox[WM_OUTBOX];
  bool            gridsResized;
  bool            inputWaiting;
  bool            gridsDirty;
  uint8_t         deferred;
  ProcessId       consolePid;
  bool            deferring;
  ProcessMessage *next;
  bool            echoKey;
  bool            gridPainted;
  struct timespec keySent;
} WmState;

/// @fn static WmWindow* wmFind(WmState *state, uint8_t id)
///
/// @brief Find a window by its identifier.
///
/// @param state The window manager's state.
/// @param id The window's identifier.
///
/// @return Returns the window, or NULL if there is none with that
/// identifier.
WM_SHARED WmWindow* wmFind(WmState *state, uint8_t id) {
  for (uint8_t ii = 0; (id != 0) && (ii < state->numWindows); ii++) {
    if (state->windows[ii].id == id) {
      return &state->windows[ii];
    }
  }
  return NULL;
}

/// @fn static bool wmContains(const GfxRect *rect, int16_t pointX,
///   int16_t pointY)
///
/// @brief Determine whether a point is inside a rectangle.
///
/// @param rect The rectangle.
/// @param pointX The point's column.
/// @param pointY The point's row.
///
/// @return Returns true if it is, false otherwise.
WM_SHARED bool wmContains(const GfxRect *rect, int16_t pointX,
  int16_t pointY
) {
  return (pointX >= rect->x) && (pointX < rect->x + rect->w)
    && (pointY >= rect->y) && (pointY < rect->y + rect->h);
}

/// @fn static void wmDamage(WmState *state, const GfxRect *rect)
///
/// @brief Add a rectangle to what the next frame repaints, or repaint the
/// whole screen if the list is full.
///
/// @param state The window manager's state.
/// @param rect The rectangle.
///
/// @return This function returns no value.
WM_SHARED void wmDamage(WmState *state, const GfxRect *rect) {
  if (state->numDamage < WM_MAX_DAMAGE) {
    state->damage[state->numDamage++] = *rect;
  } else {
    state->paintAll = true;
  }
}

/// @fn static WinEvent* wmSend(WmState *state, ProcessId pid, uint8_t type,
///   uint8_t window)
///
/// @brief Hand the Client overlay an event for a client, which it sends on
/// its next call.  The window server's own windows have no client, and an
/// event for them is dropped.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId, or 0.
/// @param type The WinEventType.
/// @param window The window the event is about.
///
/// @return Returns the event, for the caller to fill in the rest of, or NULL
/// if it was dropped.
WM_SHARED WinEvent* wmSend(WmState *state, ProcessId pid, uint8_t type,
  uint8_t window
) {
  if ((pid == 0) || (state->numOutbox == WM_OUTBOX)) {
    return NULL;
  }
  WmOutgoing *outgoing = &state->outbox[state->numOutbox++];
  memset(outgoing, 0, sizeof(*outgoing));
  outgoing->pid = pid;
  outgoing->event.type = type;
  outgoing->event.window = window;
  return &outgoing->event;
}

/// @fn static WmWindow* wmRaise(WmState *state, WmWindow *window)
///
/// @brief Move a window to the top of its layer, repainting it if it moved.
///
/// @param state The window manager's state.
/// @param window The window.
///
/// @return Returns the window where it now is.
WM_SHARED WmWindow* wmRaise(WmState *state, WmWindow *window) {
  uint8_t index = (uint8_t) (window - state->windows);
  uint8_t top = index;
  while ((top + 1 < state->numWindows)
    && (state->windows[top + 1].layer == window->layer)
  ) {
    top++;
  }
  if (top != index) {
    WmWindow raised = *window;
    memmove(&state->windows[index], &state->windows[index + 1],
      sizeof(WmWindow) * (size_t) (top - index));
    state->windows[top] = raised;
    wmDamage(state, &raised.frame);
  }
  return &state->windows[top];
}

/// @fn static void wmButtonRect(const WmWindow *window, uint8_t button,
///   GfxRect *rect)
///
/// @brief Find where a decorated window's caption button is:  the three are
/// flush right in the title bar, minimize, maximize and close.
///
/// @param window The window.
/// @param button The WmButton.
/// @param rect Where to store the button's rectangle.
///
/// @return This function returns no value.
WM_SHARED void wmButtonRect(const WmWindow *window, uint8_t button,
  GfxRect *rect
) {
  rect->x = window->frame.x + window->frame.w - WM_BORDER
    - ((WM_BUTTON_CLOSE + 1 - button) * WM_BUTTON_WIDTH);
  rect->y = window->frame.y + WM_BORDER;
  rect->w = WM_BUTTON_WIDTH;
  rect->h = WM_TITLE_HEIGHT;
}

/// @fn static uint8_t wmButtonAt(const WmWindow *window, int16_t pointX,
///   int16_t pointY)
///
/// @brief Find which of a window's caption buttons is at a point.
///
/// @param window The window.
/// @param pointX The point's column.
/// @param pointY The point's row.
///
/// @return Returns the WmButton, or WM_BUTTON_NONE.
WM_SHARED uint8_t wmButtonAt(const WmWindow *window, int16_t pointX,
  int16_t pointY
) {
  if ((window->flags & WIN_FLAG_DECORATED) == 0) {
    return WM_BUTTON_NONE;
  }
  for (uint8_t button = WM_BUTTON_MINIMIZE; button <= WM_BUTTON_CLOSE;
    button++
  ) {
    GfxRect rect;
    wmButtonRect(window, button, &rect);
    if (wmContains(&rect, pointX, pointY)) {
      return button;
    }
  }
  return WM_BUTTON_NONE;
}

/// @fn static WmWindow* wmWindowAt(WmState *state, int16_t pointX,
///   int16_t pointY)
///
/// @brief Find the shown window in front at a point.
///
/// @param state The window manager's state.
/// @param pointX The point's column.
/// @param pointY The point's row.
///
/// @return Returns the window, or NULL if the point is on the desktop.
WM_SHARED WmWindow* wmWindowAt(WmState *state, int16_t pointX,
  int16_t pointY
) {
  for (int ii = state->numWindows - 1; ii >= 0; ii--) {
    WmWindow *window = &state->windows[ii];
    if ((window->state != WIN_STATE_MINIMIZED)
      && wmContains(&window->frame, pointX, pointY)
    ) {
      return window;
    }
  }
  return NULL;
}

/// @fn static void wmDamageButton(WmState *state, uint8_t id,
///   uint8_t button)
///
/// @brief Repaint one of a window's caption buttons.
///
/// @param state The window manager's state.
/// @param id The window's identifier, or 0 for none.
/// @param button The WmButton, or WM_BUTTON_NONE for none.
///
/// @return This function returns no value.
WM_SHARED void wmDamageButton(WmState *state, uint8_t id, uint8_t button) {
  WmWindow *window = wmFind(state, id);
  if ((window != NULL) && (button != WM_BUTTON_NONE)) {
    GfxRect rect;
    wmButtonRect(window, button, &rect);
    wmDamage(state, &rect);
  }
}

/// @fn static void wmDamageDecorations(WmState *state, uint8_t id)
///
/// @brief Repaint a window's border and title bar, which show whether it's
/// active.
///
/// @param state The window manager's state.
/// @param id The window's identifier, or 0 for none.
///
/// @return This function returns no value.
WM_SHARED void wmDamageDecorations(WmState *state, uint8_t id) {
  WmWindow *window = wmFind(state, id);
  if ((window == NULL) || (window->state == WIN_STATE_MINIMIZED)) {
    return;
  }
  const GfxRect *frame = &window->frame;
  GfxRect sides[4] = {
    { frame->x, frame->y, frame->w, WM_FRAME_TOP },
    { frame->x, frame->y, WM_BORDER, frame->h },
    { frame->x + frame->w - WM_BORDER, frame->y, WM_BORDER, frame->h },
    { frame->x, frame->y + frame->h - WM_BORDER, frame->w, WM_BORDER },
  };
  for (uint8_t ii = 0; ii < sizeof(sides) / sizeof(sides[0]); ii++) {
    wmDamage(state, &sides[ii]);
  }
}

/// @fn static void wmFocus(WmState *state, uint8_t id, uint8_t detail)
///
/// @brief Tell a window's owner it gained or lost the keys.
///
/// @param state The window manager's state.
/// @param id The window's identifier, or 0 for none.
/// @param detail 1 if it gained them, 0 if it lost them.
///
/// @return This function returns no value.
WM_SHARED void wmFocus(WmState *state, uint8_t id, uint8_t detail) {
  WmWindow *window = wmFind(state, id);
  if (window != NULL) {
    WinEvent *event = wmSend(state, window->owner, WIN_EVENT_FOCUS, id);
    if (event != NULL) {
      event->detail = detail;
    }
  }
}

/// @fn static WmWindow* wmActivate(WmState *state, WmWindow *window)
///
/// @brief Give a window the keys.  A normal or popup window also goes to the
/// top of its layer, and a normal one is drawn as the active window; a
/// desktop or topmost window stays where it is, and the normal window drawn
/// as active stays so, so that clicking a taskbar doesn't flicker it.  The
/// owners of the windows that lost and gained the keys are told, in that
/// order.
///
/// @param state The window manager's state.
/// @param window The window, or NULL to give no window the keys.
///
/// @return Returns the window where it now is.
WM_SHARED WmWindow* wmActivate(WmState *state, WmWindow *window) {
  uint8_t id = 0;
  if (window != NULL) {
    if ((window->layer == WIN_LAYER_NORMAL)
      || (window->layer == WIN_LAYER_POPUP)
    ) {
      window = wmRaise(state, window);
    }
    id = window->id;
  }
  if (((window == NULL) || (window->layer == WIN_LAYER_NORMAL))
    && (state->active != id)
  ) {
    wmDamageDecorations(state, state->active);
    state->active = id;
    wmDamageDecorations(state, id);
  }
  if (state->focus != id) {
    wmFocus(state, state->focus, 0);
    state->focus = id;
    wmFocus(state, id, 1);
  }
  return window;
}

/// @fn static WmWindow* wmTopNormal(WmState *state)
///
/// @brief Find the shown normal window in front of the others.
///
/// @param state The window manager's state.
///
/// @return Returns the window, or NULL if no normal window is shown.
WM_SHARED WmWindow* wmTopNormal(WmState *state) {
  for (int ii = state->numWindows - 1; ii >= 0; ii--) {
    WmWindow *window = &state->windows[ii];
    if ((window->layer == WIN_LAYER_NORMAL)
      && (window->state != WIN_STATE_MINIMIZED)
    ) {
      return window;
    }
  }
  return NULL;
}

/// @fn static void wmResized(WmState *state, const WmWindow *window,
///   const GfxRect *before)
///
/// @brief Tell a window's owner that its body's size changed, if it did.
/// A window with a text grid is told by the Grid overlay, once the grid has
/// followed the body.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param before Its frame before the change.
///
/// @return This function returns no value.
WM_SHARED void wmResized(WmState *state, const WmWindow *window,
  const GfxRect *before
) {
  if ((before->w == window->frame.w) && (before->h == window->frame.h)) {
    return;
  } else if (window->content != NULL) {
    ((WmGrid*) window->content)->resized = true;
    state->gridsResized = true;
    return;
  }
  WinEvent *event = wmSend(state, window->owner, WIN_EVENT_RESIZED,
    window->id);
  if (event != NULL) {
    event->x = window->frame.w;
    event->y = window->frame.h;
    if ((window->flags & WIN_FLAG_DECORATED) != 0) {
      event->x -= 2 * WM_BORDER;
      event->y -= WM_FRAME_TOP + WM_BORDER;
    }
  }
}

/// @fn static void wmBody(const WmWindow *window, GfxRect *body)
///
/// @brief Find where a window's body is:  its frame, less the border and
/// title bar if it is decorated.
///
/// @param window The window.
/// @param body Where to store the body's rectangle, in screen coordinates.
///
/// @return This function returns no value.
WM_SHARED void wmBody(const WmWindow *window, GfxRect *body) {
  *body = window->frame;
  if ((window->flags & WIN_FLAG_DECORATED) != 0) {
    body->x += WM_BORDER;
    body->y += WM_FRAME_TOP;
    body->w -= 2 * WM_BORDER;
    body->h -= WM_FRAME_TOP + WM_BORDER;
  }
}

/// @fn static void wmGridPlace(WmState *state, const WmWindow *window,
///   GfxRect *place)
///
/// @brief Find where a window's text grid is on the screen and how many
/// columns and rows of cells fit it:  its record's rectangle, stretched on
/// each axis it is anchored to both sides of.
///
/// @param state The window manager's state.
/// @param window The window, whose content is a WmGrid.
/// @param place Where to store the grid's top-left corner, in screen
///   coordinates, and its columns and rows as w and h.
///
/// @return This function returns no value.
WM_SHARED void wmGridPlace(WmState *state, const WmWindow *window,
  GfxRect *place
) {
  const WmGrid *grid = (const WmGrid*) window->content;
  GfxRect body;
  wmBody(window, &body);
  int16_t width = grid->rect.w;
  int16_t height = grid->rect.h;
  if ((grid->anchor & (WIN_ANCHOR_LEFT | WIN_ANCHOR_RIGHT))
    == (WIN_ANCHOR_LEFT | WIN_ANCHOR_RIGHT)
  ) {
    width = body.w - grid->rect.x - grid->marginRight;
  }
  if ((grid->anchor & (WIN_ANCHOR_TOP | WIN_ANCHOR_BOTTOM))
    == (WIN_ANCHOR_TOP | WIN_ANCHOR_BOTTOM)
  ) {
    height = body.h - grid->rect.y - grid->marginBottom;
  }
  place->x = body.x + grid->rect.x;
  place->y = body.y + grid->rect.y;
  place->w = width / state->display.builtInFont.glyphW;
  place->h = height / state->display.builtInFont.glyphH;
  if (place->w < 1) {
    place->w = 1;
  } else if (place->w > UINT8_MAX) {
    place->w = UINT8_MAX;
  }
  if (place->h < 1) {
    place->h = 1;
  } else if (place->h > WM_GRID_MAX_ROWS) {
    place->h = WM_GRID_MAX_ROWS;
  }
}

/// @fn static void wmGridMargins(WmState *state, const WmWindow *window,
///   WmGrid *grid)
///
/// @brief Work out the margins of a window's text grid:  the parts of the
/// body above, below, left of and right of its cells, as far as the body
/// goes, leaving out those that are empty.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param grid The window's grid, whose columns and rows are set.
///
/// @return This function returns no value.
WM_SHARED void wmGridMargins(WmState *state, const WmWindow *window,
  WmGrid *grid
) {
  GfxRect body;
  wmBody(window, &body);
  int16_t left = body.x - window->frame.x;
  int16_t top = body.y - window->frame.y;
  int16_t right = left + body.w;
  int16_t bottom = top + body.h;
  int16_t cellsRight
    = grid->left + grid->columns * state->display.builtInFont.glyphW;
  int16_t cellsBottom
    = grid->top + grid->rows * state->display.builtInFont.glyphH;
  if (cellsRight > right) {
    cellsRight = right;
  }
  if (cellsBottom > bottom) {
    cellsBottom = bottom;
  }
  GfxRect margins[WM_GRID_MARGINS] = {
    { left, top, body.w, grid->top - top },
    { left, cellsBottom, body.w, bottom - cellsBottom },
    { left, grid->top, grid->left - left, cellsBottom - grid->top },
    { cellsRight, grid->top, right - cellsRight, cellsBottom - grid->top },
  };
  grid->numMargins = 0;
  for (uint8_t ii = 0; ii < WM_GRID_MARGINS; ii++) {
    if ((margins[ii].w > 0) && (margins[ii].h > 0)) {
      grid->margins[grid->numMargins++] = margins[ii];
    }
  }
}

/// @fn static void wmGridDirty(WmState *state, WmGrid *grid, uint8_t first,
///   uint8_t last)
///
/// @brief Mark rows of a text grid to be repainted.
///
/// @param state The window manager's state.
/// @param grid The grid.
/// @param first The first row.
/// @param last The last row, which may be before first to mark none.
///
/// @return This function returns no value.
WM_SHARED void wmGridDirty(WmState *state, WmGrid *grid, uint8_t first,
  uint8_t last
) {
  for (uint8_t row = first; (row <= last) && (row < grid->rows); row++) {
    grid->dirty[WM_GRID_DIRTY_BYTE(row)] |= WM_GRID_DIRTY_BIT(row);
    state->gridsDirty = true;
  }
}

/// @fn static void wmGridUnbind(WmGrid *grid)
///
/// @brief Unbind a text grid from its session, which has ended or been
/// closed:  forget the keys held for it, and let go of the messages the
/// console has finished and of their arguments.
///
/// @param grid The grid.
///
/// @return This function returns no value.
WM_SHARED void wmGridUnbind(WmGrid *grid) {
  grid->port = WM_NO_PORT;
  grid->numHeld = 0;
  grid->reportWanted = false;
  grid->winsizeWanted = false;
  if ((grid->input != NULL) && processMessageDone(grid->input)) {
    processMessageRelease(grid->input);
    grid->input = NULL;
  }
  if ((grid->winsize != NULL) && processMessageDone(grid->winsize)) {
    processMessageRelease(grid->winsize);
    grid->winsize = NULL;
  }
  if ((grid->input == NULL) && (grid->inputArgs != NULL)) {
    MESSAGE_ARGS_DESTROY(grid->inputArgs);
    grid->inputArgs = NULL;
  }
  if ((grid->winsize == NULL) && (grid->winsizeArgs != NULL)) {
    MESSAGE_ARGS_DESTROY(grid->winsizeArgs);
    grid->winsizeArgs = NULL;
  }
}

#endif // WM_H
