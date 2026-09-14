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

/// @file LstatDriver.c
///
/// @brief Overlay implementation of lstat for FAT32.

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
/// @brief Populate a struct stat for a path on a FAT32 filesystem.
///
/// @details FAT32 has no concept of file ownership or Unix-style permission
///          bits, so st_uid and st_gid are always set to the
///          FILESYSTEM_UID_UNKNOWN/FILESYSTEM_GID_UNKNOWN sentinels (and the
///          permission bits of st_mode are left clear) rather than guessed
///          at here: it's the driver-agnostic FILESYSTEM_LSTAT command
///          handler's job, not this driver's, to decide what a filesystem
///          that doesn't track ownership should report -- see
///          usr/src/filesystems/common/LstatCommandHandler.c.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param path         The null-terminated path to stat.
/// @param statbuf      Pointer to the caller-supplied struct stat to
///                      populate.
/// @param errorNumber  [out] Set to the errno value the caller should see
///                     on failure (0 on success).  Must not be NULL.
///
/// @return 0 on success, or -1 if any argument is NULL, the path does not
///         exist, or an I/O error occurred.
///
int driverLstat(
    void *driverState, const char *path, struct stat *statbuf,
    int *errorNumber
) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;

  if ((ds == NULL) || (path == NULL) || (statbuf == NULL)) {
    *errorNumber = fat32ErrorToErrno(FAT32_INVALID_PARAMETER);
    return -1;
  }

  memset(statbuf, 0, sizeof(*statbuf));
  statbuf->st_uid = FILESYSTEM_UID_UNKNOWN;
  statbuf->st_gid = FILESYSTEM_GID_UNKNOWN;
  statbuf->st_nlink = 1;
  statbuf->st_blksize = (blksize_t) ds->bytesPerCluster;

  uint32_t    parentCluster;
  const char *nameComponent = NULL;
  int result = fat32ResolveParentDirectory(
    ds, path, &parentCluster, &nameComponent);
  if (result != FAT32_SUCCESS) {
    *errorNumber = fat32ErrorToErrno(result);
    return -1;
  }

  if ((nameComponent == NULL) || (nameComponent[0] == '\0')) {
    // The path names the root directory itself, which -- unlike every other
    // directory on a FAT32 volume -- has no directory entry of its own to
    // read attributes, a size, or timestamps from, and so no on-disk
    // location for fat32EntryLocationToIno to derive an inode number from.
    // 0 is never a real fat32EntryLocationToIno result (the lowest possible
    // LBA it can ever compute from is the start of the data region, well
    // past 0), so it's a safe, unambiguous sentinel for "the root" here --
    // matching the convention real inode-based filesystems already use 0
    // for (e.g. ext2 never issues inode 0 to a real file).
    statbuf->st_ino = (ino_t) 0;
    statbuf->st_mode = S_IFDIR;
    *errorNumber = 0;
    return 0;
  }

  Fat32DirSearchResult searchResult;
  searchResult.longName = NULL;
  result = fat32SearchDirectory(ds, parentCluster, nameComponent, &searchResult);
  if (result != FAT32_SUCCESS) {
    free(searchResult.longName);
    *errorNumber = fat32ErrorToErrno(result);
    return -1;
  }
  free(searchResult.longName);

  ino_t ino = fat32EntryLocationToIno(
    ds, searchResult.dirCluster, searchResult.offsetInCluster);
  fat32PopulateStat(ds, &searchResult.entry, ino, statbuf);

  *errorNumber = 0;
  return 0;
}
