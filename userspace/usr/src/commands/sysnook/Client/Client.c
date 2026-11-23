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

/// @file Client.c
///
/// @brief The window server's side of the window protocol:  registering with
/// the scheduler, keeping the connected clients, and answering their
/// messages.  Entered once per message from a client.

#include "Wm.h"
#include "UserProcesses.h"

/// @def WM_DISCONNECT_TRIES
///
/// @brief How many times wmClientEnd yields for a free message before it
/// gives up on the clients left.
#define WM_DISCONNECT_TRIES 100

/// @fn static int wmClientIndex(WmState *state, ProcessId pid)
///
/// @brief Find a connected client.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
///
/// @return Returns the client's index in state->clients, or -1 if it isn't
/// connected.
static int wmClientIndex(WmState *state, ProcessId pid) {
  for (int ii = 0; ii < (int) state->numClients; ii++) {
    if (state->clients[ii] == pid) {
      return ii;
    }
  }
  return -1;
}

/// @fn static void wmForgetClient(WmState *state, ProcessId pid)
///
/// @brief Drop a client from the connected clients, if it was one.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
///
/// @return This function returns no value.
static void wmForgetClient(WmState *state, ProcessId pid) {
  int index = wmClientIndex(state, pid);
  if (index >= 0) {
    state->numClients--;
    state->clients[index] = state->clients[state->numClients];
  }
}

/// @fn static void wmHello(WmState *state, ProcessId pid,
///   WinHelloArgs *helloArgs)
///
/// @brief Add a client to the connected clients and tell it the screen and
/// the theme's sizes.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
/// @param helloArgs The message's arguments.
///
/// @return This function returns no value.
static void wmHello(WmState *state, ProcessId pid, WinHelloArgs *helloArgs) {
  if (wmClientIndex(state, pid) < 0) {
    if (state->numClients == WM_MAX_CLIENTS) {
      helloArgs->status = WIN_ERR_NO_MEMORY;
      return;
    }
    state->clients[state->numClients++] = pid;
  }

  GfxDisplay *display = &state->display;
  helloArgs->role = WIN_ROLE_NONE;
  helloArgs->cellWidth = display->builtInFont.glyphW;
  helloArgs->cellHeight = display->builtInFont.glyphH;
  helloArgs->titleHeight = WM_TITLE_HEIGHT;
  helloArgs->borderWidth = 0;
  helloArgs->buttonHeight = 0;
  helloArgs->scrollBarWidth = 0;
  helloArgs->screenWidth = display->width;
  helloArgs->screenHeight = display->height;
  helloArgs->workArea.x = 0;
  helloArgs->workArea.y = 0;
  helloArgs->workArea.w = (int16_t) display->width;
  helloArgs->workArea.h = (int16_t) display->height;
  helloArgs->status = WIN_OK;
}

/// @fn void* wmClientStart(void *args)
///
/// @brief Register with the scheduler as the window server, so that other
/// processes can connect.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmClientStart(void *args) {
  if (windowServerRegister() != 0) {
    fputs("sysnook: not registered as the window server\n", stderr);
  }
  return args;
}

/// @fn void* wmClientMessage(void *args)
///
/// @brief Handle the window protocol message in the state:  WIN_MSG_HELLO
/// connects a client, WIN_MSG_GOODBYE and WIN_MSG_CLIENT_ENDED forget one,
/// and every other request is answered WIN_ERR_NOT_SUPPORTED.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmClientMessage(void *args) {
  WmState *state = (WmState*) args;
  ProcessMessage *message = state->message;
  uint8_t type = (uint8_t) processMessageType(message);
  void *data = processMessageData(message);
  bool waiting = processMessageWaiting(message);

  if (type == WIN_MSG_CLIENT_ENDED) {
    wmForgetClient(state, (ProcessId) (uintptr_t) data);
  } else if (type == WIN_MSG_HELLO) {
    wmHello(state, processMessageFrom(message)->processId,
      (WinHelloArgs*) data);
  } else if (type == WIN_MSG_GOODBYE) {
    wmForgetClient(state, processMessageFrom(message)->processId);
  } else if (data != NULL) {
    if (waiting) {
      *((uint8_t*) data) = WIN_ERR_NOT_SUPPORTED;
    } else {
      MESSAGE_ARGS_DESTROY(data);
    }
  }

  processMessageSetDone(message);
  if (waiting == false) {
    processMessageRelease(message);
  }
  return args;
}

/// @fn static void wmClientDrain(void)
///
/// @brief Finish every message waiting in the window server's queue without
/// acting on it, as the window server ends:  a request keeps the
/// WIN_ERR_DISCONNECTED its sender preset, and a one-way message's arguments
/// are destroyed.  The scheduler's WIN_MSG_CLIENT_ENDED messages come from
/// the kernel's message pool, which they would otherwise keep empty.
///
/// @return This function returns no value.
static void wmClientDrain(void) {
  ProcessMessage *message = NULL;
  while ((message = processMessageQueuePop()) != NULL) {
    int64_t type = processMessageType(message);
    void *data = processMessageData(message);
    bool waiting = processMessageWaiting(message);
    if ((waiting == false) && (data != NULL)
      && ((type & ~((int64_t) 0xff)) == WIN_COMMAND_SIGNATURE)
      && (type != (WIN_COMMAND_SIGNATURE | WIN_MSG_CLIENT_ENDED))
    ) {
      MESSAGE_ARGS_DESTROY(data);
    }
    processMessageSetDone(message);
    if (waiting == false) {
      processMessageRelease(message);
    }
  }
}

/// @fn void* wmClientEnd(void *args)
///
/// @brief Tell every connected client that the window server is going away.
/// The kernel's message pool is smaller than the number of clients, so when it
/// is empty this drains its own queue and yields for the clients to release
/// the messages they've read, for a bounded time, since a client might never
/// read its queue.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmClientEnd(void *args) {
  WmState *state = (WmState*) args;
  for (int ii = 0; ii < (int) state->numClients; ii++) {
    ProcessMessage *message = getAvailableMessage();
    for (int tries = 0; (message == NULL) && (tries < WM_DISCONNECT_TRIES);
      tries++
    ) {
      wmClientDrain();
      processYield();
      message = getAvailableMessage();
    }
    if (message == NULL) {
      break;
    }
    processMessageInit(message, WIN_COMMAND_SIGNATURE | WIN_MSG_DISCONNECTED,
      NULL, 0, /* waiting= */ false);
    if (sendProcessMessageToPid(state->clients[ii], message)
      != processSuccess
    ) {
      processMessageRelease(message);
    }
  }
  state->numClients = 0;
  return args;
}
