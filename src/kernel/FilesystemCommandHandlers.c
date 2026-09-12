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

/// @file FilesystemCommandHandlers.c
///
/// @brief Kernel-linked build's copy of the filesystem command handlers.
///
/// The logic that answers each FILESYSTEM_* message (FILESYSTEM_OPEN_FILE,
/// FILESYSTEM_CLOSE_FILE, etc.) lives once, under usr/src/filesystems/common/,
/// and is shared verbatim by the kernel-linked, contiguous, and overlay
/// filesystem builds.  The contiguous and overlay builds reach it by
/// symlinking those files into their own source trees; Arduino IDE only
/// discovers source files physically under src/kernel, so this file instead
/// pulls them in with #include.  See Fat32Filesystem.c for the equivalent
/// treatment of the FAT32 driver itself.

// Standard C includes
#include <errno.h>

// NanoOs includes
#include "MemoryManager.h"
#include "Filesystem.h"
#include "NanoOs.h"
#include "Processes.h"
#include "../user/NanoOsStdio.h"
#include "../user/NanoOsDebug.h"

#define NANO_OS_KERNEL_BUILD
#include "../../usr/src/filesystems/common/OpenFileCommandHandler.c"
#include "../../usr/src/filesystems/common/CloseFileCommandHandler.c"
#include "../../usr/src/filesystems/common/ReadFileCommandHandler.c"
#include "../../usr/src/filesystems/common/WriteFileCommandHandler.c"
#include "../../usr/src/filesystems/common/RemoveFileCommandHandler.c"
#include "../../usr/src/filesystems/common/SeekFileCommandHandler.c"
#include "../../usr/src/filesystems/common/DumpOpenFilesCommandHandler.c"
#include "../../usr/src/filesystems/common/GetFileBlockMetadataCommandHandler.c"
#include "../../usr/src/filesystems/common/EndOfFileCommandHandler.c"
#include "../../usr/src/filesystems/common/FormatCommandHandler.c"

/// @var fat32CommandHandlers
///
/// @brief Array of command handlers, indexed by FilesystemCommandResponse.
/// Exported (unlike the equivalent tables in contiguous/overlay's own main.c,
/// which are private to a single-driver binary) so that whichever
/// platform-specific HAL code creates the filesystem process (see
/// restartBuiltinFilesystem in HalCommon.c) can point a FilesystemState's
/// commandHandlers member at it, without Filesystem.c itself ever having to
/// name this -- or any other -- driver's handlers directly.
KEEP_IN_FLASH
const FilesystemCommandHandler fat32CommandHandlers[NUM_FILESYSTEM_COMMANDS] = {
  OpenFile,             // FILESYSTEM_OPEN_FILE
  CloseFile,            // FILESYSTEM_CLOSE_FILE
  ReadFile,             // FILESYSTEM_READ_FILE
  WriteFile,            // FILESYSTEM_WRITE_FILE
  RemoveFile,           // FILESYSTEM_REMOVE_FILE
  SeekFile,             // FILESYSTEM_SEEK_FILE
  DumpOpenFiles,        // FILESYSTEM_DUMP_OPEN_FILES
  GetFileBlockMetadata, // FILESYSTEM_GET_FILE_BLOCK_METADATA
  EndOfFile,            // FILESYSTEM_END_OF_FILE
  Format,               // FILESYSTEM_FORMAT
};

