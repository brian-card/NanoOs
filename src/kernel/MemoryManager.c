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
#include "MemoryManager.h"
#include "NanoOs.h"
#include "NanoOsOverlayFunctions.h"
#include "Tasks.h"
#include "../user/NanoOsStdio.h"

/****************** Begin Custom Memory Management Functions ******************/

/// @def memNode
///
/// @brief Get a pointer to the MemNode for a memory address.
#define memNode(ptr) \
  (((ptr) != NULL) ? &((MemNode*) (ptr))[-1] : NULL)

/// @def sizeOfMemory
///
/// @brief Retrieve the size of a block of dynamic memory.  This information is
/// stored sizeof(MemNode) bytes before the pointer.
#define sizeOfMemory(ptr) \
  (((ptr) != NULL) ? ((uint32_t) memNode(ptr)->size) : 0)

/// @def isDynamicPointer
///
/// @brief Determine whether or not a pointer was allocated from the allocators
/// in this library.
#define isDynamicPointer(ptr) \
  ((((uintptr_t) (ptr)) >= memoryManagerState->start) \
    && (((uintptr_t) (ptr)) <= memoryManagerState->end))

#ifndef NANO_OS_MEM_DEBUG

// If NANO_OS_MEM_DEBUG isn't defined then we don't want ANY debugging going on
// here.  Oveerride the debug macros if they were defined by NANO_OS_DEBUG.
#undef startDebugMessage
#define startDebugMessage(message) {}
#undef printDebugString
#define printDebugString(message) {}
#undef printDebugInt
#define printDebugInt(value) {}
#undef printDebugHex
#define printDebugHex(value) {}

#endif // NANO_OS_MEM_DEBUG

#ifdef __cplusplus
extern "C"
{
#endif

/// @fn void localFree(MemoryManagerState *memoryManagerState, void *ptr)
///
/// @brief Free a previously-allocated block of memory.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param ptr A pointer to the block of memory to free.
///
/// @return This function always succeeds and returns no value.
void localFree(MemoryManagerState *memoryManagerState, void *ptr) {
  startDebugMessage("In localFree\n");
  if (isDynamicPointer(ptr)) {
    MemNode *memNode = memNode(ptr);
    
    // This is memory that was previously allocated from one of our allocators.
    startDebugMessage("Freeing ");
    printDebugInt(memNode->size);
    printDebugString(" bytes at 0x");
    printDebugHex(ptr);
    printDebugString(" from process ");
    printDebugInt(memNode->owner);
    printDebugString("\n");
    startDebugMessage("memNode = 0x");
    printDebugHex(memNode);
    printDebugString("\n");
    
    // Splice out memNode from the allocated list.
    if (memNode->prev != NULL) {
#ifdef NANO_OS_MEM_DEBUG
      MemNode *cur;
      for (cur = memoryManagerState->allocated; cur != NULL; cur = cur->next) {
        if (cur == memNode->prev) {
          break;
        }
      }
      if (cur == NULL) {
        startDebugMessage("ERROR!!!  memNode->prev is not allocated!!\n");
        exit(1);
      }
#endif // NANO_OS_MEM_DEBUG
      startDebugMessage("Updating memNode->prev->next\n");
      memNode->prev->next = memNode->next;
    }
    if (memNode->next != NULL) {
      startDebugMessage("Updating memNode->next->prev\n");
      memNode->next->prev = memNode->prev;
    }
    if (memoryManagerState->allocated == memNode) {
      startDebugMessage("Updating memoryManagerState->allocated\n");
      memoryManagerState->allocated = memNode->next;
    }
    
    // Put the memNode in the right place in the free list.
    startDebugMessage("Searching free list in reverse order\n");
    MemNode *cur = memoryManagerState->lastFree;
#ifdef NANO_OS_MEM_DEBUG
    if (((uintptr_t) cur) < ((uintptr_t) memNode)) {
      // This should be impossible.
      startDebugMessage("ERROR!!! cur (0x");
      printDebugHex(cur);
      printDebugString(") < memNode (0x");
      printDebugHex(memNode);
      printDebugString(")\n");
      exit(1);
    }
#endif // NANO_OS_MEM_DEBUG
    while (((uintptr_t) cur->prev) > ((uintptr_t) memNode)) {
      cur = cur->prev;
    }
    startDebugMessage("cur = ");
    printDebugHex(cur);
    printDebugString("\n");
    
#ifdef NANO_OS_MEM_DEBUG
    if (((uintptr_t) cur) < ((uintptr_t) memNode)) {
      // This should be impossible.
      startDebugMessage("ERROR!!! cur (0x");
      printDebugHex(cur);
      printDebugString(") < memNode (0x");
      printDebugHex(memNode);
      printDebugString(")\n");
      exit(1);
    }
#endif // NANO_OS_MEM_DEBUG
    memNode->next = cur;
    startDebugMessage("memNode->next = 0x");
    printDebugHex(memNode->next);
    printDebugString("\n");
    
    memNode->prev = cur->prev;
    startDebugMessage("memNode->prev = 0x");
    printDebugHex(memNode->prev);
    printDebugString("\n");
    
    startDebugMessage("Increasing memoryManagerState->bytesFree from ");
    printDebugInt(memoryManagerState->bytesFree);
    memoryManagerState->bytesFree += memNode->size;
    printDebugString(" to ");
    printDebugInt(memoryManagerState->bytesFree);
    printDebugString("\n");
    
    MemNode *next
      = (MemNode*) (((uint8_t*) memNode) + memNode->size + sizeof(MemNode));
    
    if (next != cur) {
      startDebugMessage("next != cur\n");
      startDebugMessage("Setting cur->prev to 0x");
      printDebugHex(memNode);
      printDebugString("\n");
      
      cur->prev = memNode;
    } else {
      // Do memory compaction between memNode and cur.
      startDebugMessage("next == cur\n");
      startDebugMessage("Doing memory compaction\n");
      
      memNode->size += cur->size + sizeof(MemNode);
#ifdef NANO_OS_MEM_DEBUG
      if ((cur->next != NULL)
        && (((uintptr_t) cur->next) < ((uintptr_t) memNode))
      ) {
        // This should be impossible.
        startDebugMessage("ERROR!!! cur->next (0x");
        printDebugHex(cur->next);
        printDebugString(") < memNode (0x");
        printDebugHex(memNode);
        printDebugString(")\n");
        exit(1);
      }
#endif // NANO_OS_MEM_DEBUG
      memNode->next = cur->next;
      if (memNode->next != NULL) {
        memNode->next->prev = memNode;
      }
      if (memoryManagerState->lastFree == cur) {
        startDebugMessage("Setting memoryManagerState->lastFree to memNode\n");
        memoryManagerState->lastFree = memNode;
      }
      
      startDebugMessage("Increasing memoryManagerState->bytesFree from ");
      printDebugInt(memoryManagerState->bytesFree);
      memoryManagerState->bytesFree += sizeof(MemNode);
      printDebugString(" to ");
      printDebugInt(memoryManagerState->bytesFree);
      printDebugString("\n");
    }
    
    if (memNode->prev != NULL) {
      startDebugMessage("memNode->prev != NULL\n");
      
      MemNode *prev = memNode->prev;
      startDebugMessage("prev = 0x");
      printDebugHex(prev);
      printDebugString("\n");
      
      next = (MemNode*) (((uint8_t*) prev) + prev->size + sizeof(MemNode));
      startDebugMessage("next = 0x");
      printDebugHex(next);
      printDebugString("\n");
      
      if (next != memNode) {
        startDebugMessage("next != memNode\n");
        startDebugMessage("Setting prev->next to memNode\n");
        
#ifdef NANO_OS_MEM_DEBUG
        if (((uintptr_t) memNode) < ((uintptr_t) prev)) {
          // This should be impossible.
          startDebugMessage("ERROR!!! memNode (0x");
          printDebugHex(memNode);
          printDebugString(") < prev (0x");
          printDebugHex(prev);
          printDebugString(")\n");
          exit(1);
        }
#endif // NANO_OS_MEM_DEBUG
        prev->next = memNode;
      } else {
        // Do memory compaction between prev and memNode.
        startDebugMessage("next == memNode\n");
        startDebugMessage("Doing memory compaction\n");
        
        prev->size += memNode->size + sizeof(MemNode);
        startDebugMessage("prev->size = ");
        printDebugInt(prev->size);
        printDebugString("\n");
        
#ifdef NANO_OS_MEM_DEBUG
        if ((memNode->next != NULL)
          && (((uintptr_t) memNode->next) < ((uintptr_t) prev))
        ) {
          // This should be impossible.
          startDebugMessage("ERROR!!! memNode->next (0x");
          printDebugHex(memNode->next);
          printDebugString(") < prev (0x");
          printDebugHex(prev);
          printDebugString(")\n");
          exit(1);
        }
#endif // NANO_OS_MEM_DEBUG
        prev->next = memNode->next;
        if (prev->next != NULL) {
          prev->next->prev = prev;
        }
        startDebugMessage("prev->next = 0x");
        printDebugHex(prev->next);
        printDebugString("\n");
        
        if (memoryManagerState->lastFree == memNode) {
          startDebugMessage("Setting memoryManagerState->lastFree to prev\n");
          memoryManagerState->lastFree = prev;
        }
        
        startDebugMessage("Increasing memoryManagerState->bytesFree from ");
        printDebugInt(memoryManagerState->bytesFree);
        memoryManagerState->bytesFree += sizeof(MemNode);
        printDebugString(" to ");
        printDebugInt(memoryManagerState->bytesFree);
        printDebugString("\n");
      }
    } else {
      startDebugMessage("memNode->prev == NULL\n");
      startDebugMessage("Setting memoryManagerState->firstFree to memNode\n");
      memoryManagerState->firstFree = memNode;
    }
  } else {
    // This is not something we can free.  Ignore it.
    startDebugMessage("Error: Request to free non-dynamic memory 0x");
    printDebugHex(ptr);
    printDebugString("\n");
  }
  
  return;
}

/// @fn void localFreeTaskMemory(
///   MemoryManagerState *memoryManagerState, TaskId taskId)
///
/// @brief Free *ALL* the memory owned by a task given its task ID.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param taskIUd The ID of the task to free the memory of.
///
/// @return This function always succeeds and returns no value.
void localFreeTaskMemory(
  MemoryManagerState *memoryManagerState, TaskId taskId
) {
  for (MemNode *cur = memoryManagerState->allocated; cur != NULL; ) {
    MemNode *next = cur->next;
    if (cur->owner == taskId) {
      localFree(memoryManagerState, &cur[1]);
    }
    cur = next;
  }
  
  return;
}

/// @fn void* localRealloc(MemoryManagerState *memoryManagerState,
///   void *ptr, size_t size, TaskId taskId)
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
/// @param taskId The ID of the task making the request.
///
/// @return Returns a pointer to size-adjusted memory on success, NULL on
/// failure or on free.
void* localRealloc(MemoryManagerState *memoryManagerState,
  void *ptr, size_t size, TaskId taskId
) {
  startDebugMessage("In localRealloc\n");
  // We need to fix the size to be aligned with our memory model.
  size += sizeof(size_t) - 1;
  size &= ~(sizeof(size_t) - 1);
  
  if (size == 0) {
    // In this case, there's no point in going through any path below.  Just
    // free it, return NULL, and be done with it.
    localFree(memoryManagerState, ptr);
    return NULL;
  } else if ((size + sizeof(MemNode)) > memoryManagerState->bytesFree) {
    // Sanity test failed.  We're being asked for more memory than is available
    // in the system.  Fail immediately.
    startDebugMessage("Error: Request to allocate ");
    printDebugInt(size);
    printDebugString(" bytes, which is more than available memory of ");
    printDebugInt(memoryManagerState->bytesFree);
    printDebugString(" bytes\n");
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
      startDebugMessage("Reallocating less memory than availabe\n");
      startDebugMessage("Returing ptr\n");
      return ptr;
    } else if (next == memoryManagerState->lastFree) {
      // We're being asked to extend the last block that was allocated.  Just
      // extend it if we have enough space.
      if ((memNode->size + next->size) >= size) {
        startDebugMessage("Extending last memory block\n");
        MemNode lastFree = *memoryManagerState->lastFree;
        next = (MemNode*) (charPointer + size);
        next->prev = lastFree.prev;
        next->next = NULL;
        if (next->prev != NULL) {
#ifdef NANO_OS_MEM_DEBUG
          if (((uintptr_t) next) < ((uintptr_t) next->prev)) {
            // This should be impossible.
            startDebugMessage("ERROR!!! next (0x");
            printDebugHex(next);
            printDebugString(") < next->prev (0x");
            printDebugHex(next->prev);
            printDebugString(")\n");
            exit(1);
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
    startDebugMessage("ERROR: Asked to reallocate a non-dynamic pointer\n");
    return NULL;
  }
  
  // We're allocating new memory.  Search from the beginning.
  startDebugMessage("Allocating ");
  printDebugInt(size);
  printDebugString(" bytes, searching from beginning\n");
  MemNode *cur = NULL;
  for (cur = memoryManagerState->firstFree; cur != NULL; cur = cur->next) {
#ifdef NANO_OS_MEM_DEBUG
    if (((uintptr_t) cur->prev) >= ((uintptr_t) cur)) {
      startDebugMessage("ERROR!!! cur->prev (0x");
      printDebugHex(cur->prev);
      printDebugString(") >= cur (0x");
      printDebugHex(cur);
      printDebugString(")\n");
      exit(1);
    }
    
    if ((cur->next == NULL) && (cur == memoryManagerState->lastFree)) {
      // Do nothing.  This is just a guard against the next case.
    } else if (((uintptr_t) cur->next) <= ((uintptr_t) cur)) {
      startDebugMessage("ERROR!!! cur->next (0x");
      printDebugHex(cur->next);
      printDebugString(") <= cur (0x");
      printDebugHex(cur);
      printDebugString(")\n");
      exit(1);
    }
#endif // NANO_OS_MEM_DEBUG
    
    if (cur->size >= size) {
      break;
    }
    
    startDebugMessage("0x");
    printDebugHex(cur);
    printDebugString(" only has ");
    printDebugInt(cur->size);
    printDebugString(" bytes available, need ");
    printDebugInt(size);
    printDebugString("\n");
#ifdef NANO_OS_MEM_DEBUG
    //// msleep(100);
#endif // NANO_OS_MEM_DEBUG
  }
  startDebugMessage("Memory search complete\n");
  
  if (cur != NULL) {
    // Memory allocation has succeeded.
    startDebugMessage("Found available memory node 0x");
    printDebugHex(cur);
    printDebugString("\n");
    startDebugMessage("cur->size = ");
    printDebugInt(cur->size);
    printDebugString("\n");
    
    returnValue = &cur[1];
    startDebugMessage("returnValue = 0x");
    printDebugHex(returnValue);
    printDebugString("\n");
    
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
      startDebugMessage("Not enough space in memoryManagerState->lastFree\n");
      return NULL;
    }
    startDebugMessage("next = 0x");
    printDebugHex(next);
    printDebugString("\n");
    
    // Update the links on the next pointer.
    next->prev = cur->prev;
    startDebugMessage("next->prev = 0x");
    printDebugHex(next->prev);
    printDebugString("\n");
    if (next->prev != NULL) {
#ifdef NANO_OS_MEM_DEBUG
      if (((uintptr_t) next) < ((uintptr_t) next->prev)) {
        // This should be impossible.
        startDebugMessage("ERROR!!! next (0x");
        printDebugHex(next);
        printDebugString(") < next->prev (0x");
        printDebugHex(next->prev);
        printDebugString(")\n");
        exit(1);
      }
#endif // NANO_OS_MEM_DEBUG
      next->prev->next = next;
    }
    
    if (next != cur->next) {
      startDebugMessage("next (0x");
      printDebugHex(next);
      printDebugString(") != cur->next (0x");
      printDebugHex(cur->next);
      printDebugString(")\n");
      startDebugMessage("Updating metadata for next\n");
#ifdef NANO_OS_MEM_DEBUG
      if ((cur->next != NULL)
        && (((uintptr_t) cur->next) < ((uintptr_t) next))
      ) {
        // This should be impossible.
        startDebugMessage("ERROR!!! cur->next (0x");
        printDebugHex(cur->next);
        printDebugString(") < next (0x");
        printDebugHex(next);
        printDebugString(")\n");
        exit(1);
      }
#endif // NANO_OS_MEM_DEBUG
      next->next = cur->next;
      startDebugMessage("next->next = 0x");
      printDebugHex(next->next);
      printDebugString("\n");
      
      if (next->next != NULL) {
        next->next->prev = next;
      }
      
      // Reduce the free space by the delta between how much we were requested
      // and how much used to be managed by this node.
      next->size = cur->size - size - sizeof(MemNode);
      startDebugMessage("next->size = ");
      printDebugInt(next->size);
      printDebugString("\n");
    } else {
      startDebugMessage("next == cur->next\n");
      startDebugMessage("*NOT* updating metadata for next\n");
      // Reduce bytesFree by the delta.
      memoryManagerState->bytesFree += sizeof(MemNode);
      memoryManagerState->bytesFree -= (cur->size - size);
    }
    
    cur->size = size;
    startDebugMessage("New cur->size = ");
    printDebugInt(cur->size);
    printDebugString("\n");
    
    // Update the first and last pointers.
    if (cur == memoryManagerState->firstFree) {
      startDebugMessage("Updating memoryManagerState->firstFree to next\n");
      memoryManagerState->firstFree = next;
    }
    if (cur == memoryManagerState->lastFree) {
      startDebugMessage("Updating memoryManagerState->lastFree to next\n");
      memoryManagerState->lastFree = next;
    }
    
    // Move cur to the allocated list.
    cur->next = memoryManagerState->allocated;
    startDebugMessage("cur->next = 0x");
    printDebugHex(cur->next);
    printDebugString("\n");
    
    if (cur->next != NULL) {
      startDebugMessage("Setting cur->next->prev to cur\n");
      cur->next->prev = cur;
    }
    
    cur->prev = NULL;
    
    startDebugMessage("Updating memoryManagerState->allocated to cur\n");
    memoryManagerState->allocated = cur;
    
    // Set the owner for the memory.
    cur->owner = taskId;
    
    // Reduce system memory.
    startDebugMessage("Updating memoryManagerState->bytesFree from ");
    printDebugInt(memoryManagerState->bytesFree);
    printDebugString(" to ");
    memoryManagerState->bytesFree -= size + sizeof(MemNode);
    printDebugInt(memoryManagerState->bytesFree);
    printDebugString("\n");
    
    startDebugMessage("Allocating ");
    printDebugInt(cur->size);
    printDebugString(" bytes at 0x");
    printDebugHex(returnValue);
    printDebugString("\n");
  } else {
    startDebugMessage("Error: Could not find memory node with enough space\n");
  }
  
  if ((returnValue != NULL) && (ptr != NULL)) {
    // Because of the logic above, we're guaranteed that this means that the
    // address of returnValue is not the same as the address of ptr.  Copy
    // the data from the old memory to the new memory and free the old
    // memory.
    startDebugMessage("Copying old memory to new memory\n");
    memcpy(returnValue, ptr, sizeOfMemory(ptr));
    localFree(memoryManagerState, ptr);
  }
  
  return returnValue;
}

#ifdef __cplusplus
} // extern "C"
#endif

/******************* End Custom Memory Management Functions *******************/

/// @fn int memoryManagerReallocCommandHandler(
///   MemoryManagerState *memoryManagerState, TaskMessage *incoming)
///
/// @brief Command handler for a MEMORY_MANAGER_REALLOC command.  Extracts the
/// ReallocMessage from the message and passes the parameters to localRealloc.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   task.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerReallocCommandHandler(
  MemoryManagerState *memoryManagerState, TaskMessage *incoming
) {
  // We're going to reuse the incoming message as the outgoing message.
  TaskMessage *response = incoming;

  int returnValue = 0;
  ReallocMessage *reallocMessage
    = nanoOsMessageDataPointer(incoming, ReallocMessage*);
  void *clientReturnValue
    = localRealloc(memoryManagerState,
      reallocMessage->ptr, reallocMessage->size,
      taskId(taskMessageFrom(incoming)));
  reallocMessage->ptr = clientReturnValue;
  reallocMessage->size = 0;
  if (clientReturnValue != NULL) {
    reallocMessage->size = sizeOfMemory(clientReturnValue);
  }
  
  TaskDescriptor *from = taskMessageFrom(incoming);
  NanoOsMessage *nanoOsMessage = (NanoOsMessage*) taskMessageData(incoming);
  
  // We need to mark waiting as true here so that taskMessageSetDone signals
  // the client side correctly.
  taskMessageInit(response, reallocMessage->responseType,
    nanoOsMessage, sizeof(*nanoOsMessage), true);
  if (taskMessageQueuePush(from, response) != taskSuccess) {
    returnValue = -1;
  }
  
  // The client is waiting on us.  Mark the incoming message done now.  Do *NOT*
  // release it since the client is still using it.
  if (taskMessageSetDone(incoming) != taskSuccess) {
    returnValue = -1;
  }
  
  return returnValue;
}

/// @fn int memoryManagerFreeCommandHandler(
///   MemoryManagerState *memoryManagerState, TaskMessage *incoming)
///
/// @brief Command handler for a MEMORY_MANAGER_FREE command.  Extracts the
/// pointer to free from the message and then calls localFree.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   task.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerFreeCommandHandler(
  MemoryManagerState *memoryManagerState, TaskMessage *incoming
) {
  int returnValue = 0;

  void *ptr = nanoOsMessageDataPointer(incoming, void*);
  localFree(memoryManagerState, ptr);
  if (taskMessageRelease(incoming) != taskSuccess) {
    printString("ERROR: "
      "Could not release message from memoryManagerFreeCommandHandler.\n");
    returnValue = -1;
  }

  return returnValue;
}

/// @fn int memoryManagerGetFreeMemoryCommandHandler(
///   MemoryManagerState *memoryManagerState, TaskMessage *incoming)
///
/// @brief Command handler for MEMORY_MANAGER_GET_FREE_MEMORY.  Gets the amount
/// of free dynamic memory left in the system.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   task.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerGetFreeMemoryCommandHandler(
  MemoryManagerState *memoryManagerState, TaskMessage *incoming
) {
  // We're going to reuse the incoming message as the outgoing message.
  TaskMessage *response = incoming;

  int returnValue = 0;
  
  TaskDescriptor *from = taskMessageFrom(incoming);
  // We need to mark waiting as true here so that taskMessageSetDone signals the
  // client side correctly.
  taskMessageInit(response, MEMORY_MANAGER_RETURNING_FREE_MEMORY,
    NULL, memoryManagerState->bytesFree, true);
  if (taskMessageQueuePush(from, response) != taskSuccess) {
    returnValue = -1;
  }
  
  // The client is waiting on us.  Mark the incoming message done now.  Do *NOT*
  // release it since the client is still using it.
  if (taskMessageSetDone(incoming) != taskSuccess) {
    returnValue = -1;
  }
  
  return returnValue;
}

/// @fn int memoryManagerFreeTaskMemoryCommandHandler(
///   MemoryManagerState *memoryManagerState, TaskMessage *incoming)
///
/// @brief Command handler for a MEMORY_MANAGER_FREE_TASK_MEMORY command.
/// Extracts the task ID from the message and then calls
/// localFreeTaskMemory.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   task.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerFreeTaskMemoryCommandHandler(
  MemoryManagerState *memoryManagerState, TaskMessage *incoming
) {
  int returnValue = 0;
  NanoOsMessage *nanoOsMessage = (NanoOsMessage*) taskMessageData(incoming);
  if (taskId(taskMessageFrom(incoming)) == NANO_OS_SCHEDULER_TASK_ID) {
    TaskId taskId = nanoOsMessageDataValue(incoming, TaskId);
    localFreeTaskMemory(memoryManagerState, taskId);
    nanoOsMessage->data = 0;
  } else {
    printString(
      "ERROR: Only the scheduler may free another task's memory.\n");
    nanoOsMessage->data = 1;
    returnValue = -1;
  }
  
  if (taskMessageWaiting(incoming) == true) {
    // The client is waiting on us.  Mark the message as done.
    if (taskMessageSetDone(incoming) != taskSuccess) {
      printString("ERROR: Could not mark message done in "
        "memoryManagerFreeTaskMemoryCommandHandler.\n");
      returnValue = -1;
    }
  } else {
    // the client is *NOT* waiting on us.  Release the message.
    taskMessageRelease(incoming);
  }
  
  return returnValue;
}

/// @fn int memoryManagerAssignMemoryCommandHandler(
///   MemoryManagerState *memoryManagerState, TaskMessage *incoming)
///
/// @brief Command handler for the MEMORY_MANAGER_ASSIGN_MEMORY command. Makes
/// sure that the memory falls in the range of dynamic memory and, if so,
/// assigns it to the specified task ID.  If the provided pointer is not in the
/// range of dynamic memory, no action is taken.
///
/// @note This function can only be called from the scheduler.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   task.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerAssignMemoryCommandHandler(
  MemoryManagerState *memoryManagerState, TaskMessage *incoming
) {
  int returnValue = 0;
  
  if (taskId(taskMessageFrom(incoming)) == NANO_OS_SCHEDULER_TASK_ID) {
    AssignMemoryParams *assignMemoryParams
      = (AssignMemoryParams*) taskMessageData(incoming);
    if (isDynamicPointer(assignMemoryParams->ptr)) {
      memNode(assignMemoryParams->ptr)->owner = assignMemoryParams->taskId;
    }
  } else {
    printString(
      "ERROR: Only the scheduler may assign memory to another task.\n");
    returnValue = -1;
  }
  
  taskMessageSetDone(incoming);
  
  return returnValue;
}

/// @fn int memoryManagerDumpMemoryAllocations(
///   MemoryManagerState *memoryManagerState, TaskMessage *incoming)
///
/// @brief Command handler for MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS.  Walk
/// the memory allocation list and display information about all of the
/// allocations and their owning processes.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param incoming A pointer to the message received from the requesting
///   task.
///
/// @return Returns 0 on success, error code on failure.
int memoryManagerDumpMemoryAllocations(
  MemoryManagerState *memoryManagerState, TaskMessage *incoming
) {
  int returnValue = 0;
  
  printString("Outstanding allocations:\n");
  for (MemNode *cur = memoryManagerState->allocated;
    cur != NULL;
    cur = cur->next
  ) {
    printString("  0x");
    printHex(&cur[1]);
    printString(": ");
    printInt(cur->size);
    printString(" bytes owned by ");
    printInt(cur->owner);
    printString("\n");
  }
  
  printString("Available memory blocks:\n");
  for (MemNode *cur = memoryManagerState->firstFree;
    cur != NULL;
    cur = cur->next
  ) {
    printString("  0x");
    printHex(&cur[1]);
    printString(": ");
    printInt(cur->size);
    printString(" bytes available\n");
  }
  
  taskMessageSetDone(incoming);
  
  return returnValue;
}

/// @typedef MemoryManagerCommandHandler
///
/// @brief Signature of command handler for a memory manager command.
typedef int (*MemoryManagerCommandHandler)(
  MemoryManagerState *memoryManagerState, TaskMessage *incoming);

/// @var memoryManagerCommandHandlers
///
/// @brief Array of function pointers for handlers for commands that are
/// understood by this library.
const MemoryManagerCommandHandler memoryManagerCommandHandlers[] = {
  memoryManagerReallocCommandHandler,       // MEMORY_MANAGER_REALLOC
  memoryManagerFreeCommandHandler,          // MEMORY_MANAGER_FREE
  memoryManagerGetFreeMemoryCommandHandler, // MEMORY_MANAGER_GET_FREE_MEMORY
  // MEMORY_MANAGER_FREE_TASK_MEMORY:
  memoryManagerFreeTaskMemoryCommandHandler,
  memoryManagerAssignMemoryCommandHandler,  // MEMORY_MANAGER_ASSIGN_MEMORY
  // MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS:
  memoryManagerDumpMemoryAllocations,
};

/// @fn void handleMemoryManagerMessages(
///   MemoryManagerState *memoryManagerState)
///
/// @brief Handle memory manager messages from the task's queue until there
/// are no more waiting.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
///
/// @return This function returns no value.
void handleMemoryManagerMessages(MemoryManagerState *memoryManagerState) {
  TaskMessage *taskMessage = taskMessageQueuePop();
  while (taskMessage != NULL) {
    MemoryManagerCommand messageType
      = (MemoryManagerCommand) taskMessageType(taskMessage);
    if (messageType >= NUM_MEMORY_MANAGER_COMMANDS) {
      taskMessage = taskMessageQueuePop();
      continue;
    }
    
    memoryManagerCommandHandlers[messageType](
      memoryManagerState, taskMessage);
    
    taskMessage = taskMessageQueuePop();
  }
  
  return;
}

/// @fn void initializeGlobals(MemoryManagerState *memoryManagerState,
///   jmp_buf returnBuffer, char *stack)
///
/// @brief Initialize the global variables that will be needed by the memory
/// management functions and then resume execution in the main task function.
///
/// @param memoryManagerState A pointer to the MemoryManagerState
///   structure that holds the values used for memory allocation and
///   deallocation.
/// @param returnBuffer The jmp_buf that will be used to resume execution in the
///   main task function.
/// @param stack A pointer to the stack in allocateMemoryManagerStack.  Passed
///   just so that the compiler doesn't optimize it out.
///
/// @return This function returns no value and, indeed, never actually returns.
void initializeGlobals(MemoryManagerState *memoryManagerState,
  jmp_buf returnBuffer, char *stack
) {
  // The buffer needs to be machine-width aligned, so we need to use a pointer
  // as the placeholder value.  This ensures that the compiler puts it at a
  // valid (aligned) address.
  char *mallocBufferEnd = NULL;
  
  // Set up the memory manager's state.
  memoryManagerState->start = (uintptr_t) HAL->bottomOfHeap();
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
  memoryManagerState->firstFree->owner = TASK_ID_NOT_SET;
  
  printDebugString("Leaving initializeGlobals in MemoryManager.c\n");
  longjmp(returnBuffer, (int) ((intptr_t) stack));
}

/// @fn void allocateMemoryManagerStack(MemoryManagerState *memoryManagerState,
///   jmp_buf returnBuffer, int stackSize, char *topOfStack)
///
/// @brief Allocate space on the stack for the main task and then call
/// initializeGlobals to finish the initialization task.
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
///   main task function.
/// @param stackSize The desired stack size to allocate.
/// @param topOfStack A pointer to the first stack pointer that gets created.
///
/// @return This function returns no value and, indeed, never actually returns.
void allocateMemoryManagerStack(MemoryManagerState *memoryManagerState,
  jmp_buf returnBuffer, int stackSize, char *topOfStack
) {
  char stack[MEMORY_MANAGER_TASK_STACK_CHUNK_SIZE];
  memset(stack, 0, MEMORY_MANAGER_TASK_STACK_CHUNK_SIZE);
  
  if (topOfStack == NULL) {
    topOfStack = stack;
  }
  
  if (stackSize > MEMORY_MANAGER_TASK_STACK_CHUNK_SIZE) {
    allocateMemoryManagerStack(
      memoryManagerState,
      returnBuffer,
      stackSize - MEMORY_MANAGER_TASK_STACK_CHUNK_SIZE,
      topOfStack);
  }
  
  initializeGlobals(memoryManagerState, returnBuffer, topOfStack);
}

//// /// @fn void printMemoryManagerState(MemoryManagerState *memoryManagerState)
//// ///
//// /// @brief Debugging function to print all the values of the MemoryManagerState.
//// ///
//// /// @param memoryManagerState A pointer to the MemoryManagerState maintained by
//// ///   the task.
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
/// @brief Main task for the memory manager that will configure all the
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
  TaskMessage *schedulerMessage = NULL;
  jmp_buf returnBuffer;
  if (setjmp(returnBuffer) == 0) {
    allocateMemoryManagerStack(&memoryManagerState, returnBuffer,
      HAL->memoryManagerStackSize(MEMORY_MANAGER_DEBUG), NULL);
  }
  printDebugString("Returned from allocateMemoryManagerStack.\n");
  
  //// printMemoryManagerState(&memoryManagerState);
  printDebugString("memoryManagerState.firstFree->size = ");
  printDebugInt(memoryManagerState.firstFree->size);
  printDebugString("\n");
  printConsoleString("Using ");
  printConsoleULong(memoryManagerState.firstFree->size);
  printConsoleString(" bytes of dynamic memory.\n");
  releaseConsole();
  
  while (1) {
    schedulerMessage = (TaskMessage*) taskYield();
    if (schedulerMessage != NULL) {
      // We have a message from the scheduler that we need to task.  This
      // is not the expected case, but it's the priority case, so we need to
      // list it first.
      MemoryManagerCommand messageType
        = (MemoryManagerCommand) taskMessageType(schedulerMessage);
      if (messageType < NUM_MEMORY_MANAGER_COMMANDS) {
        memoryManagerCommandHandlers[messageType](
          &memoryManagerState, schedulerMessage);
      } else {
        printString("ERROR: Received unknown memory manager command ");
        printInt(messageType);
        printString(" from scheduler.\n");
      }
    } else {
      // No message from the scheduler.  Handle any user task messages in
      // our message queue.
      handleMemoryManagerMessages(&memoryManagerState);
    }
  }
  
  return NULL;
}

/// @fn size_t getFreeMemory(void)
///
/// @brief Send a MEMORY_MANAGER_GET_FREE_MEMORY command to the memory manager
/// task and wait for a reply.
///
/// @return Returns the size, in bytes, of available dynamic memory on success,
/// 0 on failure.
size_t getFreeMemory(void) {
  size_t returnValue = 0;
  
  TaskMessage sent;
  memset(&sent, 0, sizeof(sent));
  taskMessageInit(&sent, MEMORY_MANAGER_GET_FREE_MEMORY, NULL, 0, true);
  
  if (sendTaskMessageToTaskId(NANO_OS_MEMORY_MANAGER_TASK_ID, &sent)
    != taskSuccess
  ) {
    // Nothing more we can do.
    return returnValue;
  }
  
  TaskMessage *response = taskMessageWaitForReplyWithType(&sent, false,
    MEMORY_MANAGER_RETURNING_FREE_MEMORY, NULL);
  returnValue = taskMessageSize(response);
  
  return returnValue;
}

/// @fn void* memoryManagerSendReallocMessage(void *ptr, size_t size)
///
/// @brief Send a MEMORY_MANAGER_REALLOC command to the memory manager task
/// and wait for a reply.
///
/// @param ptr The pointer to send to the task.
/// @param size The size to send to the task.
///
/// @return Returns the data pointer returned in the reply.
void* memoryManagerSendReallocMessage(void *ptr, size_t size) {
  void *returnValue = NULL;
  
  ReallocMessage reallocMessage;
  reallocMessage.ptr = ptr;
  reallocMessage.size = size;
  reallocMessage.responseType = MEMORY_MANAGER_RETURNING_POINTER;
  
  TaskMessage *sent
    = sendNanoOsMessageToTaskId(NANO_OS_MEMORY_MANAGER_TASK_ID,
    MEMORY_MANAGER_REALLOC, /* func= */ 0,
    (NanoOsMessageData) ((uintptr_t) &reallocMessage),
    true);
  
  if (sent == NULL) {
    // Nothing more we can do.
    return returnValue; // NULL
  }
  
  TaskMessage *response = taskMessageWaitForReplyWithType(sent, false,
    MEMORY_MANAGER_RETURNING_POINTER, NULL);
  if (response == NULL) {
    // Something is wrong.  Fail.
    return returnValue; // NULL
  }
  
  // The handler set the pointer back in the structure we sent it, so grab it
  // out of the structure we already have.
  returnValue = reallocMessage.ptr;
  taskMessageRelease(sent);
  
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
  if (ptr != NULL) {
    sendNanoOsMessageToTaskId(
      NANO_OS_MEMORY_MANAGER_TASK_ID, MEMORY_MANAGER_FREE,
      (NanoOsMessageData) 0, (NanoOsMessageData) ((intptr_t) ptr), false);
  }
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

