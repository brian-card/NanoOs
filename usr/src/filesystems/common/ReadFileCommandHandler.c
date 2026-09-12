////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file ReadFileCommandHandler.c
///
/// @brief Filesystem-driver-agnostic implementation of a
/// FILESYSTEM_READ_FILE command handler.  Shared verbatim by the
/// kernel-linked, contiguous, and overlay filesystem builds.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#ifndef NANO_OS_KERNEL_BUILD
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"
#include "FilesystemUtils.h"
#endif // NANO_OS_KERNEL_BUILD

// Prototypes used by this handler.
int driverFread(
    void *driverState, void *ptr, uint32_t length, void *fileHandle);

/// @fn void* ReadFile(void *args)
///
/// @brief Command handler for an fread call.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the length member of the provided FilesystemIoCommandArgs
/// to the number of bytes successfully read.  This function always returns
/// the filesystemState pointer provided as args.
void* ReadFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemIoCommandArgs *filesystemIoCommandArgs
    = (FilesystemIoCommandArgs*) processMessageData(processMessage);
  int returnValue = 0;
  if (filesystemState->driverState != NULL) {
    uint32_t length = filesystemIoCommandArgs->length;
    if (length > (uint32_t) (~0u >> 1)) {
      // Clamp to the platform INT_MAX: driverFread/driverFwrite return an int.
      length = (uint32_t) (~0u >> 1);
    }
    NanoOsFile *nanoOsFile = filesystemIoCommandArgs->file;
    returnValue = driverFread(filesystemState->driverState,
      filesystemIoCommandArgs->buffer, length, nanoOsFile->file);
    if (returnValue >= 0) {
      // Return value is the number of bytes read.  Set the length variable to
      // it and set it to 0 to indicate good status.
      nanoOsFile->currentPosition += returnValue;
      filesystemIoCommandArgs->length = returnValue;
    } else {
      // Return value is a negative error code.
      // Tell the caller that we read nothing.
      filesystemIoCommandArgs->length = 0;
    }
  }

  processMessageSetDone(processMessage);
  return filesystemState;
}
