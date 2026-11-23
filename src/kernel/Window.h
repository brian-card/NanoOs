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

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
/// @param hello The window server's answer to WIN_MSG_HELLO.
typedef struct WinSession {
  ProcessId    server;
  WinHelloArgs hello;
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
    return WIN_ERR_DISCONNECTED;
  }
  processMessageWaitForDone(message, NULL);
  processMessageRelease(message);
  return (WinError) *((uint8_t*) args);
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
  session->server = 0;
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

#ifdef __cplusplus
}
#endif

#endif // WINDOW_H
