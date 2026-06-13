///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              06.11.2026
///
/// @file              Filesystem.h
///
/// @brief             Definitions in support of the filesystem overlay driver.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef OVERLAY_FILESYSTEM_H
#define OVERLAY_FILESYSTEM_H

#include "../../../../../src/kernel/Filesystem.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define GET_PARTITION_INFO_OVERLAY  blockOverlayId(1)
#define DRIVER_INIT_OVERLAY         blockOverlayId(2)
#define OPEN_FILE_OVERLAY           blockOverlayId(3)
#define CLOSE_FILE_OVERLAY          blockOverlayId(4)
#define READ_FILE_OVERLAY           blockOverlayId(5)
#define WRITE_FILE_OVERLAY          blockOverlayId(6)
#define REMOVE_FILE_OVERLAY         blockOverlayId(7)
#define SEEK_FILE_OVERLAY           blockOverlayId(8)
#define DUMP_OPEN_FILES_OVERLAY     blockOverlayId(9)
#define GET_FILE_BLOCK_META_OVERLAY blockOverlayId(10)
#define FIRST_FS_OVERLAY_ID         11

/// @def readBytes
///
/// @brief Read a value from a memory address that may be unaligned.
///
/// @param dst A pointer to the destination memory.  This is expected to be a
///   multi-byte type.
/// @param src A pointer to the source memory.
#define readBytes(dst, src) memcpy(dst, src, sizeof(*(dst)))

/// @def writeBytes
///
/// @brief Write a value to a memory address that may be unaligned.
///
/// @param dst A pointer to the destination memory.
/// @param src A pointer to the source memory.  This is expected to be a
///   multi-byte type.
#define writeBytes(dst, src) memcpy(dst, src, sizeof(*(src)))

#ifdef __cplusplus
}
#endif

#endif // OVERLAY_FILESYSTEM_H

