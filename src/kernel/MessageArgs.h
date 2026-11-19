///////////////////////////////////////////////////////////////////////////////
///
/// @file              MessageArgs.h
///
/// @brief             Storage for the argument structures and buffers that
///                    message payloads point at.
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

#ifndef MESSAGE_ARGS_H
#define MESSAGE_ARGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @typedef MessageArgsOwnership
///
/// @brief Who frees storage made with MESSAGE_ARGS_CREATE or
/// MESSAGE_ARGS_CREATE_BYTES.
typedef uint8_t MessageArgsOwnership;

/// @def MESSAGE_ARGS_ROUND_TRIP
///
/// @brief The sender owns the storage before and after the exchange and frees
/// it, unless the recipient adopts it with MESSAGE_ARGS_ADOPT.
#define MESSAGE_ARGS_ROUND_TRIP ((MessageArgsOwnership) 0x01)

/// @def MESSAGE_ARGS_ONE_WAY
///
/// @brief Ownership passes to the recipient when the message is sent, and the
/// recipient frees the storage.  The only kind that may be sent without
/// waiting.
#define MESSAGE_ARGS_ONE_WAY ((MessageArgsOwnership) 0x02)


/// @typedef MessageBufferDirection
///
/// @brief Which way the data in a buffer shared with MESSAGE_BUFFER_SHARE
/// moves.
typedef uint8_t MessageBufferDirection;

/// @def MESSAGE_BUFFER_IN
///
/// @brief The recipient reads the buffer and must not write it.
#define MESSAGE_BUFFER_IN ((MessageBufferDirection) 0x01)

/// @def MESSAGE_BUFFER_OUT
///
/// @brief The recipient writes the buffer and the sender reads it after
/// unsharing it.
#define MESSAGE_BUFFER_OUT ((MessageBufferDirection) 0x02)

/// @def MESSAGE_BUFFER_INOUT
///
/// @brief The recipient reads and writes the buffer.
#define MESSAGE_BUFFER_INOUT \
  ((MessageBufferDirection) (MESSAGE_BUFFER_IN | MESSAGE_BUFFER_OUT))

/// @typedef MessageArgsCopyFn
///
/// @brief A function that rebuilds an adopted block of `size` bytes at a new
/// address, fixing up the pointers the block holds into itself.
typedef void (*MessageArgsCopyFn)(void *destination, const void *source,
  size_t size);

#ifdef NANO_OS_MESSAGE_ARGS_CHECKED

/// @def MESSAGE_ARGS_ERROR_DOUBLE_FREE
///
/// @brief Storage was freed twice.
#define MESSAGE_ARGS_ERROR_DOUBLE_FREE       0

/// @def MESSAGE_ARGS_ERROR_BAD_POINTER
///
/// @brief A pointer that the checker never handed out was freed, unshared, or
/// adopted, or an unshare named a different original buffer.
#define MESSAGE_ARGS_ERROR_BAD_POINTER       1

/// @def MESSAGE_ARGS_ERROR_WRITE_AFTER_FREE
///
/// @brief Freed storage was written before it was handed out again.
#define MESSAGE_ARGS_ERROR_WRITE_AFTER_FREE  2

/// @def MESSAGE_ARGS_ERROR_FREE_IN_FLIGHT
///
/// @brief Round-trip storage was freed while the message carrying it was not
/// yet done.
#define MESSAGE_ARGS_ERROR_FREE_IN_FLIGHT    3

/// @def MESSAGE_ARGS_ERROR_WRONG_OWNER
///
/// @brief Round-trip storage was freed by a process other than its owner.
#define MESSAGE_ARGS_ERROR_WRONG_OWNER       4

/// @def MESSAGE_ARGS_ERROR_WRONG_RECIPIENT
///
/// @brief One-way storage that had been sent was freed by a process other than
/// its recipient.
#define MESSAGE_ARGS_ERROR_WRONG_RECIPIENT   5

/// @def MESSAGE_ARGS_ERROR_FIRE_AND_FORGET
///
/// @brief Round-trip storage was sent without waiting.
#define MESSAGE_ARGS_ERROR_FIRE_AND_FORGET   6

/// @def MESSAGE_ARGS_ERROR_MEMORY_MANAGER
///
/// @brief Storage from MESSAGE_ARGS_CREATE was sent to the memory manager,
/// whose production expansion would recurse into the memory manager.
#define MESSAGE_ARGS_ERROR_MEMORY_MANAGER    7

/// @def MESSAGE_ARGS_ERROR_IN_MODIFIED
///
/// @brief The recipient wrote a buffer shared MESSAGE_BUFFER_IN.
#define MESSAGE_ARGS_ERROR_IN_MODIFIED       8

/// @def MESSAGE_ARGS_ERROR_FREE_AFTER_ADOPT
///
/// @brief The sender freed storage after the recipient adopted it.
#define MESSAGE_ARGS_ERROR_FREE_AFTER_ADOPT  9

/// @def MESSAGE_ARGS_ERROR_SEND_AFTER_FREE
///
/// @brief Freed storage was sent or adopted.
#define MESSAGE_ARGS_ERROR_SEND_AFTER_FREE  10

/// @def MESSAGE_ARGS_ERROR_LEAK
///
/// @brief A process exited on its own with storage it hadn't freed.
#define MESSAGE_ARGS_ERROR_LEAK             11

/// @def MESSAGE_ARGS_ERROR_ARENA_FULL
///
/// @brief An allocation failed because the arena was full.
#define MESSAGE_ARGS_ERROR_ARENA_FULL       12

/// @def MESSAGE_ARGS_ERROR_OVERLAY_POINTER
///
/// @brief A buffer in the overlay window was shared, where another process's
/// overlay can replace it before the recipient reads or writes it.
#define MESSAGE_ARGS_ERROR_OVERLAY_POINTER  13

/// @def MESSAGE_ARGS_ERROR_SHARE_RECIPIENT
///
/// @brief A message carried a buffer shared with a process other than the one
/// the message was sent to.
#define MESSAGE_ARGS_ERROR_SHARE_RECIPIENT  14

/// @def MESSAGE_ARGS_NUM_ERRORS
///
/// @brief The number of kinds of error the checker counts.
#define MESSAGE_ARGS_NUM_ERRORS             15

/// @struct MessageArgsCounts
///
/// @brief What the checker has seen since the last messageArgsResetCounts.
///
/// @param errors The number of each MESSAGE_ARGS_ERROR_* found.
/// @param reclaimed Blocks freed for a process that was killed or replaced.
/// @param orphaned Blocks left to their recipient by a process that ended while
///   they were in flight.
/// @param live Blocks currently allocated.
/// @param droppedReports Reports lost because the report queue was full.
typedef struct MessageArgsCounts {
  uint16_t errors[MESSAGE_ARGS_NUM_ERRORS];
  uint16_t reclaimed;
  uint16_t orphaned;
  uint16_t live;
  uint16_t droppedReports;
} MessageArgsCounts;

void* messageArgsAllocate(size_t size, MessageArgsOwnership ownership,
  bool explicitLifetime);
void messageArgsFree(void *ptr);
int messageArgsAdopt(void **ptr, ProcessId pid, MessageArgsCopyFn copyFn);
int messageArgsAdoptWith(void **ptr, ProcessId pid, MessageArgsCopyFn copyFn,
  void* (*allocate)(size_t size), int (*assign)(void *ptr, ProcessId pid));
void* messageBufferShare(void *ptr, size_t size,
  MessageBufferDirection direction, ProcessId recipient);
void messageBufferUnshare(void *shared, void *original,
  MessageBufferDirection direction);
void messageArgsSending(ProcessMessage *processMessage, ProcessId recipient);
void messageArgsSendFailed(ProcessMessage *processMessage);
void messageArgsSendComplete(ProcessMessage *processMessage);
void messageArgsProcessEnded(ProcessId pid, bool reportLeaks);
void messageArgsFlushReports(void);
const MessageArgsCounts* messageArgsCounts(void);
uint16_t messageArgsErrorTotal(void);
void messageArgsResetCounts(void);

/// @def MESSAGE_ARGS_ALLOCATE
///
/// @brief Declare `type *name` pointing at zeroed storage for a scoped,
/// blocking, round-trip message.  `name` is NULL if there was no storage.
#define MESSAGE_ARGS_ALLOCATE(type, name) \
  type *name = (type*) messageArgsAllocate( \
    sizeof(type), MESSAGE_ARGS_ROUND_TRIP, false)

/// @def MESSAGE_ARGS_FREE
///
/// @brief Free storage from MESSAGE_ARGS_ALLOCATE.
#define MESSAGE_ARGS_FREE(name) messageArgsFree(name)

/// @def MESSAGE_ARGS_CREATE
///
/// @brief Allocate zeroed storage for a `type` whose lifetime the caller
/// manages, or NULL if there was none.
#define MESSAGE_ARGS_CREATE(type, ownership) \
  ((type*) messageArgsAllocate(sizeof(type), (ownership), true))

/// @def MESSAGE_ARGS_CREATE_BYTES
///
/// @brief Allocate `size` zeroed bytes whose lifetime the caller manages, or
/// NULL if there were none.
#define MESSAGE_ARGS_CREATE_BYTES(size, ownership) \
  messageArgsAllocate((size), (ownership), true)

/// @def MESSAGE_ARGS_DESTROY
///
/// @brief Free storage from MESSAGE_ARGS_CREATE or MESSAGE_ARGS_CREATE_BYTES.
#define MESSAGE_ARGS_DESTROY(ptr) messageArgsFree(ptr)

/// @def MESSAGE_ARGS_DESTROY_WITH
///
/// @brief Free storage from MESSAGE_ARGS_CREATE or MESSAGE_ARGS_CREATE_BYTES
/// in a process that can't use the normal allocator, such as the scheduler,
/// with `freeFunction` in place of free.
#define MESSAGE_ARGS_DESTROY_WITH(ptr, freeFunction) messageArgsFree(ptr)

/// @def MESSAGE_ARGS_ADOPT
///
/// @brief Take a sender's round-trip block for process `pid`, updating `ptr`
/// to where it now lives.  Returns 0 on success, -errno on failure.
#define MESSAGE_ARGS_ADOPT(ptr, pid, copyFn) \
  messageArgsAdopt((void**) &(ptr), (pid), (copyFn))

/// @def MESSAGE_ARGS_ADOPT_MOVES
///
/// @brief Whether MESSAGE_ARGS_ADOPT can move a block, so that a caller
/// re-derives the pointers it took from the block only when it has to.
#define MESSAGE_ARGS_ADOPT_MOVES true

/// @def MESSAGE_BUFFER_SHARE
///
/// @brief Yield the pointer to put in an argument structure for a buffer of
/// `size` bytes at `ptr` shared with process `recipient`, or NULL if it
/// couldn't be shared.
#define MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient) \
  messageBufferShare((void*) (ptr), (size), (direction), (recipient))

/// @def MESSAGE_BUFFER_UNSHARE
///
/// @brief End the sharing of `original` through `shared` once the exchange is
/// done.
#define MESSAGE_BUFFER_UNSHARE(shared, original, direction) \
  messageBufferUnshare((void*) (shared), (void*) (original), (direction))

/// @def MESSAGE_ARGS_SENDING
///
/// @brief Kernel hook: a message is about to be pushed to `recipient`.
#define MESSAGE_ARGS_SENDING(processMessage, recipient) \
  messageArgsSending((processMessage), (recipient))

/// @def MESSAGE_ARGS_SEND_FAILED
///
/// @brief Kernel hook: the push announced by MESSAGE_ARGS_SENDING failed.
#define MESSAGE_ARGS_SEND_FAILED(processMessage) \
  messageArgsSendFailed(processMessage)

/// @def MESSAGE_ARGS_SEND_COMPLETE
///
/// @brief Kernel hook: a message whose sender waited in place is done.
#define MESSAGE_ARGS_SEND_COMPLETE(processMessage) \
  messageArgsSendComplete(processMessage)

/// @def MESSAGE_ARGS_PROCESS_ENDED
///
/// @brief Kernel hook: process `pid` exited, was killed, or was replaced.
#define MESSAGE_ARGS_PROCESS_ENDED(pid, reportLeaks) \
  messageArgsProcessEnded((pid), (reportLeaks))

/// @def MESSAGE_ARGS_FLUSH_REPORTS
///
/// @brief Kernel hook: print reports queued by processes that can't print.
#define MESSAGE_ARGS_FLUSH_REPORTS() messageArgsFlushReports()

#else // NANO_OS_MESSAGE_ARGS_CHECKED

#define MESSAGE_ARGS_ALLOCATE(type, name) \
  type name##Storage = {0}; \
  type *name = &name##Storage

#define MESSAGE_ARGS_FREE(name) ((void) (name))

#define MESSAGE_ARGS_CREATE(type, ownership) \
  ((type*) calloc(1, sizeof(type)))

#define MESSAGE_ARGS_CREATE_BYTES(size, ownership) calloc(1, (size))

#define MESSAGE_ARGS_DESTROY(ptr) free(ptr)

#define MESSAGE_ARGS_DESTROY_WITH(ptr, freeFunction) freeFunction(ptr)

#define MESSAGE_ARGS_ADOPT(ptr, pid, copyFn) assignMemory((ptr), (pid))

#define MESSAGE_ARGS_ADOPT_MOVES false

#define MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient) ((void*) (ptr))

#define MESSAGE_BUFFER_UNSHARE(shared, original, direction) ((void) 0)

#define MESSAGE_ARGS_SENDING(processMessage, recipient) ((void) 0)

#define MESSAGE_ARGS_SEND_FAILED(processMessage) ((void) 0)

#define MESSAGE_ARGS_SEND_COMPLETE(processMessage) ((void) 0)

#define MESSAGE_ARGS_PROCESS_ENDED(pid, reportLeaks) ((void) 0)

#define MESSAGE_ARGS_FLUSH_REPORTS() ((void) 0)

#endif // NANO_OS_MESSAGE_ARGS_CHECKED

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MESSAGE_ARGS_H
