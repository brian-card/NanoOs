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

/// @file Fat32Filesystem.c
///
/// @brief Kernel-linked build's entry point into the FAT32 driver.
///
/// There are three ways NanoOs can run a filesystem: linked directly into
/// the kernel binary (this file; used by targets too small to spare a
/// process/overlay for it, e.g. the Mega 2560), as a single contiguous
/// overlay process (usr/src/filesystems/contiguous/), or as a set of
/// individually-loaded block overlays (usr/src/filesystems/overlay/).  All
/// three run the exact same FAT32 driver logic, which lives once under
/// usr/src/filesystems/drivers/fat32/.  The contiguous and overlay builds
/// reach it by symlinking those files into their own source trees; Arduino
/// IDE only discovers source files physically under src/kernel, so this
/// file instead pulls them in with #include.  Each driver file exports a
/// generic driverXxx symbol (driverFopen, driverFclose, etc.) rather than a
/// FAT32-specific name, since which concrete filesystem backs a
/// FilesystemState is a build-time choice (which driver source is linked
/// in), not a runtime one.

// Standard C includes
#include <string.h>

// NanoOs includes
#include "MemoryManager.h"
#include "Fat32Filesystem.h"
#include "../user/NanoOsStdio.h"
#include "../user/NanoOsDebug.h"

#include "../../usr/src/filesystems/drivers/fat32/FilesystemInitDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/OpenFileDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/CloseFileDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/ReadFileDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/WriteFileDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/RemoveFileDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/SeekFileDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/GetFileBlockMetadataDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/GetFilenameDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/EndOfFileDriver.c"
#include "../../usr/src/filesystems/drivers/fat32/FormatDriver.c"

