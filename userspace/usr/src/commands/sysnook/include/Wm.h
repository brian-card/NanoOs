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

/// @def WM_MAX_CLIENTS
///
/// @brief The most processes connected to the window server at once.
#define WM_MAX_CLIENTS 16

/// @def WM_DEFAULT_BUDGET
///
/// @brief The bytes of the window server's heap each client's windows,
/// widgets and grids may take, unless --budget says otherwise.
#define WM_DEFAULT_BUDGET 16384

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

/// @def WM_MAX_DIALOG_EVENTS
///
/// @brief The most events about stock dialogs kept for the Dialogs overlay
/// between its calls.
#define WM_MAX_DIALOG_EVENTS 8

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

/// @def WM_CHANGED_QUIET_MS
///
/// @brief How long a text field has to go unchanged before its owner is sent
/// WIN_EVENT_CHANGED, unless something it does first sends it sooner.
#define WM_CHANGED_QUIET_MS 250

/// @def WM_MOD_ALT
///
/// @brief The modifier bits of either Alt:  the VDP reports the right one as
/// AltGr.
#define WM_MOD_ALT (GFX_MOD_ALT | GFX_MOD_ALT_GR)

/// @enum WmGrab
///
/// @brief What the press that is held down started.
///
/// @param WM_GRAB_NONE Nothing.
/// @param WM_GRAB_MOVE Moving a window by its title bar.
/// @param WM_GRAB_RESIZE Resizing a window by its edges.
/// @param WM_GRAB_BUTTON Pressing a caption button, which acts on release.
/// @param WM_GRAB_WIDGET Pressing a widget, which acts on release.
/// @param WM_GRAB_MENU A menu is open, whether or not a button is held.  It
///   comes after WM_GRAB_WIDGET, since both take every pointer event.
typedef enum WmGrab {
  WM_GRAB_NONE = 0,
  WM_GRAB_MOVE,
  WM_GRAB_RESIZE,
  WM_GRAB_BUTTON,
  WM_GRAB_WIDGET,
  WM_GRAB_MENU,
} WmGrab;

/// @struct WmMenuState
///
/// @brief The menu that is open, if one is, and how Alt has been used.
///
/// @param open Whether a menu is open.
/// @param window The identifier of the open menu's window.
/// @param bar The index of its menu bar among the window's widgets.
/// @param menu The open menu's index in the bar.
/// @param item The index of the highlighted item, or WM_NO_WIDGET.
/// @param down Whether the left button was down at the last pointer event.
/// @param altAlone Whether an Alt is down and nothing else has been pressed
///   since.
/// @param swallow The key that closed the menu, whose release is the menu's
///   too, or 0.
/// @param rect Where the open menu's dropdown is on the screen.
typedef struct WmMenuState {
  bool    open;
  uint8_t window;
  uint8_t bar;
  uint8_t menu;
  uint8_t item;
  bool    down;
  bool    altAlone;
  uint8_t swallow;
  GfxRect rect;
} WmMenuState;

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
/// @param owner The ProcessId of the client that owns it, or 0 for the
///   window server's own.
/// @param face The color of the body.
/// @param frame Where the window is, in screen coordinates, including its
///   decorations.
/// @param restored The frame a maximized window goes back to.
/// @param title The title, on the heap, or NULL.
/// @param content The window's WmGrid, or NULL.
/// @param widgets The window's WmWidgets, or NULL.
/// @param ownerWindow The window that owns this one, or 0.
/// @param dialog The WmDialog of a stock dialog, or NULL.
typedef struct WmWindow {
  uint8_t   id;
  uint8_t   layer;
  uint8_t   flags;
  uint8_t   state;
  uint8_t   restoreState;
  ProcessId owner;
  GfxColor  face;
  GfxRect   frame;
  GfxRect   restored;
  char     *title;
  void     *content;
  void     *widgets;
  uint8_t   ownerWindow;
  void     *dialog;
} WmWindow;

/// @struct WmWidget
///
/// @brief One widget of a window's template, as the window server keeps it.
/// The bytes come first, because ez80-clang can't address a multi-byte field
/// at an offset of 126 or 127.
///
/// @param kind The WinWidgetKind.
/// @param id The application's identifier for it, unique in the window, or
///   0 for none.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param align A label's WIN_ALIGN_* value.
/// @param group A radio button's group.
/// @param maxLength A text field's most bytes, which its text's storage
///   holds.
/// @param caret The byte of a text field's text the caret is before.
/// @param scroll The first byte of a text field's text that is shown.
/// @param textLength The bytes at text.
/// @param selected Whether a text field's whole text is selected, so that
///   the next key typed replaces it.
/// @param value A box's or toggle button's state, or a list's selection,
///   WIN_NO_ITEM for none.
/// @param top The first item a list shows.
/// @param itemsSize The bytes of a list's items, a menu bar's menus or a
///   canvas's WmCanvas at text.
/// @param numItems The number of a list's items or a menu bar's menus.
/// @param rect Its place in the body when the template was given, which
///   wmWidgetRect moves and stretches by its anchors.
/// @param marginRight How far its right edge was from the body's then.
/// @param marginBottom How far its bottom edge was from the body's then.
/// @param text Its text, with no terminator, a list's NUL-separated items,
///   a menu bar's menus, or a canvas's WmCanvas, on the heap, or NULL.
typedef struct WmWidget {
  uint8_t  kind;
  uint8_t  id;
  uint8_t  flags;
  uint8_t  anchor;
  uint8_t  align;
  uint8_t  group;
  uint8_t  maxLength;
  uint8_t  caret;
  uint8_t  scroll;
  uint8_t  textLength;
  bool     selected;
  uint16_t value;
  uint16_t top;
  uint16_t itemsSize;
  uint16_t numItems;
  GfxRect  rect;
  int16_t  marginRight;
  int16_t  marginBottom;
  char    *text;
} WmWidget;

/// @struct WmWidgets
///
/// @brief A window's widgets, in template order.
///
/// @param count The number of widgets.
/// @param focus The index of the widget with the keyboard focus, or
///   WM_NO_WIDGET.
/// @param pressed The index of the widget held down, or WM_NO_WIDGET.
/// @param pressInside Whether the pointer is still on the widget held down.
/// @param bytes The bytes they take on the heap, this structure and their
///   texts included, which are charged to the window's owner.
/// @param widget The widgets, as many as count:  the block is allocated with
///   room for all of them past the one sizeof(WmWidgets) holds, since C++
///   has no flexible array members.
typedef struct WmWidgets {
  uint8_t  count;
  uint8_t  focus;
  uint8_t  pressed;
  bool     pressInside;
  uint16_t bytes;
  WmWidget widget[1];
} WmWidgets;

/// @def WM_NO_WIDGET
///
/// @brief The index that names none of a window's widgets.
#define WM_NO_WIDGET 0xFF

/// @def WM_LIST_ITEM_HEIGHT
///
/// @brief The height of a list's row.
#define WM_LIST_ITEM_HEIGHT 10

/// @def WM_SCROLL_BAR_WIDTH
///
/// @brief The width of a list's scroll bar, and the height of each of its
/// arrows.
#define WM_SCROLL_BAR_WIDTH 10

/// @struct WmGrid
///
/// @brief A text grid:  rows and columns of cells, each a character byte and
/// an attribute byte, the foreground ANSI color in its low 4 bits and the
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
///   to the window's frame, which are painted the body's color instead of
///   the whole body, so that no cell shows it between its old colors and
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
/// @param used The bytes of the heap its windows' widgets and grids take.
/// @param message The WIN_MSG_EVENTS the process hasn't marked done, or NULL.
/// @param args The events message's arguments, a persistent round trip that
///   lives as long as the client.
/// @param pending The events waiting for the process to finish with the last
///   message.
typedef struct WmClient {
  ProcessId       pid;
  uint8_t         numPending;
  bool            leaving;
  uint16_t        used;
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
/// @param gridPainted Whether the next frame repaints text grid rows, or a
///   text field a key changed.
/// @param keySent When the key echoKey waits on was sent.
/// @param budget The bytes of the heap each client may take.
/// @param menu The open menu.
/// @param ending Whether the window server has stopped and is giving its
///   clients' windows up, when the driver discards their preuploads with
///   the display.
/// @param refocus The windows that stopped and started being active since
///   the last frame, or 0, whose focused widgets the Restack overlay
///   repaints.
/// @param raised The window raised since the last frame, or 0.
/// @param raisedFrom The index the raised window was raised from.
/// @param widgetKey The key a widget took the press of, whose release the
///   Input overlay drops without asking the Controls overlay, or 0.
/// @param damageOwner The identifier of the window whose own contents
///   changed under each rectangle of damage, which the Paint overlay repaints
///   only where that window shows, or 0 to repaint everything there.
/// @param heldWindow The window of the text field whose WIN_EVENT_CHANGED is
///   held until the field has been quiet for WM_CHANGED_QUIET_MS, or 0.
/// @param heldWidget That text field's identifier.
/// @param heldSince When that text field last changed.
/// @param numOwned The number of windows that have an owner window.
/// @param numDialogEvents The number of entries in dialogEvents.
/// @param dialogEvents The events about stock dialogs the Client overlay
///   kept from their clients for the Dialogs overlay.
/// @param timedKind The WmTimed of the frame the Client overlay times.
/// @param timedStart When what that frame shows happened.
/// @param timedEnd When that frame was drawn.
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
  uint8_t         pad[3];
  GfxDisplay      display;
  struct timespec started;
  GfxRect         damage[WM_MAX_DAMAGE];
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
  uint16_t        budget;
  WmMenuState     menu;
  bool            ending;
  uint8_t         refocus[2];
  uint8_t         raised;
  uint8_t         raisedFrom;
  uint8_t         widgetKey;
  uint8_t         damageOwner[WM_MAX_DAMAGE];
  uint8_t         heldWindow;
  uint8_t         heldWidget;
  struct timespec heldSince;
  uint8_t         numOwned;
  uint8_t         numDialogEvents;
  WinEvent        dialogEvents[WM_MAX_DIALOG_EVENTS];
  uint8_t         timedKind;
  const struct timespec *timedStart;
  struct timespec timedEnd;
} WmState;

/// @struct WmPaintJob
///
/// @brief What the Paint overlay hands the Widgets overlay to paint a
/// window's widgets.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param clip The rectangle being painted.
typedef struct WmPaintJob {
  WmState        *state;
  const WmWindow *window;
  const GfxRect  *clip;
} WmPaintJob;

/// @struct WmInputJob
///
/// @brief What the Input overlay hands the Controls overlay:  a press, a
/// pointer event while a widget is held, or a key, in a window with widgets.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param event The event.
/// @param handled Whether the Controls overlay used the event, which it
///   sets.
typedef struct WmInputJob {
  WmState        *state;
  WmWindow       *window;
  const GfxEvent *event;
  bool            handled;
} WmInputJob;

/// @def WM_CANVAS_PRELOADS
///
/// @brief The most preuploads one canvas's records are split into.
#define WM_CANVAS_PRELOADS 8

/// @def WM_CANVAS_CHUNK
///
/// @brief The most bytes of records one of a canvas's preuploads holds,
/// which is the command buffer's size.
#define WM_CANVAS_CHUNK 512

/// @struct WmCanvas
///
/// @brief A canvas's records, as the driver's preuploads of them.
///
/// @param count The number of preuploads.
/// @param preloads Their handles, in order.
typedef struct WmCanvas {
  uint8_t   count;
  GfxHandle preloads[WM_CANVAS_PRELOADS];
} WmCanvas;

/// @struct WmCanvasJob
///
/// @brief What the Template and Property overlays hand the Canvas overlay to
/// discard the preuploads of the canvases among some of a window's widgets.
///
/// @param state The window manager's state.
/// @param widgets The window's widgets.
/// @param from The index of the first widget.
/// @param to The index after the last.
typedef struct WmCanvasJob {
  WmState   *state;
  WmWidgets *widgets;
  uint8_t    from;
  uint8_t    to;
} WmCanvasJob;

/// @struct WmMenusCheck
///
/// @brief What the Template overlay hands the Property overlay to check a
/// menu bar's menus.
///
/// @param menus The menus.
/// @param size The bytes at menus.
/// @param count The number of menus, or -1 if they are malformed, which the
///   Property overlay sets.
typedef struct WmMenusCheck {
  const uint8_t *menus;
  uint16_t       size;
  int16_t        count;
} WmMenusCheck;

/// @struct WmWidgetJob
///
/// @brief What one overlay hands the Lists or Menus overlay:  a list or
/// menu bar to paint, or an event for one.
///
/// @param state The window manager's state.
/// @param window The widget's window, or NULL if it has gone.
/// @param widget The widget, or NULL if it has gone.
/// @param rect Where the widget is.
/// @param event The event, or NULL to paint.
/// @param focused Whether the widget shows the focus, to paint.
/// @param handled Whether the widget used the event, which the overlay
///   sets.
typedef struct WmWidgetJob {
  WmState        *state;
  WmWindow       *window;
  WmWidget       *widget;
  GfxRect         rect;
  const GfxEvent *event;
  bool            focused;
  bool            handled;
} WmWidgetJob;

/// @struct WmTemplateJob
///
/// @brief What the Window overlay hands the Template overlay:  a template to
/// add to a window, a window whose widgets to free, or a widget request.
///
/// @param state The window manager's state.
/// @param window The window, or NULL for a request.
/// @param widgets The template.
/// @param data A request's arguments.
/// @param size The bytes at widgets.
/// @param pid The ProcessId of the request's sender.
/// @param type A request's WinMsgType.
/// @param waiting Whether the request's sender waits for an answer.
/// @param status The WinError the Template overlay sets.
typedef struct WmTemplateJob {
  WmState       *state;
  WmWindow      *window;
  const uint8_t *widgets;
  void          *data;
  uint16_t       size;
  ProcessId      pid;
  uint8_t        type;
  bool           waiting;
  uint8_t        status;
} WmTemplateJob;

/// @def WM_NOT_PENDING
///
/// @brief A WmDialog's pending value while no list request is outstanding.
#define WM_NOT_PENDING 0xFF

/// @struct WmDialog
///
/// @brief What the window server keeps about a stock dialog.
///
/// @param kind The WinDialogKind.
/// @param ended Whether its result has been sent, so that it takes no more
///   input until its client destroys it.
/// @param parent The stock file dialog a message the window server opened
///   itself answers, or 0 for a client's.
/// @param pending The WIN_LIST_* bits of the list request outstanding, or
///   WM_NOT_PENDING.
/// @param maximum The most bytes of the text or path.
/// @param asking What a file dialog's own message asks:  WM_ASK_NONE,
///   WM_ASK_REPLACE or WM_ASK_MISSING.
/// @param started Whether a file dialog has shown a directory yet.
/// @param truncated Whether the listing coming has more entries than are
///   kept.
/// @param listingSize The bytes at listing.
/// @param placesSize The bytes at places.
/// @param suffix The only suffix a file dialog shows, or NULL for every file.
/// @param directory The directory a file dialog shows, ending in '/', or
///   NULL.
/// @param places The places' paths, each NUL-ended, in the order shown, or
///   NULL.
/// @param listing The entries of the listing coming, as they were sent, or
///   NULL.
typedef struct WmDialog {
  uint8_t  kind;
  bool     ended;
  uint8_t  parent;
  uint8_t  pending;
  uint8_t  maximum;
  uint8_t  asking;
  bool     started;
  bool     truncated;
  uint16_t listingSize;
  uint16_t placesSize;
  char    *suffix;
  char    *directory;
  char    *places;
  uint8_t *listing;
} WmDialog;

/// @def WM_ASK_NONE
///
/// @brief A file dialog asks nothing.
#define WM_ASK_NONE    0
/// @def WM_ASK_REPLACE
///
/// @brief A save dialog asks whether to replace the file chosen.
#define WM_ASK_REPLACE 1
/// @def WM_ASK_MISSING
///
/// @brief An open dialog says the file named doesn't exist.
#define WM_ASK_MISSING 2

/// @def WM_DIALOG_BUTTONS
///
/// @brief The identifier of a stock dialog's button whose result is
/// WIN_RESULT_CANCEL, the other buttons' being this plus their WIN_RESULT_*.
#define WM_DIALOG_BUTTONS 20

/// @def WM_DIALOG_PLACES
///
/// @brief The identifier of a file dialog's list of places.
#define WM_DIALOG_PLACES  31

/// @def WM_DIALOG_ENTRIES
///
/// @brief The identifier of a file dialog's list of a directory's entries.
#define WM_DIALOG_ENTRIES 32

/// @def WM_DIALOG_LOOK_IN
///
/// @brief The identifier of a file dialog's label of the directory shown.
#define WM_DIALOG_LOOK_IN 33

/// @def WM_DIALOG_ICON_SIZE
///
/// @brief The width and height of a message's icon.
#define WM_DIALOG_ICON_SIZE 16

/// @def WM_DIALOG_ICON
///
/// @brief The identifier of a message's icon, a canvas.
#define WM_DIALOG_ICON 30

/// @def WM_DIALOG_TEMPLATE
///
/// @brief The bytes of the buffer a stock dialog's template is made in.
#define WM_DIALOG_TEMPLATE 1024

/// @def WM_DIALOG_PARTS
///
/// @brief The number of an open request's fields:  title, text, initial text
/// and suffix.
#define WM_DIALOG_PARTS 4

/// @struct WmDialogText
///
/// @brief One of an open request's fields, which has no terminator.
///
/// @param text The bytes.
/// @param length How many.
typedef struct WmDialogText {
  const char *text;
  uint8_t     length;
} WmDialogText;

/// @struct WmDialogOpen
///
/// @brief A stock dialog to open.
///
/// @param kind The WinDialogKind.
/// @param icon A message's WinDialogIcon.
/// @param buttons A message's WinDialogButtons.
/// @param maximum The most bytes of the text or path.
/// @param owner The window that owns it.
/// @param parent The file dialog a message of the window server's own
///   answers, or 0.
/// @param pid The client it is opened for.
/// @param parts The title, text, initial text and suffix.
typedef struct WmDialogOpen {
  uint8_t      kind;
  uint8_t      icon;
  uint8_t      buttons;
  uint8_t      maximum;
  uint8_t      owner;
  uint8_t      parent;
  ProcessId    pid;
  WmDialogText parts[WM_DIALOG_PARTS];
} WmDialogOpen;

/// @def WM_ICON_RECORDS
///
/// @brief The bytes of the canvas records a message's icon is drawn with.
#define WM_ICON_RECORDS 64

/// @struct WmMakeJob
///
/// @brief What the Dialogs overlay hands the Make overlay to make a stock
/// dialog's template.
///
/// @param state The window manager's state.
/// @param open The dialog.
/// @param buffer Where the template goes:  WM_DIALOG_TEMPLATE bytes.
/// @param size Out:  the bytes of the template.
/// @param frame Out:  the size of the dialog's frame.
/// @param focus Out:  the widget that has the focus first.
/// @param iconSize Out:  the bytes at icon, 0 for no icon.
/// @param icon Out:  the canvas records of a message's icon.
typedef struct WmMakeJob {
  WmState            *state;
  const WmDialogOpen *open;
  uint8_t            *buffer;
  uint16_t            size;
  GfxRect             frame;
  uint8_t             focus;
  uint16_t            iconSize;
  uint8_t             icon[WM_ICON_RECORDS];
} WmMakeJob;

/// @struct WmWindowJob
///
/// @brief What another overlay hands the Window overlay to make or take
/// away a window.
///
/// @param state The window manager's state.
/// @param create The window to make, as a client asks, or NULL to take away
///   window.
/// @param pid The client the window is made for.
/// @param window The window to take away.
typedef struct WmWindowJob {
  WmState             *state;
  WinCreateWindowArgs *create;
  ProcessId            pid;
  uint8_t              window;
} WmWindowJob;

/// @struct WmDialogJob
///
/// @brief What the Dialogs overlay hands the Files overlay about a file
/// dialog, and what the Files overlay wants it to do in turn.
///
/// @param state The window manager's state.
/// @param window The file dialog.
/// @param dialog Its WmDialog.
/// @param event An event about its widgets, or NULL.
/// @param listing A listing its client sent, or NULL.
/// @param initial The initial path, for wmFilesStart.
/// @param result The WIN_RESULT_* its own message ended with.
/// @param checked Out:  whether the listing taken answered a check of the
///   path chosen.
/// @param errorNumber Out:  why the path checked has no entry, or 0.
/// @param kind Out:  the WIN_ENTRY_* of the path checked.
/// @param placesShown Out:  whether a directory was shown, whose place is
///   then highlighted.
/// @param ask Out:  the text of a message to open over it, on the heap, or
///   NULL.
typedef struct WmDialogJob {
  WmState                    *state;
  WmWindow                   *window;
  WmDialog                   *dialog;
  const WinEvent             *event;
  const WinDialogListingArgs *listing;
  const char                 *initial;
  uint8_t                     result;
  bool                        checked;
  uint8_t                     errorNumber;
  uint8_t                     kind;
  bool                        placesShown;
  char                       *ask;
} WmDialogJob;

/// @fn static bool wmIntersect(const GfxRect *first, const GfxRect *second,
///   GfxRect *result)
///
/// @brief Find where two rectangles overlap.
///
/// @param first One rectangle.
/// @param second The other.
/// @param result Where to store the overlap, which may be either input.
///
/// @return Returns true if they overlap, false otherwise.
WM_SHARED bool wmIntersect(const GfxRect *first, const GfxRect *second,
  GfxRect *result
) {
  int16_t left = first->x;
  int16_t top = first->y;
  int16_t right = first->x + first->w;
  int16_t bottom = first->y + first->h;

  if (second->x > left) {
    left = second->x;
  }

  if (second->y > top) {
    top = second->y;
  }

  if ((second->x + second->w) < right) {
    right = second->x + second->w;
  }

  if ((second->y + second->h) < bottom) {
    bottom = second->y + second->h;
  }
  if ((left >= right) || (top >= bottom)) {
    return false;
  }

  result->x = left;
  result->y = top;
  result->w = right - left;
  result->h = bottom - top;

  return true;
}

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
  return (pointX >= rect->x) && (pointX < (rect->x + rect->w))
    && (pointY >= rect->y) && (pointY < (rect->y + rect->h));
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
    state->damageOwner[state->numDamage] = 0;
    state->damage[state->numDamage++] = *rect;
  } else {
    state->paintAll = true;
  }
}

/// @fn static void wmDamageOwned(WmState *state, const GfxRect *rect,
///   uint8_t owner)
///
/// @brief Mark a rectangle where a window's own contents changed to be
/// repainted next frame, only where the window shows.
///
/// @param state The window manager's state.
/// @param rect The rectangle, in screen coordinates.
/// @param owner The window's identifier.
///
/// @return This function returns no value.
WM_SHARED void wmDamageOwned(WmState *state, const GfxRect *rect,
  uint8_t owner
) {
  uint8_t index = state->numDamage;

  wmDamage(state, rect);

  if (index < state->numDamage) {
    state->damageOwner[index] = owner;
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

/// @fn static void wmChanged(WmState *state, const WmWindow *window,
///   const WmWidget *widget, uint16_t value)
///
/// @brief Tell a text field's or list's owner it changed, with a text's
/// length or a list's selection, once for each message of input:  a
/// WIN_EVENT_CHANGED for the widget already waiting to go out is brought up
/// to date instead of sending another.
///
/// @param state The window manager's state.
/// @param window The field's window.
/// @param widget The text field or list.
/// @param value The event's value.
///
/// @return This function returns no value.
WM_SHARED void wmChanged(WmState *state, const WmWindow *window,
  const WmWidget *widget, uint16_t value
) {
  WinEvent *event = NULL;

  for (uint8_t ii = 0; ii < state->numOutbox; ii++) {
    WmOutgoing *outgoing = &state->outbox[ii];
    if ((outgoing->pid == window->owner)
      && (outgoing->event.type == WIN_EVENT_CHANGED)
      && (outgoing->event.window == window->id)
      && (outgoing->event.widget == widget->id)
    ) {
      event = &outgoing->event;
    }
  }

  if (event == NULL) {
    event = wmSend(state, window->owner, WIN_EVENT_CHANGED, window->id);
  }
  if (event != NULL) {
    event->widget = widget->id;
    event->value = value;
  }
}

/// @fn static uint16_t wmCountItems(const char *items, uint16_t size)
///
/// @brief Count a list's items:  each ended by a NUL, which the last may
/// lack.
///
/// @param items The items.
/// @param size The bytes at items.
///
/// @return Returns the number of items.
WM_SHARED uint16_t wmCountItems(const char *items, uint16_t size) {
  uint16_t count = 0;

  for (uint16_t ii = 0; ii < size; ii++) {
    if (items[ii] == '\0') {
      count++;
    }
  }

  if ((size > 0) && (items[size - 1] != '\0')) {
    count++;
  }

  return count;
}

/// @fn static const char* wmItem(const WmWidget *list, uint16_t index,
///   uint16_t *length)
///
/// @brief Find one of a list's items.
///
/// @param list The list.
/// @param index The item's index, which must be under its number of items.
/// @param length Where to store the bytes of its text.
///
/// @return Returns a pointer to its text, which has no terminator.
WM_SHARED const char* wmItem(const WmWidget *list, uint16_t index,
  uint16_t *length
) {
  const char *at = list->text;
  const char *end = list->text + list->itemsSize;

  for (; index > 0; index--) {
    while (*at != '\0') {
      at++;
    }

    at++;
  }

  const char *stop = at;
  while ((stop < end) && (*stop != '\0')) {
    stop++;
  }

  *length = (uint16_t) (stop - at);

  return at;
}

/// @fn static const uint8_t* wmSkipText(const uint8_t *text)
///
/// @brief Step past a length byte and its text.
///
/// @param text The length byte.
///
/// @return Returns a pointer to the byte after the text.
WM_SHARED const uint8_t* wmSkipText(const uint8_t *text) {
  return text + 1 + *text;
}

/// @fn static uint8_t* wmMenuItem(uint8_t *menu, uint8_t index)
///
/// @brief Find one of a menu's items.
///
/// @param menu The menu's title length byte.
/// @param index The item's index, which must be under its count.
///
/// @return Returns a pointer to the item's identifier.
WM_SHARED uint8_t* wmMenuItem(uint8_t *menu, uint8_t index) {
  uint8_t *at = ((uint8_t*) wmSkipText(menu)) + 1;

  for (; index > 0; index--) {
    at = (uint8_t*) wmSkipText(wmSkipText(at + 2));
  }

  return at;
}

/// @fn static uint8_t* wmMenu(const WmWidget *bar, uint8_t index)
///
/// @brief Find one of a menu bar's menus.
///
/// @param bar The menu bar.
/// @param index The menu's index, which must be under its number of menus.
///
/// @return Returns a pointer to the menu's title length byte.
WM_SHARED uint8_t* wmMenu(const WmWidget *bar, uint8_t index) {
  uint8_t *at = (uint8_t*) bar->text;

  for (; index > 0; index--) {
    at = wmMenuItem(at, *wmSkipText(at));
  }

  return at;
}

/// @fn static void wmDiscardCanvas(WmState *state, WmWidget *widget)
///
/// @brief Discard a canvas's preuploads, if the widget is a canvas with
/// any and the window server isn't ending.  Its WmCanvas is left for the
/// caller to free.
///
/// @param state The window manager's state.
/// @param widget The widget.
///
/// @return This function returns no value.
WM_SHARED void wmDiscardCanvas(WmState *state, WmWidget *widget) {
  WmCanvas *canvas = (WmCanvas*) widget->text;
  if ((widget->kind != WIN_WIDGET_CANVAS) || (canvas == NULL)
    || state->ending
  ) {
    return;
  }

  for (uint8_t ii = 0; ii < canvas->count; ii++) {
    gfxDiscardPreupload(&state->display, canvas->preloads[ii]);
  }

  canvas->count = 0;
}

/// @def WM_DOUBLE_CLICK_MS
///
/// @brief The longest time between two presses that make a
/// double-click.
#define WM_DOUBLE_CLICK_MS 500

/// @def WM_DOUBLE_CLICK_DISTANCE
///
/// @brief The farthest two presses that make a double-click
/// can be apart on either axis.
#define WM_DOUBLE_CLICK_DISTANCE 4

/// @fn static uint32_t wmNowMs(WmState *state)
///
/// @brief Get the time since the window server started.  The clock is read
/// when an input message is handled, since GfxEvent carries no time.
///
/// @param state The window manager's state.
///
/// @return Returns the milliseconds since the window server started.
WM_SHARED uint32_t wmNowMs(WmState *state) {
  struct timespec now;
  timespec_get(&now, TIME_UTC);

  return (uint32_t) (((now.tv_sec - state->started.tv_sec) * 1000L)
    + ((now.tv_nsec - state->started.tv_nsec) / 1000000L));
}

/// @fn static bool wmDoubleClick(WmState *state, const WmWindow *window,
///   int16_t pointX, int16_t pointY)
///
/// @brief Determine whether a press on a title bar or a list item is the
/// second of a double-click, and remember it as the first of the next one
/// if not.
///
/// @param state The window manager's state.
/// @param window The window.
/// @param pointX The pointer's column.
/// @param pointY The pointer's row.
///
/// @return Returns true if it is, false otherwise.
WM_SHARED bool wmDoubleClick(WmState *state, const WmWindow *window,
  int16_t pointX, int16_t pointY
) {
  uint32_t now = wmNowMs(state);
  int16_t distanceX = pointX - state->lastPressAt.x;
  int16_t distanceY = pointY - state->lastPressAt.y;

  if ((state->lastPressWindow == window->id)
    && ((now - state->lastPressTime) <= WM_DOUBLE_CLICK_MS)
    && (distanceX <= WM_DOUBLE_CLICK_DISTANCE)
    && (distanceX >= -WM_DOUBLE_CLICK_DISTANCE)
    && (distanceY <= WM_DOUBLE_CLICK_DISTANCE)
    && (distanceY >= -WM_DOUBLE_CLICK_DISTANCE)
  ) {
    state->lastPressWindow = 0;
    return true;
  }

  state->lastPressWindow = window->id;
  state->lastPressTime = now;
  state->lastPressAt.x = pointX;
  state->lastPressAt.y = pointY;

  return false;
}

/// @fn static WmWindow* wmRaise(WmState *state, WmWindow *window)
///
/// @brief Move a window to the top of its layer, and have the Restack
/// overlay repaint what the windows it moved above were covering of it.
/// The work is left to that overlay because its size would overflow the
/// overlays that call this.
///
/// @param state The window manager's state.
/// @param window The window.
///
/// @return Returns the window where it now is.
WM_SHARED WmWindow* wmRaise(WmState *state, WmWindow *window) {
  uint8_t index = (uint8_t) (window - state->windows);
  uint8_t top = index;

  while (((top + 1) < state->numWindows)
    && (state->windows[top + 1].layer == window->layer)
  ) {
    top++;
  }

  if (top != index) {
    WmWindow raised = *window;
    if (state->raised != 0) {
      WmWindow *earlier = wmFind(state, state->raised);
      if (earlier != NULL) {
        wmDamage(state, &earlier->frame);
      }
    }

    memmove(&state->windows[index], &state->windows[index + 1],
      sizeof(WmWindow) * ((size_t) (top - index)));
    state->windows[top] = raised;
    state->raised = raised.id;
    state->raisedFrom = index;
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
/// active:  that window alone, where it shows.
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

  for (uint8_t ii = 0; ii < (sizeof(sides) / sizeof(sides[0])); ii++) {
    wmDamageOwned(state, &sides[ii], id);
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
    state->refocus[0] = state->active;
    state->active = id;
    wmDamageDecorations(state, id);
    state->refocus[1] = id;
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

/// @fn static void wmDialogFree(WmDialog *dialog)
///
/// @brief Free a stock dialog's WmDialog and what it holds.
///
/// @param dialog The WmDialog, or NULL.
///
/// @return This function returns no value.
WM_SHARED void wmDialogFree(WmDialog *dialog) {
  if (dialog != NULL) {
    free(dialog->suffix);
    free(dialog->directory);
    free(dialog->places);
    free(dialog->listing);
    free(dialog);
  }
}

/// @fn static WmWindow* wmRoot(WmState *state, WmWindow *window)
///
/// @brief Find the window at the top of a window's chain of owners.
///
/// @param state The window manager's state.
/// @param window The window.
///
/// @return Returns the window that owns the chain, which is the window itself
/// if nothing owns it.
WM_SHARED WmWindow* wmRoot(WmState *state, WmWindow *window) {
  WmWindow *owner = wmFind(state, window->ownerWindow);

  while (owner != NULL) {
    window = owner;
    owner = wmFind(state, window->ownerWindow);
  }

  return window;
}

/// @fn static bool wmOwns(WmState *state, const WmWindow *owner,
///   const WmWindow *window)
///
/// @brief Find whether a window is owned by another, directly or through a
/// chain of owners.
///
/// @param state The window manager's state.
/// @param owner The possible owner.
/// @param window The window.
///
/// @return Returns true if owner owns window, false otherwise, and false for
/// the same window.
WM_SHARED bool wmOwns(WmState *state, const WmWindow *owner,
  const WmWindow *window
) {
  const WmWindow *up = wmFind(state, window->ownerWindow);

  while (up != NULL) {
    if (up->id == owner->id) {
      return true;
    }

    up = wmFind(state, up->ownerWindow);
  }

  return false;
}

/// @fn static WmWindow* wmModalFor(WmState *state, WmWindow *window)
///
/// @brief Find the modal window that takes a window's input instead of it:
/// the frontmost shown modal window of its family that neither is it nor
/// owns it.
///
/// @param state The window manager's state.
/// @param window The window.
///
/// @return Returns the modal window, or NULL if the window takes its own
/// input.
WM_SHARED WmWindow* wmModalFor(WmState *state, WmWindow *window) {
  WmWindow *root = wmRoot(state, window);

  for (int ii = state->numWindows - 1; ii >= 0; ii--) {
    WmWindow *modal = &state->windows[ii];
    if (((modal->flags & WIN_FLAG_MODAL) != 0)
      && (modal->state != WIN_STATE_MINIMIZED)
      && wmOwns(state, root, modal)
    ) {
      if ((modal == window) || wmOwns(state, modal, window)) {
        return NULL;
      }

      return modal;
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

/// @fn static void wmWidgetRect(const WmWindow *window,
///   const WmWidget *widget, GfxRect *rect)
///
/// @brief Find where a widget is on the screen:  its template rectangle,
/// kept as far from the body's right or bottom edge as it was if it is
/// anchored to that edge, and stretched if it is anchored to both of an
/// axis's edges.
///
/// @param window The widget's window.
/// @param widget The widget.
/// @param rect Where to store the widget's rectangle, in screen coordinates.
///
/// @return This function returns no value.
WM_SHARED void wmWidgetRect(const WmWindow *window, const WmWidget *widget,
  GfxRect *rect
) {
  GfxRect body;
  wmBody(window, &body);
  *rect = widget->rect;

  if ((widget->anchor & WIN_ANCHOR_RIGHT) != 0) {
    int16_t right = body.w - widget->marginRight;

    if ((widget->anchor & WIN_ANCHOR_LEFT) != 0) {
      rect->w = right - rect->x;
    } else {
      rect->x = right - rect->w;
    }
  }
  if ((widget->anchor & WIN_ANCHOR_BOTTOM) != 0) {
    int16_t bottom = body.h - widget->marginBottom;

    if ((widget->anchor & WIN_ANCHOR_TOP) != 0) {
      rect->h = bottom - rect->y;
    } else {
      rect->y = bottom - rect->h;
    }
  }

  rect->x += body.x;
  rect->y += body.y;
}

/// @fn static WmWidget* wmWidgetById(WmWidgets *widgets, uint8_t id)
///
/// @brief Find a window's widget by its identifier.
///
/// @param widgets The window's widgets, or NULL.
/// @param id The identifier, which isn't 0.
///
/// @return Returns the widget, or NULL if the window has none of that
/// identifier.
WM_SHARED WmWidget* wmWidgetById(WmWidgets *widgets, uint8_t id) {
  for (uint8_t ii = 0; (widgets != NULL) && (ii < widgets->count); ii++) {
    if (widgets->widget[ii].id == id) {
      return &widgets->widget[ii];
    }
  }

  return NULL;
}

/// @fn WM_SHARED void wmDamageWidget(WmState *state, const WmWindow *window,
///   const WmWidget *widget)
///
/// @brief Mark where a widget is to be painted again.
///
/// @param state The window manager's state.
/// @param window The widget's window.
/// @param widget The widget.
///
/// @return This function returns no value.
WM_SHARED void wmDamageWidget(WmState *state, const WmWindow *window,
  const WmWidget *widget
) {
  GfxRect rect;
  wmWidgetRect(window, widget, &rect);
  wmDamageOwned(state, &rect, window->id);
}

/// @fn static void wmReleaseChanged(WmState *state)
///
/// @brief Send the WIN_EVENT_CHANGED held for a text field now, with its
/// text's length, so that it reaches its owner before what follows.
///
/// @param state The window manager's state.
///
/// @return This function returns no value.
WM_SHARED void wmReleaseChanged(WmState *state) {
  WmWindow *window = wmFind(state, state->heldWindow);
  WmWidget *field = (window != NULL)
    ? wmWidgetById((WmWidgets*) window->widgets, state->heldWidget) : NULL;

  if (field != NULL) {
    wmChanged(state, window, field, field->textLength);
  }

  state->heldWindow = 0;
}

/// @fn static uint16_t wmTextStorage(const WmWidget *widget)
///
/// @brief Find the bytes a widget's text takes on the heap:  a text field's
/// most, which its storage holds from the start, a list's items, and any
/// other widget's text.
///
/// @param widget The widget.
///
/// @return Returns the bytes.
WM_SHARED uint16_t wmTextStorage(const WmWidget *widget) {
  if (widget->kind == WIN_WIDGET_TEXT_FIELD) {
    return widget->maxLength;
  } else if ((widget->kind == WIN_WIDGET_LIST)
    || (widget->kind == WIN_WIDGET_MENU_BAR)
    || (widget->kind == WIN_WIDGET_CANVAS)
  ) {
    return widget->itemsSize;
  }

  return widget->textLength;
}

/// @fn static uint16_t wmListRows(const GfxRect *rect)
///
/// @brief Find how many rows of items a list of a size shows.
///
/// @param rect The list's rectangle.
///
/// @return Returns the number of rows.
WM_SHARED uint16_t wmListRows(const GfxRect *rect) {
  int16_t rows = (rect->h - 2) / WM_LIST_ITEM_HEIGHT;

  return (rows < 1) ? 1 : (uint16_t) rows;
}

/// @fn static void wmShowSelection(const WmWindow *window, WmWidget *list)
///
/// @brief Scroll a list as little as shows its selection, and no further
/// than shows its last item at the bottom.
///
/// @param window The list's window.
/// @param list The list.
///
/// @return This function returns no value.
WM_SHARED void wmShowSelection(const WmWindow *window, WmWidget *list) {
  GfxRect rect;
  wmWidgetRect(window, list, &rect);
  uint16_t rows = wmListRows(&rect);

  if (list->value != WIN_NO_ITEM) {
    if (list->value < list->top) {
      list->top = list->value;
    } else if (list->value >= (list->top + rows)) {
      list->top = (uint16_t) (list->value - rows + 1);
    }
  }

  if ((list->top + rows) > list->numItems) {
    list->top = (list->numItems > rows) ? (uint16_t) (list->numItems - rows)
      : 0;
  }
}

/// @def WM_FIELD_INSET
///
/// @brief How far in from a text field's edge its text starts:  its border
/// and a 3-pixel margin.
#define WM_FIELD_INSET 4

/// @fn static uint8_t wmFieldScroll(const WmState *state,
///   const WmWindow *window, const WmWidget *widget)
///
/// @brief Find the first byte of a text field's text to show:  its scroll,
/// moved as little as keeps the caret inside the field.
///
/// @param state The window manager's state.
/// @param window The field's window.
/// @param widget The text field.
///
/// @return Returns the byte.
WM_SHARED uint8_t wmFieldScroll(const WmState *state, const WmWindow *window,
  const WmWidget *widget
) {
  GfxRect rect;
  wmWidgetRect(window, widget, &rect);
  int16_t shown = (rect.w - (2 * WM_FIELD_INSET))
    / state->display.builtInFont.glyphW;
  if (shown < 1) {
    shown = 1;
  }

  uint8_t scroll = widget->scroll;
  if (widget->caret < scroll) {
    scroll = widget->caret;
  } else if ((widget->caret - scroll) > shown) {
    scroll = (uint8_t) (widget->caret - shown);
  }

  return scroll;
}

/// @fn static int32_t wmGridBytes(uint8_t columns, uint8_t rows)
///
/// @brief Find the bytes of the heap a text grid of a size takes, which its
/// window's owner is charged.
///
/// @param columns The grid's columns.
/// @param rows The grid's rows.
///
/// @return Returns the bytes.
WM_SHARED int32_t wmGridBytes(uint8_t columns, uint8_t rows) {
  return ((int32_t) sizeof(WmGrid))
    + (((int32_t) columns) * rows * WM_GRID_PLANES);
}

/// @fn static bool wmCharge(WmState *state, ProcessId pid, int32_t bytes)
///
/// @brief Charge a client for bytes of the heap its windows take, or refund
/// it for a negative number, unless the charge would take it past the
/// budget.  The window server's own windows are free.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
/// @param bytes The bytes to charge.
///
/// @return Returns true if the bytes were charged, false if they would take
/// the client past its budget.
WM_SHARED bool wmCharge(WmState *state, ProcessId pid, int32_t bytes) {
  for (uint8_t ii = 0; ii < state->numClients; ii++) {
    WmClient *client = state->clients[ii];
    if (client->pid != pid) {
      continue;
    }

    int32_t used = ((int32_t) client->used) + bytes;
    if ((bytes > 0) && (used > state->budget)) {
      return false;
    }

    client->used = (uint16_t) ((used < 0) ? 0 : used);

    return true;
  }

  return true;
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
    = grid->left + (grid->columns * state->display.builtInFont.glyphW);
  int16_t cellsBottom
    = grid->top + (grid->rows * state->display.builtInFont.glyphH);

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

/// @fn static void wmGridCursor(WmGrid *grid)
///
/// @brief Swap the colors of the cell under a text grid's cursor, if it is
/// shown, which is how the cursor is drawn:  once after a change to the grid,
/// and again before the next, to take it away.
///
/// @param grid The grid.
///
/// @return This function returns no value.
WM_SHARED void wmGridCursor(WmGrid *grid) {
  if (grid->cursorShown && (grid->cursorRow < grid->rows)
    && (grid->cursorColumn < grid->columns)
  ) {
    uint8_t *attribute = &grid->cells[(((size_t) grid->rows) * grid->columns)
      + (((size_t) grid->cursorRow) * grid->columns) + grid->cursorColumn];
    *attribute = WIN_ATTRIBUTE(WIN_ATTRIBUTE_BACKGROUND(*attribute),
      WIN_ATTRIBUTE_FOREGROUND(*attribute));
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
