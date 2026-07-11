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

// Standard C includes
#include "string.h"

// NanoOs includes
#include "Console.h"
#include "Hal.h"
#include "Logger.h"
#include "MemoryManager.h"
#include "NanoOs.h"
#include "OverlayFunctions.h"
#include "Scheduler.h"
#include "Processes.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsStdio.h"

/****************** Begin Custom Memory Management Functions ******************/

/// @def isDynamicPointer
///
/// @brief Determine whether or not a pointer was allocated from the allocators
/// in this library.
#define isDynamicPointer(ptr) \
  ((((uintptr_t) (ptr)) >= memoryManagerState->start) \
    && (((uintptr_t) (ptr)) <= memoryManagerState->end))

/// @def memNode
///
/// @brief Get a pointer to the MemNode for a memory address.
#define memNode(ptr) \
  ((isDynamicPointer(ptr) == true) ? &((MemNode*) (ptr))[-1] : NULL)

/// @def sizeOfMemory
///
/// @brief Retrieve the size of a block of dynamic memory.  This information is
/// stored sizeof(MemNode) bytes before the pointer.
#define sizeOfMemory(ptr) \
  ((isDynamicPointer(ptr) == true) ? ((uint32_t) memNode(ptr)->size) : 0)

#ifdef __cplusplus
extern "C"
{
#endif

/// @fn void localFree(MemoryManagerState *memoryManagerState,
///   void *ptr, ProcessId callingPid)
///
/// @brief Free a previously-allocated block of memory.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param ptr A pointer to the block of memory to free.
/// @param callingPid The PID of the process freeing the memory.
///
/// @return This function always succeeds and returns no value.
void localFree(MemoryManagerState *memoryManagerState,
  void *ptr, ProcessId callingPid
) {
  (void) callingPid; // Used for debugging, so make the compiler ignore it.

  logDebug("In localFree\n");
  if (!isDynamicPointer(ptr)) {
    // This is not something we can free.  Ignore it.
    logDebug("Error: Request to free non-dynamic memory 0x%llx\n",
      (unsigned long long int) (uintptr_t) ptr);
    return;
  }

  MemNode *memNode = memNode(ptr);

  // This is memory that was previously allocated from one of our allocators.
  logDebug("Process %lld freeing %lld bytes at 0x%llx from process %lld\n",
    (long long int) callingPid, (long long int) memNode->size,
    (unsigned long long int) (uintptr_t) ptr, (long long int) memNode->owner);
  logDebug("memNode = 0x%llx\n", (unsigned long long int) (uintptr_t) memNode);

  MemNode *cur = NULL;
#ifdef NANO_OS_MEM_DEBUG
  for (cur = memoryManagerState->allocated; cur != NULL; cur = cur->next) {
    if (cur == memNode) {
      break;
    }
  }
  if (cur == NULL) {
    logDebug("ERROR!!!  memNode is not allocated!!\n");
    HAL->power->enterMode(HAL_POWER_MODE_OFF);
  }
#endif // NANO_OS_MEM_DEBUG

  // Splice out memNode from the allocated list.
  if (memNode->prev != NULL) {
#ifdef NANO_OS_MEM_DEBUG
    for (cur = memoryManagerState->allocated; cur != NULL; cur = cur->next) {
      if (cur == memNode->prev) {
        break;
      }
    }
    if (cur == NULL) {
      logDebug("ERROR!!!  memNode->prev is not allocated!!\n");
      logDebug("memNode->prev = 0x%llx\n",
        (unsigned long long int) (uintptr_t) memNode->prev);
      HAL->power->enterMode(HAL_POWER_MODE_OFF);
    }
#endif // NANO_OS_MEM_DEBUG
    logDebug("Updating memNode->prev->next\n");
    memNode->prev->next = memNode->next;
  }
  if (memNode->next != NULL) {
    logDebug("Updating memNode->next->prev\n");
    memNode->next->prev = memNode->prev;
  }
  if (memoryManagerState->allocated == memNode) {
    logDebug("Updating memoryManagerState->allocated\n");
    memoryManagerState->allocated = memNode->next;
  }

  // Put the memNode in the right place in the free list.
  logDebug("Searching free list in reverse order\n");
  cur = memoryManagerState->lastFree;
#ifdef NANO_OS_MEM_DEBUG
  if (((uintptr_t) cur) < ((uintptr_t) memNode)) {
    // This should be impossible.
    logDebug("ERROR!!! cur (0x%llx) < memNode (0x%llx)\n",
      (unsigned long long int) (uintptr_t) cur,
      (unsigned long long int) (uintptr_t) memNode);
    HAL->power->enterMode(HAL_POWER_MODE_OFF);
  }
#endif // NANO_OS_MEM_DEBUG
  while (((uintptr_t) cur->prev) > ((uintptr_t) memNode)) {
    cur = cur->prev;
  }
  logDebug("cur = 0x%llx\n", (unsigned long long int) (uintptr_t) cur);

#ifdef NANO_OS_MEM_DEBUG
  if (((uintptr_t) cur) < ((uintptr_t) memNode)) {
    // This should be impossible.
    logDebug("ERROR!!! cur (0x%llx) < memNode (0x%llx)\n",
      (unsigned long long int) (uintptr_t) cur,
      (unsigned long long int) (uintptr_t) memNode);
    HAL->power->enterMode(HAL_POWER_MODE_OFF);
  }
#endif // NANO_OS_MEM_DEBUG
  memNode->next = cur;
  logDebug("memNode->next = 0x%llx\n",
    (unsigned long long int) (uintptr_t) memNode->next);

  memNode->prev = cur->prev;
  logDebug("memNode->prev = 0x%llx\n",
    (unsigned long long int) (uintptr_t) memNode->prev);

  size_t bytesFreeBefore = memoryManagerState->bytesFree;
  memoryManagerState->bytesFree += memNode->size;
  logDebug("Increasing memoryManagerState->bytesFree from %lld to %lld\n",
    (long long int) bytesFreeBefore,
    (long long int) memoryManagerState->bytesFree);

  MemNode *next
    = (MemNode*) (((uint8_t*) memNode) + memNode->size + sizeof(MemNode));

  if (next != cur) {
    logDebug("next != cur\n");
    logDebug("Setting cur->prev to 0x%llx\n",
      (unsigned long long int) (uintptr_t) memNode);

    cur->prev = memNode;
  } else {
    // Do memory compaction between memNode and cur.
    logDebug("next == cur\n");
    logDebug("Doing memory compaction\n");

    memNode->size += cur->size + sizeof(MemNode);
#ifdef NANO_OS_MEM_DEBUG
    if ((cur->next != NULL)
      && (((uintptr_t) cur->next) < ((uintptr_t) memNode))
    ) {
      // This should be impossible.
      logDebug("ERROR!!! cur->next (0x%llx) < memNode (0x%llx)\n",
        (unsigned long long int) (uintptr_t) cur->next,
        (unsigned long long int) (uintptr_t) memNode);
      HAL->power->enterMode(HAL_POWER_MODE_OFF);
    }
#endif // NANO_OS_MEM_DEBUG
    memNode->next = cur->next;
    if (memNode->next != NULL) {
      memNode->next->prev = memNode;
    }
    if (memoryManagerState->lastFree == cur) {
      logDebug("Setting memoryManagerState->lastFree to memNode\n");
      memoryManagerState->lastFree = memNode;
    }

    bytesFreeBefore = memoryManagerState->bytesFree;
    memoryManagerState->bytesFree += sizeof(MemNode);
    logDebug("Increasing memoryManagerState->bytesFree from %lld to %lld\n",
      (long long int) bytesFreeBefore,
      (long long int) memoryManagerState->bytesFree);
  }

  if (memNode->prev == NULL) {
    logDebug("memNode->prev == NULL\n");
    logDebug("Setting memoryManagerState->firstFree to memNode\n");
    memoryManagerState->firstFree = memNode;
    return;
  }

  logDebug("memNode->prev != NULL\n");

  MemNode *prev = memNode->prev;
  logDebug("prev = 0x%llx\n", (unsigned long long int) (uintptr_t) prev);

  next = (MemNode*) (((uint8_t*) prev) + prev->size + sizeof(MemNode));
  logDebug("next = 0x%llx\n", (unsigned long long int) (uintptr_t) next);

  if (next != memNode) {
    logDebug("next != memNode\n");
    logDebug("Setting prev->next to memNode\n");

#ifdef NANO_OS_MEM_DEBUG
    if (((uintptr_t) memNode) < ((uintptr_t) prev)) {
      // This should be impossible.
      logDebug("ERROR!!! memNode (0x%llx) < prev (0x%llx)\n",
        (unsigned long long int) (uintptr_t) memNode,
        (unsigned long long int) (uintptr_t) prev);
      HAL->power->enterMode(HAL_POWER_MODE_OFF);
    }
#endif // NANO_OS_MEM_DEBUG
    prev->next = memNode;
  } else {
    // Do memory compaction between prev and memNode.
    logDebug("next == memNode\n");
    logDebug("Doing memory compaction\n");

    prev->size += memNode->size + sizeof(MemNode);
    logDebug("prev->size = %lld\n", (long long int) prev->size);

#ifdef NANO_OS_MEM_DEBUG
    if ((memNode->next != NULL)
      && (((uintptr_t) memNode->next) < ((uintptr_t) prev))
    ) {
      // This should be impossible.
      logDebug("ERROR!!! memNode->next (0x%llx) < prev (0x%llx)\n",
        (unsigned long long int) (uintptr_t) memNode->next,
        (unsigned long long int) (uintptr_t) prev);
      HAL->power->enterMode(HAL_POWER_MODE_OFF);
    }
#endif // NANO_OS_MEM_DEBUG
    prev->next = memNode->next;
    if (prev->next != NULL) {
      prev->next->prev = prev;
    }
    logDebug("prev->next = 0x%llx\n",
      (unsigned long long int) (uintptr_t) prev->next);

    if (memoryManagerState->lastFree == memNode) {
      logDebug("Setting memoryManagerState->lastFree to prev\n");
      memoryManagerState->lastFree = prev;
    }

    bytesFreeBefore = memoryManagerState->bytesFree;
    memoryManagerState->bytesFree += sizeof(MemNode);
    logDebug("Increasing memoryManagerState->bytesFree from %lld to %lld\n",
      (long long int) bytesFreeBefore,
      (long long int) memoryManagerState->bytesFree);
  }

  return;
}

/// @fn void localFreeProcessMemory(
///   MemoryManagerState *memoryManagerState, ProcessId pid,
///   ProcessId callingPid)
///
/// @brief Free *ALL* the memory owned by a process given its process ID.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param pid The ID of the process to free the memory of.
/// @param callingPid The ID of the process making the call.
///
/// @return This function always succeeds and returns no value.
void localFreeProcessMemory(
  MemoryManagerState *memoryManagerState, ProcessId pid, ProcessId callingPid
) {
  for (MemNode *cur = memoryManagerState->allocated; cur != NULL; ) {
    MemNode *next = cur->next;
    if (cur->owner == pid) {
      logDebug("Freeing 0x%llx\n", (unsigned long long int) (uintptr_t) &cur[1]);
      localFree(memoryManagerState, &cur[1], callingPid);
    }
    cur = next;
  }
  
  return;
}

/// @fn void* localRealloc(MemoryManagerState *memoryManagerState,
///   void *ptr, size_t size, Processid pid)
///
/// @brief Reallocate a provided pointer to a new size.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param ptr A pointer to the original block of dynamic memory.  If this value
///   is NULL, new memory will be allocated.
/// @param size The new size desired for the memory block at ptr.  If this value
///   is 0, the provided pointer will be freed.
/// @param pid The ID of the process making the request.
///
/// @return Returns a pointer to size-adjusted memory on success, NULL on
/// failure or on free.
void* localRealloc(MemoryManagerState *memoryManagerState,
  void *ptr, size_t size, ProcessId pid
) {
  logDebug("In localRealloc\n");
  // We need to fix the size to be aligned with our memory model.
  size += sizeof(size_t) - 1;
  size &= ~(sizeof(size_t) - 1);

  if (size == 0) {
    // In this case, there's no point in going through any path below.  Just
    // free it, return NULL, and be done with it.
    localFree(memoryManagerState, ptr, pid);
    return NULL;
  } else if ((size + sizeof(MemNode)) > memoryManagerState->bytesFree) {
    // Sanity test failed.  We're being asked for more memory than is available
    // in the system.  Fail immediately.
    logDebug("Error: Request to allocate %lld bytes, which is more than "
      "available memory of %lld bytes\n",
      (long long int) size, (long long int) memoryManagerState->bytesFree);
    return NULL;
  }
  
  void *returnValue = NULL;
  char *charPointer = (char*) ptr;
  MemNode *next = NULL;
  if (isDynamicPointer(ptr)) {
    // This pointer was allocated from our allocator.
    MemNode *memNode = memNode(ptr);
    size_t oldSize = sizeOfMemory(ptr);
    next = (MemNode*) (charPointer + oldSize);
    
    if (size <= oldSize) {
      // We're fitting into a block that's larger than or equal to the size
      // being requested.  *DO NOT* update the size in this case.  Just
      // return the current pointer.
      logDebug("Reallocating less memory than availabe\n");
      logDebug("Returing ptr\n");
      return ptr;
    } else if (next == memoryManagerState->lastFree) {
      // We're being asked to extend the last block that was allocated.  Just
      // extend it if we have enough space.
      if ((memNode->size + next->size) >= size) {
        logDebug("Extending last memory block\n");
        MemNode lastFree = *memoryManagerState->lastFree;
        next = (MemNode*) (charPointer + size);
        next->prev = lastFree.prev;
        next->next = NULL;
        if (next->prev != NULL) {
#ifdef NANO_OS_MEM_DEBUG
          if (((uintptr_t) next) < ((uintptr_t) next->prev)) {
            // This should be impossible.
            logDebug("ERROR!!! next (0x%llx) < next->prev (0x%llx)\n",
              (unsigned long long int) (uintptr_t) next,
              (unsigned long long int) (uintptr_t) next->prev);
            HAL->power->enterMode(HAL_POWER_MODE_OFF);
          }
#endif // NANO_OS_MEM_DEBUG
          next->prev->next = next;
        }
        // Reduce the free space by the delta between how much we were requested
        // and how much used to be managed by this node.
        size_t delta = size - memNode->size;
        memoryManagerState->bytesFree -= delta;
        next->size = lastFree.size - delta;
        memNode->size = size;
        if (memoryManagerState->firstFree == memoryManagerState->lastFree) {
          memoryManagerState->firstFree = next;
        }
        memoryManagerState->lastFree = next;
        return ptr;
      }
      
      // If we made it this far then we don't have enough memory to grant the
      // request at the end of memory.  Fall through to the logic below.
    }
  } else if (ptr != NULL) {
    // We're being asked to reallocate a pointer that was *NOT* allocated by
    // this allocator.  This is not valid and we cannot do this.  Fail.
    logDebug("ERROR: Asked to reallocate a non-dynamic pointer\n");
    return NULL;
  }

  // We're allocating new memory.  Search from the beginning.
  logDebug("Allocating %lld bytes, searching from beginning\n",
    (long long int) size);
  MemNode *cur = NULL;
  for (cur = memoryManagerState->firstFree; cur != NULL; cur = cur->next) {
#ifdef NANO_OS_MEM_DEBUG
    if (((uintptr_t) cur->prev) >= ((uintptr_t) cur)) {
      logDebug("ERROR!!! cur->prev (0x%llx) >= cur (0x%llx)\n",
        (unsigned long long int) (uintptr_t) cur->prev,
        (unsigned long long int) (uintptr_t) cur);
      HAL->power->enterMode(HAL_POWER_MODE_OFF);
    }

    if ((cur->next == NULL) && (cur == memoryManagerState->lastFree)) {
      // Do nothing.  This is just a guard against the next case.
    } else if (((uintptr_t) cur->next) <= ((uintptr_t) cur)) {
      logDebug("ERROR!!! cur->next (0x%llx) <= cur (0x%llx)\n",
        (unsigned long long int) (uintptr_t) cur->next,
        (unsigned long long int) (uintptr_t) cur);
      HAL->power->enterMode(HAL_POWER_MODE_OFF);
    }
#endif // NANO_OS_MEM_DEBUG

    if (cur->size >= size) {
      break;
    }

    logDebug("0x%llx only has %lld bytes available, need %lld\n",
      (unsigned long long int) (uintptr_t) cur,
      (long long int) cur->size, (long long int) size);
#ifdef NANO_OS_MEM_DEBUG
    //// msleep(100);
#endif // NANO_OS_MEM_DEBUG
  }
  logDebug("Memory search complete\n");

  if (cur != NULL) {
    // Memory allocation has succeeded.
    logDebug("Found available memory node 0x%llx\n",
      (unsigned long long int) (uintptr_t) cur);
    logDebug("cur->size = %lld\n", (long long int) cur->size);

    returnValue = &cur[1];
    logDebug("returnValue = 0x%llx\n",
      (unsigned long long int) (uintptr_t) returnValue);

    charPointer = (char*) returnValue;
    
    if (cur->size >= (size + sizeof(MemNode))) {
      // This is the expected case.
      next = (MemNode*) (charPointer + size);
    } else if (cur->next != NULL) {
      next = cur->next;
      size = cur->size;
    } else {
      // cur == memoryManagerState->lastFree and there isn't enough memory left
      // for the data plus a memory node.  We could get particular about this
      // and allow for NULL pointers in firstFree and lastFree but I *REALLY*
      // don't want to add in the code complexity to manage those cases.  We
      // need this algorithm to be as compact as possible and that adds extra
      // codespace.  This should be a pretty rare occurrence, so just disallow
      // it rather than trying to do something fancy.
      logDebug("Not enough space in memoryManagerState->lastFree\n");
      return NULL;
    }
    logDebug("next = 0x%llx\n", (unsigned long long int) (uintptr_t) next);

    // Update the links on the next pointer.
    next->prev = cur->prev;
    logDebug("next->prev = 0x%llx\n",
      (unsigned long long int) (uintptr_t) next->prev);
    if (next->prev != NULL) {
#ifdef NANO_OS_MEM_DEBUG
      if (((uintptr_t) next) < ((uintptr_t) next->prev)) {
        // This should be impossible.
        logDebug("ERROR!!! next (0x%llx) < next->prev (0x%llx)\n",
          (unsigned long long int) (uintptr_t) next,
          (unsigned long long int) (uintptr_t) next->prev);
        HAL->power->enterMode(HAL_POWER_MODE_OFF);
      }
#endif // NANO_OS_MEM_DEBUG
      next->prev->next = next;
    }

    if (next != cur->next) {
      logDebug("next (0x%llx) != cur->next (0x%llx)\n",
        (unsigned long long int) (uintptr_t) next,
        (unsigned long long int) (uintptr_t) cur->next);
      logDebug("Updating metadata for next\n");
#ifdef NANO_OS_MEM_DEBUG
      if ((cur->next != NULL)
        && (((uintptr_t) cur->next) < ((uintptr_t) next))
      ) {
        // This should be impossible.
        logDebug("ERROR!!! cur->next (0x%llx) < next (0x%llx)\n",
          (unsigned long long int) (uintptr_t) cur->next,
          (unsigned long long int) (uintptr_t) next);
        HAL->power->enterMode(HAL_POWER_MODE_OFF);
      }
#endif // NANO_OS_MEM_DEBUG
      next->next = cur->next;
      logDebug("next->next = 0x%llx\n",
        (unsigned long long int) (uintptr_t) next->next);

      if (next->next != NULL) {
        next->next->prev = next;
      }

      // Reduce the free space by the delta between how much we were requested
      // and how much used to be managed by this node.
      next->size = cur->size - size - sizeof(MemNode);
      logDebug("next->size = %lld\n", (long long int) next->size);
    } else {
      logDebug("next == cur->next\n");
      logDebug("*NOT* updating metadata for next\n");
      // Reduce bytesFree by the delta.
      memoryManagerState->bytesFree += sizeof(MemNode);
      memoryManagerState->bytesFree -= (cur->size - size);
    }

    cur->size = size;
    logDebug("New cur->size = %lld\n", (long long int) cur->size);

    // Update the first and last pointers.
    if (cur == memoryManagerState->firstFree) {
      logDebug("Updating memoryManagerState->firstFree to next\n");
      memoryManagerState->firstFree = next;
    }
    if (cur == memoryManagerState->lastFree) {
      logDebug("Updating memoryManagerState->lastFree to next\n");
      memoryManagerState->lastFree = next;
    }

    // Move cur to the allocated list.
    cur->next = memoryManagerState->allocated;
    logDebug("cur->next = 0x%llx\n",
      (unsigned long long int) (uintptr_t) cur->next);

    if (cur->next != NULL) {
      logDebug("Setting cur->next->prev to cur\n");
      cur->next->prev = cur;
    }

    cur->prev = NULL;

    logDebug("Updating memoryManagerState->allocated to cur\n");
    memoryManagerState->allocated = cur;

    // Set the owner for the memory.
    cur->owner = pid;

    // Reduce system memory.
    size_t bytesFreeBefore = memoryManagerState->bytesFree;
    memoryManagerState->bytesFree -= size + sizeof(MemNode);
    logDebug("Updating memoryManagerState->bytesFree from %lld to %lld\n",
      (long long int) bytesFreeBefore,
      (long long int) memoryManagerState->bytesFree);

    logDebug("Allocating %lld bytes at 0x%llx\n",
      (long long int) cur->size,
      (unsigned long long int) (uintptr_t) returnValue);
  } else {
    logDebug("Error: Could not find memory node with enough space\n");
  }

  if ((returnValue != NULL) && (ptr != NULL)) {
    // Because of the logic above, we're guaranteed that this means that the
    // address of returnValue is not the same as the address of ptr.  Copy
    // the data from the old memory to the new memory and free the old
    // memory.
    logDebug("Copying old memory to new memory\n");
    memcpy(returnValue, ptr, sizeOfMemory(ptr));
    localFree(memoryManagerState, ptr, pid);
  }
  
  return returnValue;
}

#ifdef __cplusplus
} // extern "C"
#endif

/******************* End Custom Memory Management Functions *******************/

int memoryManagerDumpMemoryAllocationsCommandHandler(
  MemoryManagerState *memoryManagerState, ProcessMessage *incoming
);
/// @fn int memoryManagerReallocCommandHandler(
///   MemoryManagerState *memoryManagerState, ProcessMessage *incoming)
///
/// @brief Command handler for a MEMORY_MANAGER_REALLOC command.  Extracts the
/// ReallocMessage from the message and passes the parameters to localRealloc.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   process.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerReallocCommandHandler(
  MemoryManagerState *memoryManagerState, ProcessMessage *incoming
) {
  int returnValue = 0;
  
  ReallocMessage *reallocMessage
    = (ReallocMessage*) processMessageData(incoming);
  if (reallocMessage == NULL) {
    ProcessDescriptor *processDescriptor = processMessageFrom(incoming);
    if (processDescriptor == NULL) {
      logError("Received MEMORY_MANAGER_REALLOC message from "
        "unknown process\n");
      returnValue = -EINVAL;
      goto exit;
    }

    logError("Process %d sent NULL ReallocMessage\n",
      processPid(processDescriptor));
    returnValue = -EINVAL;
    goto exit;
  }
  
  void *clientReturnValue
    = localRealloc(memoryManagerState,
      reallocMessage->ptr, reallocMessage->size,
      processPid(processMessageFrom(incoming)));
  if (clientReturnValue != NULL) {
    reallocMessage->size = sizeOfMemory(clientReturnValue);
  } else if ((reallocMessage->size > 0)
    && (processPid(processMessageFrom(incoming))
      != SCHEDULER_STATE->schedulerPid
    )
  ) {
    logError("Failed to allocate %lld bytes for process %lld\n",
      (long long int) reallocMessage->size,
      (long long int) processPid(processMessageFrom(incoming)));
    memoryManagerDumpMemoryAllocationsCommandHandler(memoryManagerState, NULL);
    do {
      break;
      ProcessMessage *filesystemCommand = getAvailableMessage();
      if (filesystemCommand == NULL) {
        logError("Could not get filesystemCommand message\n");
        break;
      }
      processMessageInit(filesystemCommand,
        FILESYSTEM_DUMP_OPEN_FILES, NULL, 0, true);
      if (sendProcessMessageToProcess(
        &SCHEDULER_STATE->allProcesses[SCHEDULER_STATE->rootFsPid - 1],
        filesystemCommand) != processSuccess
      ) {
        logError("Could not send FILESYSTEM_DUMP_OPEN_FILES "
          "message to root FS process %d\n", SCHEDULER_STATE->rootFsPid);
      }
      processMessageRelease(filesystemCommand);
    } while (0);
    reallocMessage->size = 0;
  } else {
    reallocMessage->size = 0;
  }
  reallocMessage->ptr = clientReturnValue;
  
  // The client is waiting on us.  Mark the incoming message done now.  Do
  // *NOT* release it since the client is still using it.
  if (processMessageSetDone(incoming) != processSuccess) {
    returnValue = -1;
  }
  
exit:
  return returnValue;
}

/// @fn int memoryManagerFreeCommandHandler(
///   MemoryManagerState *memoryManagerState, ProcessMessage *incoming)
///
/// @brief Command handler for a MEMORY_MANAGER_FREE command.  Extracts the
/// pointer to free from the message and then calls localFree.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   process.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerFreeCommandHandler(
  MemoryManagerState *memoryManagerState, ProcessMessage *incoming
) {
  int returnValue = 0;

  MemoryManagerFreeArgs *memoryManagerFreeArgs
    = (MemoryManagerFreeArgs*) processMessageData(incoming);
  localFree(memoryManagerState, memoryManagerFreeArgs->ptr,
    processPid(processMessageFrom(incoming)));
  if (processMessageSetDone(incoming) != processSuccess) {
    logError(
      "Could not set message done from memoryManagerFreeCommandHandler.\n");
    returnValue = -1;
  }

  return returnValue;
}

/// @fn int memoryManagerGetFreeMemoryCommandHandler(
///   MemoryManagerState *memoryManagerState, ProcessMessage *incoming)
///
/// @brief Command handler for MEMORY_MANAGER_GET_FREE_MEMORY.  Gets the amount
/// of free dynamic memory left in the system.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   process.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerGetFreeMemoryCommandHandler(
  MemoryManagerState *memoryManagerState, ProcessMessage *incoming
) {
  int returnValue = 0;
  MemoryManagerGetFreeMemoryArgs *memoryManagerGetFreeMemoryArgs
    = (MemoryManagerGetFreeMemoryArgs*) processMessageData(incoming);
  memoryManagerGetFreeMemoryArgs->bytesFree = memoryManagerState->bytesFree;
  
  // The client is waiting on us.  Mark the incoming message done now.  Do *NOT*
  // release it since the client is still using it.
  if (processMessageSetDone(incoming) != processSuccess) {
    returnValue = -1;
  }
  
  return returnValue;
}

/// @fn int memoryManagerFreeProcessMemoryCommandHandler(
///   MemoryManagerState *memoryManagerState, ProcessMessage *incoming)
///
/// @brief Command handler for a MEMORY_MANAGER_FREE_PROCESS_MEMORY command.
/// Extracts the process ID from the message and then calls
/// localFreeProcessMemory.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   process.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerFreeProcessMemoryCommandHandler(
  MemoryManagerState *memoryManagerState, ProcessMessage *incoming
) {
  int returnValue = 0;
  MemoryManagerFreeProcessMemoryArgs *memoryManagerFreeProcessMemoryArgs
    = (MemoryManagerFreeProcessMemoryArgs*) processMessageData(incoming);
  if (processPid(processMessageFrom(incoming))
    == SCHEDULER_STATE->schedulerPid
  ) {
    localFreeProcessMemory(memoryManagerState,
      memoryManagerFreeProcessMemoryArgs->pid,
      processPid(processMessageFrom(incoming)));
    memoryManagerFreeProcessMemoryArgs->returnValue = 0;
  } else {
    logError("Only the scheduler may free another process's memory.\n");
    memoryManagerFreeProcessMemoryArgs->returnValue = 1;
    returnValue = -1;
  }

  if (processMessageWaiting(incoming) == true) {
    // The client is waiting on us.  Mark the message as done.
    if (processMessageSetDone(incoming) != processSuccess) {
      logError("Could not mark message done in "
        "memoryManagerFreeProcessMemoryCommandHandler.\n");
      returnValue = -1;
    }
  } else {
    // the client is *NOT* waiting on us.  Release the message.
    processMessageRelease(incoming);
  }
  
  return returnValue;
}

/// @fn int memoryManagerAssignMemoryCommandHandler(
///   MemoryManagerState *memoryManagerState, ProcessMessage *incoming)
///
/// @brief Command handler for the MEMORY_MANAGER_ASSIGN_MEMORY command. Makes
/// sure that the memory falls in the range of dynamic memory and, if so,
/// assigns it to the specified process ID.  If the provided pointer is not in
/// the range of dynamic memory, no action is taken.
///
/// @note This function can only be called from the scheduler.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   process.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerAssignMemoryCommandHandler(
  MemoryManagerState *memoryManagerState, ProcessMessage *incoming
) {
  int returnValue = 0;
  
  if (processPid(processMessageFrom(incoming))
    == SCHEDULER_STATE->schedulerPid
  ) {
    AssignMemoryArgs *assignMemoryArgs
      = (AssignMemoryArgs*) processMessageData(incoming);
    if (isDynamicPointer(assignMemoryArgs->ptr)) {
      // Make sure the pointer being assigned is allocated.
      MemNode *cur = memoryManagerState->allocated;
      for (; cur != NULL; cur = cur->next) {
        if (&cur[1] == assignMemoryArgs->ptr) {
          break;
        }
      }
      if (cur != NULL) {
        cur->owner = assignMemoryArgs->pid;
      } else {
        logError("Attempt to assign unallocated memory 0x%llx\n",
          (unsigned long long int) (uintptr_t) assignMemoryArgs->ptr);
        returnValue = -1;
        memoryManagerDumpMemoryAllocationsCommandHandler(
          memoryManagerState, NULL);
      }
    } else {
      logWarn("Attempt to assign non-dynamic memory 0x%llx\n",
        (unsigned long long int) (uintptr_t) assignMemoryArgs->ptr);
      returnValue = -1;
    }
  } else {
    logError("Only the scheduler may assign memory to another process.\n");
    returnValue = -1;
  }
  
  processMessageData(incoming) = (void*) ((intptr_t) returnValue);
  processMessageSetDone(incoming);
  
  return returnValue;
}

/// @fn int memoryManagerDumpMemoryAllocationsCommandHandler(
///   MemoryManagerState *memoryManagerState, ProcessMessage *incoming)
///
/// @brief Command handler for MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS.  Walk
/// the memory allocation list and display information about all of the
/// allocations and their owning processes.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   process.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerDumpMemoryAllocationsCommandHandler(
  MemoryManagerState *memoryManagerState, ProcessMessage *incoming
) {
  int returnValue = 0;

  logInfo("Outstanding allocations:\n");
  MemNode *prev = NULL;
  for (MemNode *cur = memoryManagerState->allocated;
    cur != NULL;
    cur = cur->next
  ) {
    logInfo("  0x%llx: %lld bytes owned by %lld\n",
      (unsigned long long int) (uintptr_t) &cur[1],
      (long long int) cur->size, (long long int) cur->owner);
    if (cur->prev != prev) {
      logInfo("  - cur->prev = 0x%llx\n",
        (unsigned long long int) (uintptr_t) &cur->prev[1]);
    }
    prev = cur;
  }

  logInfo("Available memory blocks:\n");
  prev = NULL;
  for (MemNode *cur = memoryManagerState->firstFree;
    cur != NULL;
    cur = cur->next
  ) {
    logInfo("  0x%llx: %lld bytes available\n",
      (unsigned long long int) (uintptr_t) &cur[1], (long long int) cur->size);
    if (cur->prev != prev) {
      logInfo("  - cur->prev = 0x%llx\n",
        (unsigned long long int) (uintptr_t) &cur->prev[1]);
    }
    prev = cur;
  }
  
  processMessageSetDone(incoming);
  
  return returnValue;
}

/// @typedef MemoryManagerCommandHandler
///
/// @brief Signature of command handler for a memory manager command.
typedef int (*MemoryManagerCommandHandler)(
  MemoryManagerState *memoryManagerState, ProcessMessage *incoming);

/// @var memoryManagerCommandHandlers
///
/// @brief Array of function pointers for handlers for commands that are
/// understood by this library.
KEEP_IN_FLASH
const MemoryManagerCommandHandler memoryManagerCommandHandlers[] = {
  memoryManagerReallocCommandHandler,       // MEMORY_MANAGER_REALLOC
  memoryManagerFreeCommandHandler,          // MEMORY_MANAGER_FREE
  memoryManagerGetFreeMemoryCommandHandler, // MEMORY_MANAGER_GET_FREE_MEMORY
  // MEMORY_MANAGER_FREE_PROCESS_MEMORY:
  memoryManagerFreeProcessMemoryCommandHandler,
  memoryManagerAssignMemoryCommandHandler,  // MEMORY_MANAGER_ASSIGN_MEMORY
  // MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS:
  memoryManagerDumpMemoryAllocationsCommandHandler,
};

/// @fn void handleMemoryManagerMessages(
///   MemoryManagerState *memoryManagerState)
///
/// @brief Handle memory manager messages from the process's queue until there
/// are no more waiting.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
///
/// @return This function returns no value.
void handleMemoryManagerMessages(MemoryManagerState *memoryManagerState) {
  ProcessMessage *processMessage = processMessageQueueWait(NULL);
  while (processMessage != NULL) {
    if ((processMessageType(processMessage) & 0xffffffffffffff00)
      != MEMORY_MANAGER_COMMAND_SIGNATURE
    ) {
      logError("received unknown signature 0x%llx from process %d\n",
        (unsigned long long int)
          (processMessageType(processMessage) & 0xffffffffffffff00),
        processPid(processMessageFrom(processMessage)));
      // Don't attempt to process this message further.
      processMessage = processMessageQueuePop();
      continue;
    }

    MemoryManagerCommand messageType
      = (MemoryManagerCommand) (processMessageType(processMessage) & 0xff);
    if (messageType >= NUM_MEMORY_MANAGER_COMMANDS) {
      logError("%s: Unrecognized message type %lld\n",
        __func__, (long long int) messageType);

      processMessage = processMessageQueuePop();
      continue;
    }
    
    memoryManagerCommandHandlers[messageType](
      memoryManagerState, processMessage);
    
    processMessage = processMessageQueuePop();
  }
  
  return;
}

/// @fn void initializeGlobals(MemoryManagerState *memoryManagerState,
///   jmp_buf returnBuffer, char *stack)
///
/// @brief Initialize the global variables that will be needed by the memory
/// management functions and then resume execution in the main process function.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param returnBuffer The jmp_buf that will be used to resume execution in the
///   main process function.
/// @param stack A pointer to the stack in allocateMemoryManagerStack.  Passed
///   just so that the compiler doesn't optimize it out.
///
/// @return This function returns no value and, indeed, never actually returns.
void initializeGlobals(MemoryManagerState *memoryManagerState,
  jmp_buf returnBuffer, char *stack
) {
  // We can't leave the memory manager's stack end where it is because we will
  // overwrite it the contents of dynamically allocated memory.  Set it to our
  // boundary here.
  uint64_t stackEnd = THREAD_STACK_END_VALUE;
  if (threadSetStackEnd(getRunningProcess()->mainThread, &stackEnd)
    != processSuccess
  ) {
    logError("%s: ERROR: Could not set stack end for memory manager\n",
      __func__);
  }
  
  // The buffer needs to be machine-width aligned, so we need to use a pointer
  // as the placeholder value.  This ensures that the compiler puts it at a
  // valid (aligned) address.
  char *mallocBufferEnd = NULL;
  
  // Set up the memory manager's state.
  void *bottomOfHeapVal = NULL;
  HAL->memory->bottomOfHeap(MEMORY_MANAGER_DEBUG, &bottomOfHeapVal);
  memoryManagerState->start = (uintptr_t) bottomOfHeapVal;
  memoryManagerState->end = (uintptr_t) &mallocBufferEnd;
  memoryManagerState->bytesFree
    = ((size_t) memoryManagerState->end)
    - ((size_t) memoryManagerState->start)
    + 1;
  memoryManagerState->bytesFree &= ~(sizeof(size_t) - 1);
  memoryManagerState->bytesFree -= sizeof(MemNode);
  memoryManagerState->allocated = NULL;
  memoryManagerState->firstFree = (MemNode*) memoryManagerState->start;
  memoryManagerState->lastFree = memoryManagerState->firstFree;
  
  // Setup the first node in the free list.
  memoryManagerState->firstFree->next = NULL;
  memoryManagerState->firstFree->prev = NULL;
  memoryManagerState->firstFree->size = memoryManagerState->bytesFree;
  memoryManagerState->firstFree->owner = PROCESS_ID_NOT_SET;
  
  logDebug("Leaving initializeGlobals in MemoryManager.c\n");
  longjmp(returnBuffer, (int) ((intptr_t) stack));
}

/// @fn void allocateMemoryManagerStack(MemoryManagerState *memoryManagerState,
///   jmp_buf returnBuffer, int stackSize, char *topOfStack)
///
/// @brief Allocate space on the stack for the main process and then call
/// initializeGlobals to finish the initialization process.
///
/// @details
/// This function is way more involved than it should be.  It really should just
/// declare a single buffer and then call initializeGlobals.  The problem was
/// that the compiler kept optimizing the stack out when it was that simple.
/// I guess it could detect that it was never used.  That won't work for our
/// purposes, so I had to make it more complicated.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param returnBuffer The jmp_buf that will be used to resume execution in the
///   main process function.
/// @param stackSize The desired stack size to allocate.
/// @param topOfStack A pointer to the first stack pointer that gets created.
///
/// @return This function returns no value and, indeed, never actually returns.
void allocateMemoryManagerStack(MemoryManagerState *memoryManagerState,
  jmp_buf returnBuffer, int stackSize, char *topOfStack
) {
  char stack[MEMORY_MANAGER_PROCESS_STACK_CHUNK_SIZE];
  memset(stack, 0, MEMORY_MANAGER_PROCESS_STACK_CHUNK_SIZE);
  
  if (topOfStack == NULL) {
    topOfStack = stack;
  }
  
  if (stackSize > MEMORY_MANAGER_PROCESS_STACK_CHUNK_SIZE) {
    allocateMemoryManagerStack(
      memoryManagerState,
      returnBuffer,
      stackSize - MEMORY_MANAGER_PROCESS_STACK_CHUNK_SIZE,
      topOfStack);
  }
  
  initializeGlobals(memoryManagerState, returnBuffer, topOfStack);
}

//// /// @fn void printMemoryManagerState(MemoryManagerState *memoryManagerState)
//// ///
//// /// @brief Debugging function to print all the values of the MemoryManagerState.
//// ///
//// /// @param memoryManagerState A pointer to the MemoryManagerState maintained by
//// ///   the process.
//// ///
//// /// @return This function returns no value.
//// void printMemoryManagerState(MemoryManagerState *memoryManagerState) {
////   extern int __heap_start;
////   printString("memoryManagerState.mallocBuffer = ");
////   printInt((uintptr_t) memoryManagerState->mallocBuffer);
////   printString("\n");
////   printString("memoryManagerState.mallocNext = ");
////   printInt((uintptr_t) memoryManagerState->mallocNext);
////   printString("\n");
////   printString("memoryManagerState.mallocStart = ");
////   printInt(memoryManagerState->mallocStart);
////   printString("\n");
////   printString("memoryManagerState.mallocEnd = ");
////   printInt(memoryManagerState->mallocEnd);
////   printString("\n");
////   printString("&__heap_start = ");
////   printInt((uintptr_t) &__heap_start);
////   printString("\n");
////   
////   return;
//// }

/// @fn void* runMemoryManager(void *args)
///
/// @brief Main process for the memory manager that will configure all the
/// variables and be responsible for handling the messages.
///
/// @param args Any arguments passed by the scheduler.  Ignored by this
///   function.
///
/// @return This function never exits its main loop, so never returns, however
/// it would return NULL if it returned anything.
void* runMemoryManager(void *args) {
  (void) args;
  printConsoleString("\n");
  
  MemoryManagerState memoryManagerState;
  jmp_buf returnBuffer;
  if (setjmp(returnBuffer) == 0) {
    size_t mmStackSize = 0;
    HAL->memory->memoryManagerStackSize(MEMORY_MANAGER_DEBUG, &mmStackSize);
    allocateMemoryManagerStack(&memoryManagerState, returnBuffer,
      mmStackSize, NULL);
  }
  logDebug("Returned from allocateMemoryManagerStack.\n");

  //// printMemoryManagerState(&memoryManagerState);
  logDebug("memoryManagerState.firstFree->size = %lld\n",
    (long long int) memoryManagerState.firstFree->size);
  printConsoleString("Using ");
  printConsoleULong(memoryManagerState.firstFree->size);
  printConsoleString(" bytes of dynamic memory.\n");
  releaseConsole();
  
  while (1) {
    processYield();
    handleMemoryManagerMessages(&memoryManagerState);
  }
  
  return NULL;
}

/// @fn size_t getFreeMemory(void)
///
/// @brief Send a MEMORY_MANAGER_GET_FREE_MEMORY command to the memory manager
/// process and wait for a reply.
///
/// @return Returns the size, in bytes, of available dynamic memory on success,
/// 0 on failure.
size_t getFreeMemory(void) {
  size_t returnValue = 0;
  
  MemoryManagerGetFreeMemoryArgs memoryManagerGetFreeMemoryArgs = {
    .bytesFree = 0,
  };

  ProcessMessage *sent
    = initSendProcessMessageToPid(SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_GET_FREE_MEMORY,
    &memoryManagerGetFreeMemoryArgs,
    sizeof(memoryManagerGetFreeMemoryArgs), true);
  if (sent == NULL) {
    logError("initSendProcessMessageToPid returned NULL\n");
    return returnValue; // 0
  }

  processMessageWaitForDone(sent, NULL);
  processMessageRelease(sent);
  return memoryManagerGetFreeMemoryArgs.bytesFree;
}

/// @fn void* memoryManagerSendReallocMessage(void *ptr, size_t size)
///
/// @brief Send a MEMORY_MANAGER_REALLOC command to the memory manager process
/// and wait for a reply.
///
/// @param ptr The pointer to send to the process.
/// @param size The size to send to the process.
///
/// @return Returns the data pointer returned in the reply.
void* memoryManagerSendReallocMessage(void *ptr, size_t size) {
  void *returnValue = NULL;
  
  ReallocMessage reallocMessage;
  reallocMessage.ptr = ptr;
  reallocMessage.size = size;
  
  ProcessMessage *sent
    = initSendProcessMessageToPid(SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_REALLOC,
    &reallocMessage, sizeof(reallocMessage), true);
  
  if (sent == NULL) {
    // Nothing more we can do.
    return returnValue; // NULL
  }
  
  processMessageWaitForDone(sent, NULL);
  
  // The handler set the pointer back in the structure we sent it, so grab it
  // out of the structure we already have.
  returnValue = reallocMessage.ptr;
  processMessageRelease(sent);
  
  return returnValue;
}

/// @fn void memoryManagerFree(void *ptr)
///
/// @brief Free previously-allocated memory.  The provided pointer may have
/// been allocated either by the system memory functions or from our static
/// memory pool.
///
/// @param ptr A pointer to the block of memory to free.
///
/// @return This function always succeeds and returns no value.
void memoryManagerFree(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  MemoryManagerFreeArgs memoryManagerFreeArgs = {
    .ptr = ptr,
  };
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_FREE,
    &memoryManagerFreeArgs, sizeof(memoryManagerFreeArgs), false);
  if (processMessage == NULL) {
    logError("Could not send MEMORY_MANAGER_FREE message to "
      "memory manager; memory leak\n");
    return;
  }

  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

  return;
}

/// @fn void* memoryManagerRealloc(void *ptr, size_t size)
///
/// @brief Reallocate a provided pointer to a new size.
///
/// @param ptr A pointer to the original block of dynamic memory.  If this value
///   is NULL, new memory will be allocated.
/// @param size The new size desired for the memory block at ptr.  If this value
///   is 0, the provided pointer will be freed.
///
/// @return Returns a pointer to size-adjusted memory on success, NULL on
/// failure or free.
void* memoryManagerRealloc(void *ptr, size_t size) {
  return memoryManagerSendReallocMessage(ptr, size);
}

/// @fn void* memoryManagerMalloc(size_t size)
///
/// @brief Allocate but do not clear memory.
///
/// @param size The size of the block of memory to allocate in bytes.
///
/// @return Returns a pointer to newly-allocated memory of the specified size
/// on success, NULL on failure.
void* memoryManagerMalloc(size_t size) {
  return memoryManagerSendReallocMessage(NULL, size);
}

/// @fn void* memoryManagerCalloc(size_t nmemb, size_t size)
///
/// @brief Allocate memory and clear all the bytes to 0.
///
/// @param nmemb The number of elements to allocate in the memory block.
/// @param size The size of each element to allocate in the memory block.
///
/// @return Returns a pointer to zeroed newly-allocated memory of the specified
/// size on success, NULL on failure.
void* memoryManagerCalloc(size_t nmemb, size_t size) {
  size_t totalSize = nmemb * size;
  void *returnValue = memoryManagerSendReallocMessage(NULL, totalSize);
  
  if (returnValue != NULL) {
    memset(returnValue, 0, totalSize);
  }
  return returnValue;
}

/// @fn int dumpMemoryAllocations(void)
///
/// @brief Make the memory manager dump metadata about all its outstanding
/// allocations.
///
/// @return Returns 0 on success, -errno on failure.
int dumpMemoryAllocations(void) {
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS,
    NULL, 0, /* waiting= */ true);
  if (processMessage == NULL) { 
    logError("Could not send message "
      "MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS to memory manager\n");
    return -EBUSY;
  }
  
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);
  
  return 0;
}

