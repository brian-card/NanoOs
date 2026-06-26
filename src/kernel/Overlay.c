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
#include "NanoOs.h"
#include "OverlayFunctions.h"
#include "../user/NanoOsLibC.h"
#include "Scheduler.h"
#include "Processes.h"

// Must come last
#include "../user/NanoOsStdio.h"

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
  
  NanoOsOverlayMap *overlayMap = HAL->memory->overlayMap;
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

/// @fn void* callOverlayFunctionFromFile(const void *od, const void *o,
///   const char *function, void *args)
///
/// @brief Kernel function to load an overlay from a file into memory, find a
/// designated function, call it with the provided arguments, and return the
/// return value.
///
/// @param od The path to the overlay directory on the filesystem.  If this
///   parameter is OVERLAY_SAME_NAMESPACE then the overlay path currently in
///   use will be used.
/// @param o The name of the overlay minus the ".overlay" file extension
///   that is local to the overlay directory.
/// @param function The name of the function exported by the overlay.
/// @param args Any arguments to be passed to the function in the overlay, cast
///   to a void*.  This parameter may be NULL.
///
/// @return Returns the value returned by the overlay function on success, NULL
/// on failure.
void* callOverlayFunctionFromFile(const void *od, const void *o,
  const char *function, void *args
) {
  const char *overlayDir = (const char*) od;
  const char *overlay = (const char*) o;
  void *returnValue = NULL;
  if ((overlay == NULL) || (function == NULL)) {
    fprintf(stderr, "ERROR: One or more NULL arguments provided to "
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
  FileBlockMetadata overlayArray[2];
  overlayArray[0].blockDevice = runningProcess->overlay.blockDevice;
  overlayArray[0].startBlock  = runningProcess->overlay.startBlock;
  overlayArray[0].numBlocks   = runningProcess->overlay.numBlocks;
  
  // We need to allocate enough space for all of the strings we need.  We need
  // the overlay directory, a slash, the name of the overlay, the overlay
  // extension, a NULL byte, the function name, and a trailing NULL byte.
  char *overlayInfo = (char*) malloc(
    ((overlayDir == OVERLAY_SAME_NAMESPACE)
      ? strlen(previousOverlayDir)
      : strlen(overlayDir)
    )
    + 1
    + strlen(overlay)
    + OVERLAY_EXT_LEN
    + 1
    + strlen(function)
    + 1);
  if (overlayInfo == NULL) {
    goto exit;
  }
  
  strcpy(overlayInfo,
    (overlayDir == OVERLAY_SAME_NAMESPACE) ? previousOverlayDir : overlayDir);
  strcat(overlayInfo, "/");
  strcat(overlayInfo, overlay);
  strcat(overlayInfo, OVERLAY_EXT);
  
  // Get the overlay information we need.
  if (getFileBlockMetadataFromPath(overlayInfo, &overlayArray[1]) != 0) {
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
    fprintf(stderr, "ERROR: Could not load file overlay via the scheduler\n");
    goto freeOverlayInfo;
  }

  // If we made it this far, then our new overlay has been successuflly loaded.
  OverlayFunction overlayFunction = findOverlayFunction(functionCopy);
  if (overlayFunction == NULL) {
    fprintf(stderr, "ERROR: Could not find overlay function \"%s\"\n",
      functionCopy);
    goto restorePreviousOverlay;
  }
  
  // The overlay function was found.  Get our return value.
  returnValue = overlayFunction(args);
  
restorePreviousOverlay:
  // Don't check the return value here.  No point.
  schedulerReplaceOverlay(previousOverlayDir, &overlayArray[0]);
  
  // Release all the memory for the copies.
freeOverlayInfo:
  free(overlayInfo);
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
    fprintf(stderr, "ERROR: One or more NULL arguments provided to "
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
  FileBlockMetadata overlayArray[2];
  overlayArray[0].blockDevice = runningProcess->overlay.blockDevice;
  overlayArray[0].startBlock  = runningProcess->overlay.startBlock;
  overlayArray[0].numBlocks   = runningProcess->overlay.numBlocks;
  
  overlayArray[1].blockDevice = runningProcess->overlay.blockDevice;
  if (deviceId != OVERLAY_SAME_NAMESPACE) {
    HAL->blockDevice->get(
      (int) ((intptr_t) deviceId), &overlayArray[1].blockDevice);
    if (overlayArray[1].blockDevice == NULL) {
      // No such block device.
      goto exit; // return NULL
    }
  }
  
  // The number of blocks in block-based overlays always have to be the same.
  // So, just copy what we're already using.
  overlayArray[1].numBlocks = runningProcess->overlay.numBlocks;
  overlayArray[1].startBlock
    = (((uintptr_t) overlay) * overlayArray[1].numBlocks) + 1;
  
  char *functionCopy = (char*) malloc(strlen(function) + 1);
  if (functionCopy == NULL) {
    // Out of memory.  Bail.
    goto exit; // return NULL
  }
  strcpy(functionCopy, function);
  
  if (schedulerReplaceOverlay(deviceId, &overlayArray[1]) != 0) {
    fprintf(stderr, "ERROR: Could not load block overlay via the scheduler\n");
    goto exit;
  }

  
  // If we made it this far, then our new overlay has been successuflly loaded.
  OverlayFunction overlayFunction = findOverlayFunction(functionCopy);
  if (overlayFunction == NULL) {
    fprintf(stderr, "ERROR: Could not find overlay function \"%s\"\n",
      functionCopy);
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

  OverlayFunction _start = findOverlayFunction("_start");
  if (_start == NULL) {
    fprintf(stderr,
      "Could not find exported _start function in \"%s\" overlay.\n",
      commandPath);
    return 1;
  }
  printDebugString("Found _start function\n");

  MainArgs mainArgs = {
    .argc = argc,
    .argv = argv,
  };
  printDebugString("Calling _start function at address 0x");
  printDebugHex((uintptr_t) _start);
  printDebugString("\n");
  int returnValue = (int) ((intptr_t) _start(&mainArgs));
  printDebugString("Got return value ");
  printDebugInt(returnValue);
  printDebugString(" from _start function\n");
  if ((returnValue < 0) || (returnValue > 255)) {
    // Invalid return value.
    returnValue = -EOTHER;
  }

  return returnValue;
}

