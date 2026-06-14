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

/// @file ReadFileDriver.c
///
/// @brief Overlay implementation of fread for FAT32.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFat32.h"

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Read data from a previously-opened FAT32 file.
///
/// @details Starting at the handle's current position, up to @p length bytes
///          are copied into the caller-supplied buffer.  The read is clamped
///          at end-of-file so that it never overruns the recorded file size.
///          The handle's currentPosition and currentCluster fields are updated
///          to reflect the new position after the read completes.
///
///          Reading is performed one sector at a time using the shared
///          blockBuffer; the cluster chain is followed transparently when the
///          current position crosses a cluster boundary.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param ptr          Destination buffer; must be at least @p length bytes.
/// @param length       Maximum number of bytes to read.
/// @param fileHandle   Pointer to the Fat32FileHandle (passed as void*).
///
/// @return The number of bytes actually read (>= 0), or a negative FAT32
///         error code on failure.
///
int32_t driverFread(
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

  if (!handle->canRead) {
    return FAT32_INVALID_PARAMETER;
  }

  // Clamp the request to the bytes remaining between the current position and
  // end-of-file so that we never read past the logical file boundary.
  if (handle->currentPosition >= handle->fileSize) {
    return 0;
  }

  uint32_t remaining = handle->fileSize - handle->currentPosition;
  if (length > remaining) {
    length = remaining;
  }

  if (length == 0) {
    return 0;
  }

  FilesystemState    *fs = ds->filesystemState;
  BlockDevice *bd = fs->blockDevice;

  uint8_t  *dest      = (uint8_t *) ptr;
  uint32_t  totalRead = 0;

  while (totalRead < length) {
    // ---- Determine the current cluster ----
    // If currentCluster is zero the file was created empty and never had a
    // cluster allocated; there is nothing to read.
    if (handle->currentCluster < FAT32_CLUSTER_FIRST_VALID) {
      break;
    }

    // Byte offset within the current cluster.
    uint32_t offsetInCluster =
      handle->currentPosition % ds->bytesPerCluster;

    // Sector index within the cluster and byte offset within that sector.
    uint32_t sectorInCluster = offsetInCluster / ds->bytesPerSector;
    uint32_t offsetInSector  = offsetInCluster % ds->bytesPerSector;

    // Absolute sector on the block device.
    uint32_t lba =
      fat32ClusterToLba(ds, handle->currentCluster) + sectorInCluster;

    // Read one sector into the shared block buffer.
    int ioResult = bd->readBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      return (totalRead > 0) ? (int32_t) totalRead : FAT32_ERROR;
    }

    // How many usable bytes remain in this sector?
    uint32_t bytesLeftInSector = ds->bytesPerSector - offsetInSector;

    // How many bytes to copy from this sector: the minimum of the sector
    // remainder and the overall request remainder.
    uint32_t toCopy = length - totalRead;
    if (toCopy > bytesLeftInSector) {
      toCopy = bytesLeftInSector;
    }

    memcpy(dest + totalRead, fs->blockBuffer + offsetInSector, toCopy);

    totalRead                += toCopy;
    handle->currentPosition  += toCopy;

    // ---- Advance to the next cluster if we have reached the boundary ----
    if (((handle->currentPosition % ds->bytesPerCluster) == 0)
        && (totalRead < length)
    ) {
      uint32_t nextCluster;
      int fatResult =
        fat32ReadFatEntry(ds, handle->currentCluster, &nextCluster);
      if (fatResult != FAT32_SUCCESS) {
        return (totalRead > 0) ? (int32_t) totalRead : FAT32_ERROR;
      }

      // End-of-chain before the file size is reached — the FAT is
      // inconsistent, but return what we have rather than corrupting memory.
      if (nextCluster >= FAT32_CLUSTER_EOC_MIN) {
        break;
      }

      handle->currentCluster = nextCluster;
    }
  }

  return (int32_t) totalRead;
}

