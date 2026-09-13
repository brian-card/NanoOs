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

/// @file ClosedirDriver.c
///
/// @brief Overlay implementation of closedir for FAT32.

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
/// @brief Close a previously-opened FAT32 directory stream.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param dirHandle    Pointer to the Fat32DirHandle to close (passed as
///                     void*).
///
/// @return FAT32_SUCCESS on success, or FAT32_INVALID_PARAMETER if either
///         argument is NULL.
///
int driverClosedir(void *driverState, void *dirHandle) {
  Fat32DriverState *ds     = (Fat32DriverState *) driverState;
  Fat32DirHandle   *handle = (Fat32DirHandle *)   dirHandle;

  if ((ds == NULL) || (handle == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  free(handle->entry);
  free(handle);

  return FAT32_SUCCESS;
}
