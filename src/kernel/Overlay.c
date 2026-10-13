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

#include "Hal.h"
#include "Logger.h"
#include "NanoOs.h"
#include "OverlayFunctions.h"
#include "../user/NanoOsLibC.h"
#include "Scheduler.h"
#include "Processes.h"

// Must come last
#include "../user/NanoOsStdio.h"

/// @var overlayMap
///
/// @brief Memory address where overlays are loaded.  Populated once from
/// HAL->memory->overlayMap() by initializeSchedulerState(); everything else
/// reads this directly instead of going through the HAL on every call.
NanoOsOverlayMap *overlayMap = NULL;

/// @fn OverlayFunction findOverlayFunction(const char *overlayFunctionName)
///
/// @brief Find a function in an overlay that's been previously loaded into RAM.
///
/// @param overlayFunctionName The name of the function in the overlay to find.
///
/// @return Returns a pointer to the found function on success, NULL on failure.
OverlayFunction findOverlayFunction(const char *overlayFunctionName) {
  uint16_t cur = 0;
  int comp = 0;
  OverlayFunction overlayFunction = NULL;
  if (overlayFunctionName == NULL) {
    return overlayFunction; // NULL
  }

  for (uint16_t ii = 0, jj = overlayMap->numExports - 1; ii <= jj;) {
    cur = (ii + jj) >> 1;
    comp = strcmp(overlayMap->exports[cur].name, overlayFunctionName);
    if (comp == 0) {
      overlayFunction = overlayMap->exports[cur].fn;
      break;
    } else if (comp < 0) { // cur < overlayFunctionName
      // Move the left bound to one greater than cur.
      ii = cur + 1;
    } else if (cur > 0) { // comp > 0, overlayFunctionName < cur
      // Move the right bound to one less than cur.
      jj = cur - 1;
    } else {
      // We're out of indexes to search.  overlayFunctionName not found.
      break;
    }
  }
  
  return overlayFunction;
}

/// @var _overlayExt
///
/// @brief Local copy of OVERLAY_EXT (defined in Overlay.h) kept in this
/// translation unit's own storage.  If OVERLAY_EXT's definition ever
/// changes, this picks up the change automatically since it's initialized
/// from the macro rather than duplicating the literal.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _overlayExt[] KEEP_IN_FLASH = OVERLAY_EXT;

/// @var _pathSeparator
///
/// @brief Path separator used to join an overlay directory with an overlay
/// name.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _pathSeparator[] KEEP_IN_FLASH = "/";

/// @fn void* callOverlayFunctionFromFile(const void *od, const void *oo,
///   const char *function, void *args)
///
/// @brief Kernel function to load an overlay from a file into memory, find a
/// designated function, call it with the provided arguments, and return the
/// return value.
///
/// @param od The path to the overlay directory on the filesystem.  If this
///   parameter is OVERLAY_SAME_NAMESPACE then the overlay path currently in
///   use will be used.
/// @param oo The name of the overlay minus the ".overlay" file extension
///   that is local to the overlay directory.
/// @param function The name of the function exported by the overlay.
/// @param args Any arguments to be passed to the function in the overlay, cast
///   to a void*.  This parameter may be NULL.
///
/// @return Returns the value returned by the overlay function on success, NULL
/// on failure.
void* callOverlayFunctionFromFile(const void *od, const void *oo,
  const char *function, void *args
) {
  const char *overlayDir = (const char*) od;
  const char *overlay = (const char*) oo;
  void *returnValue = NULL;
  if ((overlay == NULL) || (function == NULL)) {
    logError("One or more NULL arguments provided to "
      "callOverlayFunctionFromFile\n");
    goto exit; // return NULL
  }
  
  ProcessDescriptor *runningProcess = getRunningProcess();
  if (runningProcess == NULL) {
    // This should be impossible.
    goto exit; // return NULL
  }
  
  // Keep track of the overlay that's currently running and the one we need.
  char *previousOverlayDir = (char*) runningProcess->overlayNamespace;

  // We need to allocate enough space for the two FileBlockMetadata objects and
  // all of the strings we need.  For the strings, we need the overlay
  // directory, a slash, the name of the overlay, the overlay extension, a NUL
  // byte, the function name, and a trailing NUL byte.
  char *allocation = (char*) malloc(
    (2 * sizeof(OverlayDescriptor))
    + ((overlayDir == OVERLAY_SAME_NAMESPACE)
      ? strlen(previousOverlayDir)
      : strlen(overlayDir)
    )
    + 1
    + strlen(overlay)
    + OVERLAY_EXT_LEN
    + 1
    + strlen(function)
    + 1);
  if (allocation == NULL) {
    goto exit;
  }
  OverlayDescriptor *overlayArray = (OverlayDescriptor*) allocation;
  char *overlayInfo = allocation + (2 * sizeof(OverlayDescriptor));

  overlayArray[0].fileBlockMetadata.blockDevice
    = runningProcess->overlay.fileBlockMetadata.blockDevice;
  overlayArray[0].fileBlockMetadata.startBlock
    = runningProcess->overlay.fileBlockMetadata.startBlock;
  overlayArray[0].fileBlockMetadata.numBlocks
    = runningProcess->overlay.fileBlockMetadata.numBlocks;
  overlayArray[0].memory = NULL;

  strcpy(overlayInfo,
    (overlayDir == OVERLAY_SAME_NAMESPACE) ? previousOverlayDir : overlayDir);
  strcat(overlayInfo, _pathSeparator);
  strcat(overlayInfo, overlay);
  strcat(overlayInfo, _overlayExt);
  
  // Get the overlay information we need.
  overlayArray[1].memory = NULL;
  if (getFileBlockMetadataFromPath(overlayInfo,
    &overlayArray[1].fileBlockMetadata) != 0
  ) {
    // We can't proceed
    goto freeOverlayInfo;
  }
  
  char *functionCopy = overlayInfo + strlen(overlayInfo) + 1;
  strcpy(functionCopy, function);
  
  // Terminate the overlayInfo string at the end of the directory path.
  overlayInfo[strlen(
    (overlayDir == OVERLAY_SAME_NAMESPACE) ? previousOverlayDir : overlayDir)]
    = '\0';
  
  // args does not get copied.  We have no way of knowing what the proper way
  // to copy the data would be.  The caller is responsible for allocating data
  // in dynamic memory before the pointer is passed to this function.
  
  if (overlayDir != OVERLAY_SAME_NAMESPACE) {
    overlayDir = overlayInfo;
  }
  if (schedulerReplaceOverlay(overlayDir, &overlayArray[1]) != 0) {
    logError("Could not load file overlay via the scheduler\n");
    goto freeOverlayInfo;
  }

  // If we made it this far, then our new overlay has been successuflly loaded.
  OverlayFunction overlayFunction = findOverlayFunction(functionCopy);
  if (overlayFunction == NULL) {
    logError("Could not find overlay function \"%s\"\n", functionCopy);
    goto restorePreviousOverlay;
  }
  
  // The overlay function was found.  Get our return value.
  returnValue = overlayFunction(args);
  
restorePreviousOverlay:
  // Don't check the return value here.  No point.
  schedulerReplaceOverlay(previousOverlayDir, &overlayArray[0]);

  // Release all the memory for the copies.
freeOverlayInfo:
  free(allocation);
exit:
  return returnValue;
}

/// @fn void* callOverlayFunctionFromBlockDevice(
///   const void *deviceId, const void *overlay,
///   const char *function, void *args)
///
/// @brief Kernel function to load an overlay from blocks on a device into
/// memory, find a designated function, call it with the provided arguments,
/// and return the return value.
///
/// @param deviceId The zero-based index of the block device the overlay is on,
///   cast to a void*.  If this parameter is OVERLAY_SAME_NAMESPACE then the
///   device currently in use will be used.
/// @param overlay The zero-based index of the block-based overlay on the
///   device, cast to a void*.
/// @param function The name of the function exported by the overlay.
/// @param args Any arguments to be passed to the function in the overlay, cast
///   to a void*.  This parameter may be NULL.
///
/// @return Returns the value returned by the overlay function on success, NULL
/// on failure.
void* callOverlayFunctionFromBlockDevice(
  const void *deviceId, const void *overlay,
  const char *function, void *args
) {
  void *returnValue = NULL;
  if (function == NULL) {
    logError("One or more NULL arguments provided to "
      "callOverlayFunctionFromBlockDevice\n");
    goto exit; // return NULL
  }
  
  ProcessDescriptor *runningProcess = getRunningProcess();
  if (runningProcess == NULL) {
    // This should be impossible.
    goto exit; // return NULL
  }
  
  // Keep track of the overlay that's currently running and the one we need.
  void *previousOverlayDevice = runningProcess->overlayNamespace;
  OverlayDescriptor overlayArray[2];
  overlayArray[0].fileBlockMetadata.blockDevice
    = runningProcess->overlay.fileBlockMetadata.blockDevice;
  overlayArray[0].fileBlockMetadata.startBlock
    = runningProcess->overlay.fileBlockMetadata.startBlock;
  overlayArray[0].fileBlockMetadata.numBlocks
    = runningProcess->overlay.fileBlockMetadata.numBlocks;
  overlayArray[0].memory = NULL;
  
  overlayArray[1].fileBlockMetadata.blockDevice
    = runningProcess->overlay.fileBlockMetadata.blockDevice;
  overlayArray[1].memory = NULL;
  if (deviceId != OVERLAY_SAME_NAMESPACE) {
    if (HAL->blockDevice != NULL) {
      HAL->blockDevice->get(
        (int) ((intptr_t) deviceId),
        &overlayArray[1].fileBlockMetadata.blockDevice);
    } else {
      overlayArray[1].fileBlockMetadata.blockDevice = NULL;
    }
    if (overlayArray[1].fileBlockMetadata.blockDevice == NULL) {
      // No such block device.
      goto exit; // return NULL
    }
  }
  
  // The number of blocks in block-based overlays always have to be the same.
  // So, just copy what we're already using.
  overlayArray[1].fileBlockMetadata.numBlocks
    = runningProcess->overlay.fileBlockMetadata.numBlocks;
  overlayArray[1].fileBlockMetadata.startBlock
    = (((uintptr_t) overlay) * overlayArray[1].fileBlockMetadata.numBlocks) + 1;
  
  char *functionCopy = (char*) malloc(strlen(function) + 1);
  if (functionCopy == NULL) {
    // Out of memory.  Bail.
    goto exit; // return NULL
  }
  strcpy(functionCopy, function);
  
  if (schedulerReplaceOverlay(deviceId, &overlayArray[1]) != 0) {
    logError("Could not load block overlay via the scheduler\n");
    goto exit;
  }

  
  // If we made it this far, then our new overlay has been successuflly loaded.
  OverlayFunction overlayFunction = findOverlayFunction(functionCopy);
  if (overlayFunction == NULL) {
    logError("Could not find overlay function \"%s\"\n", functionCopy);
    goto restorePreviousOverlay;
  }
  
  // The overlay function was found.  Get our return value.
  returnValue = overlayFunction(args);
  
restorePreviousOverlay:
  schedulerReplaceOverlay(previousOverlayDevice, &overlayArray[0]);
  
  // Release all the memory for the copies.
  free(functionCopy);
exit:
  return returnValue;
}

/// @var startFunctionName
///
/// @brief Name of the exported entry-point function every command overlay
/// must provide.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
const char startFunctionName[] KEEP_IN_FLASH = "_start";

/// @fn int runOverlayCommand(const char *commandPath,
///   int argc, char **argv)
///
/// @brief Run a command that's in overlay format on the filesystem.
///
/// @param commandPath The full path to the command overlay file on the
///   filesystem.
/// @param argc The number of arguments from the command line.
/// @param argv The of arguments from the command line as an array of C strings.
///
/// @return Returns 0 on success, a negative SUS value on failure.
int runOverlayCommand(const char *commandPath,
  int argc, char **argv
) {
  // The overlay is already loaded by the scheduler, so there's no need to load
  // it manually.

  OverlayFunction _start = findOverlayFunction(startFunctionName);
  if (_start == NULL) {
    logError("Could not find exported _start function in \"%s\" overlay.\n",
      commandPath);
    return 1;
  }
  logDebug("Found _start function\n");

  MainArgs mainArgs = {
    .argc = argc,
    .argv = argv,
  };
  logDebug("Calling _start function at address 0x%lx\n",
    (unsigned long int) (uintptr_t) _start);
  int returnValue = (int) ((intptr_t) _start(&mainArgs));
  logDebug("Got return value %ld from _start function\n",
    (long int) returnValue);
  if ((returnValue < 0) || (returnValue > 255)) {
    // Invalid return value.
    returnValue = -EOTHER;
  }

  return returnValue;
}

/// @fn OverlayFifoNode* overlayFifoPop(OverlayFifo *overlayFifo)
///
/// @brief Pop a pointer to an OverlayFifoNode from an OverlayFifo without the
/// overhead of a function call.
///
/// @param overlayFifo A pointer to an OverlayFifo to pop the pointer from.
///
/// @return Returns a pointer to an OverlayFifoNode on success, NULL on
/// failure.
OverlayFifoNode* overlayFifoPop(OverlayFifo *overlayFifo) {
  OverlayFifoNode *overlayFifoNode = NULL;
  if ((overlayFifo == NULL) || (overlayFifo->numElements == 0)) {
    return overlayFifoNode; // NULL
  }

  coroutineEnterCriticalSection();
  overlayFifoNode = &overlayFifo->nodes[overlayFifo->head];
  overlayFifo->head++;
  if (overlayFifo->head == overlayFifo->size) {
    // Doing this saves stack space and time relative to
    // overlayFifo->head %= overlayFifo->size
    overlayFifo->head = 0;
  }
  overlayFifo->numElements--;
  coroutineExitCriticalSection();

  return overlayFifoNode;
}

/// @fn OverlayFifoNode* overlayFifoGet(
///   OverlayFifo *overlayFifo, void *overlayId, uint8_t overlayIdLength)
///
/// @brief Get a pointer to an OverlayFifoNode from an OverlayFifo without the
///   overhead of a function call.
///
/// @param overlayFifo A pointer to the OverlayFifo to get the OverlayFifoNode
///   from.
/// @param overlayId The ID of the OverlayFifoNode to get.
/// @param overlayIdLength The number of bytes occupied by the memory pointed to
///   by the overlayId.
///
/// @return Returns a pointer to the found OverlayFifoNode on success, NULL on
/// failure.
OverlayFifoNode* overlayFifoGet(
  OverlayFifo *overlayFifo, void *overlayId, uint8_t overlayIdLength
) {
  OverlayFifoNode *overlayFifoNode = NULL;
  if ((overlayFifo == NULL) || (overlayFifo->numElements == 0)) {
    return overlayFifoNode; // NULL
  }

  coroutineEnterCriticalSection();
  for (uint8_t ii = overlayFifo->head; ; ii++) {
    if (ii == overlayFifo->size) {
      ii = 0;
    }
    if (ii == overlayFifo->tail) {
      break;
    }
    OverlayFifoNode *candidateFifoNode = &overlayFifo->nodes[ii];

    if (candidateFifoNode->overlayIdLength != overlayIdLength) {
      continue;
    }
    if (overlayIdLength == 0) {
      if (candidateFifoNode->overlayId == overlayId) {
        overlayFifoNode = candidateFifoNode;
        break;
      }
    } else if (strncmp((char*) overlayId, (char*) candidateFifoNode->overlayId,
      overlayIdLength) == 0
    ) {
      overlayFifoNode = candidateFifoNode;
      break;
    }
  }

  if (overlayFifoNode != NULL) {
    overlayFifoNode->missesRemaining = 3;
  }

  coroutineExitCriticalSection();
  return overlayFifoNode;
}

/// @fn OverlayFifoNode* overlayFifoPush(
///   OverlayFifo *overlayFifo, void *overlayId, uint8_t overlayIdLength,
///   void* (*fifoMalloc)(size_t)
///
/// @brief Push an overlay into an overlay FIFO.
///
/// @param overlayFifo A pointer to an OverlayFifo to push the overlay onto.
/// @param overlayId The full path to a file-based overlay or the block index of
///   a block-based overlay.
/// @param overlayIdLength The number of bytes at the pointer provided in the
///   overlayId parameter.  If this value is 0 then the overlayId in the FIFO
///   will be set with `=`, otherwise, memory will be allocated for it and the
///   memory contents will be copied.
///
/// @note As this is an implementation of an LRU cache, this function will evict
/// the entry that's least-recently used if the FIFO is full in order to make
/// room for the entry being pushed.  The intent is that this function should
/// always succeed.
///
/// @return Returns a pointer to the OverlayFifoNode used on success, NULL on
/// failure.
OverlayFifoNode* overlayFifoPush(
  OverlayFifo *overlayFifo, void *overlayId, uint8_t overlayIdLength,
  void* (*fifoMalloc)(size_t)
) {
  if (overlayFifo == NULL) {
    errno = EINVAL;
    return NULL;
  }

  coroutineEnterCriticalSection();
  OverlayFifoNode *overlayFifoNode = &overlayFifo->nodes[overlayFifo->tail];
  if (overlayFifo->numElements < overlayFifo->size) {
    // We're using the old tail, so update it now.
    overlayFifo->tail++;
    if (overlayFifo->tail == overlayFifo->size) {
      // Doing this saves stack space and time relative to
      // overlayFifo->tail %= overlayFifo->size
      overlayFifo->tail = 0;
    }
    overlayFifo->numElements++;

    goto setOverlayFifoNode;
  }

  // If we got this far then the FIFO is currently full.  We need to make room.
  // Run through the FIFO, decrementing the missesRemaining until we find one
  // that's 0 and use that.
  //
  // Note:  What we're doing here is essentially repeatedly popping from and
  // pushing to the queue with the coroutine's critical section locked.  Because
  // of that, we need to update the head and tail indexes as we do this walk.
  //
  // Note:  Because the queue is full, head == tail at the start of this loop.
  // We will still update them as if we didn't know that.
  overlayFifoNode = &overlayFifo->nodes[overlayFifo->head];
  overlayFifo->head++;
  if (overlayFifo->head == overlayFifo->size) {
    overlayFifo->head = 0;
  }
  while (overlayFifoNode->missesRemaining != 0) {
    overlayFifoNode->missesRemaining--;

    // "Push" to the tail of the FIFO.  We're leaving the data in place, so
    // there's no actual data to update apart from the tail index.
    overlayFifo->tail++;
    if (overlayFifo->tail == overlayFifo->size) {
      overlayFifo->tail = 0;
    }

    // "Pop" the head of the FIFO.
    overlayFifoNode = &overlayFifo->nodes[overlayFifo->head];
    overlayFifo->head++;
    if (overlayFifo->head == overlayFifo->size) {
      overlayFifo->head = 0;
    }
  }

setOverlayFifoNode:
  overlayFifoNode->missesRemaining = 3;
  overlayFifoNode->overlayIdLength = overlayIdLength;

  // The overlay is already loaded into the overlayMap pointer at the time this
  // function is called, so just copy it from there.  We *MUST* do this *FIRST*
  // before we do a malloc.  malloc transfers control to the memory manager.
  // That context switch means that other processes can potentially overwrite
  // the overlayMap area during that time.
  ProcessDescriptor *processDescriptor = getRunningProcess();
  memcpy(overlayFifoNode->overlay, overlayMap,
    processDescriptor->overlay.fileBlockMetadata.numBlocks
    * processDescriptor->overlay.fileBlockMetadata.blockDevice->blockSize);

  if (overlayIdLength == 0) {
    overlayFifoNode->overlayId = overlayId;
  } else {
    // Allocate and copy memory.  missesRemaining should protect us while we're
    // not inside a critical section.
    overlayFifoNode->overlayId = fifoMalloc(overlayIdLength);
    if (overlayFifoNode->overlayId == NULL) {
      errno = ENOMEM;
      return NULL;
    }
    memcpy(overlayFifoNode->overlayId, overlayId, overlayIdLength);
  }

  coroutineExitCriticalSection();
  return overlayFifoNode;
}

/// @var fileCache
///
/// @brief Storage for an OverlayFifo to be used by
/// callOverlayFunctionFromFileCache.
OverlayFifo *fileCache = NULL;

/// @fn void* callOverlayFunctionFromFileCache(const void *od, const void *oo,
///   const char *function, void *args)
///
/// @brief Kernel function to load an overlay from a file into memory, find a
/// designated function, call it with the provided arguments, and return the
/// return value.
///
/// @param od The path to the overlay directory on the filesystem.  If this
///   parameter is OVERLAY_SAME_NAMESPACE then the overlay path currently in
///   use will be used.
/// @param oo The name of the overlay minus the ".overlay" file extension
///   that is local to the overlay directory.
/// @param function The name of the function exported by the overlay.
/// @param args Any arguments to be passed to the function in the overlay, cast
///   to a void*.  This parameter may be NULL.
///
/// @return Returns the value returned by the overlay function on success, NULL
/// on failure.
void* callOverlayFunctionFromFileCache(const void *od, const void *oo,
  const char *function, void *args
) {
  const char *overlayDir = (const char*) od;
  const char *overlay = (const char*) oo;
  void *returnValue = NULL;
  char *functionCopy = NULL;

  if ((overlay == NULL) || (function == NULL)) {
    logError("One or more NULL arguments provided to "
      "callOverlayFunctionFromFile\n");
    goto exit; // return NULL
  }
  
  ProcessDescriptor *runningProcess = getRunningProcess();
  if (runningProcess == NULL) {
    // This should be impossible.
    goto exit; // return NULL
  }
  
  // Keep track of the overlay that's currently running and the one we need.
  char *previousOverlayDir = (char*) runningProcess->overlayNamespace;

  // We need to allocate enough space for the two FileBlockMetadata objects and
  // all of the strings we need.  For the strings, we need the overlay
  // directory, a slash, the name of the overlay, the overlay extension, a NUL
  // byte, the function name, and a trailing NUL byte.
  char *allocation = (char*) malloc(
    (2 * sizeof(OverlayDescriptor))
    + ((overlayDir == OVERLAY_SAME_NAMESPACE)
      ? strlen(previousOverlayDir)
      : strlen(overlayDir)
    )
    + 1
    + strlen(overlay)
    + OVERLAY_EXT_LEN
    + 1
    + strlen(function)
    + 1);
  if (allocation == NULL) {
    goto exit;
  }
  OverlayDescriptor *overlayArray = (OverlayDescriptor*) allocation;
  char *overlayInfo = allocation + (2 * sizeof(OverlayDescriptor));

  overlayArray[0].overlayId
    = runningProcess->overlay.overlayId;
  overlayArray[0].overlayIdLength
    = runningProcess->overlay.overlayIdLength;
  overlayArray[0].fileBlockMetadata.blockDevice
    = runningProcess->overlay.fileBlockMetadata.blockDevice;
  overlayArray[0].fileBlockMetadata.startBlock
    = runningProcess->overlay.fileBlockMetadata.startBlock;
  overlayArray[0].fileBlockMetadata.numBlocks
    = runningProcess->overlay.fileBlockMetadata.numBlocks;
  overlayArray[0].memory
    = runningProcess->overlay.memory;

  strcpy(overlayInfo,
    (overlayDir == OVERLAY_SAME_NAMESPACE) ? previousOverlayDir : overlayDir);
  strcat(overlayInfo, _pathSeparator);
  strcat(overlayInfo, overlay);
  strcat(overlayInfo, _overlayExt);
  
  memset(&overlayArray[1], 0, sizeof(OverlayDescriptor));
  overlayArray[0].overlayId = overlayInfo;
  overlayArray[0].overlayIdLength = strlen(overlayInfo) + 1;
  OverlayFifoNode *overlayFifoNode = overlayFifoGet(
    fileCache, overlayArray[0].overlayId, overlayArray[0].overlayIdLength);
  if (overlayFifoNode != NULL) {
    overlayArray[1].memory = overlayFifoNode->overlay;
    goto loadAndCallFunction;
  }
  
  // Get the overlay information we need.
  if (getFileBlockMetadataFromPath(overlayInfo,
    &overlayArray[1].fileBlockMetadata) != 0
  ) {
    // We can't proceed
    goto freeOverlayInfo;
  }
  
loadAndCallFunction:
  functionCopy = overlayInfo + strlen(overlayInfo) + 1;
  strcpy(functionCopy, function);
  
  // Terminate the overlayInfo string at the end of the directory path.
  overlayInfo[strlen(
    (overlayDir == OVERLAY_SAME_NAMESPACE) ? previousOverlayDir : overlayDir)]
    = '\0';
  
  // args does not get copied.  We have no way of knowing what the proper way
  // to copy the data would be.  The caller is responsible for allocating data
  // in dynamic memory before the pointer is passed to this function.
  
  if (overlayDir != OVERLAY_SAME_NAMESPACE) {
    overlayDir = overlayInfo;
  }
  if (schedulerReplaceOverlay(overlayDir, &overlayArray[1]) != 0) {
    logError("Could not load file overlay via the scheduler\n");
    goto freeOverlayInfo;
  }
  if (overlayArray[1].memory == NULL) {
    overlayFifoNode = overlayFifoPush(
      fileCache, overlayInfo, strlen(overlayInfo) + 1, memoryManagerMalloc);
  }

  // If we made it this far, then our new overlay has been successuflly loaded.
  OverlayFunction overlayFunction = findOverlayFunction(functionCopy);
  if (overlayFunction == NULL) {
    logError("Could not find overlay function \"%s\"\n", functionCopy);
    goto restorePreviousOverlay;
  }
  
  // The overlay function was found.  Get our return value.
  returnValue = overlayFunction(args);
  
restorePreviousOverlay:
  // Don't check the return value here.  No point.
  schedulerReplaceOverlay(previousOverlayDir, &overlayArray[0]);

  // Release all the memory for the copies.
freeOverlayInfo:
  free(allocation);
exit:
  return returnValue;
}

