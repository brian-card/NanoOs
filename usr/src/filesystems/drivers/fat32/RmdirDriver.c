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

/// @file RmdirDriver.c
///
/// @brief Overlay implementation of rmdir for FAT32.

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
/// @brief Determine whether a directory holds anything besides its "." and
///        ".." entries.
///
/// @param ds            Pointer to an initialized Fat32DriverState.
/// @param firstCluster  The first cluster of the directory to check.
///
/// @return FAT32_SUCCESS if the directory is empty, FAT32_DIRECTORY_NOT_EMPTY
///         if it is not, or FAT32_ERROR on an I/O failure.
///
static int fat32CheckDirectoryEmpty(
    Fat32DriverState *ds,
    uint32_t firstCluster
) {
  FilesystemState *fs = ds->filesystemState;
  BlockDevice     *bd = fs->blockDevice;
  uint32_t entriesPerSector = ds->bytesPerSector / FAT32_DIRECTORY_ENTRY_SIZE;
  uint32_t currentCluster = firstCluster;

  while ((currentCluster >= FAT32_CLUSTER_FIRST_VALID)
    && (currentCluster < FAT32_CLUSTER_EOC_MIN)
  ) {
    uint32_t clusterLba = fat32ClusterToLba(ds, currentCluster);
    for (uint8_t sector = 0; sector < ds->sectorsPerCluster; sector++) {
      if (bd->readBlocks(bd->context, clusterLba + sector, 1,
        bd->blockSize, fs->blockBuffer) != 0
      ) {
        return FAT32_ERROR;
      }

      for (uint32_t ii = 0; ii < entriesPerSector; ii++) {
        Fat32DirectoryEntry *entry = (Fat32DirectoryEntry*)
          (fs->blockBuffer + (ii * FAT32_DIRECTORY_ENTRY_SIZE));
        if (entry->name[0] == FAT32_ENTRY_END_OF_DIR) {
          return FAT32_SUCCESS;
        }
        if ((entry->name[0] == FAT32_ENTRY_FREE)
          || ((entry->attributes & FAT32_ATTR_LONG_NAME)
            == FAT32_ATTR_LONG_NAME)
          || (entry->name[0] == FAT32_ENTRY_DOT)
        ) {
          continue;
        }
        return FAT32_DIRECTORY_NOT_EMPTY;
      }
    }

    uint32_t nextCluster;
    if (fat32ReadFatEntry(ds, currentCluster, &nextCluster) != FAT32_SUCCESS) {
      return FAT32_ERROR;
    }
    currentCluster = nextCluster;
  }

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Remove an empty directory from a FAT32 filesystem.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param pathname     The null-terminated path of the directory to remove.
///
/// @return 0 on success, or the negated errno value on failure.
///
int driverRmdir(void *driverState, const char *pathname) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;

  if ((ds == NULL) || (pathname == NULL)) {
    return -fat32ErrorToErrno(FAT32_INVALID_PARAMETER);
  }

  Fat32DirSearchResult searchResult;
  memset(&searchResult, 0, sizeof(searchResult));

  uint32_t    parentCluster = 0;
  const char *name = NULL;
  uint32_t    firstCluster = 0;

  int result = fat32ResolveEntryPath(ds, pathname, &parentCluster, &name);
  if (result == FAT32_SUCCESS) {
    result = fat32SearchDirectory(ds, parentCluster, name, &searchResult);
  }
  if ((result == FAT32_SUCCESS)
    && ((searchResult.entry.attributes & FAT32_ATTR_DIRECTORY) == 0)
  ) {
    result = FAT32_NOT_A_DIRECTORY;
  }
  if (result == FAT32_SUCCESS) {
    uint16_t clusterHigh;
    uint16_t clusterLow;
    memcpy(&clusterHigh, &searchResult.entry.firstClusterHigh,
      sizeof(uint16_t));
    memcpy(&clusterLow, &searchResult.entry.firstClusterLow,
      sizeof(uint16_t));
    firstCluster = ((uint32_t) clusterHigh << 16) | clusterLow;
    result = fat32CheckDirectoryEmpty(ds, firstCluster);
  }

  // The entry is removed before the clusters are freed so that an
  // interruption can leak the clusters but never leave an entry that refers
  // to free ones.
  if (result == FAT32_SUCCESS) {
    result = fat32InvalidateDirectoryEntries(ds, parentCluster, &searchResult);
  }
  if ((result == FAT32_SUCCESS) && (firstCluster >= FAT32_CLUSTER_FIRST_VALID)) {
    result = fat32FreeClusterChain(ds, firstCluster);
  }

  free(searchResult.longName);
  return -fat32ErrorToErrno(result);
}
