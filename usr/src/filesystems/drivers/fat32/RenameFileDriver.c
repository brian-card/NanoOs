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

/// @file RenameFileDriver.c
///
/// @brief Overlay implementation of rename for FAT32.

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
/// @brief Determine whether any open file handle refers to a directory entry.
///
/// @param ds     Pointer to an initialized Fat32DriverState.
/// @param entry  The directory search result for the entry to check.
///
/// @return true if an open file's handle refers to the entry, false otherwise.
///
static bool fat32EntryIsOpen(
    Fat32DriverState *ds,
    const Fat32DirSearchResult *entry
) {
  for (NanoOsFile *nanoOsFile = ds->filesystemState->openFiles;
    nanoOsFile != NULL;
    nanoOsFile = nanoOsFile->next
  ) {
    Fat32FileHandle *handle = (Fat32FileHandle*) nanoOsFile->file;
    if ((handle != NULL)
      && (handle->directoryCluster == entry->dirCluster)
      && (handle->directoryOffset == entry->offsetInCluster)
    ) {
      return true;
    }
  }

  return false;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Rename a file or directory on a FAT32 filesystem, replacing an
///        existing regular file at the new path.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param oldpath      The null-terminated current path of the file.
/// @param newpath      The null-terminated path the file is to have.
///
/// @return 0 on success, or the negated errno value on failure.
///
int driverRename(void *driverState, const char *oldpath, const char *newpath) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;

  if ((ds == NULL) || (oldpath == NULL) || (newpath == NULL)) {
    return -fat32ErrorToErrno(FAT32_INVALID_PARAMETER);
  }

  Fat32DirSearchResult oldEntry;
  Fat32DirSearchResult newEntry;
  Fat32DirSearchResult createdEntry;
  memset(&oldEntry, 0, sizeof(oldEntry));
  memset(&newEntry, 0, sizeof(newEntry));
  memset(&createdEntry, 0, sizeof(createdEntry));

  uint32_t    oldParentCluster = 0;
  uint32_t    newParentCluster = 0;
  const char *oldName = NULL;
  const char *newName = NULL;
  bool        targetExists = false;

  int result = fat32ResolveEntryPath(
    ds, oldpath, &oldParentCluster, &oldName);
  if (result == FAT32_SUCCESS) {
    result = fat32SearchDirectory(ds, oldParentCluster, oldName, &oldEntry);
  }
  if (result == FAT32_SUCCESS) {
    result = fat32ResolveEntryPath(
      ds, newpath, &newParentCluster, &newName);
  }
  if (result == FAT32_SUCCESS) {
    result = fat32SearchDirectory(ds, newParentCluster, newName, &newEntry);
    if (result == FAT32_SUCCESS) {
      targetExists = true;
    } else if (result == FAT32_FILE_NOT_FOUND) {
      result = FAT32_SUCCESS;
    }
  }

  bool oldIsDirectory
    = (oldEntry.entry.attributes & FAT32_ATTR_DIRECTORY) != 0;
  if ((result == FAT32_SUCCESS) && oldIsDirectory
    && (newParentCluster != oldParentCluster)
  ) {
    // Moving a directory to a new parent would also require rewriting its
    // ".." entry, which is not supported.
    result = FAT32_INVALID_PARAMETER;
  }

  // A case-only rename finds the source again as the target.
  bool targetIsSource = targetExists
    && (newEntry.dirCluster == oldEntry.dirCluster)
    && (newEntry.offsetInCluster == oldEntry.offsetInCluster);

  if ((result == FAT32_SUCCESS) && targetExists && !targetIsSource) {
    if (oldIsDirectory
      || ((newEntry.entry.attributes & FAT32_ATTR_DIRECTORY) != 0)
    ) {
      result = FAT32_INVALID_PARAMETER;
    } else if (fat32EntryIsOpen(ds, &newEntry)) {
      result = FAT32_TOO_MANY_OPEN_FILES;
    }
  }

  if ((result == FAT32_SUCCESS) && fat32EntryIsOpen(ds, &oldEntry)) {
    // An open handle writes its size back to the entry's old location on
    // close.
    result = FAT32_TOO_MANY_OPEN_FILES;
  }

  if ((result == FAT32_SUCCESS) && targetExists && !targetIsSource) {
    uint16_t clusterHigh;
    uint16_t clusterLow;
    memcpy(&clusterHigh, &newEntry.entry.firstClusterHigh, sizeof(uint16_t));
    memcpy(&clusterLow, &newEntry.entry.firstClusterLow, sizeof(uint16_t));
    uint32_t firstCluster = ((uint32_t) clusterHigh << 16) | clusterLow;
    if (firstCluster >= FAT32_CLUSTER_FIRST_VALID) {
      result = fat32FreeClusterChain(ds, firstCluster);
    }
    if (result == FAT32_SUCCESS) {
      result = fat32InvalidateDirectoryEntries(
        ds, newParentCluster, &newEntry);
    }
  }

  // The new entry is written before the old one is invalidated so that an
  // interruption leaves the file reachable.
  if (result == FAT32_SUCCESS) {
    result = fat32CreateFileEntry(ds, newParentCluster, newName,
      &oldEntry.entry, &createdEntry);
  }
  if (result == FAT32_SUCCESS) {
    result = fat32InvalidateDirectoryEntries(
      ds, oldParentCluster, &oldEntry);
  }

  free(createdEntry.longName);
  free(newEntry.longName);
  free(oldEntry.longName);
  return -fat32ErrorToErrno(result);
}
