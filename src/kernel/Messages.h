///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              03.07.2025
///
/// @file              Messages.h
///
/// @brief             Message-only definitions and functionality.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
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
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef MESSAGES_H
#define MESSAGES_H

#include "stdint.h"
#include "stdbool.h"
#include "stdlib.h"
#include "time.h"

// Do forward declarations before including headers from this library to avoid
// circular dependencies.
typedef struct msg_t msg_t;
typedef struct msg_q_t msg_q_t;

#include "CoroutineSync.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Return statuses.  These need to match the corresponding thrd_* values.
#define msg_success    0
#define msg_busy       1
#define msg_error      2
#define msg_nomem      3
#define msg_timedout   4

// Mutex types
#define msg_mtx_plain     0
#define msg_mtx_recursive 1
#define msg_mtx_timed     2

/// @enum msg_safety_t
///
/// @brief Indicator of what level of safety to employ with message operations
/// (process, thread, or coroutine).
typedef enum msg_safety_t {
#ifdef THREAD_SAFE_COROUTINES
  MSG_THRD_SAFE,
#endif // THREAD_SAFE_COROUTINES
  MSG_CORO_SAFE,
} msg_safety_t;

// Forward declarations so that the function pointer typedefs will work.
typedef struct msg_t msg_t;
typedef struct msg_q_t msg_q_t;

/// @struct msg_sync_t
///
/// @brief Structure of function pointers that will be used for synchronization
/// operations for the queues and messages.
typedef struct msg_sync_t {
  int      (*mtx_init)(void *mtx, int type);
  int      (*mtx_lock)(void *mtx);
  int      (*mtx_unlock)(void *mtx);
  void     (*mtx_destroy)(void *mtx);
  int      (*mtx_timedlock)(void *mtx, const struct timespec *ts);
  int      (*mtx_trylock)(void *mtx);
  int      (*cnd_broadcast)(void *cond);
  void     (*cnd_destroy)(void *cond);
  int      (*cnd_init)(void *cond);
  int      (*cnd_signal)(void *cond);
  int      (*cnd_timedwait)(void *cond, void *mtx, const struct timespec *ts);
  int      (*cnd_wait)(void *cond, void *mtx);
} msg_sync_t;

// Array of these structures declared and instantiated in Messages.c.
extern msg_sync_t msg_sync_array[];

/// @union msg_mtx_t
///
/// Union of all possible valid mutexe types for a msg_t or msg_q_t.
typedef union msg_mtx_t {
#ifdef THREAD_SAFE_COROUTINES
  mtx_t thrd_mtx;
#endif // THREAD_SAFE_COROUTINES
  coro_mtx_t coro_mtx;
} msg_mtx_t;

/// @union msg_cnd_t
///
/// Union of all possible valid condition types for a msg_t or msg_q_t.
typedef union msg_cnd_t {
#ifdef THREAD_SAFE_COROUTINES
  cnd_t thrd_cnd;
#endif // THREAD_SAFE_COROUTINES
  coro_cnd_t coro_cnd;
} msg_cnd_t;

/// @union msg_endpoint_t
///
/// @brief Union of all possible valid endpoints for a msg_t to be sent to (or
/// received from).
typedef union msg_endpoint_t {
#ifdef THREAD_SAFE_COROUTINES
  thrd_t thrd;
#endif // THREAD_SAFE_COROUTINES
  coro_t coro;
} msg_endpoint_t;

/// @struct msg_t
///
/// @brief Definition for a message that can be pushed onto a message queue.
///
/// @param type Integer value designating the type of message for the receiving
///   entity.
/// @param data A pointer to the data of the message.
/// @param size The number of bytes pointed to by the data pointer.
/// @param next A pointer to the next msg_t in a message queue.
/// @param waiting A Boolean flag to indicate whether or not the sender is
///   waiting on a response message from the recipient of the message.
/// @param done A Boolean flag to indicate whether or not the receiving entity
///   has handled the message yet.
/// @param in_use A Boolean flag to indicate whether or not this msg_t is in
///   use.
/// @param from A msg_endpoint_t union that represents the sending entity.
/// @param to A msg_endpoint_t union that represents the receiveing entity.
/// @param condition A msg_cnd_t union that will allow for signalling between
///   the endpoints.
/// @param lock A msg_mtx_t union to guard the condition.
/// @param configured Whether or not the members of the message that require
///   initialization have been configured yet.
/// @param dynamically_allocated Whether or not the message was dynamically
///   allocated with msg_create.
/// @param deallocator A pointer to a function that will deallocate a msg_t in a
///   custom way.
/// @param msg_sync A pointer to the msg_sync_t that defines the synchronization
///   primitive functions to use with the conditions and mutexes in this object.
/// @param reply_to A pointer to the msg_q_t to reply to.
typedef struct msg_t {
  int64_t type;
  void *data;
  size_t size;
  struct msg_t *next;
  bool waiting;
  bool done;
  bool in_use;
  msg_endpoint_t from;
  msg_endpoint_t to;
  msg_cnd_t condition;
  msg_mtx_t lock;
  bool configured;
  bool dynamically_allocated;
  msg_sync_t *msg_sync;
  msg_q_t *reply_to;
} msg_t;

/// @struct msg_q_t
///
/// @brief Definition for a thread's message queue.
///
/// @param head The head of the message queue.  Messages will be removed from
///   this pointer.
/// @param tail The tail of the message queue.  Messages will be added to this
///   pointer.
/// @param condition A msg_cnd_t union that will allow for signalling between
///   the endpoints.
/// @param lock A msg_mtx_t union to guard the condition.
/// @param dynamically_allocated Whether or not the message queue was
///   dynamically allocated.
/// @param deallocator A pointer to a function that will deallocate a msg_q_t in
///   a custom way.
/// @param msg_sync A pointer to the msg_sync_t that defines the synchronization
///   primitive functions to use with the conditions and mutexes in this object.
typedef struct msg_q_t {
  msg_t *head;
  msg_t *tail;
  msg_cnd_t condition;
  msg_mtx_t lock;
  bool dynamically_allocated;
  msg_sync_t *msg_sync;
} msg_q_t;

/// @enum msg_element_t
///
/// @brief Enumeration of member elements of msg_t that are user-accessible.
typedef enum msg_element_t {
  MSG_ELEMENT_TYPE,
  MSG_ELEMENT_DATA,
  MSG_ELEMENT_SIZE,
  MSG_ELEMENT_WAITING,
  MSG_ELEMENT_DONE,
  MSG_ELEMENT_IN_USE,
  MSG_ELEMENT_FROM,
  MSG_ELEMENT_TO,
  NUM_MSG_ELEMENTS
} msg_element_t;

// Message functions
msg_t* msg_create(msg_safety_t msg_safety);
msg_t* msg_destroy(msg_t *msg);
int msg_init(msg_t *msg, msg_safety_t msg_safety,
  int64_t type, void *data, size_t size, bool waiting);
int msg_release(msg_t *msg);
int msg_set_done(msg_t *msg);
int msg_wait_for_done(msg_t *msg, const struct timespec *ts);
msg_t* msg_wait_for_reply(msg_t *sent,
  bool release, const struct timespec *ts);
msg_t* msg_wait_for_reply_with_type(msg_t *sent,
  bool release, int64_t type, const struct timespec *ts);

// Message element accessors
void* msg_element(msg_t *msg, msg_element_t msg_element);
#define msg_type(msg_ptr) \
  (*((int64_t*) msg_element((msg_ptr), MSG_ELEMENT_TYPE)))
#define msg_data(msg_ptr) \
  (*((void**) msg_element((msg_ptr), MSG_ELEMENT_DATA)))
#define msg_size(msg_ptr) \
  (*((size_t*) msg_element((msg_ptr), MSG_ELEMENT_SIZE)))
#define msg_waiting(msg_ptr) \
  (*((bool*) msg_element((msg_ptr), MSG_ELEMENT_WAITING)))
#define msg_done(msg_ptr) \
  (*((bool*) msg_element((msg_ptr), MSG_ELEMENT_DONE)))
#define msg_in_use(msg_ptr) \
  (*((bool*) msg_element((msg_ptr), MSG_ELEMENT_IN_USE)))
#define msg_from(msg_ptr) \
  (*((msg_endpoint_t*) msg_element((msg_ptr), MSG_ELEMENT_FROM)))
#define msg_to(msg_ptr) \
  (*((msg_endpoint_t*) msg_element((msg_ptr), MSG_ELEMENT_TO)))

// Message queue functions
msg_q_t* msg_q_create(msg_q_t *q, msg_safety_t msg_safety);
int msg_q_destroy(msg_q_t *queue);
msg_t* msg_q_peek(msg_q_t *queue);
msg_t* msg_q_pop(msg_q_t *queue);
msg_t* msg_q_pop_type(msg_q_t *queue, int64_t type);
msg_t* msg_q_wait(msg_q_t *queue, const struct timespec *ts);
msg_t* msg_q_wait_for_type(msg_q_t *queue, int64_t type,
  const struct timespec *ts);
int msg_q_push(msg_q_t *queue, msg_q_t *reply_to, msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif // MESSAGES_H

