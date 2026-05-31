///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              11.30.2024
///
/// @file              MemoryManager.h
///
/// @brief             Dynamic memory management functionality for NanoOs.
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

#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include "NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def MEMORY_MANAGER_COMMAND_SIGNATURE
///
/// @brief 64-bit, little-endian value to use as a signature to indicate to the
/// memory manager process that the command is intended for it.  This is
/// "MEMRYCMD" expressed as a little-endian value.
#define MEMORY_MANAGER_COMMAND_SIGNATURE ((uint64_t) 0x444D4359524D454D)

/// @def MEMORY_MANAGER_PROCESS_STACK_CHUNK_SIZE
///
/// @brief The size, in bytes, of one chunk of the main memory process's stack.
#define MEMORY_MANAGER_PROCESS_STACK_CHUNK_SIZE 32

/// @def MEMORY_MANAGER_DEBUG
///
/// @brief Whether or not the memory manager is emitting debug prints.  Set
/// this to 1 to enable.
#define MEMORY_MANAGER_DEBUG 0

/// @def MEMORY_MANAGER_DEBUG_STACK_SIZE
///
/// @brief The stack size, in bytes, of the main memory manager process that
/// will handle messages when in debug mode.  This needs to be as small as
/// possible while still allowing debug prints to work.
#define MEMORY_MANAGER_DEBUG_STACK_SIZE 768

/// @struct ReallocMessage
///
/// @brief Structure that holds the data needed to make a request to reallocate
/// an existing pointer.
///
/// @param signature The 64-bit signature that indicates that this is a memory
///   management command.  This should always be the value
///   MEMORY_MANAGER_COMMAND_SIGNATURE.
/// @param ptr The pointer to be reallocated.  If this value is NULL, new
///   memory will be allocated.
/// @param size The number of bytes to allocate.  If this value is 0, the memory
///   at ptr will be freed.
typedef struct ReallocMessage {
  uint64_t  signature;
  void     *ptr;
  size_t    size;
} ReallocMessage;

/// @struct MemoryManagerFreeArgs
///
/// @brief Structure that holds the data to request to free an existing pointer.
///
/// @param signature The 64-bit signature that indicates that this is a memory
///   management command.  This should always be the value
///   MEMORY_MANAGER_COMMAND_SIGNATURE.
/// @param ptr The pointer to be freed.
typedef struct MemoryManagerFreeArgs {
  uint64_t  signature;
  void     *ptr;
} MemoryManagerFreeArgs;

/// @struct AssignMemoryArgs
///
/// @brief Functional parameters to the MEMORY_MANAGER_ASSIGN_MEMORY command.
///
/// @param ptr A pointer to the memory to assign.
/// @param pid The ProcessId of the process to assign the memory to.
typedef struct AssignMemoryArgs {
  void *ptr;
  ProcessId pid;
} AssignMemoryArgs;

/// @enum MemoryManagerCommandResponse
///
/// @brief Commands and responses recognized by the memory manager.
typedef enum MemoryManagerCommandResponse {
  // Commands:
  MEMORY_MANAGER_REALLOC,
  MEMORY_MANAGER_FREE,
  MEMORY_MANAGER_GET_FREE_MEMORY,
  MEMORY_MANAGER_FREE_PROCESS_MEMORY,
  MEMORY_MANAGER_ASSIGN_MEMORY,
  MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS,
  NUM_MEMORY_MANAGER_COMMANDS,
  // Responses:
  MEMORY_MANAGER_RETURNING_POINTER,
  MEMORY_MANAGER_RETURNING_FREE_MEMORY,
} MemoryManagerCommand;

// Function prototypes
void* runMemoryManager(void *args);

void memoryManagerFree(void *ptr);
#ifdef free
#undef free
#endif // free
#define free(ptr) memoryManagerFree(ptr)

void* memoryManagerRealloc(void *ptr, size_t size);
#ifdef realloc
#undef realloc
#endif // realloc
#define realloc(ptr, size) memoryManagerRealloc(ptr, size)

void* memoryManagerMalloc(size_t size);
#ifdef malloc
#undef malloc
#endif // malloc
#define malloc(size) memoryManagerMalloc(size)

void* memoryManagerCalloc(size_t nmemb, size_t size);
#ifdef calloc
#undef calloc
#endif // calloc
#define calloc(nmemb, size) memoryManagerCalloc(nmemb, size)

size_t getFreeMemory(void);
int dumpMemoryAllocations(void);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // MEMORY_MANAGER_H

