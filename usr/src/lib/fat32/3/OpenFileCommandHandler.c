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

/// @file OpenFileCommandHandler.c
///
/// @brief Overlay implementation of an fopen command handler.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "../../filesystem/include/OverlayFilesystem.h"

// Prototypes used by this overlay.
void* fopenImplementation(
    void *driverState,
    const char *filePath,
    const char *mode);

/// @fn void* OpenFile(void *args)
///
/// @brief Overlay implementation of a FAT32 fopen function.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the returnValue member of the provided FilesystemFopenArgs
/// to a valid FILE pointer on success, sets it to NULL on failure.  This
/// function always returns the filesystemState pointer provided as args.
void* OpenFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  NanoOsFile *nanoOsFile = NULL;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemFopenArgs *fopenArgs
    = (FilesystemFopenArgs*) processMessageData(processMessage);

  printDebugString("Opening file \"");
  printDebugString(pathname);
  printDebugString("\" in mode \"");
  printDebugString(mode);
  printDebugString("\"\n");

  if (filesystemState->driverState != NULL) {
    void *fileHandle = fopenImplementation(
      filesystemState->driverState,
      fopenArgs->pathname, fopenArgs->mode);
    if (fileHandle != NULL) {
      nanoOsFile = (NanoOsFile*) malloc(sizeof(NanoOsFile));
      if (nanoOsFile != NULL) {
        nanoOsFile->file = fileHandle;
        nanoOsFile->currentPosition = 0;
        nanoOsFile->fd = fopenArgs->fd;
        nanoOsFile->owner = processPid(processMessageFrom(processMessage));
        filesystemState->numOpenFiles++;

        nanoOsFile->next = filesystemState->openFiles;
        nanoOsFile->prev = NULL;
        if (filesystemState->openFiles != NULL) {
          filesystemState->openFiles->prev = nanoOsFile;
        }
        filesystemState->openFiles = nanoOsFile;
      } else {
        filesystemState->driverFclose(filesystemState->driverState, fileHandle);
      }
    } else {
      printString("ERROR: driverFopen returned NULL\n");
    }
  } else {
    printString("ERROR: driverState is not valid!\n");
  }

  fopenArgs->returnValue = nanoOsFile;
  processMessageSetDone(processMessage);
  return filesystemState;
}

