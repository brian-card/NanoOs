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

/// @file FormatCommandHandler.c
///
/// @brief Filesystem-driver-agnostic implementation of a FILESYSTEM_FORMAT
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
int driverFormat(
    FilesystemState *filesystemState,
    const char *volumeLabel, uint32_t clusterSize);

/// @fn void* Format(void *args)
///
/// @brief Command handler for a format call.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the returnValue member of the provided Fat32FormatArgs to the
/// value that is to be used by the calling process, i.e. FAT32_SUCCESS on
/// success and a FAT32 error code on failure.  This function always returns
/// the filesystemState pointer provided as args.  A payload whose signature
/// isn't FAT32_FORMAT_SIGNATURE -- built for some other driver's format
/// command -- is refused outright, since this build only ever links one
/// driver and has no way to route it anywhere else.  Formatting a filesystem
/// that is currently mounted (i.e. has open files) is refused too; the
/// caller is expected to unmount first.
void* Format(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  Fat32FormatArgs *formatArgs
    = (Fat32FormatArgs*) processMessageData(processMessage);
  int returnValue = -1;

  if (formatArgs->signature != FAT32_FORMAT_SIGNATURE) {
    printString(
      "ERROR: Format arguments have the wrong signature; refusing\n");
  } else if (filesystemState->numOpenFiles == 0) {
    returnValue = driverFormat(
      filesystemState, formatArgs->volumeLabel, formatArgs->clusterSize);
  } else {
    printString("ERROR: Refusing to format a filesystem with open files\n");
  }

  formatArgs->returnValue = returnValue;
  processMessageSetDone(processMessage);
  return filesystemState;
}
