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

/// @file OpendirDriver.c
///
/// @brief Overlay implementation of opendir for FAT32.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#ifndef NANO_OS_KERNEL_BUILD
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"
#endif // NANO_OS_KERNEL_BUILD
#include "Fat32.h"

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Open a directory stream on a FAT32 filesystem.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param path         The null-terminated path of the directory to open.
///
/// @return A pointer to a heap-allocated Fat32DirHandle on success, or NULL
///         if either argument is NULL, the path does not exist, or the path
///         names a regular file rather than a directory.
///
void* driverOpendir(void *driverState, const char *path) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;

  if ((ds == NULL) || (path == NULL)) {
    return NULL;
  }

  uint32_t dirCluster;
  if (fat32ResolveDirectory(ds, path, &dirCluster) != FAT32_SUCCESS) {
    return NULL;
  }

  Fat32DirHandle *handle
    = (Fat32DirHandle *) malloc(sizeof(Fat32DirHandle));
  if (handle == NULL) {
    return NULL;
  }

  handle->currentCluster = dirCluster;
  handle->offsetInCluster = 0;
  handle->nextSequence = 0;
  handle->entry = NULL;

  return (void *) handle;
}
