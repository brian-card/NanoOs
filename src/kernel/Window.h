///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.22.2026
///
/// @file              Window.h
///
/// @brief             The client library of the window protocol:  what a
///                    process uses to connect to the window server and talk
///                    to it.
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

#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
// An overlay's errno goes through the user API; NanoOsUser.h gives the rest.
#ifdef NANO_OS_OVERLAY_BUILD
#include <errno.h>
#endif

#include "WindowProtocol.h"
#include "UserMessages.h"
#include "UserProcesses.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @def WIN_OUT_OF_LINE
///
/// @brief Declare a helper that every wrapper calls out of line, so that each
/// wrapper costs a call rather than a copy of the helper.  `unused` keeps a
/// translation unit that calls none of a helper's wrappers warning-free.
#define WIN_OUT_OF_LINE static __attribute__((noinline, unused))

/// @struct WinSession
///
/// @brief A client's connection to the window server.
///
/// @param server The window server's ProcessId, or 0 before winConnect.
/// @param connected Whether the window server is still there.
/// @param reported Whether winNextEvent has returned WIN_EVENT_DISCONNECTED.
/// @param numEvents The number of entries in events.
/// @param nextEvent The entry of events winNextEvent returns next.
/// @param hello The window server's answer to WIN_MSG_HELLO.
/// @param events The events of the last WIN_MSG_EVENTS.
typedef struct WinSession {
  ProcessId    server;
  bool         connected;
  bool         reported;
  uint8_t      numEvents;
  uint8_t      nextEvent;
  WinHelloArgs hello;
  WinEvent     events[WIN_EVENTS_PER_MESSAGE];
} WinSession;

/// @fn static inline WinError winCall_(WinSession *session, uint8_t type,
///   void *args, size_t size)
///
/// @brief Send the window server a synchronous message and wait for it to be
/// done, with the status at the start of its arguments preset to
/// WIN_ERR_DISCONNECTED.  The wait has no timeout because the kernel marks
/// done every message to a process that ends.
///
/// @param session The connection.
/// @param type The WinMsgType.
/// @param args The arguments, from MESSAGE_ARGS_ALLOCATE, or NULL if there
///   was no storage for them.
/// @param size The size of the arguments.
///
/// @return Returns the status the window server set, WIN_ERR_NO_MEMORY if
/// there was no storage or message, or WIN_ERR_DISCONNECTED if the send
/// failed.
WIN_OUT_OF_LINE WinError winCall_(WinSession *session, uint8_t type,
  void *args, size_t size
) {
  if (args == NULL) {
    return WIN_ERR_NO_MEMORY;
  }
  *((uint8_t*) args) = WIN_ERR_DISCONNECTED;
  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return WIN_ERR_NO_MEMORY;
  }
  processMessageInit(message, WIN_COMMAND_SIGNATURE | type,
    args, size, /* waiting= */ true);
  if (sendProcessMessageToPid(session->server, message) != processSuccess) {
    processMessageRelease(message);
    session->connected = false;
    return WIN_ERR_DISCONNECTED;
  }
  processMessageWaitForDone(message, NULL);
  processMessageRelease(message);
  WinError status = (WinError) *((uint8_t*) args);
  if (status == WIN_ERR_DISCONNECTED) {
    session->connected = false;
  }
  return status;
}

/// @fn static inline void winPost_(WinSession *session, uint8_t type,
///   void *args, size_t size)
///
/// @brief Send the window server a one-way message, which then owns its
/// arguments, or destroy the arguments if it couldn't be sent.
///
/// @param session The connection.
/// @param type The WinMsgType.
/// @param args The arguments, from MESSAGE_ARGS_CREATE with
///   MESSAGE_ARGS_ONE_WAY, or NULL for a message that has none.
/// @param size The size of the arguments.
///
/// @return This function returns no value.
WIN_OUT_OF_LINE void winPost_(WinSession *session, uint8_t type, void *args,
  size_t size
) {
  ProcessMessage *message = getAvailableMessage();
  if (message != NULL) {
    processMessageInit(message, WIN_COMMAND_SIGNATURE | type,
      args, size, /* waiting= */ false);
    if (sendProcessMessageToPid(session->server, message) == processSuccess) {
      return;
    }
    processMessageRelease(message);
    session->connected = false;
  }
  if (args != NULL) {
    MESSAGE_ARGS_DESTROY(args);
  }
}

/// @fn static inline int winErrno(WinError status)
///
/// @brief Turn a window server's status into what the window calls return.
///
/// @param status The WinError.
///
/// @return Returns 0 for WIN_OK, or a negative errno value.
WIN_OUT_OF_LINE int winErrno(WinError status) {
  if (status == WIN_OK) {
    return 0;
  } else if (status == WIN_ERR_PERMISSION) {
    return -EPERM;
  } else if (status == WIN_ERR_NO_MEMORY) {
    return -ENOMEM;
  } else if (status == WIN_ERR_INVALID) {
    return -EINVAL;
  } else if (status == WIN_ERR_NOT_FOUND) {
    return -ENOENT;
  } else if (status == WIN_ERR_NOT_SUPPORTED) {
    return -ENOTSUP;
  }
  return -ENOTCONN;
}

/// @fn static inline WinError winConnect(WinSession *session)
///
/// @brief Connect to the window server and greet it, keeping its answer.
///
/// @param session The connection to fill in.
///
/// @return Returns WIN_OK on success, WIN_ERR_NOT_FOUND if no window server
/// is running, WIN_ERR_PERMISSION if it runs as another user, or another
/// WinError if it didn't answer.
static inline WinError winConnect(WinSession *session) {
  memset(session, 0, sizeof(*session));
  uint8_t server = 0;
  if (windowServerConnect(&server) != 0) {
    if (errno == ENOENT) {
      return WIN_ERR_NOT_FOUND;
    } else if (errno == EPERM) {
      return WIN_ERR_PERMISSION;
    }
    return WIN_ERR_DISCONNECTED;
  }
  session->server = server;
  session->connected = true;

  MESSAGE_ARGS_ALLOCATE(WinHelloArgs, helloArgs);
  WinError status = winCall_(session, WIN_MSG_HELLO, helloArgs,
    sizeof(*helloArgs));
  if (status == WIN_OK) {
    session->hello = *helloArgs;
  }
  MESSAGE_ARGS_FREE(helloArgs);
  return status;
}

/// @fn static inline void winDisconnect(WinSession *session)
///
/// @brief Tell the window server the client is done with it.
///
/// @param session The connection.
///
/// @return This function returns no value.
static inline void winDisconnect(WinSession *session) {
  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return;
  }
  processMessageInit(message, WIN_COMMAND_SIGNATURE | WIN_MSG_GOODBYE,
    NULL, 0, /* waiting= */ false);
  if (sendProcessMessageToPid(session->server, message) != processSuccess) {
    processMessageRelease(message);
  }
}

/// @fn static inline int winCreateWindowWith(WinSession *session,
///   const char *title, GfxRect frame, uint8_t layer, uint8_t flags,
///   const uint8_t *widgets, uint16_t templateSize)
///
/// @brief Create a window in the normal state, with the widgets of a
/// template.
///
/// @param session The connection.
/// @param title The title, which the window server copies, or NULL.
/// @param frame Where the window goes, in screen coordinates, decorations
///   included.
/// @param layer The WinLayer.
/// @param flags The WIN_FLAG_* bits.
/// @param widgets The template's widget records, which the window server
///   copies, or NULL.
/// @param templateSize The number of bytes at widgets.
///
/// @return Returns the window's identifier on success, or a negative errno
/// value on failure.
WIN_OUT_OF_LINE int winCreateWindowWith(WinSession *session,
  const char *title, GfxRect frame, uint8_t layer, uint8_t flags,
  const uint8_t *widgets, uint16_t templateSize
) {
  MESSAGE_ARGS_ALLOCATE(WinCreateWindowArgs, args);
  if (args == NULL) {
    return -ENOMEM;
  }
  size_t length = 0;
  if (title != NULL) {
    length = strlen(title);
    if (length > WIN_TITLE_MAX) {
      length = WIN_TITLE_MAX;
    }
  }
  args->layer = layer;
  args->flags = flags;
  args->state = WIN_STATE_NORMAL;
  args->owner = WIN_NONE;
  args->titleLength = (uint8_t) length;
  args->templateSize = (widgets != NULL) ? templateSize : 0;
  args->frame = frame;
  args->title = NULL;
  args->widgets = NULL;
  WinError status = WIN_ERR_NO_MEMORY;
  if (length > 0) {
    args->title = (const char*) MESSAGE_BUFFER_SHARE(title, length,
      MESSAGE_BUFFER_IN, session->server);
  }
  if (args->templateSize > 0) {
    args->widgets = (const uint8_t*) MESSAGE_BUFFER_SHARE(widgets,
      args->templateSize, MESSAGE_BUFFER_IN, session->server);
  }
  if (((length == 0) || (args->title != NULL))
    && ((args->templateSize == 0) || (args->widgets != NULL))
  ) {
    status = winCall_(session, WIN_MSG_CREATE_WINDOW, args, sizeof(*args));
  }
  if (args->widgets != NULL) {
    MESSAGE_BUFFER_UNSHARE(args->widgets, widgets, MESSAGE_BUFFER_IN);
  }
  if (args->title != NULL) {
    MESSAGE_BUFFER_UNSHARE(args->title, title, MESSAGE_BUFFER_IN);
  }
  int result = winErrno(status);
  if (status == WIN_OK) {
    result = args->window;
  }
  MESSAGE_ARGS_FREE(args);
  return result;
}

/// @fn static inline int winCreateWindow(WinSession *session,
///   const char *title, GfxRect frame, uint8_t layer, uint8_t flags)
///
/// @brief Create a window in the normal state, with no widgets.
///
/// @param session The connection.
/// @param title The title, which the window server copies, or NULL.
/// @param frame Where the window goes, in screen coordinates, decorations
///   included.
/// @param layer The WinLayer.
/// @param flags The WIN_FLAG_* bits.
///
/// @return Returns the window's identifier on success, or a negative errno
/// value on failure.
static inline int winCreateWindow(WinSession *session, const char *title,
  GfxRect frame, uint8_t layer, uint8_t flags
) {
  return winCreateWindowWith(session, title, frame, layer, flags, NULL, 0);
}

/// @fn static inline int winGridWrite(WinSession *session, uint8_t window,
///   uint8_t widget, const uint8_t *operations, uint16_t size)
///
/// @brief Carry out text grid operations, waiting until the window server
/// has.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The text grid.
/// @param operations The WinGridOperation records, which the window server
///   copies.
/// @param size The number of bytes at operations.
///
/// @return Returns 0 on success, or a negative errno value on failure.
WIN_OUT_OF_LINE int winGridWrite(WinSession *session, uint8_t window,
  uint8_t widget, const uint8_t *operations, uint16_t size
) {
  MESSAGE_ARGS_ALLOCATE(WinBufferArgs, args);
  if (args == NULL) {
    return -ENOMEM;
  }
  args->window = window;
  args->widget = widget;
  args->size = size;
  args->data = (const uint8_t*) MESSAGE_BUFFER_SHARE(operations, size,
    MESSAGE_BUFFER_IN, session->server);
  WinError status = WIN_ERR_NO_MEMORY;
  if (args->data != NULL) {
    status = winCall_(session, WIN_MSG_GRID_WRITE, args, sizeof(*args));
    MESSAGE_BUFFER_UNSHARE(args->data, operations, MESSAGE_BUFFER_IN);
  }
  MESSAGE_ARGS_FREE(args);
  return winErrno(status);
}

/// @struct WinCanvas
///
/// @brief A canvas's Graphics.h records being built in a buffer of the
/// caller's.
///
/// @param buffer Where the records go.
/// @param capacity The bytes at buffer.
/// @param size The bytes of records so far.
/// @param full Whether a record didn't fit.
typedef struct WinCanvas {
  uint8_t *buffer;
  uint16_t capacity;
  uint16_t size;
  bool     full;
} WinCanvas;

/// @fn static inline void winCanvasBegin(WinCanvas *canvas, uint8_t *buffer,
///   uint16_t capacity)
///
/// @brief Start building a canvas's records, with none, in a buffer.
///
/// @param canvas The records.
/// @param buffer Where they go.
/// @param capacity The bytes at buffer, at most WIN_CANVAS_MAX of which are
///   sent.
///
/// @return This function returns no value.
static inline void winCanvasBegin(WinCanvas *canvas, uint8_t *buffer,
  uint16_t capacity
) {
  canvas->buffer = buffer;
  canvas->capacity = capacity;
  canvas->size = 0;
  canvas->full = false;
}

/// @fn static inline void winCanvasRecord_(WinCanvas *canvas, uint8_t tag,
///   const void *params, uint16_t size)
///
/// @brief Add a Graphics.h record, its tag and its parameters, to a canvas's
/// records, or mark them full if it doesn't fit.
///
/// @param canvas The records.
/// @param tag The GfxCmdType.
/// @param params The parameters.
/// @param size The bytes at params.
///
/// @return This function returns no value.
WIN_OUT_OF_LINE void winCanvasRecord_(WinCanvas *canvas, uint8_t tag,
  const void *params, uint16_t size
) {
  if (canvas->size + 1 + size > canvas->capacity) {
    canvas->full = true;
    return;
  }
  canvas->buffer[canvas->size] = tag;
  memcpy(&canvas->buffer[canvas->size + 1], params, size);
  canvas->size = (uint16_t) (canvas->size + 1 + size);
}

/// @fn static inline void winCanvasColor(WinCanvas *canvas, GfxColor color)
///
/// @brief Add a record that sets the color of what is drawn next.
///
/// @param canvas The records.
/// @param color The color.
///
/// @return This function returns no value.
WIN_OUT_OF_LINE void winCanvasColor(WinCanvas *canvas, GfxColor color) {
  winCanvasRecord_(canvas, GFX_CMD_SET_FG_COLOR, &color, sizeof(color));
}

/// @fn static inline void winCanvasRect(WinCanvas *canvas, GfxRect rect,
///   bool fill)
///
/// @brief Add a record that draws a rectangle, filled or as a 1-pixel
/// outline.
///
/// @param canvas The records.
/// @param rect The rectangle, in the canvas's coordinates.
/// @param fill Whether it is filled.
///
/// @return This function returns no value.
WIN_OUT_OF_LINE void winCanvasRect(WinCanvas *canvas, GfxRect rect,
  bool fill
) {
  uint8_t params[sizeof(GfxRect) + 1];
  memcpy(params, &rect, sizeof(rect));
  params[sizeof(GfxRect)] = fill ? 1 : 0;
  winCanvasRecord_(canvas, GFX_CMD_DRAW_RECT, params, sizeof(params));
}

/// @fn static inline void winCanvasLine(WinCanvas *canvas, GfxPoint from,
///   GfxPoint to)
///
/// @brief Add a record that draws a line, both ends included.
///
/// @param canvas The records.
/// @param from One end, in the canvas's coordinates.
/// @param to The other end.
///
/// @return This function returns no value.
WIN_OUT_OF_LINE void winCanvasLine(WinCanvas *canvas, GfxPoint from,
  GfxPoint to
) {
  GfxPoint points[2] = { from, to };
  winCanvasRecord_(canvas, GFX_CMD_DRAW_LINE, points, sizeof(points));
}

/// @fn static inline void winCanvasText(WinCanvas *canvas, GfxPoint at,
///   const char *text)
///
/// @brief Add a record that draws text in the built-in font, cut to 255
/// bytes.
///
/// @param canvas The records.
/// @param at Its top-left corner, in the canvas's coordinates.
/// @param text The text.
///
/// @return This function returns no value.
WIN_OUT_OF_LINE void winCanvasText(WinCanvas *canvas, GfxPoint at,
  const char *text
) {
  size_t length = strlen(text);
  if (length > UINT8_MAX) {
    length = UINT8_MAX;
  }
  uint8_t params[sizeof(GfxPoint) + 1 + UINT8_MAX];
  memcpy(params, &at, sizeof(at));
  params[sizeof(GfxPoint)] = (uint8_t) length;
  memcpy(&params[sizeof(GfxPoint) + 1], text, length);
  winCanvasRecord_(canvas, GFX_CMD_DRAW_TEXT, params,
    (uint16_t) (sizeof(GfxPoint) + 1 + length));
}

/// @fn static inline int winSetCanvas(WinSession *session, uint8_t window,
///   uint8_t widget, const WinCanvas *canvas)
///
/// @brief Replace what a canvas draws.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The canvas.
/// @param canvas The records, at most WIN_CANVAS_MAX bytes.
///
/// @return Returns 0 on success, -ENOMEM if the records didn't fit their
/// buffer, or another negative errno value on failure.
WIN_OUT_OF_LINE int winSetCanvas(WinSession *session, uint8_t window,
  uint8_t widget, const WinCanvas *canvas
) {
  if (canvas->full) {
    return -ENOMEM;
  }
  MESSAGE_ARGS_ALLOCATE(WinBufferArgs, args);
  if (args == NULL) {
    return -ENOMEM;
  }
  args->window = window;
  args->widget = widget;
  args->size = canvas->size;
  args->data = NULL;
  WinError status = WIN_ERR_NO_MEMORY;
  if (canvas->size > 0) {
    args->data = (const uint8_t*) MESSAGE_BUFFER_SHARE(canvas->buffer,
      canvas->size, MESSAGE_BUFFER_IN, session->server);
  }
  if ((canvas->size == 0) || (args->data != NULL)) {
    status = winCall_(session, WIN_MSG_SET_CANVAS, args, sizeof(*args));
  }
  if (args->data != NULL) {
    MESSAGE_BUFFER_UNSHARE(args->data, canvas->buffer, MESSAGE_BUFFER_IN);
  }
  MESSAGE_ARGS_FREE(args);
  return winErrno(status);
}

/// @fn static inline void winGridRecord(uint8_t *record, uint8_t widget,
///   uint8_t flags, GfxRect rect)
///
/// @brief Fill in a template record for a text grid anchored to all four
/// sides of the body, so that it stretches with the window.
///
/// @param record Where to write the record's WIN_RECORD_HEADER_SIZE bytes.
/// @param widget The grid's identifier.
/// @param flags The WIN_WIDGET_* bits, such as WIN_WIDGET_SESSION.
/// @param rect The grid's place in the body.
///
/// @return This function returns no value.
static inline void winGridRecord(uint8_t *record, uint8_t widget,
  uint8_t flags, GfxRect rect
) {
  record[WIN_RECORD_KIND] = WIN_WIDGET_TEXT_GRID;
  record[WIN_RECORD_ID] = widget;
  record[WIN_RECORD_FLAGS] = flags;
  record[WIN_RECORD_ANCHOR] = WIN_ANCHOR_LEFT | WIN_ANCHOR_TOP
    | WIN_ANCHOR_RIGHT | WIN_ANCHOR_BOTTOM;
  memcpy(&record[WIN_RECORD_RECT], &rect, sizeof(rect));
}

/// @struct WinTemplate
///
/// @brief A template of widget records being built in a buffer of the
/// caller's.
///
/// @param buffer Where the records go.
/// @param capacity The bytes at buffer.
/// @param size The bytes of records so far.
typedef struct WinTemplate {
  uint8_t *buffer;
  uint16_t capacity;
  uint16_t size;
} WinTemplate;

/// @fn static inline void winTemplateBegin(WinTemplate *widgets,
///   uint8_t *buffer, uint16_t capacity)
///
/// @brief Start an empty template in a buffer.
///
/// @param widgets The template.
/// @param buffer Where its records go.
/// @param capacity The bytes at buffer.
///
/// @return This function returns no value.
static inline void winTemplateBegin(WinTemplate *widgets, uint8_t *buffer,
  uint16_t capacity
) {
  widgets->buffer = buffer;
  widgets->capacity = capacity;
  widgets->size = 0;
}

/// @fn static inline bool winAddRecord_(WinTemplate *widgets, uint8_t kind,
///   uint8_t id, uint8_t flags, uint8_t anchor, GfxRect rect,
///   const uint8_t *fields, uint8_t numFields, const char *text)
///
/// @brief Add a widget record to a template:  its header, its kind's fields
/// before its text, and its text, cut to 255 bytes.  Nothing is added if the
/// record doesn't fit.
///
/// @param widgets The template.
/// @param kind The WinWidgetKind.
/// @param id The widget's identifier, unique in its window, or WIN_NONE.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body.
/// @param fields The bytes of its kind's fields that come before its text.
/// @param numFields The number of bytes at fields.
/// @param text Its text, or NULL for none.
///
/// @return Returns true if the record was added, false if it didn't fit.
WIN_OUT_OF_LINE bool winAddRecord_(WinTemplate *widgets, uint8_t kind,
  uint8_t id, uint8_t flags, uint8_t anchor, GfxRect rect,
  const uint8_t *fields, uint8_t numFields, const char *text
) {
  size_t length = (text != NULL) ? strlen(text) : 0;
  if (length > UINT8_MAX) {
    length = UINT8_MAX;
  }
  size_t size = WIN_RECORD_HEADER_SIZE + numFields + 1 + length;
  if (widgets->size + size > widgets->capacity) {
    return false;
  }
  uint8_t *record = &widgets->buffer[widgets->size];
  record[WIN_RECORD_KIND] = kind;
  record[WIN_RECORD_ID] = id;
  record[WIN_RECORD_FLAGS] = flags;
  record[WIN_RECORD_ANCHOR] = anchor;
  memcpy(&record[WIN_RECORD_RECT], &rect, sizeof(rect));
  memcpy(&record[WIN_RECORD_HEADER_SIZE], fields, numFields);
  record[WIN_RECORD_HEADER_SIZE + numFields] = (uint8_t) length;
  memcpy(&record[WIN_RECORD_HEADER_SIZE + numFields + 1], text, length);
  widgets->size = (uint16_t) (widgets->size + size);
  return true;
}

/// @fn static inline bool winAddLabel(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, uint8_t anchor, GfxRect rect, uint8_t align,
///   const char *text)
///
/// @brief Add a label to a template.
///
/// @param widgets The template.
/// @param id The label's identifier, or WIN_NONE.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body.
/// @param align WIN_ALIGN_LEFT, WIN_ALIGN_CENTER or WIN_ALIGN_RIGHT.
/// @param text Its text.
///
/// @return Returns true if the label was added, false if it didn't fit.
static inline bool winAddLabel(WinTemplate *widgets, uint8_t id,
  uint8_t flags, uint8_t anchor, GfxRect rect, uint8_t align,
  const char *text
) {
  return winAddRecord_(widgets, WIN_WIDGET_LABEL, id, flags, anchor, rect,
    &align, 1, text);
}

/// @fn static inline bool winAddButton(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, uint8_t anchor, GfxRect rect, const char *text)
///
/// @brief Add a push button to a template.  WIN_WIDGET_DEFAULT makes it the
/// one Enter presses, and WIN_WIDGET_CANCEL the one Escape does.
///
/// @param widgets The template.
/// @param id The button's identifier, which its WIN_EVENT_COMMAND carries.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body.
/// @param text Its text.
///
/// @return Returns true if the button was added, false if it didn't fit.
static inline bool winAddButton(WinTemplate *widgets, uint8_t id,
  uint8_t flags, uint8_t anchor, GfxRect rect, const char *text
) {
  return winAddRecord_(widgets, WIN_WIDGET_BUTTON, id, flags, anchor, rect,
    NULL, 0, text);
}

/// @fn static inline bool winAddToggle(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, uint8_t anchor, GfxRect rect, bool value,
///   const char *text)
///
/// @brief Add a toggle button to a template:  a push button that stays in
/// or out.
///
/// @param widgets The template.
/// @param id The button's identifier.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body.
/// @param value Whether it starts in.
/// @param text Its text.
///
/// @return Returns true if the button was added, false if it didn't fit.
static inline bool winAddToggle(WinTemplate *widgets, uint8_t id,
  uint8_t flags, uint8_t anchor, GfxRect rect, bool value, const char *text
) {
  uint8_t fields[] = { value };
  return winAddRecord_(widgets, WIN_WIDGET_TOGGLE, id, flags, anchor, rect,
    fields, sizeof(fields), text);
}

/// @fn static inline bool winAddCheckBox(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, uint8_t anchor, GfxRect rect, bool value,
///   const char *text)
///
/// @brief Add a check box to a template.
///
/// @param widgets The template.
/// @param id The check box's identifier.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body, box and text.
/// @param value Whether it starts checked.
/// @param text Its text.
///
/// @return Returns true if the check box was added, false if it didn't fit.
static inline bool winAddCheckBox(WinTemplate *widgets, uint8_t id,
  uint8_t flags, uint8_t anchor, GfxRect rect, bool value, const char *text
) {
  uint8_t fields[] = { value };
  return winAddRecord_(widgets, WIN_WIDGET_CHECK_BOX, id, flags, anchor, rect,
    fields, sizeof(fields), text);
}

/// @fn static inline bool winAddRadio(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, uint8_t anchor, GfxRect rect, uint8_t group, bool value,
///   const char *text)
///
/// @brief Add a radio button to a template.  Setting one clears the others
/// of its group.
///
/// @param widgets The template.
/// @param id The radio button's identifier.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body, box and text.
/// @param group Its group, which numbers the radio buttons that clear each
///   other.
/// @param value Whether it starts set.
/// @param text Its text.
///
/// @return Returns true if the radio button was added, false if it didn't
/// fit.
static inline bool winAddRadio(WinTemplate *widgets, uint8_t id,
  uint8_t flags, uint8_t anchor, GfxRect rect, uint8_t group, bool value,
  const char *text
) {
  uint8_t fields[] = { group, value };
  return winAddRecord_(widgets, WIN_WIDGET_RADIO, id, flags, anchor, rect,
    fields, sizeof(fields), text);
}

/// @fn static inline bool winAddTextField(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, uint8_t anchor, GfxRect rect, uint8_t maxLength,
///   const char *text)
///
/// @brief Add a text field to a template.  WIN_WIDGET_PASSWORD shows a star
/// for each of its bytes, and WIN_WIDGET_READ_ONLY keeps its text as it is.
///
/// @param widgets The template.
/// @param id The text field's identifier, which its WIN_EVENT_CHANGED and
///   WIN_EVENT_COMMAND carry.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body.
/// @param maxLength The most bytes of text it holds, 255 if 0.
/// @param text Its text, cut to maxLength.
///
/// @return Returns true if the text field was added, false if it didn't
/// fit.
static inline bool winAddTextField(WinTemplate *widgets, uint8_t id,
  uint8_t flags, uint8_t anchor, GfxRect rect, uint8_t maxLength,
  const char *text
) {
  return winAddRecord_(widgets, WIN_WIDGET_TEXT_FIELD, id, flags, anchor,
    rect, &maxLength, 1, text);
}

/// @fn static inline bool winAddList(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, uint8_t anchor, GfxRect rect, uint16_t selection,
///   const char *items, uint16_t size)
///
/// @brief Add a list to a template.
///
/// @param widgets The template.
/// @param id The list's identifier, which its WIN_EVENT_CHANGED and
///   WIN_EVENT_COMMAND carry.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body.
/// @param selection The selected item's index, or WIN_NO_ITEM.
/// @param items Its items, each ended by a NUL, which the last may lack.
/// @param size The bytes at items, at most WIN_LIST_ITEMS_MAX.
///
/// @return Returns true if the list was added, false if it didn't fit.
static inline bool winAddList(WinTemplate *widgets, uint8_t id,
  uint8_t flags, uint8_t anchor, GfxRect rect, uint16_t selection,
  const char *items, uint16_t size
) {
  uint32_t recordSize = WIN_RECORD_HEADER_SIZE + 2 * sizeof(uint16_t) + size;
  if ((size > WIN_LIST_ITEMS_MAX)
    || (widgets->size + recordSize > widgets->capacity)
  ) {
    return false;
  }
  uint8_t *record = &widgets->buffer[widgets->size];
  record[WIN_RECORD_KIND] = WIN_WIDGET_LIST;
  record[WIN_RECORD_ID] = id;
  record[WIN_RECORD_FLAGS] = flags;
  record[WIN_RECORD_ANCHOR] = anchor;
  memcpy(&record[WIN_RECORD_RECT], &rect, sizeof(rect));
  uint8_t *fields = &record[WIN_RECORD_HEADER_SIZE];
  fields[0] = (uint8_t) selection;
  fields[1] = (uint8_t) (selection >> 8);
  fields[2] = (uint8_t) size;
  fields[3] = (uint8_t) (size >> 8);
  memcpy(&fields[2 * sizeof(uint16_t)], items, size);
  widgets->size = (uint16_t) (widgets->size + recordSize);
  return true;
}

/// @struct WinMenus
///
/// @brief A menu bar's menus being built in a buffer of the caller's.
///
/// @param buffer Where the menus go.
/// @param capacity The bytes at buffer.
/// @param size The bytes of menus so far.
/// @param countAt Where the last menu's count of items is.
typedef struct WinMenus {
  uint8_t *buffer;
  uint16_t capacity;
  uint16_t size;
  uint16_t countAt;
} WinMenus;

/// @fn static inline void winMenusBegin(WinMenus *menus, uint8_t *buffer,
///   uint16_t capacity)
///
/// @brief Start a menu bar's menus, with none, in a buffer.
///
/// @param menus The menus.
/// @param buffer Where they go.
/// @param capacity The bytes at buffer.
///
/// @return This function returns no value.
static inline void winMenusBegin(WinMenus *menus, uint8_t *buffer,
  uint16_t capacity
) {
  menus->buffer = buffer;
  menus->capacity = capacity;
  menus->size = 0;
  menus->countAt = 0;
}

/// @fn static inline bool winMenusText_(WinMenus *menus, const char *text)
///
/// @brief Add a length byte and a text, cut to 255 bytes, to a menu bar's
/// menus.
///
/// @param menus The menus.
/// @param text The text, or NULL for none.
///
/// @return Returns true if it was added, false if it didn't fit.
static inline bool winMenusText_(WinMenus *menus, const char *text) {
  size_t length = (text != NULL) ? strlen(text) : 0;
  if (length > UINT8_MAX) {
    length = UINT8_MAX;
  }
  if (menus->size + 1 + length > menus->capacity) {
    return false;
  }
  menus->buffer[menus->size] = (uint8_t) length;
  memcpy(&menus->buffer[menus->size + 1], text, length);
  menus->size = (uint16_t) (menus->size + 1 + length);
  return true;
}

/// @fn static inline bool winMenusAdd(WinMenus *menus, const char *title)
///
/// @brief Add a menu, with no items yet, to a menu bar's menus.
///
/// @param menus The menus.
/// @param title Its title, with an '&' before the letter Alt opens it with.
///
/// @return Returns true if it was added, false if it didn't fit.
static inline bool winMenusAdd(WinMenus *menus, const char *title) {
  uint16_t size = menus->size;
  if (!winMenusText_(menus, title) || (menus->size >= menus->capacity)) {
    menus->size = size;
    return false;
  }
  menus->countAt = menus->size;
  menus->buffer[menus->size++] = 0;
  return true;
}

/// @fn static inline bool winMenusAddItem(WinMenus *menus, uint8_t id,
///   uint8_t flags, const char *text, const char *shortcut)
///
/// @brief Add an item to the last menu of a menu bar's menus.
///
/// @param menus The menus.
/// @param id The item's identifier, unique in the bar, which its
///   WIN_EVENT_COMMAND carries, or 0 for a separator.
/// @param flags The WIN_MENU_* bits.
/// @param text Its text.
/// @param shortcut The shortcut text shown right of it, or NULL for none.
///
/// @return Returns true if it was added, false if it didn't fit.
static inline bool winMenusAddItem(WinMenus *menus, uint8_t id,
  uint8_t flags, const char *text, const char *shortcut
) {
  uint16_t size = menus->size;
  if ((menus->size + 2 > menus->capacity) || (menus->size == 0)) {
    return false;
  }
  menus->buffer[menus->size] = id;
  menus->buffer[menus->size + 1] = flags;
  menus->size = (uint16_t) (menus->size + 2);
  if (!winMenusText_(menus, text) || !winMenusText_(menus, shortcut)) {
    menus->size = size;
    return false;
  }
  menus->buffer[menus->countAt]++;
  return true;
}

/// @fn static inline bool winAddMenuBar(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, GfxRect rect, const WinMenus *menus)
///
/// @brief Add a menu bar to a template, anchored left, right and top so that
/// it stretches with its window.
///
/// @param widgets The template.
/// @param id The bar's identifier, which its WIN_EVENT_COMMAND carries.
/// @param flags The WIN_WIDGET_* bits.
/// @param rect Its place in the window's body.
/// @param menus Its menus, at most WIN_MENU_BAR_MAX bytes.
///
/// @return Returns true if the bar was added, false if it didn't fit.
static inline bool winAddMenuBar(WinTemplate *widgets, uint8_t id,
  uint8_t flags, GfxRect rect, const WinMenus *menus
) {
  uint32_t recordSize = WIN_RECORD_HEADER_SIZE + sizeof(uint16_t)
    + menus->size;
  if ((menus->size > WIN_MENU_BAR_MAX)
    || (widgets->size + recordSize > widgets->capacity)
  ) {
    return false;
  }
  uint8_t *record = &widgets->buffer[widgets->size];
  record[WIN_RECORD_KIND] = WIN_WIDGET_MENU_BAR;
  record[WIN_RECORD_ID] = id;
  record[WIN_RECORD_FLAGS] = flags;
  record[WIN_RECORD_ANCHOR] = WIN_ANCHOR_LEFT | WIN_ANCHOR_RIGHT
    | WIN_ANCHOR_TOP;
  memcpy(&record[WIN_RECORD_RECT], &rect, sizeof(rect));
  uint8_t *fields = &record[WIN_RECORD_HEADER_SIZE];
  fields[0] = (uint8_t) menus->size;
  fields[1] = (uint8_t) (menus->size >> 8);
  memcpy(&fields[sizeof(uint16_t)], menus->buffer, menus->size);
  widgets->size = (uint16_t) (widgets->size + recordSize);
  return true;
}

/// @fn static inline bool winAddCanvas(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, uint8_t anchor, GfxRect rect)
///
/// @brief Add a canvas, which draws what winSetCanvas gives it and passes
/// the pointer and keys on, to a template.
///
/// @param widgets The template.
/// @param id The canvas's identifier, which its WIN_EVENT_POINTER carries.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body.
///
/// @return Returns true if the canvas was added, false if it didn't fit.
static inline bool winAddCanvas(WinTemplate *widgets, uint8_t id,
  uint8_t flags, uint8_t anchor, GfxRect rect
) {
  if (widgets->size + WIN_RECORD_HEADER_SIZE > widgets->capacity) {
    return false;
  }
  uint8_t *record = &widgets->buffer[widgets->size];
  record[WIN_RECORD_KIND] = WIN_WIDGET_CANVAS;
  record[WIN_RECORD_ID] = id;
  record[WIN_RECORD_FLAGS] = flags;
  record[WIN_RECORD_ANCHOR] = anchor;
  memcpy(&record[WIN_RECORD_RECT], &rect, sizeof(rect));
  widgets->size = (uint16_t) (widgets->size + WIN_RECORD_HEADER_SIZE);
  return true;
}

/// @fn static inline bool winAddGroup(WinTemplate *widgets, uint8_t id,
///   uint8_t flags, uint8_t anchor, GfxRect rect, const char *caption)
///
/// @brief Add a group box to a template.
///
/// @param widgets The template.
/// @param id The group box's identifier, or WIN_NONE.
/// @param flags The WIN_WIDGET_* bits.
/// @param anchor The WIN_ANCHOR_* bits.
/// @param rect Its place in the window's body.
/// @param caption Its caption, or NULL for none.
///
/// @return Returns true if the group box was added, false if it didn't fit.
static inline bool winAddGroup(WinTemplate *widgets, uint8_t id,
  uint8_t flags, uint8_t anchor, GfxRect rect, const char *caption
) {
  return winAddRecord_(widgets, WIN_WIDGET_GROUP, id, flags, anchor, rect,
    NULL, 0, caption);
}

/// @fn static inline int winAddWidgets(WinSession *session, uint8_t window,
///   const WinTemplate *widgets)
///
/// @brief Add a template's widgets to a window, all of them or none.
///
/// @param session The connection.
/// @param window The window.
/// @param widgets The template, which the window server copies.
///
/// @return Returns 0 on success, or a negative errno value on failure.
WIN_OUT_OF_LINE int winAddWidgets(WinSession *session, uint8_t window,
  const WinTemplate *widgets
) {
  MESSAGE_ARGS_ALLOCATE(WinAddWidgetsArgs, args);
  if (args == NULL) {
    return -ENOMEM;
  }
  args->window = window;
  args->templateSize = widgets->size;
  args->widgets = (const uint8_t*) MESSAGE_BUFFER_SHARE(widgets->buffer,
    widgets->size, MESSAGE_BUFFER_IN, session->server);
  WinError status = WIN_ERR_NO_MEMORY;
  if ((widgets->size == 0) || (args->widgets != NULL)) {
    status = winCall_(session, WIN_MSG_ADD_WIDGETS, args, sizeof(*args));
  }
  if (args->widgets != NULL) {
    MESSAGE_BUFFER_UNSHARE(args->widgets, widgets->buffer, MESSAGE_BUFFER_IN);
  }
  MESSAGE_ARGS_FREE(args);
  return winErrno(status);
}

/// @fn static inline int winSetWidget_(WinSession *session, uint8_t window,
///   uint8_t widget, uint8_t property, uint16_t value, const void *data,
///   uint16_t size, const GfxRect *rect)
///
/// @brief Set one of a widget's properties.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
/// @param property The WinProperty.
/// @param value The value, for a property that takes one.
/// @param data The data, which the window server copies, for a property
///   that takes it, or NULL.
/// @param size The bytes at data.
/// @param rect The rectangle, for WIN_PROP_RECT, or NULL.
///
/// @return Returns 0 on success, or a negative errno value on failure.
WIN_OUT_OF_LINE int winSetWidget_(WinSession *session, uint8_t window,
  uint8_t widget, uint8_t property, uint16_t value, const void *data,
  uint16_t size, const GfxRect *rect
) {
  MESSAGE_ARGS_ALLOCATE(WinSetWidgetArgs, args);
  if (args == NULL) {
    return -ENOMEM;
  }
  args->window = window;
  args->widget = widget;
  args->property = property;
  args->value = value;
  args->size = (data != NULL) ? size : 0;
  memset(&args->rect, 0, sizeof(args->rect));
  if (rect != NULL) {
    args->rect = *rect;
  }
  args->data = NULL;
  if (args->size > 0) {
    args->data = (const uint8_t*) MESSAGE_BUFFER_SHARE(data, args->size,
      MESSAGE_BUFFER_IN, session->server);
  }
  WinError status = WIN_ERR_NO_MEMORY;
  if ((args->size == 0) || (args->data != NULL)) {
    status = winCall_(session, WIN_MSG_SET_WIDGET, args, sizeof(*args));
  }
  if (args->data != NULL) {
    MESSAGE_BUFFER_UNSHARE(args->data, data, MESSAGE_BUFFER_IN);
  }
  MESSAGE_ARGS_FREE(args);
  return winErrno(status);
}

/// @fn static inline int winSetText(WinSession *session, uint8_t window,
///   uint8_t widget, const char *text)
///
/// @brief Set a widget's text, cut to 255 bytes.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
/// @param text The text.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetText(WinSession *session, uint8_t window,
  uint8_t widget, const char *text
) {
  size_t length = strlen(text);
  if (length > UINT8_MAX) {
    length = UINT8_MAX;
  }
  return winSetWidget_(session, window, widget, WIN_PROP_TEXT, 0, text,
    (uint16_t) length, NULL);
}

/// @fn static inline int winSetItems(WinSession *session, uint8_t window,
///   uint8_t widget, const char *items, uint16_t size)
///
/// @brief Replace a list's items, keeping its selection if there are still
/// that many, or else selecting the last.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The list.
/// @param items The items, each ended by a NUL, which the last may lack.
/// @param size The bytes at items, at most WIN_LIST_ITEMS_MAX.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetItems(WinSession *session, uint8_t window,
  uint8_t widget, const char *items, uint16_t size
) {
  return winSetWidget_(session, window, widget, WIN_PROP_ITEMS, 0, items,
    size, NULL);
}

/// @fn static inline int winSetValue(WinSession *session, uint8_t window,
///   uint8_t widget, uint16_t value)
///
/// @brief Set a box's or toggle button's value, or a list's selection.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
/// @param value The value, or a list item's index or WIN_NO_ITEM.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetValue(WinSession *session, uint8_t window,
  uint8_t widget, uint16_t value
) {
  return winSetWidget_(session, window, widget, WIN_PROP_VALUE, value, NULL,
    0, NULL);
}

/// @fn static inline int winSetMenuItem(WinSession *session, uint8_t window,
///   uint8_t bar, uint8_t item, uint8_t flags)
///
/// @brief Set whether a menu bar's item is disabled and checked.
///
/// @param session The connection.
/// @param window The window.
/// @param bar The menu bar.
/// @param item The item's identifier.
/// @param flags Its WIN_MENU_DISABLED and WIN_MENU_CHECKED bits.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetMenuItem(WinSession *session, uint8_t window,
  uint8_t bar, uint8_t item, uint8_t flags
) {
  return winSetWidget_(session, window, bar, WIN_PROP_MENU_ITEM,
    (uint16_t) ((item << 8) | flags), NULL, 0, NULL);
}

/// @fn static inline int winSetEnabled(WinSession *session, uint8_t window,
///   uint8_t widget, bool enabled)
///
/// @brief Enable or disable a widget.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
/// @param enabled Whether it is enabled.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetEnabled(WinSession *session, uint8_t window,
  uint8_t widget, bool enabled
) {
  return winSetWidget_(session, window, widget, WIN_PROP_ENABLED, enabled,
    NULL, 0, NULL);
}

/// @fn static inline int winSetHidden(WinSession *session, uint8_t window,
///   uint8_t widget, bool hidden)
///
/// @brief Hide or show a widget.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
/// @param hidden Whether it is hidden.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetHidden(WinSession *session, uint8_t window,
  uint8_t widget, bool hidden
) {
  return winSetWidget_(session, window, widget, WIN_PROP_HIDDEN, hidden,
    NULL, 0, NULL);
}

/// @fn static inline int winSetRect(WinSession *session, uint8_t window,
///   uint8_t widget, GfxRect rect)
///
/// @brief Move a widget to a new place in its window's body.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
/// @param rect The new place, which its anchors then keep.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetRect(WinSession *session, uint8_t window,
  uint8_t widget, GfxRect rect
) {
  return winSetWidget_(session, window, widget, WIN_PROP_RECT, 0, NULL, 0,
    &rect);
}

/// @fn static inline int winGetWidget_(WinSession *session, uint8_t window,
///   uint8_t widget, uint8_t property, uint8_t *data, uint16_t capacity,
///   uint16_t *value)
///
/// @brief Get a widget's text or value.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
/// @param property WIN_PROP_TEXT or WIN_PROP_VALUE.
/// @param data Where the text goes, with no terminator, for WIN_PROP_TEXT.
/// @param capacity The bytes at data.
/// @param value Where the value goes for WIN_PROP_VALUE, or the text's whole
///   length for WIN_PROP_TEXT, which may exceed capacity.
///
/// @return Returns 0 on success, or a negative errno value on failure.
WIN_OUT_OF_LINE int winGetWidget_(WinSession *session, uint8_t window,
  uint8_t widget, uint8_t property, uint8_t *data, uint16_t capacity,
  uint16_t *value
) {
  MESSAGE_ARGS_ALLOCATE(WinGetWidgetArgs, args);
  if (args == NULL) {
    return -ENOMEM;
  }
  args->window = window;
  args->widget = widget;
  args->property = property;
  args->capacity = (data != NULL) ? capacity : 0;
  args->value = 0;
  args->length = 0;
  args->data = NULL;
  if (args->capacity > 0) {
    args->data = (uint8_t*) MESSAGE_BUFFER_SHARE(data, args->capacity,
      MESSAGE_BUFFER_OUT, session->server);
  }
  WinError status = WIN_ERR_NO_MEMORY;
  if ((args->capacity == 0) || (args->data != NULL)) {
    status = winCall_(session, WIN_MSG_GET_WIDGET, args, sizeof(*args));
  }
  if (args->data != NULL) {
    MESSAGE_BUFFER_UNSHARE(args->data, data, MESSAGE_BUFFER_OUT);
  }
  *value = (property == WIN_PROP_TEXT) ? args->length : args->value;
  MESSAGE_ARGS_FREE(args);
  return winErrno(status);
}

/// @fn static inline int winGetText(WinSession *session, uint8_t window,
///   uint8_t widget, char *text, uint16_t capacity)
///
/// @brief Get a widget's text, NUL-terminated, cut to fit capacity.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
/// @param text Where the text goes.
/// @param capacity The bytes at text, its terminator included.
///
/// @return Returns the text's whole length, which may be capacity or more
/// if it was cut, or a negative errno value on failure.
static inline int winGetText(WinSession *session, uint8_t window,
  uint8_t widget, char *text, uint16_t capacity
) {
  uint16_t length = 0;
  int rc = winGetWidget_(session, window, widget, WIN_PROP_TEXT,
    (uint8_t*) text, capacity - 1, &length);
  if (rc < 0) {
    text[0] = '\0';
    return rc;
  }
  text[(length < capacity - 1) ? length : capacity - 1] = '\0';
  return length;
}

/// @fn static inline int winGetValue(WinSession *session, uint8_t window,
///   uint8_t widget)
///
/// @brief Get a widget's value:  a box's state or a list's selection.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
///
/// @return Returns the value, or a negative errno value on failure.
static inline int winGetValue(WinSession *session, uint8_t window,
  uint8_t widget
) {
  uint16_t value = 0;
  int rc = winGetWidget_(session, window, widget, WIN_PROP_VALUE, NULL, 0,
    &value);
  return (rc < 0) ? rc : value;
}

/// @fn static inline int winSetWindow_(WinSession *session, uint8_t window,
///   uint8_t changes, uint8_t state, GfxColor face, const char *title)
///
/// @brief Change a window's title, state or face.
///
/// @param session The connection.
/// @param window The window.
/// @param changes The WIN_SET_* bits.
/// @param state The new WinState, with WIN_SET_STATE.
/// @param face The new color of the body, with WIN_SET_FACE.
/// @param title The new title, with WIN_SET_TITLE.
///
/// @return Returns 0 on success, or a negative errno value on failure.
WIN_OUT_OF_LINE int winSetWindow_(WinSession *session, uint8_t window,
  uint8_t changes, uint8_t state, GfxColor face, const char *title
) {
  MESSAGE_ARGS_ALLOCATE(WinSetWindowArgs, args);
  if (args == NULL) {
    return -ENOMEM;
  }
  size_t length = 0;
  if (title != NULL) {
    length = strlen(title);
    if (length > WIN_TITLE_MAX) {
      length = WIN_TITLE_MAX;
    }
  }
  memset(&args->frame, 0, sizeof(args->frame));
  args->window = window;
  args->changes = changes;
  args->state = state;
  args->face = face;
  args->titleLength = (uint8_t) length;
  args->title = NULL;
  WinError status = WIN_ERR_NO_MEMORY;
  if (length > 0) {
    args->title = (const char*) MESSAGE_BUFFER_SHARE(title, length,
      MESSAGE_BUFFER_IN, session->server);
  }
  if ((length == 0) || (args->title != NULL)) {
    status = winCall_(session, WIN_MSG_SET_WINDOW, args, sizeof(*args));
    if (length > 0) {
      MESSAGE_BUFFER_UNSHARE(args->title, title, MESSAGE_BUFFER_IN);
    }
  }
  MESSAGE_ARGS_FREE(args);
  return winErrno(status);
}

/// @fn static inline int winSetTitle(WinSession *session, uint8_t window,
///   const char *title)
///
/// @brief Change a window's title.
///
/// @param session The connection.
/// @param window The window.
/// @param title The new title, which the window server copies.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetTitle(WinSession *session, uint8_t window,
  const char *title
) {
  return winSetWindow_(session, window, WIN_SET_TITLE, 0, 0, title);
}

/// @fn static inline int winSetState(WinSession *session, uint8_t window,
///   uint8_t state)
///
/// @brief Minimize, maximize or restore a window.
///
/// @param session The connection.
/// @param window The window.
/// @param state The new WinState.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetState(WinSession *session, uint8_t window,
  uint8_t state
) {
  return winSetWindow_(session, window, WIN_SET_STATE, state, 0, NULL);
}

/// @fn static inline int winSetFace(WinSession *session, uint8_t window,
///   GfxColor face)
///
/// @brief Change the color of a window's body.
///
/// @param session The connection.
/// @param window The window.
/// @param face The color.
///
/// @return Returns 0 on success, or a negative errno value on failure.
static inline int winSetFace(WinSession *session, uint8_t window,
  GfxColor face
) {
  return winSetWindow_(session, window, WIN_SET_FACE, 0, face, NULL);
}

/// @fn static inline void winDestroyWindow(WinSession *session,
///   uint8_t window)
///
/// @brief Destroy a window.  The message is one-way, so a window the caller
/// doesn't own is left as it is without an answer.
///
/// @param session The connection.
/// @param window The window.
///
/// @return This function returns no value.
static inline void winDestroyWindow(WinSession *session, uint8_t window) {
  WinWindowArgs *args = MESSAGE_ARGS_CREATE(WinWindowArgs,
    MESSAGE_ARGS_ONE_WAY);
  if (args == NULL) {
    return;
  }
  args->window = window;
  winPost_(session, WIN_MSG_DESTROY_WINDOW, args, sizeof(*args));
}

/// @fn static inline void winRemoveWidget(WinSession *session,
///   uint8_t window, uint8_t widget)
///
/// @brief Remove a widget from a window.  The message is one-way, so a
/// widget the caller doesn't own is left as it is without an answer.
///
/// @param session The connection.
/// @param window The window.
/// @param widget The widget.
///
/// @return This function returns no value.
static inline void winRemoveWidget(WinSession *session, uint8_t window,
  uint8_t widget
) {
  WinWidgetArgs *args = MESSAGE_ARGS_CREATE(WinWidgetArgs,
    MESSAGE_ARGS_ONE_WAY);
  if (args == NULL) {
    return;
  }
  args->window = window;
  args->widget = widget;
  winPost_(session, WIN_MSG_REMOVE_WIDGET, args, sizeof(*args));
}

/// @fn static inline bool winIsDisconnected(ProcessMessage *message)
///
/// @brief Determine whether a message is the window server's
/// WIN_MSG_DISCONNECTED, which the receiver releases.
///
/// @param message A message from the caller's queue.
///
/// @return Returns true if it is, false otherwise.
static inline bool winIsDisconnected(ProcessMessage *message) {
  return processMessageType(message)
    == (WIN_COMMAND_SIGNATURE | WIN_MSG_DISCONNECTED);
}

/// @fn static inline bool winHandleMessage(WinSession *session,
///   ProcessMessage *message)
///
/// @brief Take a message from the caller's queue if it's the window
/// server's:  a WIN_MSG_EVENTS's events are copied for winNextEvent and the
/// message is marked done, and a WIN_MSG_DISCONNECTED ends the session.
///
/// @param session The connection.
/// @param message A message from the caller's queue.
///
/// @return Returns true if the message was the window server's and has been
/// dealt with, false if it is the caller's to deal with.
WIN_OUT_OF_LINE bool winHandleMessage(WinSession *session,
  ProcessMessage *message
) {
  int64_t type = processMessageType(message);
  if (type == (WIN_COMMAND_SIGNATURE | WIN_MSG_EVENTS)) {
    const WinEventsArgs *args
      = (const WinEventsArgs*) processMessageData(message);
    uint8_t count = 0;
    if ((args != NULL) && session->connected) {
      count = args->count;
      if (count > WIN_EVENTS_PER_MESSAGE) {
        count = WIN_EVENTS_PER_MESSAGE;
      }
      memcpy(session->events, args->events, count * sizeof(WinEvent));
    }
    session->numEvents = count;
    session->nextEvent = 0;
    processMessageSetDone(message);
    return true;
  } else if (type == (WIN_COMMAND_SIGNATURE | WIN_MSG_DISCONNECTED)) {
    processMessageRelease(message);
    session->connected = false;
    return true;
  }
  return false;
}

/// @fn static inline int winNextEvent(WinSession *session, WinEvent *event,
///   const struct timespec *timeout)
///
/// @brief Get the next event from the window server, waiting for one if
/// there are none.  A message on the caller's queue that isn't the window
/// server's is finished and dropped.
///
/// @param session The connection.
/// @param event Where to store the event.
/// @param timeout When to stop waiting, or NULL to wait for as long as it
///   takes.
///
/// @return Returns 0 when it stores an event, -ETIMEDOUT if the timeout came
/// first, or -ENOTCONN once the window server has gone away and
/// WIN_EVENT_DISCONNECTED has been returned.
WIN_OUT_OF_LINE int winNextEvent(WinSession *session, WinEvent *event,
  const struct timespec *timeout
) {
  while (true) {
    if (session->nextEvent < session->numEvents) {
      *event = session->events[session->nextEvent++];
      return 0;
    } else if (!session->connected) {
      if (session->reported) {
        return -ENOTCONN;
      }
      session->reported = true;
      memset(event, 0, sizeof(*event));
      event->type = WIN_EVENT_DISCONNECTED;
      return 0;
    }
    ProcessMessage *message
      = processMessageQueueWait((struct timespec*) timeout);
    if (message == NULL) {
      if (timeout != NULL) {
        return -ETIMEDOUT;
      }
    } else if (!winHandleMessage(session, message)) {
      processMessageSetDone(message);
      if (processMessageWaiting(message) == false) {
        processMessageRelease(message);
      }
    }
  }
}

#ifdef __cplusplus
}
#endif

#endif // WINDOW_H
