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
/// @brief The window server's side of the window protocol's connections:
/// registering with the scheduler, keeping the connected clients, and
/// sending them their events.  Entered once per connection message, and when
/// there are events to send.

#include "Wm.h"
#include "UserProcesses.h"

/// @def WM_DISCONNECT_TRIES
///
/// @brief How many times wmClientEnd yields for a free message before it
/// gives up on the clients left.
#define WM_DISCONNECT_TRIES 100

/// @fn static WmClient* wmClientFind(WmState *state, ProcessId pid)
///
/// @brief Find a connected client.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
///
/// @return Returns the client, or NULL if it isn't connected.
static WmClient* wmClientFind(WmState *state, ProcessId pid) {
  for (uint8_t ii = 0; ii < state->numClients; ii++) {
    if (state->clients[ii]->pid == pid) {
      return state->clients[ii];
    }
  }
  return NULL;
}

/// @fn static void wmRemoveWindows(WmState *state, ProcessId pid)
///
/// @brief Have the Window overlay take away every window a client owns.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
///
/// @return This function returns no value.
static void wmRemoveWindows(WmState *state, ProcessId pid) {
  state->opPid = pid;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Window", "wmWindowRemoveOwned",
    state);
}

/// @fn static bool wmCollect(WmState *state, WmClient *client)
///
/// @brief Release a client's events message once it has marked it done,
/// which lets the client be sent more.
///
/// @param state The window manager's state.
/// @param client The client.
///
/// @return Returns true if the client has no events message waiting on it.
static bool wmCollect(WmState *state, WmClient *client) {
  if ((client->message != NULL) && processMessageDone(client->message)) {
    processMessageRelease(client->message);
    client->message = NULL;
    state->numPosts--;
  }
  return client->message == NULL;
}

/// @fn static void wmForgetClient(WmState *state, ProcessId pid)
///
/// @brief Drop a client that has ended:  its windows, and what it was
/// connected with.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
///
/// @return This function returns no value.
static void wmForgetClient(WmState *state, ProcessId pid) {
  wmRemoveWindows(state, pid);
  for (uint8_t ii = 0; ii < state->numClients; ii++) {
    WmClient *client = state->clients[ii];
    if (client->pid == pid) {
      if (client->message != NULL) {
        processMessageRelease(client->message);
        state->numPosts--;
      }
      MESSAGE_ARGS_DESTROY(client->args);
      free(client);
      state->numClients--;
      state->clients[ii] = state->clients[state->numClients];
      return;
    }
  }
}

/// @fn static void wmHello(WmState *state, ProcessId pid,
///   WinHelloArgs *helloArgs)
///
/// @brief Add a client to the connected clients and tell it the screen, the
/// work area and the theme's sizes.
///
/// @param state The window manager's state.
/// @param pid The client's ProcessId.
/// @param helloArgs The message's arguments.
///
/// @return This function returns no value.
static void wmHello(WmState *state, ProcessId pid, WinHelloArgs *helloArgs) {
  WmClient *client = wmClientFind(state, pid);
  if (client == NULL) {
    if (state->numClients == WM_MAX_CLIENTS) {
      helloArgs->status = WIN_ERR_NO_MEMORY;
      return;
    }
    client = (WmClient*) calloc(1, sizeof(WmClient));
    if (client != NULL) {
      client->args = MESSAGE_ARGS_CREATE(WinEventsArgs,
        MESSAGE_ARGS_ROUND_TRIP);
    }
    if ((client == NULL) || (client->args == NULL)) {
      free(client);
      helloArgs->status = WIN_ERR_NO_MEMORY;
      return;
    }
    client->pid = pid;
    state->clients[state->numClients++] = client;
  }
  client->leaving = false;

  GfxDisplay *display = &state->display;
  helloArgs->role = WIN_ROLE_NONE;
  helloArgs->cellWidth = display->builtInFont.glyphW;
  helloArgs->cellHeight = display->builtInFont.glyphH;
  helloArgs->titleHeight = WM_TITLE_HEIGHT;
  helloArgs->borderWidth = WM_BORDER;
  helloArgs->buttonHeight = WM_TITLE_HEIGHT;
  helloArgs->scrollBarWidth = 0;
  helloArgs->screenWidth = display->width;
  helloArgs->screenHeight = display->height;
  helloArgs->workArea = state->workArea;
  helloArgs->status = WIN_OK;
}

/// @fn static void wmQueue(WmClient *client, const WinEvent *event)
///
/// @brief Add an event to what a client is sent next.  A RESIZED replaces
/// one for the same window that hasn't been sent, and a full queue ends with
/// WIN_EVENT_OVERFLOW.
///
/// @param client The client.
/// @param event The event.
///
/// @return This function returns no value.
static void wmQueue(WmClient *client, const WinEvent *event) {
  if (client->leaving) {
    return;
  }
  for (uint8_t ii = 0; ii < client->numPending; ii++) {
    WinEvent *pending = &client->pending[ii];
    if ((event->type == WIN_EVENT_RESIZED) && (pending->type == event->type)
      && (pending->window == event->window)
    ) {
      *pending = *event;
      return;
    }
  }
  if (client->numPending < WM_MAX_PENDING) {
    client->pending[client->numPending++] = *event;
  } else {
    memset(&client->pending[WM_MAX_PENDING - 1], 0, sizeof(WinEvent));
    client->pending[WM_MAX_PENDING - 1].type = WIN_EVENT_OVERFLOW;
  }
}

/// @fn static void wmPost(WmState *state, WmClient *client)
///
/// @brief Send a client as many of its waiting events as one WIN_MSG_EVENTS
/// holds, if it has finished with the last one and the window server isn't
/// already waiting on WM_MAX_POSTS clients.
///
/// @param state The window manager's state.
/// @param client The client.
///
/// @return This function returns no value.
static void wmPost(WmState *state, WmClient *client) {
  if ((client->numPending == 0) || !wmCollect(state, client)
    || (state->numPosts >= WM_MAX_POSTS)
  ) {
    return;
  }
  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return;
  }
  uint8_t count = client->numPending;
  if (count > WIN_EVENTS_PER_MESSAGE) {
    count = WIN_EVENTS_PER_MESSAGE;
  }
  client->args->count = count;
  memcpy(client->args->events, client->pending, count * sizeof(WinEvent));
  processMessageInit(message, WIN_COMMAND_SIGNATURE | WIN_MSG_EVENTS,
    client->args, sizeof(*client->args), /* waiting= */ true);
  if (sendProcessMessageToPid(client->pid, message) != processSuccess) {
    processMessageRelease(message);
    client->numPending = 0;
    return;
  }
  client->message = message;
  state->numPosts++;
  client->numPending -= count;
  memmove(client->pending, &client->pending[count],
    client->numPending * sizeof(WinEvent));
}

/// @fn void* wmClientFlush(void *args)
///
/// @brief Hand each event the other overlays left in the outbox to its
/// client, collect the events messages clients have finished with, and send
/// what's waiting.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmClientFlush(void *args) {
  WmState *state = (WmState*) args;
  for (uint8_t ii = 0; ii < state->numOutbox; ii++) {
    WmClient *client = wmClientFind(state, state->outbox[ii].pid);
    if (client != NULL) {
      wmQueue(client, &state->outbox[ii].event);
    }
  }
  state->numOutbox = 0;
  state->eventsWaiting = false;
  for (uint8_t ii = 0; ii < state->numClients; ii++) {
    WmClient *client = state->clients[ii];
    wmCollect(state, client);
    wmPost(state, client);
    state->eventsWaiting |= (client->numPending > 0);
  }
  return args;
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
/// @brief Handle the connection message in the state:  WIN_MSG_HELLO
/// connects a client, and WIN_MSG_GOODBYE and WIN_MSG_CLIENT_ENDED take its
/// windows away.
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
  ProcessId pid = processMessageFrom(message)->processId;
  WmClient *client = wmClientFind(state, pid);

  if (type == WIN_MSG_CLIENT_ENDED) {
    wmForgetClient(state, (ProcessId) (uintptr_t) data);
  } else if (type == WIN_MSG_HELLO) {
    wmHello(state, pid, (WinHelloArgs*) data);
  } else if ((type == WIN_MSG_GOODBYE) && (client != NULL)) {
    wmRemoveWindows(state, pid);
    client->leaving = true;
    client->numPending = 0;
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

/// @fn static void printTimes(const char *kind, WmTimes *times)
///
/// @brief Print the number of frames of a kind, and the median and longest
/// time of the first WM_TIMED_FRAMES of them.
///
/// @param kind The kind of frame.
/// @param times The frames' times, which are sorted.
///
/// @return This function returns no value.
static void printTimes(const char *kind, WmTimes *times) {
  uint16_t kept = times->count;
  if (kept > WM_TIMED_FRAMES) {
    kept = WM_TIMED_FRAMES;
  }
  for (uint16_t ii = 1; ii < kept; ii++) {
    uint16_t value = times->ms[ii];
    uint16_t jj = ii;
    while ((jj > 0) && (times->ms[jj - 1] > value)) {
      times->ms[jj] = times->ms[jj - 1];
      jj--;
    }
    times->ms[jj] = value;
  }
  unsigned int median = 0;
  unsigned int longest = 0;
  if (kept > 0) {
    median = times->ms[kept / 2];
    longest = times->ms[kept - 1];
  }
  printf("sysnook: %s frames %u median %u ms max %u ms\n", kind,
    (unsigned int) times->count, median, longest);
}

/// @fn void* wmClientEnd(void *args)
///
/// @brief Tell every connected client that the window server is going away,
/// let go of what was kept for it, its windows and their sessions included,
/// and print the frame and echo times if --timing asked for them.  The
/// kernel's message pool is smaller than the number of clients, so when it
/// is empty this drains its own queue and yields for the clients to release
/// the messages they've read, for a bounded time, since a client might never
/// read its queue.
///
/// @param args The window manager's state.
///
/// @return Always returns the provided pointer.
void* wmClientEnd(void *args) {
  WmState *state = (WmState*) args;
  for (uint8_t ii = 0; ii < state->numClients; ii++) {
    ProcessMessage *message = getAvailableMessage();
    for (int tries = 0; (message == NULL) && (tries < WM_DISCONNECT_TRIES);
      tries++
    ) {
      wmClientDrain();
      processYield();
      message = getAvailableMessage();
    }
    if (message != NULL) {
      processMessageInit(message, WIN_COMMAND_SIGNATURE | WIN_MSG_DISCONNECTED,
        NULL, 0, /* waiting= */ false);
      if (sendProcessMessageToPid(state->clients[ii]->pid, message)
        != processSuccess
      ) {
        processMessageRelease(message);
      }
    }
  }
  for (int tries = 0; (state->numPosts > 0) && (tries < WM_DISCONNECT_TRIES);
    tries++
  ) {
    for (uint8_t ii = 0; ii < state->numClients; ii++) {
      wmCollect(state, state->clients[ii]);
    }
    processYield();
  }
  while (state->numClients > 0) {
    WmClient *client = state->clients[--state->numClients];
    wmRemoveWindows(state, client->pid);
    if (client->message != NULL) {
      processMessageRelease(client->message);
    }
    MESSAGE_ARGS_DESTROY(client->args);
    free(client);
  }
  state->numPosts = 0;
  if (state->times != NULL) {
    printTimes("move", &state->times[WM_TIMED_MOVE]);
    printTimes("resize", &state->times[WM_TIMED_RESIZE]);
    printTimes("echo", &state->times[WM_TIMED_ECHO]);
  }
  return args;
}
