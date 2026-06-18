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

/// @file GetFileBlockMetaDriver.c
///
/// @brief Overlay implementation of driverGetFileBlockMetadata for FAT32.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFat32.h"

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Retrieve the block-level location and extent of an open file.
///
/// @details The LBA (Logical Block Address) of the file's first data sector
///          is written to @p startBlock and the number of sectors required to
///          hold the file's contents (rounded up from the file size) is
///          written to @p numBlocks.  Both values describe the file identified
///          by @p fileHandle, not the filesystem partition as a whole.
///
///          For an empty file (zero bytes, no allocated clusters) both outputs
///          are set to zero.
///
/// @note    The sector count is derived from the logical file size.  If the
///          file's cluster chain is fragmented the sectors beyond the first
///          cluster are not necessarily contiguous from @p startBlock.
///          Callers that intend to perform raw block reads spanning the full
///          file should verify contiguity or fall back to the filesystem's
///          normal read path.
///
/// @param ds          Pointer to a Fat32DriverState (passed as void*).
/// @param fileHandle  Pointer to the Fat32FileHandle (passed as void*).
/// @param startBlock  [out] Receives the LBA of the first sector of the
///                    file's first cluster.
/// @param numBlocks   [out] Receives the total number of sectors the file
///                    occupies.
///
/// @return FAT32_SUCCESS on success, or FAT32_INVALID_PARAMETER if any
///         argument is NULL.
///
int driverGetFileBlockMetadata(
    void *ds,
    void *fileHandle,
    uint32_t *startBlock,
    uint32_t *numBlocks
) {
  Fat32DriverState *driverState = (Fat32DriverState *) ds;
  Fat32FileHandle  *handle      = (Fat32FileHandle *)  fileHandle;

  if ((driverState == NULL) || (handle == NULL)
      || (startBlock == NULL) || (numBlocks == NULL)
  ) {
    return FAT32_INVALID_PARAMETER;
  }

  // An empty file that has never been written to has no allocated clusters.
  if (handle->firstCluster < FAT32_CLUSTER_FIRST_VALID) {
    *startBlock = 0;
    *numBlocks  = 0;
    return FAT32_SUCCESS;
  }

  *startBlock = fat32ClusterToLba(driverState, handle->firstCluster);

  if (handle->fileSize == 0) {
    *numBlocks = 0;
  } else {
    // Round up to a whole number of sectors.
    *numBlocks = (handle->fileSize + driverState->bytesPerSector - 1)
      / driverState->bytesPerSector;
  }

  return FAT32_SUCCESS;
}

