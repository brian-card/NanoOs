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

/// @file SeekFileDriver.c
///
/// @brief Overlay implementation of fseek for FAT32.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFat32.h"

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Reposition the file-position indicator of a previously-opened FAT32
///        file.
///
/// @details The new absolute position is computed from @p offset and @p whence
///          using the standard C semantics:
///
///          SEEK_SET — offset bytes from the beginning of the file.
///          SEEK_CUR — offset bytes from the current position.
///          SEEK_END — offset bytes from the end of the file.
///
///          The resulting position is validated against the range
///          [0, fileSize]; seeking before the start of the file or past
///          end-of-file is rejected with FAT32_INVALID_PARAMETER.
///
///          After the position is updated the cluster chain is walked from
///          firstCluster so that currentCluster points to the cluster
///          containing the target byte.  When the new position falls exactly
///          on a cluster boundary at end-of-file, currentCluster is set to
///          the last allocated cluster and the read/write loops are relied
///          upon to advance the chain at the start of their next iteration.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param fileHandle   Pointer to the Fat32FileHandle (passed as void*).
/// @param offset       Signed byte offset relative to @p whence.
/// @param whence       One of SEEK_SET, SEEK_CUR, or SEEK_END.
///
/// @return FAT32_SUCCESS on success, FAT32_INVALID_PARAMETER if an argument
///         is NULL, whence is unrecognised, or the computed position is out of
///         range, or FAT32_ERROR on an I/O failure while walking the cluster
///         chain.
///
int driverFseek(
    void *driverState,
    void *fileHandle,
    long offset,
    int whence
) {
  Fat32DriverState *ds     = (Fat32DriverState *) driverState;
  Fat32FileHandle  *handle = (Fat32FileHandle *)  fileHandle;

  if ((ds == NULL) || (handle == NULL)) {
    return FAT32_INVALID_PARAMETER;
  }

  // ---- Determine the base for the offset ----
  long base;
  switch (whence) {
    case SEEK_SET: base = 0;                              break;
    case SEEK_CUR: base = (long) handle->currentPosition; break;
    case SEEK_END: base = (long) handle->fileSize;        break;
    default:       return FAT32_INVALID_PARAMETER;
  }

  long newPosition = base + offset;

  // Reject positions outside [0, fileSize].
  if (newPosition < 0) {
    return FAT32_INVALID_PARAMETER;
  }
  if ((uint32_t) newPosition > handle->fileSize) {
    return FAT32_INVALID_PARAMETER;
  }

  uint32_t pos = (uint32_t) newPosition;
  handle->currentPosition = pos;

  // ---- Synchronise currentCluster with the new position ----
  //
  // If the file has no clusters (empty and never written to) or the new
  // position is zero, reset to firstCluster — no chain walk needed.
  if ((pos == 0)
      || (handle->firstCluster < FAT32_CLUSTER_FIRST_VALID)
  ) {
    handle->currentCluster = handle->firstCluster;
  } else {
    // Compute how many cluster links to follow from firstCluster.
    //
    // When the target position sits exactly at end-of-file AND falls on a
    // cluster boundary, using (fileSize - 1) avoids following the chain
    // past the last allocated cluster.  The read and write loops handle
    // the boundary transition at the start of their next iteration.
    uint32_t clustersToSkip;
    if (pos >= handle->fileSize) {
      clustersToSkip = (handle->fileSize - 1) / ds->bytesPerCluster;
    } else {
      clustersToSkip = pos / ds->bytesPerCluster;
    }

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

  return FAT32_SUCCESS;
}

