////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file

// Forward-declarations needed for Messages.h
typedef struct msg_t msg_t;
typedef struct msg_q_t msg_q_t;

#include "Messages.h"
#include "string.h"
#include "stdlib.h"

#if LOGGING_ENABLED && CTHREADS_LOGGING_ENABLED
#include "LoggingLib.h"
#else
#undef printLog
#define printLog(...) {}
#define logFile stderr
#define LOG_MALLOC_FAILURE(...) {}
#endif

/// @var msg_sync_array
///
/// @brief Array of msg_sync_t objects that hold the function pointers to use
/// with the synchronization primitives
msg_sync_t msg_sync_array[] = {
#ifdef THREAD_SAFE_COROUTINES
  {
    (int      (*)(void *mtx, int type)) mtx_init,
    (int      (*)(void *mtx)) mtx_lock,
    (int      (*)(void *mtx)) mtx_unlock,
    (void     (*)(void *mtx)) mtx_destroy,
    (int      (*)(void *mtx, const struct timespec *ts)) mtx_timedlock,
    (int      (*)(void *mtx)) mtx_trylock,
    (int      (*)(void *cond)) cnd_broadcast,
    (void     (*)(void *cond)) cnd_destroy,
    (int      (*)(void *cond)) cnd_init,
    (int      (*)(void *cond)) cnd_signal,
    (int      (*)(void *cond, void *mtx, const struct timespec *ts))
      cnd_timedwait,
    (int      (*)(void *cond, void *mtx)) cnd_wait,
  },
#endif // THREAD_SAFE_COROUTINES
  {
    (int      (*)(void *mtx, int type)) comutexInit,
    (int      (*)(void *mtx)) comutexLock,
    (int      (*)(void *mtx)) comutexUnlock,
    (void     (*)(void *mtx)) comutexDestroy,
    (int      (*)(void *mtx, const struct timespec *ts))
      comutexTimedLock,
    (int      (*)(void *mtx)) comutexTryLock,
    (int      (*)(void *cond)) coconditionBroadcast,
    (void     (*)(void *cond)) coconditionDestroy,
    (int      (*)(void *cond)) coconditionInit,
    (int      (*)(void *cond)) coconditionSignal,
    (int      (*)(void *cond, void *mtx, const struct timespec *ts))
      coconditionTimedWait,
    (int      (*)(void *cond, void *mtx)) coconditionWait,
  },
};

// Message functions

/// @fn int msg_start_use(msg_t *msg, msg_safety_t msg_safety)
///
/// @brief Set all the member elements of a msg_t to their default values so
/// that we can begin using it.
///
/// @param msg A pointer to the msg_t to configure and begin using.
/// @param msg_safety The level of safety to use for the message (process,
///   thread, or coroutine).
///
/// @return Returns msg_success on success, msg_error on error.
static inline int msg_start_use(msg_t *msg, msg_safety_t msg_safety) {
  int return_value = msg_success;
  
  if (msg != NULL) {
    if (msg->in_use == false) {
      msg->type = 0;
      msg->data = NULL;
      msg->size = 0;
      msg->next = NULL;
      msg->waiting = false;
      msg->done = true;
      msg->in_use = true;
      memset(&msg->from, 0, sizeof(msg->from));
      memset(&msg->to, 0, sizeof(msg->to));
      if (msg->configured == false) {
        msg->msg_sync = &msg_sync_array[msg_safety];
        if (msg->msg_sync->cnd_init(&msg->condition) == msg_success) {
          if (msg->msg_sync->mtx_init(&msg->lock, msg_mtx_plain | msg_mtx_timed)
            == msg_success
          ) {
            msg->configured = true;
          } else {
            msg->msg_sync->cnd_destroy(&msg->condition);
            return_value = msg_error;
          }
        }
      }
      // Don't touch msg->dynamically_allocated;
    } // Else this message is already setup
  } else {
    return_value = msg_error;
  }
  
  return return_value;
}

/// @fn msg_t* msg_create(msg_safety_t msg_safety)
///
/// @brief Dynamically allocate a new msg_t and set all its member elements to
/// a default state.
///
/// @param msg_safety The level of safety to use for the message (process,
///   thread, or coroutine).
///
/// @return Returns a pointer to a newly-allocated and configured msg_t on
/// success, NULL on failure.
msg_t* msg_create(msg_safety_t msg_safety) {
  msg_t *msg = NULL;
  // Use the allocator the user wants to use.
  msg = (msg_t*) calloc(1, sizeof(msg_t));

  if (msg != NULL) {
    if (msg_start_use(msg, msg_safety) == msg_success) {
      msg->dynamically_allocated = true;
    } else {
      // Some kind of problem.  Can't use this message.
      free(msg); msg = NULL;
    }
  }
  
  return msg;
}

/// @fn msg_t* msg_destroy(msg_t *msg)
///
/// @brief If nothing is waiting on the message, deallocate all of the message's
/// resources and return NULL.  If something is waiting on the message, mark the
/// message as done and no longer in use and broadcast to all the waiters.  Note
/// that the msg_t itself will only be freed if it was allocated by msg_create.
///
/// @param msg A pointer to the msg_t to destroy.
///
/// @return Returns NULL if the msg_t was fully deallocated, a pointer to the
/// original msg_t otherwise.
msg_t* msg_destroy(msg_t *msg) {
  if (msg == NULL) {
    // Nothing to do.
    return msg;
  }
  
  // Don't touch msg->type.
  // Don't touch msg->data.
  // Don't touch msg->size.
  // Don't touch msg->next.
  // Don't touch msg->waiting.
  msg->in_use = false;
  // Don't touch from.
  if (msg->configured == true) {
    if (msg->msg_sync->mtx_trylock(&msg->lock) == msg_success) {
      msg->done = true;
    
      if (msg->waiting == false) {
        // Nothing is waiting.  Destroy the resources.
        msg->msg_sync->mtx_unlock(&msg->lock);
        msg->msg_sync->cnd_destroy(&msg->condition);
        msg->msg_sync->mtx_destroy(&msg->lock);
        msg->configured = false;
        if (msg->dynamically_allocated == true) {
          free(msg); msg = NULL;
        }
      } else {
        // Something is waiting.  Signal the waiters.  It will be up to them to
        // destroy this message again later.
        msg->msg_sync->cnd_broadcast(&msg->condition);
        msg->msg_sync->mtx_unlock(&msg->lock);
      }
    } else {
      // We can't do any signalling.  Just tear down everything.
      msg->done = true;
      msg->waiting = false;
      msg->msg_sync->cnd_destroy(&msg->condition);
      msg->msg_sync->mtx_destroy(&msg->lock);
      msg->configured = false;
      if (msg->dynamically_allocated == true) {
        free(msg); msg = NULL;
      }
    }
  } else if (msg->dynamically_allocated == true) {
    // msg->lock and mtx->condition are not initialized, so notihing to destroy.
    free(msg); msg = NULL;
  }
  
  return msg;
}

/// @fn int msg_init(msg_t *msg, msg_safety_t msg_safety,
///   int64_t type, void *data, size_t size, bool waiting)
///
/// @brief Initialize a message to make it ready for sending to another thread.
///
/// @param msg A pointer to an allocated msg_t.  Will be properly configured if
///   not already so.
/// @param msg_safety The level of safety to use for the message (process,
///   thread, or coroutine).
/// @param type The message type to use for the message.
/// @param data A pointer to the data for the message (if any).
/// @param size The number of bytes pointed to by the data pointer.
/// @param waiting Whether or not the caller of this function will be waiting on
///   a response to this message from the destination thread.
///
/// @return Returns msg_success on success, msg_error on failure.
int msg_init(msg_t *msg, msg_safety_t msg_safety,
  int64_t type, void *data, size_t size, bool waiting
) {
  int return_value = msg_error;
  
  if (msg == NULL) {
    // Nothing we can do.  Fail.
    return return_value; // msg_error
  } else if (msg_start_use(msg, msg_safety) != msg_success) {
    // Couldn't configure this message for use for some reason.  Fail.
    return return_value; // msg_error
  }
  
  msg->type = type;
  msg->data = data;
  msg->size = size;
  msg->next = NULL;
  msg->waiting = waiting;
  msg->done = false;
  // No need to set msg->in_use since we called msg_start_use above.
  // Don't touch msg->from in case this message is being reused.
  return_value = msg_success;
  
  return return_value;
}

/// @fn int msg_release(msg_t *msg)
///
/// @brief Release a message from use.
///
/// @param msg A pointer to a previously-allocated msg_t.
///
/// @return Returns msg_success on success, msg_error on failure.
int msg_release(msg_t *msg) {
  int return_value = msg_success;
  
  if (msg == NULL) {
    // A NULL message is already released.  Just return.
    return return_value; // msg_success
  }
  
  // Don't touch msg->type.
  // Don't touch msg->data.
  // Don't touch msg->size.
  // Don't touch msg->next.
  // Don't touch msg->waiting.
  msg->in_use = false;
  // Don't touch msg->from.
  if (msg->configured == true) {
    if (msg->msg_sync->mtx_trylock(&msg->lock) == msg_success) {
      msg->done = true;
      
      if (msg->waiting == true) {
        // Something is waiting.  Signal the waiters.  It will be up to them to
        // destroy this message again later.
        msg->msg_sync->cnd_broadcast(&msg->condition);
      }
      msg->msg_sync->mtx_unlock(&msg->lock);
    } else {
      // Something is wrong here.  We're releasing a msg_t that is not owned
      // by us.  We can't do a broadcast.  Just mark it done and return an
      // error.
      msg->done = true;
      return_value = msg_error;
    }
  } else {
    // Nothing we can do but set the done flag.
    msg->done = true;
  }
  // Don't touch msg->thrd_condition.
  // Don't touch msg->thrd_lock.
  // Don't touch msg->configured.
  // Don't touch msg->dynamically_allocated.
  
  return return_value;
}

/// @fn int msg_set_done(msg_t *msg)
///
/// @brief Set the done flag for the message and signal any waiters.
///
/// @param msg A pointer to a previously-allocated msg_t.
///
/// @return Returns msg_success on success, msg_error on failure.
int msg_set_done(msg_t *msg) {
  int return_value = msg_error;
  
  if (msg == NULL) {
    // Invalid.
    return return_value; // msg_error
  }
  
  // Don't touch msg->type.
  // Don't touch msg->data.
  // Don't touch msg->size.
  // Don't touch msg->next.
  // Don't touch msg->waiting.
  // Don't touch msg->in_use.
  if (msg->configured == true) {
    msg->msg_sync->mtx_lock(&msg->lock);
    msg->done = true;
    
    if (msg->waiting == true) {
      // Something is waiting.  Signal the waiters.  It will be up to them to
      // destroy this message again later.
      return_value = msg->msg_sync->cnd_broadcast(&msg->condition);
    } else {
      return_value = msg_success;
    }
    msg->msg_sync->mtx_unlock(&msg->lock);
  } else {
    // Nothing we can do but set the done flag.
    msg->done = true;
    return_value = msg_success;
  }
  // Don't touch msg->from.thrd.
  // Don't touch msg->thrd_condition.
  // Don't touch msg->thrd_lock.
  // Don't touch msg->configured.
  // Don't touch msg->dynamically_allocated.
  
  return return_value;
}

/// @fn int msg_wait_for_done(msg_t *msg, const struct timespec *ts)
///
/// @brief Wait on a message until another thread indicates that it's done
/// or until the specified time has been reached.
///
/// @param msg A pointer to a previously-allocated msg_t.
/// @param ts A pointer to a struct timespec that specifies the end of the time
///   period to wait for.  If this pointer is NULL, an infinite timeout will be
///   used.
///
/// @return Returns msg_success on success, msg_error on failure.
int msg_wait_for_done(msg_t *msg, const struct timespec *ts) {
  int return_value = msg_error;
  
  if (msg == NULL) {
    // Invalid.
    return return_value; // msg_error
  } else if (msg->configured == false) {
    // We can't do this.  Waiting for done requires the use of the lock and
    // condition in the message.  It doesn't make any sense for us to try and
    // initialize them at this point because whatever made this call is already
    // in a bad state.  We shouldn't try to fix things because we don't know
    // what's going on above us.  Just return bad status.
    return return_value; // msg_error
  }
  
  int lock_status = msg_error;
  int wait_status = msg_error;
  if (msg->done == true) {
    return_value = msg_success;
    msg->waiting = false;
  } else {
    if (ts == NULL) {
      lock_status = msg->msg_sync->mtx_lock(&msg->lock);
    } else {
      lock_status = msg->msg_sync->mtx_timedlock(&msg->lock, ts);
    }
    if (lock_status != msg_success) {
      // Either we timed out or there's a problem with the lock.  Either way, we
      // don't want to continue and we're going to exit with an error since we
      // never received the done flag.
      return return_value; // msg_error
    }
    
    msg->waiting = true;
    while (msg->done == false) {
      if (ts == NULL) {
        wait_status = msg->msg_sync->cnd_wait(&msg->condition, &msg->lock);
      } else {
        wait_status
          = msg->msg_sync->cnd_timedwait(&msg->condition, &msg->lock, ts);
      }
      if (wait_status != msg_success) {
        // Either we timed out or there's a problem with the condition.  Again,
        // we don't want to proceed like this.
        break;
      }
    }
    msg->waiting = false;
    
    if (msg->done == true) {
      return_value = msg_success;
    }

    msg->msg_sync->mtx_unlock(&msg->lock);
  }
  
  return return_value;
}

/// @fn msg_t* msg_wait_for_reply_with_type_(
///   msg_q_t *queue, msg_t *sent, bool release,
///   int64_t *type, const struct timespec *ts)
///
/// @brief Wait for a reply from the recipient of a message.
///
/// @param sent The message that was originally sent to the recipient.
/// @param release Whether or not the provided sent message should be released
///   (*NOT* destroyed) after the recipient has indicated that they're done
///   processing our sent message.
/// @param type A pointer to an integer type of message that the caller is
///   waiting for.  If this parameter is NULL, no type will be considered.
/// @param ts A pointer to a struct timespec that holds the end time to wait
///   until for a reply.  If this parameter is NULL, then an infinite timeout
///   will be used.
///
/// @return Returns a pointer to the msg_t received from the recipient of
/// the original message on success, NULL on failure or if the provided timeout
/// time is reached.
msg_t* msg_wait_for_reply_with_type_(
  msg_q_t *queue, msg_t *sent, bool release,
  int64_t *type, const struct timespec *ts
) {
  msg_t *reply = NULL;

  if ((queue == NULL) || (sent == NULL)) {
    // Invalid.
    return reply; // NULL
  }

  // We need to grab the original recipient of the message that was sent before
  // we wait for done in case the recipient reuses this message as the reply.
  msg_endpoint_t recipient = sent->to;

  if (msg_wait_for_done(sent, ts) != msg_success) {
    // Invalid state of the message.  Fail.
    return reply; // NULL
  }

  if (release == true) {
    // We're done with the message that was originally sent and the caller has
    // indicated that it is to be released now.
    msg_release(sent);
  }

  // Recipient has processed the message.  We now need to wait for their reply.
  int lock_status = msg_success;
  if (ts == NULL) {
    lock_status = queue->msg_sync->mtx_lock(&queue->lock);
  } else {
    lock_status = queue->msg_sync->mtx_timedlock(&queue->lock, ts);
  }
  if (lock_status != msg_success) {
    // Either we've timed out or there's a problem with the lock.  Either way,
    // we're done.  Bail.
    return reply; // NULL
  }

  // mtx_timedlock will return thrd_timedout if the timeout is
  // reached, so we'll never reach this point if we've exceeded our timeout.
  msg_t *prev = NULL;
  msg_t *cur = queue->head;
  msg_t **prev_next = &queue->head;
  int searchType = 0;
  if (type != NULL) {
    // This saves us from having to dereference the pointer in every iteration
    // of the loop below.
    searchType = *type;
  }

  // Enter our main wait loop.
  int wait_status = msg_success;
  while (reply == NULL) {
    while ((cur != NULL)
      && ((memcmp(&cur->from, &recipient, sizeof(msg_endpoint_t)) != 0)
        || ((type != NULL) && (cur->type != searchType))
      )
    ) {
      prev = cur;
      prev_next = &cur->next;
      cur = cur->next;
    }

    if (cur != NULL) {
      // Desired reply was found.  Remove the message from the thread.
      reply = cur;
      *prev_next = cur->next;

      if (queue->head == NULL) {
        // Empty queue.  Set queue->tail to NULL too.
        queue->tail = NULL;
      }
      if (queue->tail == cur) {
        queue->tail = prev;
      }
      cur->next = NULL;
    } else {
      // Desired reply was not found.  Block until something else is pushed.
      if (ts == NULL) {
        wait_status
          = queue->msg_sync->cnd_wait(&queue->condition, &queue->lock);
      } else {
        wait_status = queue->msg_sync->cnd_timedwait(
          &queue->condition, &queue->lock, ts);
      }
      if (wait_status != msg_success) {
        // Something isn't as expected.  Bail.
        break;
      }
      // cnd_timedwait will return thrd_timedout if the timeout is
      // reached, so we won't continue the loop if we've exceeded our timeout.
    }

    prev = NULL;
    cur = queue->head;
    prev_next = &queue->head;
  }

  queue->msg_sync->mtx_unlock(&queue->lock);

  return reply;
}

/// @fn msg_t* msg_wait_for_reply(msg_t *sent,
///   bool release, const struct timespec *ts)
///
/// @brief Block until a reply has been received from the original recipient of
/// the provided message or until a specified future time has been reached.
///
/// @param sent The message that was originally sent to the recipient.
/// @param release Whether or not the provided sent message should be released
///   (*NOT* destroyed) after the recipient has indicated that they're done
///   processing our sent message.
/// @param ts A pointer to a struct timespec that holds the end time to wait
///   until for a reply.  If this parameter is NULL, then an infinite timeout
///   will be used.
///
/// @return Returns a pointer to the msg_t received from the recipient of
/// the original message on success, NULL on failure.
msg_t* msg_wait_for_reply(msg_t *sent, bool release,
  const struct timespec *ts
) {
  if (sent == NULL) {
    return NULL;
  }

  return msg_wait_for_reply_with_type_(
    sent->reply_to, sent, release, NULL, ts);
}

/// @fn msg_t* msg_wait_for_reply_with_type(msg_t *sent,
///   bool release, int64_t type, const struct timespec *ts)
///
/// @brief Block until a reply of a specified type has been received from the
/// original recipient of the provided message or until a specified future time
/// has been reached.
///
/// @param sent The message that was originally sent to the recipient.
/// @param release Whether or not the provided sent message should be released
///   (*NOT* destroyed) after the recipient has indicated that they're done
///   processing our sent message.
/// @param type An integer type of message that the caller is waiting for.
/// @param ts A pointer to a struct timespec that holds the end time to wait
///   until for a reply.  If this parameter is NULL, then an infinite timeout
///   will be used.
///
/// @return Returns a pointer to the msg_t received from the recipient of
/// the original message of the specified tyep on success, NULL on failure or if
/// the provided timeout time is reached.
msg_t* msg_wait_for_reply_with_type(msg_t *sent, bool release,
  int64_t type, const struct timespec *ts
) {
  if (sent == NULL) {
    return NULL;
  }

  return msg_wait_for_reply_with_type_(
    sent->reply_to, sent, release, &type, ts);
}

/// @fn void* msg_element(msg_t *msg, msg_element_t msg_element)
///
/// @brief Get a pointer to an element of a msg_t object.
///
/// @param msg A pointer to the msg_t to get the member of.
/// @param msg_element The member element of the msg_t to get.
///
/// @return Returns a pointer to the specified element on success, NULL on
/// failure.
void* msg_element(msg_t *msg, msg_element_t msg_element) {
  switch (msg_element) {
    case MSG_ELEMENT_TYPE: {
      return &msg->type;
      break;
    }

    case MSG_ELEMENT_DATA: {
      return &msg->data;
      break;
    }

    case MSG_ELEMENT_SIZE: {
      return &msg->size;
      break;
    }

    case MSG_ELEMENT_WAITING: {
      return &msg->waiting;
      break;
    }

    case MSG_ELEMENT_DONE: {
      return &msg->done;
      break;
    }

    case MSG_ELEMENT_IN_USE: {
      return &msg->in_use;
      break;
    }

    case MSG_ELEMENT_FROM: {
      return &msg->from;
      break;
    }

    case MSG_ELEMENT_TO: {
      return &msg->to;
      break;
    }

    default: {
      return NULL;
      break;
    }
  }
}

/// @fn msg_q_t* msg_q_create(msg_q_t *q, msg_safety_t msg_safety)
///
/// @brief Initialize a msg_q_t, dynamically allocating it first if necessary.
/// The level of safety guaranteed for the queue (process, thread, or coroutine)
/// will be set according to the user's specification.
///
/// @param q The msg_q_t to initialize or NULL to indicate that one is to be
///   dynamically allocated.
/// @param msg_safety The level of safety (process, thread, or coroutine) to
///   guarantee for the message.
///
/// @return On success, returns the msg_q_t pointer passed in if provided or a
/// pointer to a dynamically-allocated msg_q_t if NULL was passed in.  Returns
/// NULL on failure.
msg_q_t* msg_q_create(msg_q_t *q, msg_safety_t msg_safety) {
  msg_q_t *return_value = q;
  
  if (q == NULL) {
    // No queue was provided.  Allocate a new one.  This is the expected case.
    return_value = (msg_q_t*) calloc(1, sizeof(msg_q_t));

    if (return_value == NULL) {
      // Allocation failed.  Bail.
      goto queue_alloc_failure;
    }

    return_value->dynamically_allocated = true;
  } else {
    return_value->dynamically_allocated = false;
  }

  return_value->msg_sync = &msg_sync_array[msg_safety];

  // return_value->head and return_value->tail are initialized to NULL by the
  // calloc call.
  if (return_value->msg_sync->cnd_init(&return_value->condition)
    != msg_success
  ) {
    // Can't proceed.  Bail.
    goto cnd_init_failure;
  }
  
  if (return_value->msg_sync->mtx_init(
    &return_value->lock, msg_mtx_plain | msg_mtx_timed) != msg_success
  ) {
    // Can't proceed.  Bail.
    goto mtx_init_failure;
  }
  
  return return_value; // valid queue
  
mtx_init_failure:
  return_value->msg_sync->cnd_destroy(&return_value->condition);
cnd_init_failure:
  if (q == NULL) {
    // No queue was provided by the user, so we dynamically allocated one.
    // Destroy it.
    free(return_value);
  }
queue_alloc_failure:
  return_value = NULL;

  return return_value; // NULL
}

/// @fn int msg_q_destroy(msg_q_t *queue)
///
/// @brief Destroy the specified message queue.
///
/// @param queue The queue to destroy.
///
/// @return Returns msg_success on success or msg_error on failure.
int msg_q_destroy(msg_q_t *queue) {
  if (queue == NULL) {
    return msg_success;
  }
  
  for (msg_t *cur = queue->head; cur != NULL; ) {
    msg_t *next = cur->next;
    msg_destroy(cur);
    cur = next;
  }

  queue->head = NULL;
  queue->tail = NULL;
  
  queue->msg_sync->mtx_destroy(&queue->lock);
  queue->msg_sync->cnd_destroy(&queue->condition);
  if (queue->dynamically_allocated == true) {
    free(queue);
  }
  queue = NULL;
  
  return msg_success;
}

/// @fn msg_t* msg_q_peek(msg_q_t *queue) {
///
/// @brief Get the head of the provided message queue but do not remove it
/// from the queue.
///
/// @param queue The queue to interrogate.
///
/// @return Returns the head of the message queue, which may be NULL.
msg_t* msg_q_peek(msg_q_t *queue) {
  msg_t *head = NULL;
  
  if (queue != NULL) {
    head = queue->head;
  }
  
  return head;
}

/// @fn msg_t* msg_q_pop(msg_q_t *queue) {
///
/// @brief Get the head of the provided message queue and remove it.
///
/// @param queue The queue to pop from.
///
/// @return Returns the head of the message queue, which may be NULL.
msg_t* msg_q_pop(msg_q_t *queue) {
  msg_t *head = NULL;
  
  if ((queue == NULL)
    || (queue->msg_sync->mtx_lock(&queue->lock) != msg_success)
  ) {
    // Error case.
    return head; // NULL
  }
  
  head = queue->head;
  if (head != NULL) {
    queue->head = head->next;
    head->next = NULL;
    
    if (queue->head == NULL) {
      // Empty queue.  Set queue->tail to NULL too.
      queue->tail = NULL;
    }
  }
  
  queue->msg_sync->mtx_unlock(&queue->lock);
  
  return head;
}

/// @fn msg_t* msg_q_pop_type(msg_q_t *queue, int64_t type)
///
/// @brief Get the first message of the specified type from the provided message
/// queue and remove it from the queue.
///
/// @param queue The queue to pop from.
/// @param type The message type to look for.
///
/// @return Returns the first message of the queue with the specified type on
/// success, NULL on failure.
msg_t* msg_q_pop_type(msg_q_t *queue, int64_t type) {
  msg_t *return_value = NULL;
  if (queue == NULL) {
    return return_value; // NULL
  }
  
  msg_t *prev = NULL;
  msg_t *cur = queue->head;
  msg_t **prev_next = &queue->head;
  
  if (queue->msg_sync->mtx_lock(&queue->lock) != msg_success) {
    // Error case.
    return return_value; // NULL
  }
  
  while ((cur != NULL) && (cur->type != type)) {
    prev = cur;
    prev_next = &cur->next;
    cur = cur->next;
  }
  
  if (cur != NULL) {
    // Desired type was found.  Remove the message from the queue.
    return_value = cur;
    *prev_next = cur->next;
    
    if (queue->head == NULL) {
      // Empty queue.  Set queue->tail to NULL too.
      queue->tail = NULL;
    }
    if (queue->tail == cur) {
      queue->tail = prev;
    }
    cur->next = NULL;
  }
  
  queue->msg_sync->mtx_unlock(&queue->lock);
  
  return return_value;
}

/// @fn msg_t* msg_q_wait_for_type_(msg_q_t *queue, int64_t *type,
///   const struct timespec *ts)
///
/// @brief Wait for a message of a given type to be available in the message
/// queue or until a specified time has elapsed.  Remove the message from the
/// queue and return it if one is available before the specified time is
/// reached.
///
/// @param queue The queue to wait on and pop from.
/// @param type A pointer to the message type to look for.  If this parameter is
///   NULL then the first message of any type will be returned.
/// @param ts A pointer to a struct timespec that specifies the end of the time
///   period to wait for.  If this parameter is NULL then an infinite timeout
///   will be used.
///
/// @return Returns the first message of the provided type if one is available
/// before the specified time.  Returns NULL if no such message is available
/// within that time period or if an error occurrs.
msg_t* msg_q_wait_for_type_(msg_q_t *queue, int64_t *type,
  const struct timespec *ts
) {
  msg_t *return_value = NULL;
  if (queue == NULL) {
    return return_value; // NULL
  }
  
  msg_t *prev = NULL;
  msg_t *cur = queue->head;
  msg_t **prev_next = &queue->head;
  int lock_status = msg_success;
  int wait_status = msg_success;
  int search_type = 0;

  if (queue == NULL) {
    return return_value; // NULL
  }

  if (type != NULL) {
    // This saves us from having to dereference the pointer on each iteration
    // of the loop below.
    search_type = *type;
  }
  
  if (ts == NULL) {
    lock_status = queue->msg_sync->mtx_lock(&queue->lock);
  } else {
    lock_status = queue->msg_sync->mtx_timedlock(&queue->lock, ts);
  }
  if (lock_status != msg_success) {
    // Error case.
    return return_value; // NULL
  }
  // mtx_timedlock will return msg_timedout if the timeout is reached, so we'll
  // never reach this point if we've exceeded our timeout.
  
  while (return_value == NULL) {
    while ((cur != NULL) && (type != NULL) && (cur->type != search_type)) {
      prev = cur;
      prev_next = &cur->next;
      cur = cur->next;
    }
    
    if (cur != NULL) {
      // Desired type was found.  Remove the message from the queue.
      return_value = cur;
      *prev_next = cur->next;
      
      if (queue->head == NULL) {
        // Empty queue.  Set queue->tail to NULL too.
        queue->tail = NULL;
      }
      if (queue->tail == cur) {
        queue->tail = prev;
      }
      cur->next = NULL;
    } else {
      // Desired type was not found.  Block until something else is pushed.
      if (ts == NULL) {
        wait_status
          = queue->msg_sync->cnd_wait(&queue->condition, &queue->lock);
      } else {
        wait_status
          = queue->msg_sync->cnd_timedwait(&queue->condition, &queue->lock, ts);
      }
      if (wait_status != msg_success) {
        break;
      }
      // cnd_timedwait will return msg_timedout if the timeout is reached, so
      // we won't continue the loop if we've exceeded our timeout.
    }
    
    prev = NULL;
    cur = queue->head;
    prev_next = &queue->head;
  }
  
  queue->msg_sync->mtx_unlock(&queue->lock);
  
  return return_value;
}

/// @fn msg_t* msg_q_wait(msg_q_t *queue, const struct timespec *ts)
///
/// @brief Wait until there is a message in the queue or until a specified has
/// been reached.  If there is a message in the queue before the timeout, pop
/// it from the queue.
///
/// @param queue The queue to wait on and pop from.
/// @param ts A pointer to a struct timespec that specifies the end of the time
///   period to wait for.  If this parameter is NULL then an infinite timeout
///   will be used.
///
/// @return Returns the first message in the queue if one is available before
/// the specified time.  Returns NULL if no such message is available within
/// that time period or if an error occurrs.
msg_t* msg_q_wait(msg_q_t *queue, const struct timespec *ts) {
  return msg_q_wait_for_type_(queue, NULL, ts);
}

/// @fn msg_t* msg_q_wait_for_type(msg_q_t *queue, int64_t type,
///   const struct timespec *ts)
///
/// @brief Wait until there is a message of a specified type in the queue or
/// until a specified has been reached.  If there is a message of the specified
/// type in the queue before the timeout, pop it from the queue.
///
/// @param queue The queue to wait on and pop from.
/// @param type The message type to look for.
/// @param ts A pointer to a struct timespec that specifies the end of the time
///   period to wait for.  If this parameter is NULL then an infinite timeout
///   will be used.
///
/// @return Returns the first message of the provided type if one is available
/// before the specified time.  Returns NULL if no such message is available
/// within that time period or if an error occurrs.
msg_t* msg_q_wait_for_type(msg_q_t *queue, int64_t type,
  const struct timespec *ts
) {
  return msg_q_wait_for_type_(queue, &type, ts);
}


/// @fn int msg_q_push(msg_q_t *queue, msg_q_t *reply_to, msg_t *msg)
///
/// @brief Push a message onto the message queue of a specified thread.
///
/// @param queue The queue to push onto.
/// @param reply_to The queue that should be replied to, if any.
/// @param msg A pointer to the msg_t to push onto the destination thread's
/// message queue.
///
/// @return Returns msg_success on success, msg_error on failure.
int msg_q_push(msg_q_t *queue, msg_q_t *reply_to, msg_t *msg) {
  int return_value = msg_error;
  
  if (msg == NULL) {
    // Invalid.
    return return_value; // msg_error
  }
  
  if (queue == NULL) {
    // Destination thread has exited.  Fail.
    return return_value; // msg_error
  }
  
  if (queue->msg_sync->mtx_lock(&queue->lock) != msg_success) {
    // Error case.
    return return_value; // msg_error
  }
  
  msg->next = NULL;
  if (queue->tail != NULL) {
    queue->tail->next = msg;
    queue->tail = msg;
  } else {
    // Empty queue.  Populate both queue->head and queue->tail.
    queue->head = msg;
    queue->tail = msg;
  }
  msg->reply_to = reply_to;
  
  // Let all the waiters know that there's something new in the queue now.
  return_value = queue->msg_sync->cnd_broadcast(&queue->condition);
  
  queue->msg_sync->mtx_unlock(&queue->lock);
  
  return return_value;
}

