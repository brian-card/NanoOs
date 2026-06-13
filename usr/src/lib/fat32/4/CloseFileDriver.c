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

/// @file CloseFileImplementation.c
///
/// @brief Overlay implementation of fclose for FAT32.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFat32.h"

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Close a previously-opened FAT32 file handle.
///
/// @details If the file was opened for writing, the directory entry on disk
///          is updated with the current file size and first-cluster fields
///          from the handle so that the on-disk metadata reflects any writes
///          that occurred while the file was open.  The handle and its
///          associated file-name buffer are then freed.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param fileHandle   Pointer to the Fat32FileHandle to close (passed as
///                     void*).
///
/// @return FAT32_SUCCESS on success, FAT32_INVALID_PARAMETER if either
///         argument is NULL, or FAT32_ERROR on an I/O failure while flushing
///         the directory entry.
///
int driverFclose(void *driverState, void *fileHandle) {
  Fat32DriverState *ds     = (Fat32DriverState *) driverState;
  Fat32FileHandle  *handle = (Fat32FileHandle *)  fileHandle;

  if ((ds == NULL) || (handle == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  int result = FAT32_SUCCESS;

  // If the file was writable, flush the authoritative size (and first-cluster
  // fields, which may have changed if the file was originally empty) back to
  // the on-disk directory entry.
  if (handle->canWrite) {
    FilesystemState    *fs = ds->filesystemState;
    BlockDevice *bd = fs->blockDevice;

    // Compute the sector that contains the short directory entry.
    uint32_t sectorIndex =
      handle->directoryOffset / ds->bytesPerSector;
    uint32_t offsetInSector =
      handle->directoryOffset % ds->bytesPerSector;
    uint32_t lba =
      fat32ClusterToLba(ds, handle->directoryCluster) + sectorIndex;

    // Read the sector, patch the entry, and write it back.
    int ioResult = bd->readBlocks(
      bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
    if (ioResult != 0) {
      result = FAT32_ERROR;
    } else {
      Fat32DirectoryEntry *entry =
        (Fat32DirectoryEntry *) (fs->blockBuffer + offsetInSector);

      uint32_t entryFileSize = handle->fileSize;
      uint16_t entryClusterHigh =
        (uint16_t) (handle->firstCluster >> 16);
      uint16_t entryClusterLow =
        (uint16_t) (handle->firstCluster & 0xFFFF);
      memcpy(&entry->fileSize, &entryFileSize, sizeof(uint32_t));
      memcpy(&entry->firstClusterHigh, &entryClusterHigh,
        sizeof(uint16_t));
      memcpy(&entry->firstClusterLow, &entryClusterLow,
        sizeof(uint16_t));

      ioResult = bd->writeBlocks(
        bd->context, lba, 1, bd->blockSize, fs->blockBuffer);
      if (ioResult != 0) {
        result = FAT32_ERROR;
      }
    }
  }

  // Free the handle regardless of whether the flush succeeded — the caller
  // must not use this handle again.
  free(handle->fileName);
  free(handle);

  return result;
}

