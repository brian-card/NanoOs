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

/// @file RemoveFileDriver.c
///
/// @brief Overlay implementation of remove for FAT32.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFat32.h"

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Remove (delete) a file from a FAT32 filesystem.
///
/// @details The full path is resolved to locate the file's parent directory
///          and file name.  If the target exists and is a regular file (not a
///          directory), its cluster chain is freed and the on-disk directory
///          entries — both the short (8.3) entry and any preceding LFN
///          entries — are marked as deleted (first byte set to 0xE5) so that
///          the space can be reclaimed by future allocations.
///
///          This function does not check whether the file is currently open.
///          The caller (typically the filesystem process) is responsible for
///          ensuring that no open handles reference the file being removed.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param pathname     The null-terminated path of the file to remove.
///
/// @return FAT32_SUCCESS on success, FAT32_INVALID_PARAMETER if either
///         argument is NULL or the target is a directory, FAT32_FILE_NOT_FOUND
///         if the file (or an intermediate directory) does not exist, or
///         FAT32_ERROR on an I/O failure.
///
int driverRemove(void *driverState, const char *pathname) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;

  if ((ds == NULL) || (pathname == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Resolve the parent directory and extract the file name ----
  uint32_t    parentCluster;
  const char *fileName = NULL;

  int result = fat32ResolveParentDirectory(
    ds, pathname, &parentCluster, &fileName);
  if (result != FAT32_SUCCESS) {
    return result;
  }

  if ((fileName == NULL) || (fileName[0] == '\0')) {
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Search for the file in the parent directory ----
  Fat32DirSearchResult searchResult;
  searchResult.longName = NULL;

  result = fat32SearchDirectory(
    ds, parentCluster, fileName, &searchResult);
  if (result != FAT32_SUCCESS) {
    free(searchResult.longName);
    return result;
  }

  // Directories must not be removed with this call (use a dedicated rmdir
  // operation if one is added later).
  if (searchResult.entry.attributes & FAT32_ATTR_DIRECTORY) {
    free(searchResult.longName);
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Free the file's cluster chain ----
  uint16_t removeClusterHigh;
  uint16_t removeClusterLow;
  memcpy(&removeClusterHigh, &searchResult.entry.firstClusterHigh,
    sizeof(uint16_t));
  memcpy(&removeClusterLow, &searchResult.entry.firstClusterLow,
    sizeof(uint16_t));
  uint32_t firstCluster =
    ((uint32_t) removeClusterHigh << 16)
    | (uint32_t) removeClusterLow;

  if (firstCluster >= FAT32_CLUSTER_FIRST_VALID) {
    result = fat32FreeClusterChain(ds, firstCluster);
    if (result != FAT32_SUCCESS) {
      free(searchResult.longName);
      return result;
    }
  }

  // ---- Invalidate the directory entries (LFN + short) ----
  result = fat32InvalidateDirectoryEntries(
    ds, parentCluster, &searchResult);

  free(searchResult.longName);
  return result;
}

