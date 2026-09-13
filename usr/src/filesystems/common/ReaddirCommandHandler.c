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

/// @file ReaddirCommandHandler.c
///
/// @brief Filesystem-driver-agnostic implementation of a FILESYSTEM_READ_DIR
/// command handler.  Shared verbatim by the kernel-linked, contiguous, and
/// overlay filesystem builds; calls the selected driver's driverReaddir
/// directly, since which driver is linked in is a build-time choice, not a
/// runtime one.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#ifndef NANO_OS_KERNEL_BUILD
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"
#include "FilesystemUtils.h"
#endif // NANO_OS_KERNEL_BUILD

// Prototype used by this handler.
struct dirent* driverReaddir(
  void *driverState, void *dirHandle, int *errorNumber);

/// @def NO_DRIVER_ERRNO
///
/// @brief errno value used when no filesystem driver is linked into this
/// build at all (so there is no driver to even ask for a real error code).
///
/// @note This is a plain integer literal, not the symbolic ENODEV name: this
/// file is compiled together (via direct #include, in all three filesystem
/// build shapes) with code that already includes the toolchain's own
/// <errno.h> for unrelated reasons, and that collides with
/// src/user/NanoOsErrno.h's own, differently-numbered version of that name.
/// ***WARNING*** This value has to match ENODEV in src/user/NanoOsErrno.h.
/// If you change the numbering there, you MUST update this value too!
#define NO_DRIVER_ERRNO 14 // ENODEV

/// @fn void* Readdir(void *args)
///
/// @brief Command handler for a readdir call.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the returnValue member of the provided FilesystemReaddirArgs
/// to a pointer to the next directory entry, with errorNumber set to 0; or
/// to NULL at the end of the directory (errorNumber 0 -- not an error) or on
/// failure (errorNumber set to the errno value the caller should see).  This
/// function always returns the filesystemState pointer provided as args.
void* Readdir(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemReaddirArgs *readdirArgs
    = (FilesystemReaddirArgs*) processMessageData(processMessage);

  struct dirent *entry = NULL;
  int errorNumber = NO_DRIVER_ERRNO;
  if (filesystemState->driverState != NULL) {
    entry = driverReaddir(
      filesystemState->driverState, readdirArgs->dirp, &errorNumber);
  }

  readdirArgs->returnValue = entry;
  readdirArgs->errorNumber = errorNumber;
  processMessageSetDone(processMessage);
  return filesystemState;
}
