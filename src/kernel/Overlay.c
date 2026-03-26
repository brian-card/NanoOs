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
#include "Tasks.h"

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
  
  NanoOsOverlayMap *overlayMap = HAL->overlayMap;
  for (uint16_t ii = 0, jj = overlayMap->numExports - 1; ii <= jj;) {
    cur = (ii + jj) >> 1;
    comp = strcmp(overlayMap->exports[cur].name, overlayFunctionName);
    if (comp == 0) {
      overlayFunction = overlayMap->exports[cur].fn;
      break;
    } else if (comp < 0) { // cur < overlayFunctionName
      // Move the left bound to one greater than cur.
      ii = cur + 1;
    } else { // comp > 0, overlayFunctionName < cur
      // Move the right bound to one less than cur.
      jj = cur - 1;
    }
  }
  
  return overlayFunction;
}

/// @fn void* callOverlayFunction(const char *overlayDir, const char *overlay,
///   const char *function, void *args)
///
/// @brief Kernel function to load an overlay into memory, find a designated
/// function, call it with the provided arguments, and return the return value.
///
/// @param overlayDir The path to the overlay on the filesystem.  If this
///   parameter is NULL then this means the overlay path currently in use.
/// @param overlay The name of the overlay minus the ".overlay" file extension
///   that is local to getRunningTask()->overlayDir.
/// @param function The name of the function exported by the overlay.
/// @param args Any arguments to be passed to the function in the overlay, cast
///   to a void*.  This parameter may be NULL.
///
/// @return Returns the value returned by the overlay function on success, NULL
/// on failure.
void* callOverlayFunction(const char *overlayDir, const char *overlay,
  const char *function, void *args
) {
  void *returnValue = NULL;
  if ((overlay == NULL) || (function == NULL)) {
    fprintf(stderr,
      "ERROR: One or more NULL arguments provided to callOverlayFunction\n");
    return returnValue;
  }
  
  TaskDescriptor *runningTask = getRunningTask();
  if (runningTask == NULL) {
    // This should be impossible.
    return returnValue;
  }
  
  // Keep track of the overlay that's currently running.
  const char *previousOverlayDir = runningTask->overlayDir;
  const char *previousOverlay = runningTask->overlay;
  
  // We have to copy the arguments we were provided into dynamic memory because
  // they may be pointers into the current overlay, which we're about to
  // replace.
  char *overlayDirCopy = NULL;
  if (overlayDir != NULL) {
    overlayDirCopy = (char*) malloc(strlen(overlayDir) + 1);
    if (overlayDirCopy == NULL) {
      goto exit;
    }
    strcpy(overlayDirCopy, overlayDir);
  }
  char *overlayCopy = (char*) malloc(strlen(overlay) + 1);
  if (overlayCopy == NULL) {
    goto freeOverlayDir;
  }
  strcpy(overlayCopy, overlay);
  
  char *functionCopy = (char*) malloc(strlen(function) + 1);
  if (functionCopy == NULL) {
    goto freeOverlay;
  }
  strcpy(functionCopy, function);
  
  // args does not get copied.  We have no way of knowing what the proper way
  // to copy the data would be.  The caller is responsible for allocating data
  // in dynamic memory before the pointer is passed to this function.
  
  // We want to load the new overlay in place of the one that's currently
  // there, but we want to give other processes some time to run, too.  Also,
  // loading the overlay is an operation that shouldn't be interrupted.   The
  // scheduler will automatically load the correct overlay before a process is
  // resumed, so just set the pointers of the task's overlay and then yield.
  //
  // A few things of note:
  //
  // 1.  Because the scheduler will automatically load the correct overlay
  //     before a task is resumed, it's technically permissible to set the
  //     task's overlay pointers and then load the overlay, however, that's
  //     redundant.  It would *NOT* be permissible to load the overlay and then
  //     set the pointers as that could create an overlay of mixed content if
  //     loading the overlay was preempted.
  //
  // 2.  Setting the pointers for the overlay has to be an atomic operation.
  //     The reason it has to be atomic is because the value of the pointers
  //     are used by the scheduler, which is outside the context of this
  //     process.  There are two (2) pointers that have to be set:  The path
  //     to the overlay directory and the name of the overlay.  Because there
  //     are to operations that have to be done here, it's not sufficient to do
  //     an atomic store on a single pointer.  We need to make sure the
  //     preemption timer isn't running and then set both.  We don't have to
  //     re-enable the timer again after setting the pointers because we're
  //     going to then immediately yield after setting them.  So, we can get
  //     away with just calling cancelTimer instead of cancelAndGetTimer since
  //     there's nothing to resume.
  //
  // JBC 2025-01-24
  HAL->cancelTimer(SCHEDULER_STATE->preemptionTimer);
  if (overlayDirCopy != NULL) {
    runningTask->overlayDir = overlayDirCopy;
  }
  runningTask->overlay = overlayCopy;
  taskYield();
  
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
  // See note above on use of HAL->cancelTimer.
  HAL->cancelTimer(SCHEDULER_STATE->preemptionTimer);
  runningTask->overlay = previousOverlay;
  if (overlayDirCopy != NULL) {
    runningTask->overlayDir = previousOverlayDir;
  }
  taskYield();
  
  // Release all the memory for the copies.
  free(functionCopy);
freeOverlay:
  free(overlayCopy);
freeOverlayDir:
  free(overlayDirCopy);
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
  if (returnValue != ENOERR) {
    fprintf(stderr,
      "Got unexpected return value %d from _start in \"%s\"\n",
      returnValue, commandPath);
  }
  if ((returnValue < 0) || (returnValue > 255)) {
    // Invalid return value.
    returnValue = -EOTHER;
  }

  return returnValue;
}

