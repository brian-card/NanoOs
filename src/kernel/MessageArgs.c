///////////////////////////////////////////////////////////////////////////////
///
/// @file              MessageArgs.c
///
/// @brief             The checked implementation of MessageArgs.h, which
///                    backs message argument storage with an arena that
///                    detects misuse.  Compiled to nothing unless
///                    NANO_OS_MESSAGE_ARGS_CHECKED is defined.
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

#include "MessageArgs.h"

#ifdef NANO_OS_MESSAGE_ARGS_CHECKED

#include <string.h>

#include "Hal.h"
#include "MemoryManager.h"
#include "NanoOs.h"
#include "Processes.h"
#include "Scheduler.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsStdio.h"

/// @def NANO_OS_MESSAGE_ARGS_ARENA_SIZE
///
/// @brief The number of bytes in the checked arena, headers included.
#ifndef NANO_OS_MESSAGE_ARGS_ARENA_SIZE
#define NANO_OS_MESSAGE_ARGS_ARENA_SIZE 65536
#endif

/// @def MESSAGE_ARGS_ALIGNMENT
///
/// @brief The alignment of every block header and payload in the arena.
#define MESSAGE_ARGS_ALIGNMENT 16

/// @def MESSAGE_ARGS_MAGIC
///
/// @brief The value at the start of every block header.
#define MESSAGE_ARGS_MAGIC ((uint32_t) 0x4147534d)

/// @def MESSAGE_ARGS_POISON
///
/// @brief The byte that fills every free payload.
#define MESSAGE_ARGS_POISON ((uint8_t) 0xa5)

/// @def MESSAGE_ARGS_MAX_REPORTS
///
/// @brief The number of reports held until the scheduler prints them.
#define MESSAGE_ARGS_MAX_REPORTS 16

#define BLOCK_FREE      ((uint8_t) 0)
#define BLOCK_ALLOCATED ((uint8_t) 1)
#define BLOCK_ORPHANED  ((uint8_t) 2)

#define BLOCK_EXPLICIT  ((uint8_t) 0x01)
#define BLOCK_BUFFER    ((uint8_t) 0x02)
#define BLOCK_ADOPTED   ((uint8_t) 0x04)

/// @struct MessageArgsBlock
///
/// @brief The header in front of every block in the arena.
///
/// @param magic MESSAGE_ARGS_MAGIC.
/// @param size The payload's capacity in bytes, a multiple of
///   MESSAGE_ARGS_ALIGNMENT.
/// @param requested The number of bytes the caller asked for, or the length of
///   a shared buffer.
/// @param carrier The last message that carried the payload, or NULL.
/// @param original The sender's buffer, for a shared buffer.
/// @param state BLOCK_FREE, BLOCK_ALLOCATED, or BLOCK_ORPHANED.
/// @param ownership The MessageArgsOwnership the block was made with.
/// @param flags BLOCK_EXPLICIT, BLOCK_BUFFER, and BLOCK_ADOPTED bits.
/// @param direction The MessageBufferDirection of a shared buffer.
/// @param owner The process that allocated the block.
/// @param recipient The process the block was last sent to, or
///   PROCESS_ID_NOT_SET.
/// @param sharedWith The process a shared buffer was shared with, or
///   PROCESS_ID_NOT_SET.
typedef struct MessageArgsBlock {
  uint32_t                magic;
  uint32_t                size;
  size_t                  requested;
  ProcessMessage         *carrier;
  void                   *original;
  uint8_t                 state;
  MessageArgsOwnership    ownership;
  uint8_t                 flags;
  MessageBufferDirection  direction;
  ProcessId               owner;
  ProcessId               recipient;
  ProcessId               sharedWith;
} MessageArgsBlock;

/// @def HEADER_SIZE
///
/// @brief The bytes a block header takes in the arena.
#define HEADER_SIZE \
  ((sizeof(MessageArgsBlock) + MESSAGE_ARGS_ALIGNMENT - 1) \
    & ~((size_t) MESSAGE_ARGS_ALIGNMENT - 1))

/// @struct MessageArgsReport
///
/// @brief One error waiting to be printed.
///
/// @param error The MESSAGE_ARGS_ERROR_* value.
/// @param pid The process the error is charged to.
/// @param address The address of the storage involved.
typedef struct MessageArgsReport {
  uint8_t   error;
  ProcessId pid;
  uintptr_t address;
} MessageArgsReport;

static uint8_t _arena[NANO_OS_MESSAGE_ARGS_ARENA_SIZE]
  __attribute__((aligned(MESSAGE_ARGS_ALIGNMENT)));
static bool _arenaReady = false;
static MessageArgsCounts _counts;
static MessageArgsReport _reports[MESSAGE_ARGS_MAX_REPORTS];
static uint8_t _numReports = 0;

static const char _doubleFree[] KEEP_IN_FLASH = "double free";
static const char _badPointer[] KEEP_IN_FLASH = "bad pointer";
static const char _writeAfterFree[] KEEP_IN_FLASH = "write after free";
static const char _freeInFlight[] KEEP_IN_FLASH = "free in flight";
static const char _wrongOwner[] KEEP_IN_FLASH = "freed by non-owner";
static const char _wrongRecipient[] KEEP_IN_FLASH
  = "one-way freed by non-recipient";
static const char _fireAndForget[] KEEP_IN_FLASH
  = "round trip sent without waiting";
static const char _memoryManager[] KEEP_IN_FLASH
  = "created storage sent to memory manager";
static const char _inModified[] KEEP_IN_FLASH = "IN buffer modified";
static const char _freeAfterAdopt[] KEEP_IN_FLASH = "free after adopt";
static const char _sendAfterFree[] KEEP_IN_FLASH = "use after free";
static const char _leak[] KEEP_IN_FLASH = "leak";
static const char _arenaFull[] KEEP_IN_FLASH = "arena full";
static const char _overlayPointer[] KEEP_IN_FLASH = "overlay pointer shared";

/// @var _errorNames
///
/// @brief The name printed for each MESSAGE_ARGS_ERROR_* value.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char *const _errorNames[MESSAGE_ARGS_NUM_ERRORS] KEEP_IN_FLASH = {
  _doubleFree, _badPointer, _writeAfterFree, _freeInFlight, _wrongOwner,
  _wrongRecipient, _fireAndForget, _memoryManager, _inModified,
  _freeAfterAdopt, _sendAfterFree, _leak, _arenaFull, _overlayPointer,
};

static const char _reportPrefix[] KEEP_IN_FLASH = "MESSAGE_ARGS: ";
static const char _reportPid[] KEEP_IN_FLASH = " pid ";
static const char _reportAt[] KEEP_IN_FLASH = " at 0x";
static const char _reportDropped[] KEEP_IN_FLASH
  = "MESSAGE_ARGS: reports dropped: ";
static const char _reportNewline[] KEEP_IN_FLASH = "\n";

/// @fn static inline uint8_t* blockPayload(MessageArgsBlock *block)
///
/// @brief Get the payload that follows a block header.
///
/// @param block The block.
///
/// @return Returns a pointer to the first byte of the payload.
static inline uint8_t* blockPayload(MessageArgsBlock *block) {
  return ((uint8_t*) block) + HEADER_SIZE;
}

/// @fn static inline MessageArgsBlock* blockNext(MessageArgsBlock *block)
///
/// @brief Get the block that follows a block in the arena.
///
/// @param block The block.
///
/// @return Returns the next block, which is the end of the arena after the
/// last one.
static inline MessageArgsBlock* blockNext(MessageArgsBlock *block) {
  return (MessageArgsBlock*) (blockPayload(block) + block->size);
}

/// @fn static MessageArgsBlock* blockOf(const void *ptr)
///
/// @brief Find the block whose payload starts at a pointer.
///
/// @param ptr The pointer.
///
/// @return Returns the block, or NULL if the pointer isn't the start of a
/// payload in the arena.
static MessageArgsBlock* blockOf(const void *ptr) {
  const uint8_t *bytes = (const uint8_t*) ptr;
  if ((bytes < &_arena[HEADER_SIZE])
    || (bytes >= &_arena[NANO_OS_MESSAGE_ARGS_ARENA_SIZE])
    || (((size_t) (bytes - _arena)) % MESSAGE_ARGS_ALIGNMENT != 0)
  ) {
    return NULL;
  }

  MessageArgsBlock *block = (MessageArgsBlock*) (bytes - HEADER_SIZE);
  return (block->magic == MESSAGE_ARGS_MAGIC) ? block : NULL;
}

/// @fn static void arenaInit(void)
///
/// @brief Make the whole arena one poisoned free block, the first time it's
/// used.
///
/// @return This function returns no value.
static void arenaInit(void) {
  if (_arenaReady) {
    return;
  }

  MessageArgsBlock *block = (MessageArgsBlock*) _arena;
  memset(_arena, MESSAGE_ARGS_POISON, sizeof(_arena));
  memset(block, 0, sizeof(*block));
  block->magic = MESSAGE_ARGS_MAGIC;
  block->size = NANO_OS_MESSAGE_ARGS_ARENA_SIZE - HEADER_SIZE;
  block->state = BLOCK_FREE;
  block->recipient = PROCESS_ID_NOT_SET;
  _arenaReady = true;
}

/// @fn static void record(uint8_t error, const void *address, ProcessId pid)
///
/// @brief Count an error and queue its report.  Called inside a critical
/// section.
///
/// @param error The MESSAGE_ARGS_ERROR_* value.
/// @param address The address of the storage involved.
/// @param pid The process the error is charged to.
///
/// @return This function returns no value.
static void record(uint8_t error, const void *address, ProcessId pid) {
  _counts.errors[error]++;
  if (_numReports < MESSAGE_ARGS_MAX_REPORTS) {
    _reports[_numReports].error = error;
    _reports[_numReports].pid = pid;
    _reports[_numReports].address = (uintptr_t) address;
    _numReports++;
  } else {
    _counts.droppedReports++;
  }
}

/// @fn static void flushIfScheduler(void)
///
/// @brief Print the queued reports now if the scheduler is running, since
/// other processes may not be allowed to print.
///
/// @return This function returns no value.
static void flushIfScheduler(void) {
  if (getRunningPid() == schedulerPid) {
    messageArgsFlushReports();
  }
}

/// @fn static bool blockCarried(MessageArgsBlock *block)
///
/// @brief Determine whether a block's last carrier still carries it and isn't
/// done with it.
///
/// @param block The block.
///
/// @return Returns true if the block is in flight, false otherwise.
static bool blockCarried(MessageArgsBlock *block) {
  ProcessMessage *carrier = block->carrier;
  return (carrier != NULL)
    && (processMessageInUse(carrier) != NULL)
    && (processMessageData(carrier) == blockPayload(block))
    && (processMessageDone(carrier) == false);
}

/// @fn static void blockRelease(MessageArgsBlock *block, uint8_t flags)
///
/// @brief Poison a block's payload and return it to the free blocks.  Called
/// inside a critical section.
///
/// @param block The block.
/// @param flags The flags the free block keeps, BLOCK_ADOPTED or 0.
///
/// @return This function returns no value.
static void blockRelease(MessageArgsBlock *block, uint8_t flags) {
  if (block->state == BLOCK_ALLOCATED) {
    _counts.live--;
  }
  memset(blockPayload(block), MESSAGE_ARGS_POISON, block->size);
  block->state = BLOCK_FREE;
  block->flags = flags;
  block->carrier = NULL;
  block->original = NULL;
  block->recipient = PROCESS_ID_NOT_SET;
}

/// @fn static MessageArgsBlock* blockAllocate(size_t requested,
///   MessageArgsOwnership ownership, uint8_t flags)
///
/// @brief Hand out the first free block that fits, zeroed.
///
/// @param requested The number of bytes needed.
/// @param ownership The MessageArgsOwnership of the storage.
/// @param flags The BLOCK_EXPLICIT and BLOCK_BUFFER bits for the block.
///
/// @return Returns the block, or NULL if no free block fits.
static MessageArgsBlock* blockAllocate(size_t requested,
  MessageArgsOwnership ownership, uint8_t flags
) {
  size_t need = (requested + MESSAGE_ARGS_ALIGNMENT - 1)
    & ~((size_t) MESSAGE_ARGS_ALIGNMENT - 1);
  if (need == 0) {
    need = MESSAGE_ARGS_ALIGNMENT;
  }
  MessageArgsBlock *end
    = (MessageArgsBlock*) &_arena[NANO_OS_MESSAGE_ARGS_ARENA_SIZE];

  coroutineEnterCriticalSection();
  arenaInit();
  MessageArgsBlock *found = NULL;
  for (MessageArgsBlock *block = (MessageArgsBlock*) _arena; block < end;
    block = blockNext(block)
  ) {
    if ((block->state == BLOCK_ORPHANED) && !blockCarried(block)) {
      blockRelease(block, 0);
    }
    if (block->state != BLOCK_FREE) {
      continue;
    }

    MessageArgsBlock *next = blockNext(block);
    while ((next < end) && (next->state == BLOCK_FREE)) {
      MessageArgsBlock *after = blockNext(next);
      memset(next, MESSAGE_ARGS_POISON, HEADER_SIZE);
      block->size += (uint32_t) (((uint8_t*) after) - ((uint8_t*) next));
      next = after;
    }
    if (block->size >= need) {
      found = block;
      break;
    }
  }

  if (found == NULL) {
    record(MESSAGE_ARGS_ERROR_ARENA_FULL, NULL, getRunningPid());
    coroutineExitCriticalSection();
    return NULL;
  }

  uint8_t *payload = blockPayload(found);
  for (size_t ii = 0; ii < need; ii++) {
    if (payload[ii] != MESSAGE_ARGS_POISON) {
      record(MESSAGE_ARGS_ERROR_WRITE_AFTER_FREE, &payload[ii],
        getRunningPid());
      break;
    }
  }

  if (found->size >= need + HEADER_SIZE + MESSAGE_ARGS_ALIGNMENT) {
    MessageArgsBlock *rest = (MessageArgsBlock*) (payload + need);
    memset(rest, 0, sizeof(*rest));
    rest->magic = MESSAGE_ARGS_MAGIC;
    rest->size = (uint32_t) (found->size - need - HEADER_SIZE);
    rest->state = BLOCK_FREE;
    rest->recipient = PROCESS_ID_NOT_SET;
    found->size = (uint32_t) need;
  }

  found->requested = requested;
  found->carrier = NULL;
  found->original = NULL;
  found->state = BLOCK_ALLOCATED;
  found->ownership = ownership;
  found->flags = flags;
  found->direction = 0;
  found->owner = getRunningPid();
  found->recipient = PROCESS_ID_NOT_SET;
  found->sharedWith = PROCESS_ID_NOT_SET;
  memset(payload, 0, found->size);
  _counts.live++;
  coroutineExitCriticalSection();

  return found;
}

/// @fn void* messageArgsAllocate(size_t size,
///   MessageArgsOwnership ownership, bool explicitLifetime)
///
/// @brief Allocate zeroed argument storage from the arena.
///
/// @param size The number of bytes needed.
/// @param ownership The MessageArgsOwnership of the storage.
/// @param explicitLifetime Whether the storage came from MESSAGE_ARGS_CREATE
///   or MESSAGE_ARGS_CREATE_BYTES rather than MESSAGE_ARGS_ALLOCATE.
///
/// @return Returns a pointer to the storage, or NULL if the arena is full.
void* messageArgsAllocate(size_t size, MessageArgsOwnership ownership,
  bool explicitLifetime
) {
  MessageArgsBlock *block = blockAllocate(size, ownership,
    explicitLifetime ? BLOCK_EXPLICIT : 0);
  flushIfScheduler();
  return (block != NULL) ? blockPayload(block) : NULL;
}

/// @fn void messageArgsFree(void *ptr)
///
/// @brief Free argument storage, checking that the caller may free it now.
///
/// @param ptr The storage, or NULL.
///
/// @return This function returns no value.
void messageArgsFree(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  ProcessId pid = getRunningPid();
  coroutineEnterCriticalSection();
  MessageArgsBlock *block = blockOf(ptr);
  if (block == NULL) {
    record(MESSAGE_ARGS_ERROR_BAD_POINTER, ptr, pid);
  } else if (block->state == BLOCK_FREE) {
    record(((block->flags & BLOCK_ADOPTED) != 0)
      ? MESSAGE_ARGS_ERROR_FREE_AFTER_ADOPT : MESSAGE_ARGS_ERROR_DOUBLE_FREE,
      ptr, pid);
  } else {
    if ((block->ownership & MESSAGE_ARGS_ONE_WAY) != 0) {
      if (block->recipient == PROCESS_ID_NOT_SET) {
        if (pid != block->owner) {
          record(MESSAGE_ARGS_ERROR_WRONG_OWNER, ptr, pid);
        }
      } else if (pid != block->recipient) {
        record(MESSAGE_ARGS_ERROR_WRONG_RECIPIENT, ptr, pid);
      }
    } else {
      if (pid != block->owner) {
        record(MESSAGE_ARGS_ERROR_WRONG_OWNER, ptr, pid);
      }
      if (blockCarried(block)) {
        record(MESSAGE_ARGS_ERROR_FREE_IN_FLIGHT, ptr, pid);
      }
    }
    blockRelease(block, 0);
  }
  coroutineExitCriticalSection();
  flushIfScheduler();
}

/// @fn int messageArgsAdoptWith(void **ptr, ProcessId pid,
///   MessageArgsCopyFn copyFn, void* (*allocate)(size_t size),
///   int (*assign)(void *ptr, ProcessId pid))
///
/// @brief Move a block into memory from `allocate` that is assigned to a
/// process, and release the arena block.  Takes the allocator and the
/// assigner as parameters so the bookkeeping can be driven from outside the
/// scheduler.
///
/// @param ptr A pointer to the caller's pointer to the block, which is updated
///   to the new memory on success.
/// @param pid The process to assign the memory to.
/// @param copyFn The function that rebuilds the block at its new address, or
///   NULL to copy it byte for byte.
/// @param allocate The allocator for the new memory.
/// @param assign The function that assigns the new memory to pid.
///
/// @return Returns the value assign returned, or -EINVAL or -ENOMEM if the
/// block couldn't be moved.
int messageArgsAdoptWith(void **ptr, ProcessId pid, MessageArgsCopyFn copyFn,
  void* (*allocate)(size_t size), int (*assign)(void *ptr, ProcessId pid)
) {
  if ((ptr == NULL) || (*ptr == NULL)) {
    return -EINVAL;
  }

  coroutineEnterCriticalSection();
  MessageArgsBlock *block = blockOf(*ptr);
  if (block == NULL) {
    coroutineExitCriticalSection();
    return assign(*ptr, pid);
  } else if (block->state != BLOCK_ALLOCATED) {
    record(MESSAGE_ARGS_ERROR_SEND_AFTER_FREE, *ptr, getRunningPid());
    coroutineExitCriticalSection();
    flushIfScheduler();
    return -EINVAL;
  }
  size_t requested = block->requested;
  coroutineExitCriticalSection();

  void *copy = allocate(requested);
  if (copy == NULL) {
    return -ENOMEM;
  }
  if (copyFn != NULL) {
    copyFn(copy, *ptr, requested);
  } else {
    memcpy(copy, *ptr, requested);
  }
  int returnValue = assign(copy, pid);

  coroutineEnterCriticalSection();
  blockRelease(block, BLOCK_ADOPTED);
  coroutineExitCriticalSection();
  *ptr = copy;
  flushIfScheduler();

  return returnValue;
}

/// @fn int messageArgsAdopt(void **ptr, ProcessId pid,
///   MessageArgsCopyFn copyFn)
///
/// @brief Adopt a block for a process from the scheduler, with schedMalloc and
/// assignMemory.
///
/// @param ptr A pointer to the caller's pointer to the block.
/// @param pid The process to assign the memory to.
/// @param copyFn The function that rebuilds the block, or NULL.
///
/// @return Returns 0 on success, -errno on failure.
int messageArgsAdopt(void **ptr, ProcessId pid, MessageArgsCopyFn copyFn) {
  return messageArgsAdoptWith(ptr, pid, copyFn, schedMalloc, assignMemory);
}

/// @fn void* messageBufferShare(void *ptr, size_t size,
///   MessageBufferDirection direction, ProcessId recipient)
///
/// @brief Copy a buffer into the arena for a recipient.  An IN buffer is
/// followed by a reference copy that unsharing compares against.
///
/// @param ptr The sender's buffer, or NULL.
/// @param size The buffer's length in bytes.
/// @param direction The MessageBufferDirection of the buffer.
/// @param recipient The process the buffer is shared with.
///
/// @return Returns the copy, or NULL if ptr was NULL or the arena is full.
void* messageBufferShare(void *ptr, size_t size,
  MessageBufferDirection direction, ProcessId recipient
) {
  if (ptr == NULL) {
    return NULL;
  }

  extern NanoOsOverlayMap *overlayMap;
  if ((overlayMap != NULL)
    && (((uintptr_t) ptr) - ((uintptr_t) overlayMap)
      < HAL->memory->overlaySize)
  ) {
    coroutineEnterCriticalSection();
    record(MESSAGE_ARGS_ERROR_OVERLAY_POINTER, ptr, getRunningPid());
    coroutineExitCriticalSection();
    flushIfScheduler();
  }

  size_t capacity = ((direction & MESSAGE_BUFFER_IN) != 0) ? 2 * size : size;
  MessageArgsBlock *block
    = blockAllocate(capacity, MESSAGE_ARGS_ROUND_TRIP, BLOCK_BUFFER);
  if (block == NULL) {
    flushIfScheduler();
    return NULL;
  }

  uint8_t *payload = blockPayload(block);
  block->original = ptr;
  block->requested = size;
  block->direction = direction;
  block->sharedWith = recipient;
  if ((direction & MESSAGE_BUFFER_IN) != 0) {
    memcpy(payload, ptr, size);
    memcpy(payload + size, ptr, size);
  }

  return payload;
}

/// @fn void messageBufferUnshare(void *shared, void *original,
///   MessageBufferDirection direction)
///
/// @brief Copy an OUT buffer back to the sender, or check that an IN buffer
/// wasn't written, and release the copy.
///
/// @param shared The copy messageBufferShare returned, or NULL.
/// @param original The sender's buffer.
/// @param direction The MessageBufferDirection it was shared with.
///
/// @return This function returns no value.
void messageBufferUnshare(void *shared, void *original,
  MessageBufferDirection direction
) {
  if (shared == NULL) {
    return;
  }

  ProcessId pid = getRunningPid();
  coroutineEnterCriticalSection();
  MessageArgsBlock *block = blockOf(shared);
  if ((block == NULL) || ((block->flags & BLOCK_BUFFER) == 0)) {
    record(MESSAGE_ARGS_ERROR_BAD_POINTER, shared, pid);
  } else if (block->state == BLOCK_FREE) {
    record(MESSAGE_ARGS_ERROR_DOUBLE_FREE, shared, pid);
  } else if ((block->original != original) || (block->direction != direction)) {
    record(MESSAGE_ARGS_ERROR_BAD_POINTER, shared, pid);
  } else {
    uint8_t *payload = blockPayload(block);
    if ((direction & MESSAGE_BUFFER_OUT) != 0) {
      memcpy(original, payload, block->requested);
    } else if (memcmp(payload, payload + block->requested, block->requested)
      != 0
    ) {
      record(MESSAGE_ARGS_ERROR_IN_MODIFIED, shared, pid);
    }
    if (pid != block->owner) {
      record(MESSAGE_ARGS_ERROR_WRONG_OWNER, shared, pid);
    }
    blockRelease(block, 0);
  }
  coroutineExitCriticalSection();
  flushIfScheduler();
}

/// @fn void messageArgsSending(ProcessMessage *processMessage,
///   ProcessId recipient)
///
/// @brief Record that a message carrying arena storage is about to be pushed,
/// and check that the storage may be sent this way.
///
/// @param processMessage The message.
/// @param recipient The process it is being sent to.
///
/// @return This function returns no value.
void messageArgsSending(ProcessMessage *processMessage, ProcessId recipient) {
  void *data = processMessageData(processMessage);
  coroutineEnterCriticalSection();
  MessageArgsBlock *block = blockOf(data);
  if (block == NULL) {
    coroutineExitCriticalSection();
    return;
  }

  ProcessId pid = getRunningPid();
  if (block->state != BLOCK_ALLOCATED) {
    record(MESSAGE_ARGS_ERROR_SEND_AFTER_FREE, data, pid);
  } else {
    block->carrier = processMessage;
    block->recipient = recipient;
    if (((block->ownership & MESSAGE_ARGS_ONE_WAY) == 0)
      && (processMessageWaiting(processMessage) == false)
    ) {
      record(MESSAGE_ARGS_ERROR_FIRE_AND_FORGET, data, pid);
    }
    if (((block->flags & BLOCK_EXPLICIT) != 0)
      && (recipient == memoryManagerPid)
    ) {
      record(MESSAGE_ARGS_ERROR_MEMORY_MANAGER, data, pid);
    }
  }
  coroutineExitCriticalSection();
  flushIfScheduler();
}

/// @fn void messageArgsSendFailed(ProcessMessage *processMessage)
///
/// @brief Undo messageArgsSending for a push that failed.
///
/// @param processMessage The message.
///
/// @return This function returns no value.
void messageArgsSendFailed(ProcessMessage *processMessage) {
  coroutineEnterCriticalSection();
  MessageArgsBlock *block = blockOf(processMessageData(processMessage));
  if ((block != NULL) && (block->carrier == processMessage)) {
    block->carrier = NULL;
    block->recipient = PROCESS_ID_NOT_SET;
  }
  coroutineExitCriticalSection();
}

/// @fn void messageArgsSendComplete(ProcessMessage *processMessage)
///
/// @brief Forget the carrier of a message whose sender waited for it in
/// place, since that message is about to go out of scope.
///
/// @param processMessage The message.
///
/// @return This function returns no value.
void messageArgsSendComplete(ProcessMessage *processMessage) {
  coroutineEnterCriticalSection();
  MessageArgsBlock *block = blockOf(processMessageData(processMessage));
  if ((block != NULL) && (block->carrier == processMessage)) {
    block->carrier = NULL;
  }
  coroutineExitCriticalSection();
}

/// @fn void messageArgsProcessEnded(ProcessId pid, bool reportLeaks)
///
/// @brief Release the storage a process that has ended was responsible for.
/// Storage still in flight is left to its recipient and released once its
/// carrier lets go of it.
///
/// @param pid The process that ended.
/// @param reportLeaks Whether the process exited on its own, which makes
///   anything it left behind a leak.
///
/// @return This function returns no value.
void messageArgsProcessEnded(ProcessId pid, bool reportLeaks) {
  MessageArgsBlock *end
    = (MessageArgsBlock*) &_arena[NANO_OS_MESSAGE_ARGS_ARENA_SIZE];

  coroutineEnterCriticalSection();
  if (!_arenaReady) {
    coroutineExitCriticalSection();
    return;
  }
  for (MessageArgsBlock *block = (MessageArgsBlock*) _arena; block < end;
    block = blockNext(block)
  ) {
    if (block->state != BLOCK_ALLOCATED) {
      continue;
    }

    ProcessId responsible = block->owner;
    if (((block->ownership & MESSAGE_ARGS_ONE_WAY) != 0)
      && (block->recipient != PROCESS_ID_NOT_SET)
    ) {
      responsible = block->recipient;
    }
    if (responsible != pid) {
      continue;
    }

    if (blockCarried(block)) {
      block->state = BLOCK_ORPHANED;
      _counts.live--;
      _counts.orphaned++;
    } else {
      if (reportLeaks) {
        record(MESSAGE_ARGS_ERROR_LEAK, blockPayload(block), pid);
      } else {
        _counts.reclaimed++;
      }
      blockRelease(block, 0);
    }
  }
  coroutineExitCriticalSection();
  flushIfScheduler();
}

/// @fn void messageArgsFlushReports(void)
///
/// @brief Print and clear the queued reports.
///
/// @return This function returns no value.
void messageArgsFlushReports(void) {
  while (1) {
    coroutineEnterCriticalSection();
    if (_numReports == 0) {
      coroutineExitCriticalSection();
      break;
    }
    MessageArgsReport report = _reports[0];
    memmove(&_reports[0], &_reports[1],
      (_numReports - 1) * sizeof(_reports[0]));
    _numReports--;
    coroutineExitCriticalSection();

    printString(_reportPrefix);
    printString(_errorNames[report.error]);
    printString(_reportPid);
    printInt(report.pid);
    printString(_reportAt);
    printHex(report.address);
    printString(_reportNewline);
  }

  if (_counts.droppedReports != 0) {
    printString(_reportDropped);
    printInt(_counts.droppedReports);
    printString(_reportNewline);
    _counts.droppedReports = 0;
  }
}

/// @fn const MessageArgsCounts* messageArgsCounts(void)
///
/// @brief Get what the checker has counted.
///
/// @return Returns a pointer to the counts.
const MessageArgsCounts* messageArgsCounts(void) {
  return &_counts;
}

/// @fn uint16_t messageArgsErrorTotal(void)
///
/// @brief Get the number of errors of every kind counted.
///
/// @return Returns the sum of the error counts.
uint16_t messageArgsErrorTotal(void) {
  uint16_t total = 0;
  for (int ii = 0; ii < MESSAGE_ARGS_NUM_ERRORS; ii++) {
    total += _counts.errors[ii];
  }
  return total;
}

/// @fn void messageArgsResetCounts(void)
///
/// @brief Clear the error, reclaimed, and orphaned counts and the queued
/// reports.  The live count is kept.
///
/// @return This function returns no value.
void messageArgsResetCounts(void) {
  coroutineEnterCriticalSection();
  memset(_counts.errors, 0, sizeof(_counts.errors));
  _counts.reclaimed = 0;
  _counts.orphaned = 0;
  _counts.droppedReports = 0;
  _numReports = 0;
  coroutineExitCriticalSection();
}

#endif // NANO_OS_MESSAGE_ARGS_CHECKED
