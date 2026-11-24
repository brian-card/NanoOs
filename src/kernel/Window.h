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

/// @fn static inline int winCreateWindow(WinSession *session,
///   const char *title, GfxRect frame, uint8_t layer, uint8_t flags)
///
/// @brief Create a window in the normal state.
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
WIN_OUT_OF_LINE int winCreateWindow(WinSession *session, const char *title,
  GfxRect frame, uint8_t layer, uint8_t flags
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
  args->templateSize = 0;
  args->frame = frame;
  args->title = NULL;
  args->widgets = NULL;
  WinError status = WIN_ERR_NO_MEMORY;
  if (length > 0) {
    args->title = (const char*) MESSAGE_BUFFER_SHARE(title, length,
      MESSAGE_BUFFER_IN, session->server);
  }
  if ((length == 0) || (args->title != NULL)) {
    status = winCall_(session, WIN_MSG_CREATE_WINDOW, args, sizeof(*args));
    if (length > 0) {
      MESSAGE_BUFFER_UNSHARE(args->title, title, MESSAGE_BUFFER_IN);
    }
  }
  int result = winErrno(status);
  if (status == WIN_OK) {
    result = args->window;
  }
  MESSAGE_ARGS_FREE(args);
  return result;
}

/// @fn static inline int winSetWindow_(WinSession *session, uint8_t window,
///   uint8_t changes, uint8_t state, const char *title)
///
/// @brief Change a window's title or state.
///
/// @param session The connection.
/// @param window The window.
/// @param changes The WIN_SET_* bits.
/// @param state The new WinState, with WIN_SET_STATE.
/// @param title The new title, with WIN_SET_TITLE.
///
/// @return Returns 0 on success, or a negative errno value on failure.
WIN_OUT_OF_LINE int winSetWindow_(WinSession *session, uint8_t window,
  uint8_t changes, uint8_t state, const char *title
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
  return winSetWindow_(session, window, WIN_SET_TITLE, 0, title);
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
  return winSetWindow_(session, window, WIN_SET_STATE, state, NULL);
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
