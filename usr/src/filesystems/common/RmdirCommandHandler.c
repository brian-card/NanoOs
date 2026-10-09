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

/// @file RmdirCommandHandler.c
///
/// @brief Filesystem-driver-agnostic implementation of a FILESYSTEM_RMDIR
/// command handler.  Shared verbatim by the kernel-linked, contiguous, and
/// overlay filesystem builds.

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
int driverRmdir(void *driverState, const char *pathname);

/// @def RMDIR_NO_DRIVER_ERRNO
///
/// @brief errno value used when no filesystem driver is linked into this
/// build.
///
/// ***WARNING*** This value has to match ENODEV in src/user/NanoOsErrno.h.
/// If you change the numbering there, you MUST update this value too!
#define RMDIR_NO_DRIVER_ERRNO 14 // ENODEV

/// @fn void* Rmdir(void *args)
///
/// @brief Command handler for a rmdir call.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the returnValue member of the provided FilesystemPathArgs to 0
/// on success or the negated errno value on failure.  This function always
/// returns the filesystemState pointer provided as args.
void* Rmdir(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemPathArgs *filesystemPathArgs
    = (FilesystemPathArgs*) processMessageData(processMessage);
  int returnValue = -RMDIR_NO_DRIVER_ERRNO;
  if (filesystemState->driverState != NULL) {
    returnValue = driverRmdir(
      filesystemState->driverState, filesystemPathArgs->pathname);
  }

  filesystemPathArgs->returnValue = returnValue;
  processMessageSetDone(processMessage);
  return filesystemState;
}
