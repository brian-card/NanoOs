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

/// @file WriteFileDriver.c
///
/// @brief Overlay implementation of fwrite for FAT32.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFat32.h"

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Write data to a previously-opened FAT32 file.
///
/// @details Starting at the handle's current position (or at end-of-file when
///          the file was opened in append mode), up to @p length bytes are
///          written from the caller-supplied buffer to disk.  New clusters are
///          allocated as needed when the write extends beyond the current
///          allocation.  Partial-sector writes are performed as read-modify-
///          write cycles so that surrounding data within the sector is
///          preserved.
///
///          The handle's currentPosition, currentCluster, firstCluster, and
///          fileSize fields are updated to reflect the state after the write.
///          The on-disk directory entry is NOT flushed here; that is deferred
///          to fat32Fclose so that multiple writes do not each incur the
///          overhead of a directory-entry update.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param ptr          Source buffer containing the data to write.
/// @param length       Number of bytes to write.
/// @param fileHandle   Pointer to the Fat32FileHandle (passed as void*).
///
/// @return The number of bytes actually written (>= 0), or a negative FAT32
///         error code on failure.
///
int32_t driverFwrite(
    void *driverState,
    void *ptr,
    uint32_t length,
    void *fileHandle
) {
  Fat32DriverState *ds     = (Fat32DriverState *) driverState;
  Fat32FileHandle  *handle = (Fat32FileHandle *)  fileHandle;

  if ((ds == NULL) || (handle == NULL) || (ptr == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  if (!handle->canWrite) {
    return FAT32_INVALID_PARAMETER;
  }

  if (length == 0) {
    return 0;
  }

  // In append mode every write begins at the current end-of-file.  Skip
  // the seek if we are already positioned there to avoid an unnecessary
  // cluster-chain walk.
  if (handle->appendMode && (handle->currentPosition != handle->fileSize)) {
    handle->currentPosition = handle->fileSize;

    // Ensure currentCluster points to the last cluster of the file (or
    // stays at firstCluster if the file is still empty).
    if (handle->fileSize > 0) {
      // Walk from firstCluster to the cluster that contains the last byte.
      uint32_t clustersToSkip =
        (handle->fileSize - 1) / ds->bytesPerCluster;
      uint32_t cluster = handle->firstCluster;
      for (uint32_t i = 0; i < clustersToSkip; i++) {
        uint32_t next;
        if (fat32ReadFatEntry(ds, cluster, &next) != FAT32_SUCCESS) {
          return FAT32_ERROR;
        }
        if (next >= FAT32_CLUSTER_EOC_MIN) {
          break;
        }
        cluster = next;
      }
      handle->currentCluster = cluster;
    }
  }

  FilesystemState    *fs = ds->filesystemState;
  BlockDevice *bd = fs->blockDevice;

  const uint8_t *src          = (const uint8_t *) ptr;
  uint32_t       totalWritten = 0;

  while (totalWritten < length) {
    // ---- Ensure we have a cluster to write into ----
    if (handle->currentCluster < FAT32_CLUSTER_FIRST_VALID) {
      // The file has no clusters yet — allocate the first one.
      uint32_t newCluster;
      int allocResult = fat32AllocateCluster(ds, 0, &newCluster);
      if (allocResult != FAT32_SUCCESS) {
        return (totalWritten > 0) ? (int32_t) totalWritten : allocResult;
      }
      handle->firstCluster   = newCluster;
      handle->currentCluster = newCluster;
    }

    // If we are exactly on a cluster boundary and still have data to write,
    // we need the next cluster in the chain (or a freshly-allocated one).
    if ((handle->currentPosition != 0)
        && ((handle->currentPosition % ds->bytesPerCluster) == 0)
    ) {
      uint32_t nextCluster;
      int fatResult =
        fat32ReadFatEntry(ds, handle->currentCluster, &nextCluster);
      if (fatResult != FAT32_SUCCESS) {
        return (totalWritten > 0) ? (int32_t) totalWritten : FAT32_ERROR;
      }

      if (nextCluster >= FAT32_CLUSTER_EOC_MIN
          || nextCluster < FAT32_CLUSTER_FIRST_VALID) {
        // End of chain — extend with a new cluster.
        fatResult = fat32AllocateCluster(
          ds, handle->currentCluster, &nextCluster);
        if (fatResult != FAT32_SUCCESS) {
          return (totalWritten > 0) ? (int32_t) totalWritten : fatResult;
        }
      }

      handle->currentCluster = nextCluster;
    }

    // ---- Compute the sector and offset within the current cluster ----
    uint32_t offsetInCluster =
      handle->currentPosition % ds->bytesPerCluster;
    uint32_t sectorInCluster = offsetInCluster / ds->bytesPerSector;
    uint32_t offsetInSector  = offsetInCluster % ds->bytesPerSector;

    uint32_t lba =
      fat32ClusterToLba(ds, handle->currentCluster) + sectorInCluster;

    // Determine how many bytes to write within this sector.
    uint32_t bytesLeftInSector = ds->bytesPerSector - offsetInSector;
    uint32_t toCopy = length - totalWritten;
    if (toCopy > bytesLeftInSector) {
      toCopy = bytesLeftInSector;
    }

    // If the write does not cover the entire sector we must preserve the
    // existing content by reading the sector first.
    if ((offsetInSector != 0) || (toCopy < ds->bytesPerSector)) {
      int ioResult = bd->readBlocks(
        bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
      if (ioResult != 0) {
        return (totalWritten > 0) ? (int32_t) totalWritten : FAT32_ERROR;
      }
    }

    // Copy the caller's data into the block buffer and write the sector.
    memcpy(fs->blockBuffer + offsetInSector, src + totalWritten, toCopy);

    int ioResult = bd->writeBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      return (totalWritten > 0) ? (int32_t) totalWritten : FAT32_ERROR;
    }

    totalWritten             += toCopy;
    handle->currentPosition  += toCopy;

    // Extend the logical file size if we have written past the old end.
    if (handle->currentPosition > handle->fileSize) {
      handle->fileSize = handle->currentPosition;
    }
  }

  return (int32_t) totalWritten;
}

