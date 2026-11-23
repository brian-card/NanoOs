///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.22.2026
///
/// @file              WindowProtocol.h
///
/// @brief             The messages, structures, and packed records that a
///                    window server and its client processes exchange, with
///                    nothing that needs the process API.
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

#ifndef WINDOW_PROTOCOL_H
#define WINDOW_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "GraphicsProtocol.h"

#ifdef __cplusplus
extern "C" {
#endif


/// @def WIN_COMMAND_SIGNATURE
///
/// @brief 64-bit, little-endian value to use as a signature to indicate to a
/// window server or its client that a message is part of this protocol.
/// This is "\0WINDOWS" expressed as a little-endian value.
#define WIN_COMMAND_SIGNATURE ((int64_t) 0x53574F444E495700)


// =========================================================================
// ERROR CODES
//
// Every value is below 256, because a message's status is a uint8_t.
// =========================================================================

typedef enum WinError {
  WIN_OK = 0,
  WIN_ERR_DISCONNECTED,       // the window server is gone or never answered
  WIN_ERR_PERMISSION,         // the sender may not do this
  WIN_ERR_NO_MEMORY,          // the server or the client's budget is full
  WIN_ERR_INVALID,            // a malformed request or template
  WIN_ERR_NOT_FOUND,          // no such window, widget, or dialog
  WIN_ERR_NOT_SUPPORTED,      // not available in this window server
} WinError;


// =========================================================================
// MESSAGE TYPES
//
// Each value is the low byte of a message's type, under
// WIN_COMMAND_SIGNATURE.  Each synchronous message's arguments start with a
// uint8_t status the server sets; the client presets it to
// WIN_ERR_DISCONNECTED, which is what remains if the server ends without
// answering.  Every buffer an argument points at is shared with the server
// and copied by it, so the client may reuse it once the message is done.
// =========================================================================

typedef enum WinMsgType {
  // Client to window server
  WIN_MSG_HELLO = 0,          // sync, WinHelloArgs
  WIN_MSG_GOODBYE,            // one-way, no arguments
  WIN_MSG_CREATE_WINDOW,      // sync, WinCreateWindowArgs
  WIN_MSG_DESTROY_WINDOW,     // one-way, WinWindowArgs
  WIN_MSG_SET_WINDOW,         // sync, WinSetWindowArgs
  WIN_MSG_ADD_WIDGETS,        // sync, WinAddWidgetsArgs
  WIN_MSG_REMOVE_WIDGET,      // one-way, WinWidgetArgs
  WIN_MSG_SET_WIDGET,         // sync, WinSetWidgetArgs
  WIN_MSG_GET_WIDGET,         // sync, WinGetWidgetArgs
  WIN_MSG_SET_CANVAS,         // sync, WinBufferArgs of Graphics.h records
  WIN_MSG_GRID_WRITE,         // sync, WinBufferArgs of grid operations
  WIN_MSG_OPEN_DIALOG,        // sync, WinOpenDialogArgs
  WIN_MSG_DIALOG_LISTING,     // sync, WinDialogListingArgs
  WIN_MSG_TASK_WINDOW,        // sync, WinTaskWindowArgs; taskbar role only
  WIN_MSG_END_SESSION,        // one-way, no arguments; taskbar role only

  WIN_MSG_COUNT,

  // Window server to client
  WIN_MSG_EVENTS = 24,        // persistent round trip, WinEventsArgs
  WIN_MSG_DISCONNECTED = 25,  // one-way, no arguments

  // Scheduler to window server, one-way:  a connected client ended.  The data
  // pointer is the client's ProcessId, not a pointer to storage.
  WIN_MSG_CLIENT_ENDED = 31,
} WinMsgType;

/// @def WIN_NUM_MESSAGE_TYPES
///
/// @brief One past the highest WinMsgType value, which is what an IPC
/// capability's message-type mask has to hold.  The types are not
/// contiguous, so this is not a count.
#define WIN_NUM_MESSAGE_TYPES (WIN_MSG_CLIENT_ENDED + 1)

/// @def WIN_CLIENT_MESSAGE_MASK
///
/// @brief The message types a client may send the window server.
#define WIN_CLIENT_MESSAGE_MASK ((((uint32_t) 1) << WIN_MSG_COUNT) - 1)

/// @def WIN_SERVER_MESSAGE_MASK
///
/// @brief The message types the window server may send a client.
#define WIN_SERVER_MESSAGE_MASK \
  ((((uint32_t) 1) << WIN_MSG_EVENTS) \
  | (((uint32_t) 1) << WIN_MSG_DISCONNECTED))

GFX_STATIC_ASSERT(WIN_MSG_COUNT <= WIN_MSG_EVENTS,
  "Every client message type must be below the window server's");
GFX_STATIC_ASSERT(WIN_MSG_DISCONNECTED < WIN_MSG_CLIENT_ENDED,
  "Every window server message type must be below WIN_MSG_CLIENT_ENDED");
GFX_STATIC_ASSERT(WIN_NUM_MESSAGE_TYPES <= 32,
  "An IPC capability holds 32 message types");
GFX_STATIC_ASSERT((WIN_CLIENT_MESSAGE_MASK & WIN_SERVER_MESSAGE_MASK) == 0,
  "A window message type can't go both ways");
GFX_STATIC_ASSERT(
  ((WIN_CLIENT_MESSAGE_MASK | WIN_SERVER_MESSAGE_MASK)
    & (((uint32_t) 1) << WIN_MSG_CLIENT_ENDED)) == 0,
  "Only the scheduler may send WIN_MSG_CLIENT_ENDED");


// =========================================================================
// WINDOWS
// =========================================================================

/// @def WIN_NONE
///
/// @brief The window or widget identifier that names none.  The window
/// server assigns window identifiers; a client assigns its widgets'
/// identifiers, unique within their window.
#define WIN_NONE 0

/// @def WIN_TITLE_MAX
///
/// @brief The longest window title, in bytes.
#define WIN_TITLE_MAX 63

typedef enum WinLayer {
  WIN_LAYER_DESKTOP = 0,      // at most one, always at the bottom
  WIN_LAYER_NORMAL,
  WIN_LAYER_TOPMOST,          // above every normal window, such as a taskbar
  WIN_LAYER_POPUP,            // above everything; closes on a press outside
} WinLayer;

// A window's flags.
#define WIN_FLAG_DECORATED      0x01  // a border and a title bar
#define WIN_FLAG_IN_TASKBAR     0x02  // listed to the taskbar
#define WIN_FLAG_RESIZABLE      0x04
#define WIN_FLAG_MODAL          0x08  // its owner takes no input while open
#define WIN_FLAG_RESERVE_TOP    0x10  // takes its height from the work area
#define WIN_FLAG_RESERVE_BOTTOM 0x20
#define WIN_FLAG_RESERVE_LEFT   0x40  // takes its width from the work area
#define WIN_FLAG_RESERVE_RIGHT  0x80

typedef enum WinState {
  WIN_STATE_NORMAL = 0,
  WIN_STATE_MINIMIZED,
  WIN_STATE_MAXIMIZED,
} WinState;

typedef enum WinRole {
  WIN_ROLE_NONE = 0,
  WIN_ROLE_DESKTOP,
  WIN_ROLE_TASKBAR,
} WinRole;


// =========================================================================
// MESSAGE ARGUMENTS
//
// Synchronous arguments are inputs, then outputs.  The server writes only
// the outputs.
// =========================================================================

typedef struct WinHelloArgs {
  uint8_t  status;
  uint8_t  role;                // out: WinRole
  uint8_t  cellWidth;           // out: the default font's cell
  uint8_t  cellHeight;          // out
  uint8_t  titleHeight;         // out: the theme's sizes, in pixels
  uint8_t  borderWidth;         // out
  uint8_t  buttonHeight;        // out
  uint8_t  scrollBarWidth;      // out
  uint16_t screenWidth;         // out
  uint16_t screenHeight;        // out
  GfxRect  workArea;            // out: the screen less reserved strips
} WinHelloArgs;

typedef struct WinCreateWindowArgs {
  uint8_t        status;
  uint8_t        layer;         // in: WinLayer
  uint8_t        flags;         // in: WIN_FLAG_* bits
  uint8_t        state;         // in: WinState
  uint8_t        owner;         // in: the caller's window that owns this
                                // one, or WIN_NONE
  uint8_t        titleLength;   // in: bytes at title, at most WIN_TITLE_MAX
  uint16_t       templateSize;  // in: bytes at widgets
  uint8_t        window;        // out: the new window's identifier
  GfxRect        frame;         // in: screen coordinates, decorations
                                // included
  const char    *title;         // in
  const uint8_t *widgets;       // in: a template of widget records, or NULL
} WinCreateWindowArgs;

typedef struct WinWindowArgs {
  uint8_t window;
} WinWindowArgs;

// What a WIN_MSG_SET_WINDOW changes.
#define WIN_SET_TITLE 0x01
#define WIN_SET_STATE 0x02
#define WIN_SET_FRAME 0x04

typedef struct WinSetWindowArgs {
  uint8_t     status;
  uint8_t     window;           // in
  uint8_t     changes;          // in: WIN_SET_* bits
  uint8_t     state;            // in: WinState, with WIN_SET_STATE
  uint8_t     titleLength;      // in: with WIN_SET_TITLE
  GfxRect     frame;            // in: with WIN_SET_FRAME
  const char *title;            // in: with WIN_SET_TITLE
} WinSetWindowArgs;

typedef struct WinAddWidgetsArgs {
  uint8_t        status;
  uint8_t        window;        // in
  uint16_t       templateSize;  // in: bytes at widgets
  const uint8_t *widgets;       // in: a template of widget records
} WinAddWidgetsArgs;

typedef struct WinWidgetArgs {
  uint8_t window;
  uint8_t widget;
} WinWidgetArgs;

typedef enum WinProperty {
  WIN_PROP_TEXT = 0,            // data: the text
  WIN_PROP_VALUE,               // value: a box's state, a list's selection
  WIN_PROP_ITEMS,               // data: a list's NUL-separated items
  WIN_PROP_ENABLED,             // value: 0 or 1
  WIN_PROP_HIDDEN,              // value: 0 or 1
  WIN_PROP_FOCUS,               // no value:  give the widget the focus
  WIN_PROP_RECT,                // rect: the widget's place in the body
} WinProperty;

typedef struct WinSetWidgetArgs {
  uint8_t        status;
  uint8_t        window;        // in
  uint8_t        widget;        // in
  uint8_t        property;      // in: WinProperty
  uint16_t       value;         // in
  uint16_t       size;          // in: bytes at data
  GfxRect        rect;          // in
  const uint8_t *data;          // in
} WinSetWidgetArgs;

typedef struct WinGetWidgetArgs {
  uint8_t  status;
  uint8_t  window;              // in:  a window of the caller's, or a stock
                                //      dialog it opened
  uint8_t  widget;              // in
  uint8_t  property;            // in: WIN_PROP_TEXT or WIN_PROP_VALUE
  uint16_t capacity;            // in: bytes at data
  uint16_t value;               // out
  uint16_t length;              // out: the text's length, which may exceed
                                //      capacity; only capacity bytes are
                                //      written, with no terminator
  uint8_t *data;                // in: where the text goes
} WinGetWidgetArgs;

typedef struct WinBufferArgs {
  uint8_t        status;
  uint8_t        window;        // in
  uint8_t        widget;        // in: a canvas or a text grid
  uint16_t       size;          // in: bytes at data
  const uint8_t *data;          // in: Graphics.h records or grid operations
} WinBufferArgs;

typedef enum WinDialogKind {
  WIN_DIALOG_MESSAGE = 0,
  WIN_DIALOG_INPUT,
  WIN_DIALOG_OPEN_FILE,
  WIN_DIALOG_SAVE_FILE,
} WinDialogKind;

typedef enum WinDialogIcon {
  WIN_ICON_NONE = 0,
  WIN_ICON_INFORMATION,
  WIN_ICON_WARNING,
  WIN_ICON_ERROR,
  WIN_ICON_QUESTION,
} WinDialogIcon;

typedef enum WinDialogButtons {
  WIN_BUTTONS_OK = 0,
  WIN_BUTTONS_OK_CANCEL,
  WIN_BUTTONS_YES_NO,
  WIN_BUTTONS_YES_NO_CANCEL,
} WinDialogButtons;

// The button that ended a dialog, in a WIN_EVENT_DIALOG_RESULT's value.
// Cancel is also the result of the close button and Escape.
#define WIN_RESULT_CANCEL 0
#define WIN_RESULT_OK     1
#define WIN_RESULT_YES    2
#define WIN_RESULT_NO     3

typedef struct WinOpenDialogArgs {
  uint8_t        status;
  uint8_t        owner;         // in: the caller's window that owns it
  uint8_t        kind;          // in: WinDialogKind
  uint8_t        icon;          // in: WinDialogIcon, for a message
  uint8_t        buttons;       // in: WinDialogButtons, for a message
  uint8_t        dialog;        // out: the dialog's window identifier
  uint16_t       size;          // in: bytes at fields
  const uint8_t *fields;        // in: the title, then the text or prompt,
                                //     the initial text or path, and the
                                //     suffix, each a length byte and its
                                //     bytes
} WinOpenDialogArgs;

// The flags of a WinDialogListingArgs.
#define WIN_LISTING_FINAL 0x01  // the last part of the listing

typedef struct WinDialogListingArgs {
  uint8_t        status;
  uint8_t        dialog;        // in
  uint8_t        flags;         // in: WIN_LISTING_* bits
  uint8_t        errorNumber;   // in: why the directory couldn't be read,
                                //     or 0
  uint16_t       size;          // in: bytes at entries
  const uint8_t *entries;       // in: per entry, WIN_ENTRY_FILE or
                                //     WIN_ENTRY_DIRECTORY, a length byte,
                                //     and the name
} WinDialogListingArgs;

#define WIN_ENTRY_FILE      0
#define WIN_ENTRY_DIRECTORY 1

typedef enum WinTaskOperation {
  WIN_TASK_GET = 0,             // read the title and state
  WIN_TASK_ACTIVATE,            // restore if minimized, and activate
  WIN_TASK_TOGGLE,              // minimize if active, activate otherwise
} WinTaskOperation;

typedef struct WinTaskWindowArgs {
  uint8_t  status;
  uint8_t  window;              // in: any client's window
  uint8_t  operation;           // in: WinTaskOperation
  uint8_t  state;               // out: WinState
  uint8_t  active;              // out: 1 if it is the active window
  uint8_t  titleLength;         // out
  uint8_t  capacity;            // in: bytes at title
  char    *title;               // in: where the title goes, with no
                                //     terminator
} WinTaskWindowArgs;


// =========================================================================
// EVENTS  (window server to client)
//
// One WIN_MSG_EVENTS per client is in flight at a time, as a persistent
// round trip:  the server posts it once the client has marked the last one
// done.
// =========================================================================

/// @def WIN_EVENTS_PER_MESSAGE
///
/// @brief The number of events one WIN_MSG_EVENTS carries.
#define WIN_EVENTS_PER_MESSAGE 8

typedef enum WinEventType {
  WIN_EVENT_CLOSE_REQUESTED = 0,
  WIN_EVENT_RESIZED,            // x, y: the body's width and height;
                                // columns, rows: a text grid's
  WIN_EVENT_STATE,              // detail: the new WinState
  WIN_EVENT_FOCUS,              // detail: 1 activated, 0 deactivated
  WIN_EVENT_COMMAND,            // widget, value
  WIN_EVENT_CHANGED,            // widget, value
  WIN_EVENT_KEY,                // key, ascii, modifiers, detail: 1 down
  WIN_EVENT_POINTER,            // widget, x, y, buttons, detail: wheel
  WIN_EVENT_SESSION_ENDED,      // widget: the text grid
  WIN_EVENT_DIALOG_RESULT,      // window: the dialog; value: WIN_RESULT_*
  WIN_EVENT_DIALOG_LIST_REQUEST, // window: the dialog; the path is read
                                 // with WIN_MSG_GET_WIDGET
  WIN_EVENT_WINDOW_LIST,        // window, detail: WinListChange
  WIN_EVENT_OVERFLOW,           // events were dropped after this one
} WinEventType;

typedef enum WinListChange {
  WIN_LIST_ADDED = 0,
  WIN_LIST_REMOVED,
  WIN_LIST_RETITLED,
  WIN_LIST_STATE,
  WIN_LIST_ACTIVATED,
} WinListChange;

typedef struct WinEvent {
  uint8_t  type;                // WinEventType
  uint8_t  window;
  uint8_t  widget;
  uint8_t  detail;
  uint8_t  key;                 // GfxKey
  uint8_t  ascii;
  uint8_t  modifiers;           // GFX_MOD_* bits
  uint8_t  buttons;
  uint8_t  columns;
  uint8_t  rows;
  uint16_t value;
  int16_t  x, y;
} WinEvent;

typedef struct WinEventsArgs {
  uint8_t  count;
  WinEvent events[WIN_EVENTS_PER_MESSAGE];
} WinEventsArgs;


// =========================================================================
// TEMPLATES  (packed widget records)
//
// A template is a sequence of widget records, each a header followed by its
// kind's fields, packed with no padding, every multi-byte value
// little-endian.  A text field is a length byte and that many bytes, with no
// terminator.
// =========================================================================

typedef enum WinWidgetKind {
  WIN_WIDGET_LABEL = 0,         // align, text
  WIN_WIDGET_BUTTON,            // text
  WIN_WIDGET_TOGGLE,            // value, text
  WIN_WIDGET_CHECK_BOX,         // value, text
  WIN_WIDGET_RADIO,             // group, value, text
  WIN_WIDGET_TEXT_FIELD,        // maximum length, text
  WIN_WIDGET_LIST,              // uint16 selection, uint16 size, items
  WIN_WIDGET_GROUP,             // caption text
  WIN_WIDGET_CANVAS,            // no fields; records by WIN_MSG_SET_CANVAS
  WIN_WIDGET_TEXT_GRID,         // no fields; rows and columns from its size
  WIN_WIDGET_COUNT
} WinWidgetKind;

// A widget record's header.
#define WIN_RECORD_KIND   0     // uint8_t WinWidgetKind
#define WIN_RECORD_ID     1     // uint8_t, unique in the window
#define WIN_RECORD_FLAGS  2     // uint8_t WIN_WIDGET_* bits
#define WIN_RECORD_ANCHOR 3     // uint8_t WIN_ANCHOR_* bits
#define WIN_RECORD_RECT   4     // GfxRect in the body:  int16_t x, y, w, h
#define WIN_RECORD_HEADER_SIZE 12

GFX_STATIC_ASSERT(WIN_RECORD_RECT + 4 * sizeof(int16_t)
  == WIN_RECORD_HEADER_SIZE,
  "A widget record's header ends with its rectangle");

// A widget's flags.
#define WIN_WIDGET_DISABLED    0x01
#define WIN_WIDGET_HIDDEN      0x02
#define WIN_WIDGET_DEFAULT     0x04  // pressed by Enter
#define WIN_WIDGET_CANCEL      0x08  // pressed by Escape
#define WIN_WIDGET_ENDS_DIALOG 0x10  // ends a dialog run by winRunDialog
#define WIN_WIDGET_PASSWORD    0x20  // a text field that shows '*'
#define WIN_WIDGET_READ_ONLY   0x40  // a text field that can't be edited
#define WIN_WIDGET_SESSION     0x80  // a text grid bound to a console session

// A widget's anchors.  A widget anchored to both sides of an axis stretches.
#define WIN_ANCHOR_LEFT   0x01
#define WIN_ANCHOR_TOP    0x02
#define WIN_ANCHOR_RIGHT  0x04
#define WIN_ANCHOR_BOTTOM 0x08

// A label's alignment.
#define WIN_ALIGN_LEFT   0
#define WIN_ALIGN_CENTER 1
#define WIN_ALIGN_RIGHT  2

/// @def WIN_MAX_WIDGETS
///
/// @brief The most widgets one window holds.
#define WIN_MAX_WIDGETS 32

/// @def WIN_LIST_ITEMS_MAX
///
/// @brief The most bytes of items one list holds.
#define WIN_LIST_ITEMS_MAX 1024


// =========================================================================
// TEXT GRID OPERATIONS  (packed records in a WIN_MSG_GRID_WRITE)
//
// An attribute is a foreground ANSI colour index in its low 4 bits and a
// background one in its high 4 bits.
// =========================================================================

typedef enum WinGridOperation {
  WIN_GRID_PUT = 0,     // row, column, attribute, length, length bytes
  WIN_GRID_CLEAR,       // top, left, bottom, right, attribute
  WIN_GRID_SCROLL,      // top, bottom, int8_t count (up if positive),
                        // attribute for the rows uncovered
  WIN_GRID_CURSOR,      // row, column, shown
  WIN_GRID_REPORTS,     // 1 if the program asked for resize reports
} WinGridOperation;

// The size of each grid operation, its tag included.
#define WIN_GRID_PUT_SIZE(length) (5 + (length))
#define WIN_GRID_CLEAR_SIZE       6
#define WIN_GRID_SCROLL_SIZE      5
#define WIN_GRID_CURSOR_SIZE      4
#define WIN_GRID_REPORTS_SIZE     2


#ifdef __cplusplus
}
#endif

#endif // WINDOW_PROTOCOL_H
