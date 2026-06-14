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

/// @file SeekFileCommandHandler.c
///
/// @brief Overlay implementation of an fseek command handler.

// Standard C includes
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "../../filesystem/include/OverlayFilesystem.h"

// Prototypes used by this overlay.
int driverFseek(
    void *driverState,
    void *fileHandle,
    long offset,
    int whence);

/// @fn void* SeekFile(void *args)
///
/// @brief Overlay implementation of an fseek command handler.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the returnValue and errorNumber values of the provided
/// FilesystemSeekArgs to the values that are to be used by the calling process.
/// This function always returns the filesystemState pointer provided as args.
void* SeekFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemSeekArgs *filesystemSeekArgs
    = (FilesystemSeekArgs*) processMessageData(processMessage);
  int returnValue = 0;
  if (filesystemState->driverState != NULL) {
    NanoOsFile *nanoOsFile = filesystemSeekArgs->stream;
    errno = 0;
    returnValue = driverFseek(
      filesystemState->driverState, nanoOsFile->file,
      filesystemSeekArgs->offset,
      filesystemSeekArgs->whence);
    if (returnValue >= 0) {
      nanoOsFile->currentPosition = returnValue;
    }
  }

  filesystemSeekArgs->returnValue = returnValue;
  filesystemSeekArgs->errorNumber = errno;
  processMessageSetDone(processMessage);
  return filesystemState;
}

