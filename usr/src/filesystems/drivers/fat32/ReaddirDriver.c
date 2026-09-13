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

/// @file ReaddirDriver.c
///
/// @brief Overlay implementation of readdir for FAT32.

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
/// @brief Read the next entry from an open FAT32 directory stream.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param dirHandle    Pointer to the Fat32DirHandle to read from (passed as
///                     void*).
/// @param errorNumber  [out] Set to 0 when the directory was simply
///                     exhausted (not an error -- the caller must not
///                     change its own errno in that case) or on success,
///                     or to the errno value the caller should see on a
///                     real I/O/allocation/argument error.  Must not be
///                     NULL.
///
/// @return A pointer to a struct dirent describing the next entry, valid
///         until the next call to driverReaddir or driverClosedir on the
///         same handle; or NULL if either argument is NULL, the directory is
///         exhausted, or an I/O or allocation error occurred.
///
struct dirent* driverReaddir(
    void *driverState, void *dirHandle, int *errorNumber
) {
  Fat32DriverState *ds     = (Fat32DriverState *) driverState;
  Fat32DirHandle   *handle = (Fat32DirHandle *)   dirHandle;

  if ((ds == NULL) || (handle == NULL)) {
    *errorNumber = fat32ErrorToErrno(FAT32_INVALID_PARAMETER);
    return NULL;
  }

  int result = fat32ReadDirectoryEntry(ds, handle);
  if (result != FAT32_SUCCESS) {
    // FAT32_FILE_NOT_FOUND here just means the directory is exhausted --
    // a normal, non-error condition the caller's errno must not change for.
    *errorNumber =
      (result == FAT32_FILE_NOT_FOUND) ? 0 : fat32ErrorToErrno(result);
    return NULL;
  }

  *errorNumber = 0;
  return handle->entry;
}
