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

/// @file CreateFileDriver.c
///
/// @brief Creation of a new, empty file for FAT32.  Kept apart from fopen's
/// driver so that each fits in an overlay on its own.

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
/// @brief Create a new, empty regular file in a directory.
///
/// @param args  Pointer to a Fat32CreateFileArgs (passed as void*), whose
///              result and returnValue are populated.
///
/// @return The args pointer provided.
///
void* Fat32CreateFile(void *args) {
  Fat32CreateFileArgs *createArgs = (Fat32CreateFileArgs*) args;
  Fat32DirectoryEntry entry;
  fat32InitNewEntry(&entry, FAT32_ATTR_ARCHIVE, createArgs->now);
  createArgs->returnValue = fat32CreateFileEntry(createArgs->ds,
    createArgs->parentCluster, createArgs->fileName, &entry,
    createArgs->result);
  return args;
}
