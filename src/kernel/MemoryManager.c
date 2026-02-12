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
  (((ptr) != NULL) ? \
    ((uint32_t) memNode(ptr)->numChunks) * memoryManagerState->bytesPerChunk \
    : 0 \
  )

/// @def isDynamicPointer
///
/// @brief Determine whether or not a pointer was allocated from the allocators
/// in this library.
#define isDynamicPointer(ptr) \
  ((((uintptr_t) (ptr)) >= memoryManagerState->start) \
    && (((uintptr_t) (ptr)) <= memoryManagerState->end))

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
  if (isDynamicPointer(ptr)) {
    // This is memory that was previously allocated from one of our allocators.
    
    // Splice out memNode from the allocated list.
    MemNode *memNode = memNode(ptr);
    if (memNode->prev != NULL) {
      memNode->prev->next = memNode->next;
    }
    if (memNode->next != NULL) {
      memNode->next->prev = memNode->prev;
    }
    if (memoryManagerState->allocated == memNode) {
      memoryManagerState->allocated = memNode->next;
    }
    
    // Put the memNode in the right place in the free list.
    MemNode *cur = memoryManagerState->lastFree;
    while (((uintptr_t) cur->prev) > ((uintptr_t) memNode)) {
      cur = cur->prev;
    }
    
    memNode->next = cur;
    memNode->prev = cur->prev;
    
    size_t numBytes
      = ((size_t) memNode->numChunks) * memoryManagerState->bytesPerChunk;
    MemNode *next
      = (MemNode*) (((uint8_t*) memNode) + numBytes + sizeof(MemNode));
    
    if (next != cur) {
      cur->prev = memNode;
    } else {
      // Do memory compaction between memNode and cur.
      memNode->numChunks += cur->numChunks;
      memNode->next = cur->next;
      if (memoryManagerState->lastFree == cur) {
        memoryManagerState->lastFree = memNode;
      }
    }
    
    if (memNode->prev != NULL) {
      MemNode *prev = memNode->prev;
      numBytes = ((size_t) prev->numChunks) * memoryManagerState->bytesPerChunk;
      next = (MemNode*) (((uint8_t*) prev) + numBytes + sizeof(MemNode));
      
      if (next != memNode) {
        prev->next = memNode;
      } else {
        // Do memory compaction between prev and memNode.
        prev->numChunks += memNode->numChunks;
        prev->next = memNode->next;
      }
    } else {
      memoryManagerState->firstFree = memNode;
    }
  } // else this is not something we can free.  Ignore it.
  
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
  if (size == 0) {
    // In this case, there's no point in going through any path below.  Just
    // free it, return NULL, and be done with it.
    localFree(memoryManagerState, ptr);
    return NULL;
  }
  
  // We need to fix the size to be aligned with our memory model.
  size_t numChunks = (size + (memoryManagerState->bytesPerChunk - 1))
    / memoryManagerState->bytesPerChunk;
  size = numChunks * memoryManagerState->bytesPerChunk;
  
  void *returnValue = NULL;
  char *charPointer = (char*) ptr;
  MemNode *next = NULL;
  if (isDynamicPointer(ptr)) {
    // This pointer was allocated from our allocators.
    MemNode *memNode = memNode(ptr);
    size_t oldSize = sizeOfMemory(ptr);
    next = (MemNode*) (charPointer + oldSize);
    
    if (size <= oldSize) {
      // We're fitting into a block that's larger than or equal to the size
      // being requested.  *DO NOT* update the size in this case.  Just
      // return the current pointer.
      return ptr;
    } else if (next == memoryManagerState->lastFree) {
      // We're being asked to extend the last block that was allocated.  Just
      // extend it if we have enough space.
      if ((memNode->numChunks + next->numChunks) >= numChunks) {
        next = (MemNode*) (charPointer + size);
        next->prev = memoryManagerState->lastFree->prev;
        next->next = NULL;
        if (next->prev != NULL) {
          next->prev->next = next;
        }
        // Reduce the free space by the delta between how much we were requested
        // and how much used to be managed by this node.
        next->numChunks = memoryManagerState->lastFree->numChunks
          - (numChunks - memNode->numChunks);
        memNode->numChunks = numChunks;
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
    return NULL;
  }
  
  // We're allocating new memory.  Search from the beginning.
  MemNode *cur = NULL;
  for (cur = memoryManagerState->firstFree; cur != NULL; cur = cur->next) {
    if (cur->numChunks >= numChunks) {
      break;
    }
  }
  
  if (cur != NULL) {
    // Memory allocation has succeeded.
    returnValue = &cur[1];
    charPointer = (char*) returnValue;
    next = (MemNode*) (charPointer + size);
    
    // Update the links on the next pointer.
    next->prev = cur->prev;
    if (next->prev != NULL) {
      next->prev->next = next;
    }
    next->next = cur->next;
    if (next->next != NULL) {
      next->next->prev = next;
    }
    
    // Reduce the free space by the delta between how much we were requested
    // and how much used to be managed by this node.
    next->numChunks = cur->numChunks - numChunks;
    cur->numChunks = numChunks;
    
    // Update the first and last pointers.
    if (cur == memoryManagerState->firstFree) {
      memoryManagerState->firstFree = next;
    }
    if (cur == memoryManagerState->lastFree) {
      memoryManagerState->lastFree = next;
    }
    
    // Move cur to the allocated list.
    cur->next = memoryManagerState->allocated;
    cur->prev = NULL;
    
    // Set the owner for the memory.
    cur->owner = taskId;
  }
  
  if ((returnValue != NULL) && (ptr != NULL)) {
    // Because of the logic above, we're guaranteed that this means that the
    // address of returnValue is not the same as the address of ptr.  Copy
    // the data from the old memory to the new memory and free the old
    // memory.
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
  size_t dynamicMemorySize = 0;
  for (MemNode *cur = memoryManagerState->firstFree;
    cur != NULL;
    cur = cur->next
  ) {
    dynamicMemorySize
      += ((size_t) cur->numChunks) * memoryManagerState->bytesPerChunk;
  }
  
  // We need to mark waiting as true here so that taskMessageSetDone signals the
  // client side correctly.
  taskMessageInit(response, MEMORY_MANAGER_RETURNING_FREE_MEMORY,
    NULL, dynamicMemorySize, true);
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
    printInt(((size_t) cur->numChunks) * memoryManagerState->bytesPerChunk);
    printString(" bytes owned by ");
    printInt(cur->owner);
    printString("\n");
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
  memoryManagerState->start = (uintptr_t) HAL->bottomOfStack();
  memoryManagerState->end = (uintptr_t) &mallocBufferEnd;
  memoryManagerState->totalMemory
    = ((size_t) memoryManagerState->end) - ((size_t) memoryManagerState->start);
  memoryManagerState->allocated = NULL;
  memoryManagerState->firstFree = (MemNode*) memoryManagerState->start;
  memoryManagerState->lastFree = memoryManagerState->firstFree;
  size_t divisor = 1 << (sizeof(memoryManagerState->firstFree->numChunks) * 8);
  size_t bytesPerChunk = memoryManagerState->totalMemory / divisor;
  memoryManagerState->bytesPerChunk
    = (bytesPerChunk + (sizeof(size_t) - 1)) & ~(sizeof(size_t) - 1);
  
  // Setup the first node in the free list.
  memoryManagerState->firstFree->next = NULL;
  memoryManagerState->firstFree->prev = NULL;
  memoryManagerState->firstFree->numChunks
    = memoryManagerState->totalMemory / memoryManagerState->bytesPerChunk;
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
  size_t dynamicMemorySize = 0;
  if (setjmp(returnBuffer) == 0) {
    allocateMemoryManagerStack(&memoryManagerState, returnBuffer,
      HAL->memoryManagerStackSize(MEMORY_MANAGER_DEBUG), NULL);
  }
  printDebugString("Returned from allocateMemoryManagerStack.\n");
  
  //// printMemoryManagerState(&memoryManagerState);
  dynamicMemorySize = memoryManagerState.firstFree->numChunks
    * memoryManagerState.bytesPerChunk;
  printDebugString("dynamicMemorySize = ");
  printDebugInt(dynamicMemorySize);
  printDebugString("\n");
  printConsoleString("Using ");
  printConsoleULong(dynamicMemorySize);
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

