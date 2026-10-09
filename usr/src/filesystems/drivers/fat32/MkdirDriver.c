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

/// @file MkdirDriver.c
///
/// @brief Overlay implementation of mkdir for FAT32.

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
/// @brief Write an empty directory into a newly-allocated cluster:  every
///        sector zeroed, with the "." and ".." entries at the start.
///
/// @param ds             Pointer to an initialized Fat32DriverState.
/// @param parentCluster  The first cluster of the new directory's parent.
/// @param dirEntry       The new directory's own entry, whose attributes,
///                       timestamps, and cluster the "." and ".." entries
///                       are made from.
///
/// @return FAT32_SUCCESS on success, FAT32_ERROR on an I/O failure.
///
static int fat32WriteEmptyDirectory(
    Fat32DriverState *ds,
    uint32_t parentCluster,
    const Fat32DirectoryEntry *dirEntry
) {
  FilesystemState *fs = ds->filesystemState;
  BlockDevice     *bd = fs->blockDevice;
  uint16_t clusterHigh;
  uint16_t clusterLow;
  memcpy(&clusterHigh, &dirEntry->firstClusterHigh, sizeof(uint16_t));
  memcpy(&clusterLow, &dirEntry->firstClusterLow, sizeof(uint16_t));
  uint32_t lba = fat32ClusterToLba(ds,
    ((uint32_t) clusterHigh << 16) | clusterLow);

  for (uint8_t sector = 0; sector < ds->sectorsPerCluster; sector++) {
    memset(fs->blockBuffer, 0, ds->bytesPerSector);
    if (sector == 0) {
      Fat32DirectoryEntry entry = *dirEntry;
      memset(entry.name, ' ', FAT32_SHORT_NAME_LENGTH);
      entry.name[0] = '.';
      memcpy(fs->blockBuffer, &entry, sizeof(entry));

      // A ".." entry that refers to the root directory records cluster 0.
      if (parentCluster == ds->rootDirectoryCluster) {
        parentCluster = 0;
      }
      entry.name[1] = '.';
      clusterHigh = (uint16_t) (parentCluster >> 16);
      clusterLow = (uint16_t) (parentCluster & 0xFFFF);
      memcpy(&entry.firstClusterHigh, &clusterHigh, sizeof(uint16_t));
      memcpy(&entry.firstClusterLow, &clusterLow, sizeof(uint16_t));
      memcpy(fs->blockBuffer + FAT32_DIRECTORY_ENTRY_SIZE,
        &entry, sizeof(entry));
    }

    if (bd->writeBlocks(bd->context, lba + sector, 1,
      bd->blockSize, fs->blockBuffer) != 0
    ) {
      return FAT32_ERROR;
    }
  }

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Create an empty directory on a FAT32 filesystem.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param pathname     The null-terminated path of the directory to create.
/// @param now          The creation time, in seconds since the Unix epoch.
///
/// @return 0 on success, or the negated errno value on failure.
///
int driverMkdir(void *driverState, const char *pathname, time_t now) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;

  if ((ds == NULL) || (pathname == NULL)) {
    return -fat32ErrorToErrno(FAT32_INVALID_PARAMETER);
  }

  Fat32DirSearchResult existingEntry;
  Fat32DirSearchResult createdEntry;
  memset(&existingEntry, 0, sizeof(existingEntry));
  memset(&createdEntry, 0, sizeof(createdEntry));

  uint32_t    parentCluster = 0;
  const char *name = NULL;
  uint32_t    cluster = 0;

  int result = fat32ResolveEntryPath(ds, pathname, &parentCluster, &name);
  if (result == FAT32_SUCCESS) {
    result = fat32SearchDirectory(ds, parentCluster, name, &existingEntry);
    if (result == FAT32_SUCCESS) {
      result = FAT32_FILE_EXISTS;
    } else if (result == FAT32_FILE_NOT_FOUND) {
      result = FAT32_SUCCESS;
    }
  }

  // The directory's contents are written before its entry is created so that
  // the entry never refers to an uninitialized cluster.
  if (result == FAT32_SUCCESS) {
    result = fat32AllocateCluster(ds, 0, &cluster);
  }
  Fat32DirectoryEntry entry;
  fat32InitNewEntry(&entry, FAT32_ATTR_DIRECTORY, now);
  uint16_t clusterHigh = (uint16_t) (cluster >> 16);
  uint16_t clusterLow = (uint16_t) (cluster & 0xFFFF);
  memcpy(&entry.firstClusterHigh, &clusterHigh, sizeof(uint16_t));
  memcpy(&entry.firstClusterLow, &clusterLow, sizeof(uint16_t));
  if (result == FAT32_SUCCESS) {
    result = fat32WriteEmptyDirectory(ds, parentCluster, &entry);
  }
  if (result == FAT32_SUCCESS) {
    result = fat32CreateFileEntry(
      ds, parentCluster, name, &entry, &createdEntry);
  }
  if ((result != FAT32_SUCCESS) && (cluster >= FAT32_CLUSTER_FIRST_VALID)) {
    fat32FreeClusterChain(ds, cluster);
  }

  free(createdEntry.longName);
  free(existingEntry.longName);
  return -fat32ErrorToErrno(result);
}
