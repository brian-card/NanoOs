///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_window_client.c
///
/// @brief             Tests of Window.h's client side, the events and
///                    disconnection a session takes from its queue, against
///                    a fake user API.
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


#include "NanoOsTest.h"

#include <string.h>

// The kernel's overlayMap is a pointer; an overlay's is the map itself.  The
// name differs from the other suites' so that they link together.
#define overlayMap windowClientOverlayMap
#include "../../src/kernel/Window.h"

NanoOsOverlayMap windowClientOverlayMap;

/// @struct FakeMessage
///
/// @brief A stand-in for a ProcessMessage whose elements the fake user API
/// hands out.
///
/// @param type The message's type.
/// @param data The message's arguments.
/// @param size The size of the arguments.
/// @param waiting Whether the sender waits for it to be done.
/// @param done Whether it has been marked done.
/// @param inUse Whether it is still in use, until it is released.
typedef struct FakeMessage {
  int64_t type;
  void   *data;
  size_t  size;
  bool    waiting;
  bool    done;
  bool    inUse;
} FakeMessage;

static NanoOsUserApi fakeUserApi;
static NanoOsApi fakeApi = { .userApi = &fakeUserApi };
static FakeMessage *queue[4];
static int queued;
static int popped;
static int waits;

/// @fn static void* fakeElement(ProcessMessage *message,
///   msg_element_t element)
///
/// @brief Hand out a pointer to a FakeMessage's element.
///
/// @param message The FakeMessage, cast.
/// @param element The element.
///
/// @return Returns a pointer to the element, or NULL for one it doesn't have.
static void* fakeElement(ProcessMessage *message, msg_element_t element) {
  FakeMessage *fake = (FakeMessage*) message;
  if (element == MSG_ELEMENT_TYPE) {
    return &fake->type;
  } else if (element == MSG_ELEMENT_DATA) {
    return &fake->data;
  } else if (element == MSG_ELEMENT_SIZE) {
    return &fake->size;
  } else if (element == MSG_ELEMENT_WAITING) {
    return &fake->waiting;
  } else if (element == MSG_ELEMENT_DONE) {
    return &fake->done;
  } else if (element == MSG_ELEMENT_IN_USE) {
    return &fake->inUse;
  }
  return NULL;
}

/// @fn static int fakeSetDone(ProcessMessage *message)
///
/// @brief Mark a FakeMessage done.
///
/// @param message The FakeMessage, cast.
///
/// @return Returns 0.
static int fakeSetDone(ProcessMessage *message) {
  ((FakeMessage*) message)->done = true;
  return 0;
}

/// @fn static int fakeRelease(ProcessMessage *message)
///
/// @brief Release a FakeMessage.
///
/// @param message The FakeMessage, cast.
///
/// @return Returns 0.
static int fakeRelease(ProcessMessage *message) {
  ((FakeMessage*) message)->inUse = false;
  return 0;
}

/// @fn static ProcessMessage* fakeWait(const struct timespec *ts)
///
/// @brief Hand out the next queued FakeMessage, or NULL as if the timeout
/// had passed.
///
/// @param ts The timeout, which is ignored.
///
/// @return Returns the message, or NULL when the queue is empty.
static ProcessMessage* fakeWait(const struct timespec *ts) {
  (void) ts;
  waits++;
  if (popped == queued) {
    return NULL;
  }
  return (ProcessMessage*) queue[popped++];
}

/// @fn static void useFakeApi(void)
///
/// @brief Point the overlay map at the fake user API, with an empty queue.
///
/// @return This function returns no value.
static void useFakeApi(void) {
  memset(&fakeUserApi, 0, sizeof(fakeUserApi));
  fakeUserApi.processMessageElement = fakeElement;
  fakeUserApi.processMessageSetDone = fakeSetDone;
  fakeUserApi.processMessageRelease = fakeRelease;
  fakeUserApi.comessageQueueWait = fakeWait;
  windowClientOverlayMap.header.osApi = &fakeApi;
  queued = 0;
  popped = 0;
  waits = 0;
}

/// @fn static void connected(WinSession *session)
///
/// @brief Fill in a session as winConnect leaves it.
///
/// @param session The session.
///
/// @return This function returns no value.
static void connected(WinSession *session) {
  memset(session, 0, sizeof(*session));
  session->server = 2;
  session->connected = true;
}

/// @fn static FakeMessage events(WinEventsArgs *args, uint8_t count)
///
/// @brief Make a WIN_MSG_EVENTS carrying events of types 1, 2, and so on.
///
/// @param args The arguments to fill in.
/// @param count The number of events.
///
/// @return Returns the message.
static FakeMessage events(WinEventsArgs *args, uint8_t count) {
  memset(args, 0, sizeof(*args));
  args->count = count;
  for (uint8_t ii = 0; ii < count; ii++) {
    args->events[ii].type = (uint8_t) (ii + 1);
    args->events[ii].window = 7;
  }
  FakeMessage message = { WIN_COMMAND_SIGNATURE | WIN_MSG_EVENTS, args,
    sizeof(*args), true, false, true };
  return message;
}

NANO_OS_TEST(window_client, events_are_copied_and_the_message_marked_done) {
  useFakeApi();
  WinSession session;
  connected(&session);
  WinEventsArgs args;
  FakeMessage message = events(&args, 2);
  NANO_OS_ASSERT_TRUE(winHandleMessage(&session,
    (ProcessMessage*) &message));
  NANO_OS_ASSERT_TRUE(message.done);
  NANO_OS_ASSERT_TRUE(message.inUse);
  NANO_OS_ASSERT_EQ_INT(2, session.numEvents);
  NANO_OS_ASSERT_EQ_INT(0, session.nextEvent);
  NANO_OS_ASSERT_EQ_INT(1, session.events[0].type);
  NANO_OS_ASSERT_EQ_INT(2, session.events[1].type);
  NANO_OS_ASSERT_EQ_INT(7, session.events[1].window);
}

NANO_OS_TEST(window_client, disconnected_is_released_and_ends_the_session) {
  useFakeApi();
  WinSession session;
  connected(&session);
  FakeMessage message = { WIN_COMMAND_SIGNATURE | WIN_MSG_DISCONNECTED, NULL,
    0, false, false, true };
  NANO_OS_ASSERT_TRUE(winHandleMessage(&session,
    (ProcessMessage*) &message));
  NANO_OS_ASSERT_FALSE(message.inUse);
  NANO_OS_ASSERT_FALSE(session.connected);
}

NANO_OS_TEST(window_client, another_message_is_left_to_the_caller) {
  useFakeApi();
  WinSession session;
  connected(&session);
  FakeMessage message = { 0x1234, NULL, 0, true, false, true };
  NANO_OS_ASSERT_FALSE(winHandleMessage(&session,
    (ProcessMessage*) &message));
  NANO_OS_ASSERT_FALSE(message.done);
  NANO_OS_ASSERT_TRUE(message.inUse);
  NANO_OS_ASSERT_TRUE(session.connected);
}

NANO_OS_TEST(window_client, next_event_returns_each_event_then_times_out) {
  useFakeApi();
  WinSession session;
  connected(&session);
  WinEventsArgs args;
  FakeMessage message = events(&args, 2);
  queue[queued++] = &message;
  struct timespec soon = { 0, 0 };
  WinEvent event;
  NANO_OS_ASSERT_EQ_INT(0, winNextEvent(&session, &event, &soon));
  NANO_OS_ASSERT_EQ_INT(1, event.type);
  NANO_OS_ASSERT_EQ_INT(0, winNextEvent(&session, &event, &soon));
  NANO_OS_ASSERT_EQ_INT(2, event.type);
  NANO_OS_ASSERT_EQ_INT(1, waits);
  NANO_OS_ASSERT_EQ_INT(-ETIMEDOUT, winNextEvent(&session, &event, &soon));
  NANO_OS_ASSERT_EQ_INT(2, waits);
}

NANO_OS_TEST(window_client, a_lost_server_is_reported_once_then_enotconn) {
  useFakeApi();
  WinSession session;
  connected(&session);
  FakeMessage message = { WIN_COMMAND_SIGNATURE | WIN_MSG_DISCONNECTED, NULL,
    0, false, false, true };
  queue[queued++] = &message;
  WinEvent event;
  NANO_OS_ASSERT_EQ_INT(0, winNextEvent(&session, &event, NULL));
  NANO_OS_ASSERT_EQ_INT(WIN_EVENT_DISCONNECTED, event.type);
  NANO_OS_ASSERT_EQ_INT(-ENOTCONN, winNextEvent(&session, &event, NULL));
  NANO_OS_ASSERT_EQ_INT(1, waits);
}

NANO_OS_TEST(window_client, another_message_is_finished_and_dropped) {
  useFakeApi();
  WinSession session;
  connected(&session);
  FakeMessage other = { 0x1234, NULL, 0, false, false, true };
  queue[queued++] = &other;
  struct timespec soon = { 0, 0 };
  WinEvent event;
  NANO_OS_ASSERT_EQ_INT(-ETIMEDOUT, winNextEvent(&session, &event, &soon));
  NANO_OS_ASSERT_TRUE(other.done);
  NANO_OS_ASSERT_FALSE(other.inUse);
}

NANO_OS_TEST(window_client, statuses_become_negative_errno_values) {
  NANO_OS_ASSERT_EQ_INT(0, winErrno(WIN_OK));
  NANO_OS_ASSERT_EQ_INT(-EPERM, winErrno(WIN_ERR_PERMISSION));
  NANO_OS_ASSERT_EQ_INT(-ENOMEM, winErrno(WIN_ERR_NO_MEMORY));
  NANO_OS_ASSERT_EQ_INT(-EINVAL, winErrno(WIN_ERR_INVALID));
  NANO_OS_ASSERT_EQ_INT(-ENOENT, winErrno(WIN_ERR_NOT_FOUND));
  NANO_OS_ASSERT_EQ_INT(-ENOTSUP, winErrno(WIN_ERR_NOT_SUPPORTED));
  NANO_OS_ASSERT_EQ_INT(-ENOTCONN, winErrno(WIN_ERR_DISCONNECTED));
}

char* nanoOsStrError(int errnum);

NANO_OS_TEST(window_client, enotconn_is_the_last_error_and_has_its_message) {
  NANO_OS_ASSERT_EQ_INT(ENOTCONN, ELAST);
  NANO_OS_ASSERT_STR_EQ("Not connected", nanoOsStrError(ENOTCONN));
  NANO_OS_ASSERT_STR_EQ("Too many open files", nanoOsStrError(EMFILE));
  NANO_OS_ASSERT_STR_EQ("Unspecified error", nanoOsStrError(ELAST + 1));
}
