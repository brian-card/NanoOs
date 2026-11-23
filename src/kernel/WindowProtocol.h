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

/// @enum WinError
///
/// @brief The window server's error codes.  Every value is below 256,
/// because a message's status is a uint8_t.
///
/// @param WIN_OK No error.
/// @param WIN_ERR_DISCONNECTED The window server is gone or never answered.
/// @param WIN_ERR_PERMISSION The sender may not do this.
/// @param WIN_ERR_NO_MEMORY The server or the client's budget is full.
/// @param WIN_ERR_INVALID A malformed request or template.
/// @param WIN_ERR_NOT_FOUND No such window, widget, or dialog.
/// @param WIN_ERR_NOT_SUPPORTED Not available in this window server.
typedef enum WinError {
  WIN_OK = 0,
  WIN_ERR_DISCONNECTED,
  WIN_ERR_PERMISSION,
  WIN_ERR_NO_MEMORY,
  WIN_ERR_INVALID,
  WIN_ERR_NOT_FOUND,
  WIN_ERR_NOT_SUPPORTED,
} WinError;

/// @enum WinMsgType
///
/// @brief The low byte of each window message's type, under
/// WIN_COMMAND_SIGNATURE.
///
/// @note Each synchronous message's arguments start with a uint8_t status the
/// server sets.  The client presets it to WIN_ERR_DISCONNECTED, which is what
/// remains if the server ends without answering.
///
/// @note Synchronous arguments are inputs, then outputs, and the server writes
/// only the outputs.  Every buffer an argument points at is shared with the
/// server and copied by it, so the client may reuse it once the message is
/// done.
///
/// @param WIN_MSG_HELLO Sync, WinHelloArgs.
/// @param WIN_MSG_GOODBYE One-way, no arguments.
/// @param WIN_MSG_CREATE_WINDOW Sync, WinCreateWindowArgs.
/// @param WIN_MSG_DESTROY_WINDOW One-way, WinWindowArgs.
/// @param WIN_MSG_SET_WINDOW Sync, WinSetWindowArgs.
/// @param WIN_MSG_ADD_WIDGETS Sync, WinAddWidgetsArgs.
/// @param WIN_MSG_REMOVE_WIDGET One-way, WinWidgetArgs.
/// @param WIN_MSG_SET_WIDGET Sync, WinSetWidgetArgs.
/// @param WIN_MSG_GET_WIDGET Sync, WinGetWidgetArgs.
/// @param WIN_MSG_SET_CANVAS Sync, WinBufferArgs of Graphics.h records.
/// @param WIN_MSG_GRID_WRITE Sync, WinBufferArgs of grid operations.
/// @param WIN_MSG_OPEN_DIALOG Sync, WinOpenDialogArgs.
/// @param WIN_MSG_DIALOG_LISTING Sync, WinDialogListingArgs.
/// @param WIN_MSG_TASK_WINDOW Sync, WinTaskWindowArgs; taskbar role only.
/// @param WIN_MSG_END_SESSION One-way, no arguments; taskbar role only.
/// @param WIN_MSG_COUNT The number of client message types.
/// @param WIN_MSG_EVENTS Persistent round trip, WinEventsArgs.  One is in
///   flight per client at a time:  the server posts it once the client has
///   marked the last one done.
/// @param WIN_MSG_DISCONNECTED One-way, no arguments.
/// @param WIN_MSG_CLIENT_ENDED One-way:  a connected client ended.  The data
///   pointer is the client's ProcessId, not a pointer to storage.
typedef enum WinMsgType {
  // Client to window server:
  WIN_MSG_HELLO = 0,
  WIN_MSG_GOODBYE,
  WIN_MSG_CREATE_WINDOW,
  WIN_MSG_DESTROY_WINDOW,
  WIN_MSG_SET_WINDOW,
  WIN_MSG_ADD_WIDGETS,
  WIN_MSG_REMOVE_WIDGET,
  WIN_MSG_SET_WIDGET,
  WIN_MSG_GET_WIDGET,
  WIN_MSG_SET_CANVAS,
  WIN_MSG_GRID_WRITE,
  WIN_MSG_OPEN_DIALOG,
  WIN_MSG_DIALOG_LISTING,
  WIN_MSG_TASK_WINDOW,
  WIN_MSG_END_SESSION,

  WIN_MSG_COUNT,

  // Window server to client:
  WIN_MSG_EVENTS = 24,
  WIN_MSG_DISCONNECTED = 25,

  // Scheduler to window server:
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

/// @enum WinLayer
///
/// @brief The layer a window is in, which orders it among the others.
///
/// @param WIN_LAYER_DESKTOP At most one window, always at the bottom.
/// @param WIN_LAYER_NORMAL An application's window.
/// @param WIN_LAYER_TOPMOST Above every normal window, such as a taskbar.
/// @param WIN_LAYER_POPUP Above everything; closes on a press outside it.
typedef enum WinLayer {
  WIN_LAYER_DESKTOP = 0,
  WIN_LAYER_NORMAL,
  WIN_LAYER_TOPMOST,
  WIN_LAYER_POPUP,
} WinLayer;

/// @def WIN_FLAG_DECORATED
///
/// @brief A window flag:  a border and a title bar.
#define WIN_FLAG_DECORATED      0x01
/// @def WIN_FLAG_IN_TASKBAR
///
/// @brief A window flag:  listed to the taskbar.
#define WIN_FLAG_IN_TASKBAR     0x02
/// @def WIN_FLAG_RESIZABLE
///
/// @brief A window flag:  the user may resize it.
#define WIN_FLAG_RESIZABLE      0x04
/// @def WIN_FLAG_MODAL
///
/// @brief A window flag:  its owner takes no input while it is open.
#define WIN_FLAG_MODAL          0x08
/// @def WIN_FLAG_RESERVE_TOP
///
/// @brief A window flag:  it takes its height from the top of the work
/// area.
#define WIN_FLAG_RESERVE_TOP    0x10
/// @def WIN_FLAG_RESERVE_BOTTOM
///
/// @brief A window flag:  it takes its height from the bottom of the
/// work area.
#define WIN_FLAG_RESERVE_BOTTOM 0x20
/// @def WIN_FLAG_RESERVE_LEFT
///
/// @brief A window flag:  it takes its width from the left of the work
/// area.
#define WIN_FLAG_RESERVE_LEFT   0x40
/// @def WIN_FLAG_RESERVE_RIGHT
///
/// @brief A window flag:  it takes its width from the right of the work
/// area.
#define WIN_FLAG_RESERVE_RIGHT  0x80

/// @enum WinState
///
/// @brief The state of a window.
typedef enum WinState {
  WIN_STATE_NORMAL = 0,
  WIN_STATE_MINIMIZED,
  WIN_STATE_MAXIMIZED,
} WinState;

/// @enum WinRole
///
/// @brief The role the window server gives a client at WIN_MSG_HELLO.
///
/// @param WIN_ROLE_NONE An ordinary application.
/// @param WIN_ROLE_DESKTOP The desktop.
/// @param WIN_ROLE_TASKBAR The taskbar, which alone may send
///   WIN_MSG_TASK_WINDOW and WIN_MSG_END_SESSION.
typedef enum WinRole {
  WIN_ROLE_NONE = 0,
  WIN_ROLE_DESKTOP,
  WIN_ROLE_TASKBAR,
} WinRole;

/// @struct WinHelloArgs
///
/// @brief Arguments and return values for WIN_MSG_HELLO.
///
/// @param status The WinError the server sets.
/// @param role Out:  the WinRole.
/// @param cellWidth Out:  the width of the default font's cell.
/// @param cellHeight Out:  the height of the default font's cell.
/// @param titleHeight Out:  the theme's title bar height, in pixels.
/// @param borderWidth Out:  the theme's border width, in pixels.
/// @param buttonHeight Out:  the theme's button height, in pixels.
/// @param scrollBarWidth Out:  the theme's scroll bar width, in pixels.
/// @param screenWidth Out:  the screen's width.
/// @param screenHeight Out:  the screen's height.
/// @param workArea Out:  the screen less its reserved strips.
typedef struct WinHelloArgs {
  uint8_t  status;
  uint8_t  role;
  uint8_t  cellWidth;
  uint8_t  cellHeight;
  uint8_t  titleHeight;
  uint8_t  borderWidth;
  uint8_t  buttonHeight;
  uint8_t  scrollBarWidth;
  uint16_t screenWidth;
  uint16_t screenHeight;
  GfxRect  workArea;
} WinHelloArgs;

/// @struct WinCreateWindowArgs
///
/// @brief Arguments and return values for WIN_MSG_CREATE_WINDOW.
///
/// @param status The WinError the server sets.
/// @param layer In:  the WinLayer.
/// @param flags In:  the WIN_FLAG_* bits.
/// @param state In:  the WinState.
/// @param owner In:  the caller's window that owns this one, or WIN_NONE.
/// @param titleLength In:  the bytes at title, at most WIN_TITLE_MAX.
/// @param templateSize In:  the bytes at widgets.
/// @param window Out:  the new window's identifier.
/// @param frame In:  the window's place in screen coordinates, decorations
///   included.
/// @param title In:  the title, with no terminator.
/// @param widgets In:  a template of widget records, or NULL.
typedef struct WinCreateWindowArgs {
  uint8_t        status;
  uint8_t        layer;
  uint8_t        flags;
  uint8_t        state;
  uint8_t        owner;
  uint8_t        titleLength;
  uint16_t       templateSize;
  uint8_t        window;
  GfxRect        frame;
  const char    *title;
  const uint8_t *widgets;
} WinCreateWindowArgs;

/// @struct WinWindowArgs
///
/// @brief Arguments for WIN_MSG_DESTROY_WINDOW.
///
/// @param window The window.
typedef struct WinWindowArgs {
  uint8_t window;
} WinWindowArgs;

/// @def WIN_SET_TITLE
///
/// @brief A WIN_MSG_SET_WINDOW change:  the title.
#define WIN_SET_TITLE 0x01
/// @def WIN_SET_STATE
///
/// @brief A WIN_MSG_SET_WINDOW change:  the state.
#define WIN_SET_STATE 0x02
/// @def WIN_SET_FRAME
///
/// @brief A WIN_MSG_SET_WINDOW change:  the frame.
#define WIN_SET_FRAME 0x04

/// @struct WinSetWindowArgs
///
/// @brief Arguments and return values for WIN_MSG_SET_WINDOW.
///
/// @param status The WinError the server sets.
/// @param window In:  the window.
/// @param changes In:  the WIN_SET_* bits.
/// @param state In:  the WinState, with WIN_SET_STATE.
/// @param titleLength In:  the bytes at title, with WIN_SET_TITLE.
/// @param frame In:  the new frame, with WIN_SET_FRAME.
/// @param title In:  the new title, with WIN_SET_TITLE.
typedef struct WinSetWindowArgs {
  uint8_t     status;
  uint8_t     window;
  uint8_t     changes;
  uint8_t     state;
  uint8_t     titleLength;
  GfxRect     frame;
  const char *title;
} WinSetWindowArgs;

/// @struct WinAddWidgetsArgs
///
/// @brief Arguments and return values for WIN_MSG_ADD_WIDGETS.
///
/// @param status The WinError the server sets.
/// @param window In:  the window.
/// @param templateSize In:  the bytes at widgets.
/// @param widgets In:  a template of widget records.
typedef struct WinAddWidgetsArgs {
  uint8_t        status;
  uint8_t        window;
  uint16_t       templateSize;
  const uint8_t *widgets;
} WinAddWidgetsArgs;

/// @struct WinWidgetArgs
///
/// @brief Arguments for WIN_MSG_REMOVE_WIDGET.
///
/// @param window The window.
/// @param widget The widget.
typedef struct WinWidgetArgs {
  uint8_t window;
  uint8_t widget;
} WinWidgetArgs;

/// @enum WinProperty
///
/// @brief A widget property that WIN_MSG_SET_WIDGET or WIN_MSG_GET_WIDGET
/// names.
///
/// @param WIN_PROP_TEXT data:  the text.
/// @param WIN_PROP_VALUE value:  a box's state, or a list's selection.
/// @param WIN_PROP_ITEMS data:  a list's NUL-separated items.
/// @param WIN_PROP_ENABLED value:  0 or 1.
/// @param WIN_PROP_HIDDEN value:  0 or 1.
/// @param WIN_PROP_FOCUS No value:  give the widget the focus.
/// @param WIN_PROP_RECT rect:  the widget's place in the body.
typedef enum WinProperty {
  WIN_PROP_TEXT = 0,
  WIN_PROP_VALUE,
  WIN_PROP_ITEMS,
  WIN_PROP_ENABLED,
  WIN_PROP_HIDDEN,
  WIN_PROP_FOCUS,
  WIN_PROP_RECT,
} WinProperty;

/// @struct WinSetWidgetArgs
///
/// @brief Arguments and return values for WIN_MSG_SET_WIDGET.
///
/// @param status The WinError the server sets.
/// @param window In:  the window.
/// @param widget In:  the widget.
/// @param property In:  the WinProperty.
/// @param value In:  the value, for a property that takes one.
/// @param size In:  the bytes at data.
/// @param rect In:  the rectangle, for WIN_PROP_RECT.
/// @param data In:  the data, for a property that takes it.
typedef struct WinSetWidgetArgs {
  uint8_t        status;
  uint8_t        window;
  uint8_t        widget;
  uint8_t        property;
  uint16_t       value;
  uint16_t       size;
  GfxRect        rect;
  const uint8_t *data;
} WinSetWidgetArgs;

/// @struct WinGetWidgetArgs
///
/// @brief Arguments and return values for WIN_MSG_GET_WIDGET.
///
/// @param status The WinError the server sets.
/// @param window In:  a window of the caller's, or a stock dialog it opened.
/// @param widget In:  the widget.
/// @param property In:  WIN_PROP_TEXT or WIN_PROP_VALUE.
/// @param capacity In:  the bytes at data.
/// @param value Out:  the value, for WIN_PROP_VALUE.
/// @param length Out:  the text's length, which may exceed capacity.  Only
///   capacity bytes are written, with no terminator.
/// @param data In:  where the text goes.
typedef struct WinGetWidgetArgs {
  uint8_t  status;
  uint8_t  window;
  uint8_t  widget;
  uint8_t  property;
  uint16_t capacity;
  uint16_t value;
  uint16_t length;
  uint8_t *data;
} WinGetWidgetArgs;

/// @struct WinBufferArgs
///
/// @brief Arguments and return values for WIN_MSG_SET_CANVAS and
/// WIN_MSG_GRID_WRITE.
///
/// @param status The WinError the server sets.
/// @param window In:  the window.
/// @param widget In:  a canvas or a text grid.
/// @param size In:  the bytes at data.
/// @param data In:  Graphics.h records or grid operations.
typedef struct WinBufferArgs {
  uint8_t        status;
  uint8_t        window;
  uint8_t        widget;
  uint16_t       size;
  const uint8_t *data;
} WinBufferArgs;

/// @enum WinDialogKind
///
/// @brief The kind of stock dialog WIN_MSG_OPEN_DIALOG opens.
///
/// @param WIN_DIALOG_MESSAGE A message with an icon and buttons.
/// @param WIN_DIALOG_INPUT A prompt with a text field.
/// @param WIN_DIALOG_OPEN_FILE A file chooser for opening a file.
/// @param WIN_DIALOG_SAVE_FILE A file chooser for saving a file.
typedef enum WinDialogKind {
  WIN_DIALOG_MESSAGE = 0,
  WIN_DIALOG_INPUT,
  WIN_DIALOG_OPEN_FILE,
  WIN_DIALOG_SAVE_FILE,
} WinDialogKind;

/// @enum WinDialogIcon
///
/// @brief The icon of a WIN_DIALOG_MESSAGE.
typedef enum WinDialogIcon {
  WIN_ICON_NONE = 0,
  WIN_ICON_INFORMATION,
  WIN_ICON_WARNING,
  WIN_ICON_ERROR,
  WIN_ICON_QUESTION,
} WinDialogIcon;

/// @enum WinDialogButtons
///
/// @brief The buttons of a WIN_DIALOG_MESSAGE.
typedef enum WinDialogButtons {
  WIN_BUTTONS_OK = 0,
  WIN_BUTTONS_OK_CANCEL,
  WIN_BUTTONS_YES_NO,
  WIN_BUTTONS_YES_NO_CANCEL,
} WinDialogButtons;

/// @def WIN_RESULT_CANCEL
///
/// @brief A WIN_EVENT_DIALOG_RESULT value:  Cancel, the close button, or
/// Escape ended the dialog.
#define WIN_RESULT_CANCEL 0
/// @def WIN_RESULT_OK
///
/// @brief A WIN_EVENT_DIALOG_RESULT value:  OK ended the dialog.
#define WIN_RESULT_OK     1
/// @def WIN_RESULT_YES
///
/// @brief A WIN_EVENT_DIALOG_RESULT value:  Yes ended the dialog.
#define WIN_RESULT_YES    2
/// @def WIN_RESULT_NO
///
/// @brief A WIN_EVENT_DIALOG_RESULT value:  No ended the dialog.
#define WIN_RESULT_NO     3

/// @struct WinOpenDialogArgs
///
/// @brief Arguments and return values for WIN_MSG_OPEN_DIALOG.
///
/// @param status The WinError the server sets.
/// @param owner In:  the caller's window that owns the dialog.
/// @param kind In:  the WinDialogKind.
/// @param icon In:  the WinDialogIcon, for a message.
/// @param buttons In:  the WinDialogButtons, for a message.
/// @param dialog Out:  the dialog's window identifier.
/// @param size In:  the bytes at fields.
/// @param fields In:  the title, then the text or prompt, the initial text or
///   path, and the suffix, each a length byte and its bytes.
typedef struct WinOpenDialogArgs {
  uint8_t        status;
  uint8_t        owner;
  uint8_t        kind;
  uint8_t        icon;
  uint8_t        buttons;
  uint8_t        dialog;
  uint16_t       size;
  const uint8_t *fields;
} WinOpenDialogArgs;

/// @def WIN_LISTING_FINAL
///
/// @brief A WinDialogListingArgs flag:  the last part of the listing.
#define WIN_LISTING_FINAL 0x01

/// @struct WinDialogListingArgs
///
/// @brief Arguments and return values for WIN_MSG_DIALOG_LISTING.
///
/// @param status The WinError the server sets.
/// @param dialog In:  the dialog.
/// @param flags In:  the WIN_LISTING_* bits.
/// @param errorNumber In:  the errno value for why the directory couldn't be
///   read, or 0.
/// @param size In:  the bytes at entries.
/// @param entries In:  per entry, WIN_ENTRY_FILE or WIN_ENTRY_DIRECTORY, a
///   length byte, and the name.
typedef struct WinDialogListingArgs {
  uint8_t        status;
  uint8_t        dialog;
  uint8_t        flags;
  uint8_t        errorNumber;
  uint16_t       size;
  const uint8_t *entries;
} WinDialogListingArgs;

/// @def WIN_ENTRY_FILE
///
/// @brief A WinDialogListingArgs entry's kind:  a file.
#define WIN_ENTRY_FILE      0
/// @def WIN_ENTRY_DIRECTORY
///
/// @brief A WinDialogListingArgs entry's kind:  a directory.
#define WIN_ENTRY_DIRECTORY 1

/// @enum WinTaskOperation
///
/// @brief What a WIN_MSG_TASK_WINDOW does to the window it names.
///
/// @param WIN_TASK_GET Read the title and state.
/// @param WIN_TASK_ACTIVATE Restore it if minimized, and activate it.
/// @param WIN_TASK_TOGGLE Minimize it if it is active, and activate it
///   otherwise.
typedef enum WinTaskOperation {
  WIN_TASK_GET = 0,
  WIN_TASK_ACTIVATE,
  WIN_TASK_TOGGLE,
} WinTaskOperation;

/// @struct WinTaskWindowArgs
///
/// @brief Arguments and return values for WIN_MSG_TASK_WINDOW.
///
/// @param status The WinError the server sets.
/// @param window In:  any client's window.
/// @param operation In:  the WinTaskOperation.
/// @param state Out:  the WinState.
/// @param active Out:  1 if it is the active window.
/// @param titleLength Out:  the title's length.
/// @param capacity In:  the bytes at title.
/// @param title In:  where the title goes, with no terminator.
typedef struct WinTaskWindowArgs {
  uint8_t  status;
  uint8_t  window;
  uint8_t  operation;
  uint8_t  state;
  uint8_t  active;
  uint8_t  titleLength;
  uint8_t  capacity;
  char    *title;
} WinTaskWindowArgs;

/// @def WIN_EVENTS_PER_MESSAGE
///
/// @brief The number of events one WIN_MSG_EVENTS carries.
#define WIN_EVENTS_PER_MESSAGE 8

/// @enum WinEventType
///
/// @brief The kind of a WinEvent, and the fields it uses.
///
/// @param WIN_EVENT_CLOSE_REQUESTED The user asked to close the window.
/// @param WIN_EVENT_RESIZED x, y:  the body's width and height.  columns,
///   rows:  a text grid's.
/// @param WIN_EVENT_STATE detail:  the new WinState.
/// @param WIN_EVENT_FOCUS detail:  1 activated, 0 deactivated.
/// @param WIN_EVENT_COMMAND widget, value.
/// @param WIN_EVENT_CHANGED widget, value.
/// @param WIN_EVENT_KEY key, ascii, modifiers, and detail:  1 down.
/// @param WIN_EVENT_POINTER widget, x, y, buttons, and detail:  the wheel.
/// @param WIN_EVENT_SESSION_ENDED widget:  the text grid.
/// @param WIN_EVENT_DIALOG_RESULT window:  the dialog.  value:  a WIN_RESULT_*.
/// @param WIN_EVENT_DIALOG_LIST_REQUEST window:  the dialog.  The path is read
///   with WIN_MSG_GET_WIDGET.
/// @param WIN_EVENT_WINDOW_LIST window, and detail:  the WinListChange.
/// @param WIN_EVENT_OVERFLOW Events were dropped after this one.
typedef enum WinEventType {
  WIN_EVENT_CLOSE_REQUESTED = 0,
  WIN_EVENT_RESIZED,
  WIN_EVENT_STATE,
  WIN_EVENT_FOCUS,
  WIN_EVENT_COMMAND,
  WIN_EVENT_CHANGED,
  WIN_EVENT_KEY,
  WIN_EVENT_POINTER,
  WIN_EVENT_SESSION_ENDED,
  WIN_EVENT_DIALOG_RESULT,
  WIN_EVENT_DIALOG_LIST_REQUEST,
  WIN_EVENT_WINDOW_LIST,
  WIN_EVENT_OVERFLOW,
} WinEventType;

/// @enum WinListChange
///
/// @brief How a WIN_EVENT_WINDOW_LIST's window changed.
typedef enum WinListChange {
  WIN_LIST_ADDED = 0,
  WIN_LIST_REMOVED,
  WIN_LIST_RETITLED,
  WIN_LIST_STATE,
  WIN_LIST_ACTIVATED,
} WinListChange;

/// @struct WinEvent
///
/// @brief One event from the window server.  Which fields an event uses
/// depends on its WinEventType.
///
/// @param type The WinEventType.
/// @param window The window.
/// @param widget The widget.
/// @param detail A small value whose meaning the WinEventType gives.
/// @param key The GfxKey.
/// @param ascii The character the key types, or 0.
/// @param modifiers The GFX_MOD_* bits.
/// @param buttons The pointer's button bits.
/// @param columns A text grid's width, in cells.
/// @param rows A text grid's height, in cells.
/// @param value A widget's value, or a WIN_RESULT_*.
/// @param x The pointer's column, or the body's width.
/// @param y The pointer's row, or the body's height.
typedef struct WinEvent {
  uint8_t  type;
  uint8_t  window;
  uint8_t  widget;
  uint8_t  detail;
  uint8_t  key;
  uint8_t  ascii;
  uint8_t  modifiers;
  uint8_t  buttons;
  uint8_t  columns;
  uint8_t  rows;
  uint16_t value;
  int16_t  x, y;
} WinEvent;

/// @struct WinEventsArgs
///
/// @brief Arguments for WIN_MSG_EVENTS.
///
/// @param count The number of events.
/// @param events The events, oldest first.
typedef struct WinEventsArgs {
  uint8_t  count;
  WinEvent events[WIN_EVENTS_PER_MESSAGE];
} WinEventsArgs;

/// @enum WinWidgetKind
///
/// @brief The kind of a widget record, and the fields that follow its
/// header.
///
/// @note A template is a sequence of widget records, each a header followed by
/// its kind's fields, packed with no padding, every multi-byte value
/// little-endian.  A text field is a length byte and that many bytes, with no
/// terminator.
///
/// @param WIN_WIDGET_LABEL align, text.
/// @param WIN_WIDGET_BUTTON text.
/// @param WIN_WIDGET_TOGGLE value, text.
/// @param WIN_WIDGET_CHECK_BOX value, text.
/// @param WIN_WIDGET_RADIO group, value, text.
/// @param WIN_WIDGET_TEXT_FIELD maximum length, text.
/// @param WIN_WIDGET_LIST uint16 selection, uint16 size, items.
/// @param WIN_WIDGET_GROUP caption text.
/// @param WIN_WIDGET_CANVAS No fields; it draws records sent with
///   WIN_MSG_SET_CANVAS.
/// @param WIN_WIDGET_TEXT_GRID No fields; its rows and columns come from its
///   size.
/// @param WIN_WIDGET_COUNT The number of widget kinds.
typedef enum WinWidgetKind {
  WIN_WIDGET_LABEL = 0,
  WIN_WIDGET_BUTTON,
  WIN_WIDGET_TOGGLE,
  WIN_WIDGET_CHECK_BOX,
  WIN_WIDGET_RADIO,
  WIN_WIDGET_TEXT_FIELD,
  WIN_WIDGET_LIST,
  WIN_WIDGET_GROUP,
  WIN_WIDGET_CANVAS,
  WIN_WIDGET_TEXT_GRID,
  WIN_WIDGET_COUNT
} WinWidgetKind;

/// @def WIN_RECORD_KIND
///
/// @brief The offset in a widget record's header of its uint8_t
/// WinWidgetKind.
#define WIN_RECORD_KIND   0
/// @def WIN_RECORD_ID
///
/// @brief The offset in a widget record's header of its uint8_t identifier,
/// unique in the window.
#define WIN_RECORD_ID     1
/// @def WIN_RECORD_FLAGS
///
/// @brief The offset in a widget record's header of its uint8_t
/// WIN_WIDGET_* bits.
#define WIN_RECORD_FLAGS  2
/// @def WIN_RECORD_ANCHOR
///
/// @brief The offset in a widget record's header of its uint8_t
/// WIN_ANCHOR_* bits.
#define WIN_RECORD_ANCHOR 3
/// @def WIN_RECORD_RECT
///
/// @brief The offset in a widget record's header of its GfxRect in the
/// body:  int16_t x, y, w, h.
#define WIN_RECORD_RECT   4
/// @def WIN_RECORD_HEADER_SIZE
///
/// @brief The size of a widget record's header.
#define WIN_RECORD_HEADER_SIZE 12

GFX_STATIC_ASSERT(WIN_RECORD_RECT + 4 * sizeof(int16_t)
  == WIN_RECORD_HEADER_SIZE,
  "A widget record's header ends with its rectangle");

/// @def WIN_WIDGET_DISABLED
///
/// @brief A widget flag:  it is disabled.
#define WIN_WIDGET_DISABLED    0x01
/// @def WIN_WIDGET_HIDDEN
///
/// @brief A widget flag:  it is hidden.
#define WIN_WIDGET_HIDDEN      0x02
/// @def WIN_WIDGET_DEFAULT
///
/// @brief A widget flag:  Enter presses it.
#define WIN_WIDGET_DEFAULT     0x04
/// @def WIN_WIDGET_CANCEL
///
/// @brief A widget flag:  Escape presses it.
#define WIN_WIDGET_CANCEL      0x08
/// @def WIN_WIDGET_ENDS_DIALOG
///
/// @brief A widget flag:  it ends a dialog run by winRunDialog.
#define WIN_WIDGET_ENDS_DIALOG 0x10
/// @def WIN_WIDGET_PASSWORD
///
/// @brief A widget flag:  a text field that shows '*'.
#define WIN_WIDGET_PASSWORD    0x20
/// @def WIN_WIDGET_READ_ONLY
///
/// @brief A widget flag:  a text field that can't be edited.
#define WIN_WIDGET_READ_ONLY   0x40
/// @def WIN_WIDGET_SESSION
///
/// @brief A widget flag:  a text grid bound to a console session.
#define WIN_WIDGET_SESSION     0x80

/// @def WIN_ANCHOR_LEFT
///
/// @brief A widget anchor:  it keeps its distance from the body's left.  A
/// widget anchored to both sides of an axis stretches.
#define WIN_ANCHOR_LEFT   0x01
/// @def WIN_ANCHOR_TOP
///
/// @brief A widget anchor:  it keeps its distance from the body's top.  A
/// widget anchored to both sides of an axis stretches.
#define WIN_ANCHOR_TOP    0x02
/// @def WIN_ANCHOR_RIGHT
///
/// @brief A widget anchor:  it keeps its distance from the body's right.  A
/// widget anchored to both sides of an axis stretches.
#define WIN_ANCHOR_RIGHT  0x04
/// @def WIN_ANCHOR_BOTTOM
///
/// @brief A widget anchor:  it keeps its distance from the body's bottom.  A
/// widget anchored to both sides of an axis stretches.
#define WIN_ANCHOR_BOTTOM 0x08

/// @def WIN_ALIGN_LEFT
///
/// @brief A label's alignment:  left.
#define WIN_ALIGN_LEFT   0
/// @def WIN_ALIGN_CENTER
///
/// @brief A label's alignment:  centered.
#define WIN_ALIGN_CENTER 1
/// @def WIN_ALIGN_RIGHT
///
/// @brief A label's alignment:  right.
#define WIN_ALIGN_RIGHT  2

/// @def WIN_MAX_WIDGETS
///
/// @brief The most widgets one window holds.
#define WIN_MAX_WIDGETS 32

/// @def WIN_LIST_ITEMS_MAX
///
/// @brief The most bytes of items one list holds.
#define WIN_LIST_ITEMS_MAX 1024

/// @enum WinGridOperation
///
/// @brief The tag of each text grid operation in a WIN_MSG_GRID_WRITE,
/// followed by its fields.
///
/// @note An attribute is a foreground ANSI colour index in its low 4 bits and a
/// background one in its high 4 bits.
///
/// @param WIN_GRID_PUT row, column, attribute, length, and length bytes.
/// @param WIN_GRID_CLEAR top, left, bottom, right, attribute.
/// @param WIN_GRID_SCROLL top, bottom, int8_t count (up if positive), and the
///   attribute for the rows uncovered.
/// @param WIN_GRID_CURSOR row, column, shown.
/// @param WIN_GRID_REPORTS 1 if the program asked for resize reports.
typedef enum WinGridOperation {
  WIN_GRID_PUT = 0,
  WIN_GRID_CLEAR,
  WIN_GRID_SCROLL,
  WIN_GRID_CURSOR,
  WIN_GRID_REPORTS,
} WinGridOperation;

/// @def WIN_GRID_PUT_SIZE
///
/// @brief The size of a WIN_GRID_PUT operation with length bytes, its tag
/// included.
#define WIN_GRID_PUT_SIZE(length) (5 + (length))

/// @def WIN_GRID_CLEAR_SIZE
///
/// @brief The size of a WIN_GRID_CLEAR operation, its tag included.
#define WIN_GRID_CLEAR_SIZE       6

/// @def WIN_GRID_SCROLL_SIZE
///
/// @brief The size of a WIN_GRID_SCROLL operation, its tag included.
#define WIN_GRID_SCROLL_SIZE      5

/// @def WIN_GRID_CURSOR_SIZE
///
/// @brief The size of a WIN_GRID_CURSOR operation, its tag included.
#define WIN_GRID_CURSOR_SIZE      4

/// @def WIN_GRID_REPORTS_SIZE
///
/// @brief The size of a WIN_GRID_REPORTS operation, its tag included.
#define WIN_GRID_REPORTS_SIZE     2

#ifdef __cplusplus
}
#endif

#endif // WINDOW_PROTOCOL_H
