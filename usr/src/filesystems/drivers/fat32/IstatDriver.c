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

/// @file IstatDriver.c
///
/// @brief Overlay implementation of istat for FAT32.

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
/// @brief Populate a struct stat directly from an inode number, on a FAT32
///        filesystem.
///
/// @details fat32EntryLocationToIno packs an inode number as
///          (LBA << 8) | slot: this reverses that, reads the one sector at
///          that LBA, and reads the entry at that slot directly -- no path
///          walk, no directory search, matching exactly what driverLstat
///          does once it has already found the same entry itself. See
///          fat32PopulateStat for the shared population logic and
///          driverLstat's own doc comment for why st_uid/st_gid are left at
///          sentinel values here.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param ino          An inode number previously returned in st_ino/d_ino
///                      by lstat/readdir on this same filesystem.
/// @param statbuf      Pointer to the caller-supplied struct stat to
///                      populate.
/// @param errorNumber  [out] Set to the errno value the caller should see
///                     on failure (0 on success).  Must not be NULL.
///
/// @return 0 on success, or -1 if driverState/statbuf is NULL, ino no
///         longer refers to a real short directory entry (e.g. the file
///         was deleted, or ino was never valid to begin with), or an I/O
///         error occurred.
///
int driverIstat(
    void *driverState, ino_t ino, struct stat *statbuf, int *errorNumber
) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;

  if ((ds == NULL) || (statbuf == NULL)) {
    *errorNumber = fat32ErrorToErrno(FAT32_INVALID_PARAMETER);
    return -1;
  }

  memset(statbuf, 0, sizeof(*statbuf));

  if (ino == 0) {
    // The root-directory sentinel (see driverLstat): root has no directory
    // entry of its own to read back.
    statbuf->st_ino = 0;
    // No FAT32_ATTR_READ_ONLY to check here either -- the root directory is
    // never read-only, so this is the same permission bits fat32PopulateStat
    // would compute for any other read-write directory.
    statbuf->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
    *errorNumber = 0;
    return 0;
  }

  uint32_t lba = (uint32_t) (ino >> 8);
  uint32_t slot = (uint32_t) (ino & 0xFF);
  uint32_t entriesPerSector = ds->bytesPerSector / FAT32_DIRECTORY_ENTRY_SIZE;
  if (slot >= entriesPerSector) {
    // Not a value fat32EntryLocationToIno could ever have produced for this
    // volume's sector size -- reject it rather than reading past the end of
    // the sector buffer below.
    *errorNumber = fat32ErrorToErrno(FAT32_INVALID_PARAMETER);
    return -1;
  }

  FilesystemState *fs = ds->filesystemState;
  BlockDevice     *bd = fs->blockDevice;
  int ioResult = bd->readBlocks(bd->context, lba, 1, bd->blockSize,
    fs->blockBuffer);
  if (ioResult != 0) {
    *errorNumber = fat32ErrorToErrno(FAT32_ERROR);
    return -1;
  }

  Fat32DirectoryEntry *entry = (Fat32DirectoryEntry *)
    (fs->blockBuffer + (slot * FAT32_DIRECTORY_ENTRY_SIZE));

  if ((entry->name[0] == FAT32_ENTRY_FREE)
      || (entry->name[0] == FAT32_ENTRY_END_OF_DIR)
      || ((entry->attributes & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME)
      || (entry->attributes & FAT32_ATTR_VOLUME_ID)
  ) {
    // The inode is stale (the file has since been deleted or its entry
    // relocated) or was never a real short entry's location to begin with.
    // Report it exactly like a path-based lookup reports a missing file.
    *errorNumber = fat32ErrorToErrno(FAT32_FILE_NOT_FOUND);
    return -1;
  }

  fat32PopulateStat(ds, entry, ino, statbuf);

  *errorNumber = 0;
  return 0;
}
