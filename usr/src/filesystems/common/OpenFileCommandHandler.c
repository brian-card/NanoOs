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
/// @brief Filesystem-driver-agnostic implementation of a FILESYSTEM_OPEN_FILE
/// command handler.  Shared verbatim by the kernel-linked, contiguous, and
/// overlay filesystem builds; calls the selected driver's driverFopen and
/// driverFclose directly, since which driver is linked in is a build-time
/// choice, not a runtime one.

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
void* driverFopen(void *driverState, const char *filePath, const char *mode,
    time_t now, int *errorNumber);
int driverFclose(void *driverState, void *fileHandle);

/// @def NO_DRIVER_ERRNO
///
/// @brief errno value used when no filesystem driver is linked into this
/// build at all (so there is no driver to even ask for a real error code).
///
/// @note This is a plain integer literal, not the symbolic ENODEV name, for
/// the same reason as in OpendirCommandHandler.c.  ***WARNING*** This value
/// has to match ENODEV in src/user/NanoOsErrno.h.
#define NO_DRIVER_ERRNO 14 // ENODEV

/// @def OUT_OF_MEMORY_ERRNO
///
/// @brief errno value used when the NanoOsFile for an opened file can't be
/// allocated.  ***WARNING*** This value has to match ENOMEM in
/// src/user/NanoOsErrno.h.
#define OUT_OF_MEMORY_ERRNO 3 // ENOMEM

/// @fn void* OpenFile(void *args)
///
/// @brief Command handler for an fopen call.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the returnValue member of the provided FilesystemFopenArgs
/// to a valid FILE pointer on success (with errorNumber set to 0), or sets
/// returnValue to NULL and errorNumber to the errno value the caller should
/// see on failure.  This function always returns the filesystemState pointer
/// provided as args.
void* OpenFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  NanoOsFile *nanoOsFile = NULL;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemFopenArgs *fopenArgs
    = (FilesystemFopenArgs*) processMessageData(processMessage);

  printDebugString("Opening file \"");
  printDebugString(fopenArgs->pathname);
  printDebugString("\" in mode \"");
  printDebugString(fopenArgs->mode);
  printDebugString("\"\n");

  int errorNumber = NO_DRIVER_ERRNO;
  if (filesystemState->driverState != NULL) {
    void *fileHandle = driverFopen(
      filesystemState->driverState,
      fopenArgs->pathname, fopenArgs->mode, fopenArgs->now, &errorNumber);
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
        driverFclose(filesystemState->driverState, fileHandle);
        errorNumber = OUT_OF_MEMORY_ERRNO;
      }
    }
  } else {
    printString("ERROR: driverState is not valid!\n");
  }

  fopenArgs->returnValue = nanoOsFile;
  fopenArgs->errorNumber = errorNumber;
  processMessageSetDone(processMessage);
  return filesystemState;
}
