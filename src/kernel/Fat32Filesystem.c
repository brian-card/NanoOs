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

/// @file Fat32Filesystem.c
///
/// @brief Low-level implementation of the FAT32 driver.

// Standard C includes

// NanoOs includes
#include "Fat32Filesystem.h"

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Read the boot sector from the partition's first LBA into the
///        filesystem's pre-allocated block buffer.
///
/// @param filesystemState A pointer to an initialized FilesystemState whose
///   startLba, blockDevice, and blockBuffer are already valid.
///
/// @return FAT32_SUCCESS on success, FAT32_ERROR on a read failure.
///
int fat32ReadBootSector(FilesystemState *filesystemState) {
  BlockStorageDevice *blockDevice = filesystemState->blockDevice;

  int returnValue = blockDevice->readBlocks(
    blockDevice->context,
    filesystemState->startLba,
    1,
    blockDevice->blockSize,
    filesystemState->blockBuffer
  );

  return (returnValue == 0) ? FAT32_SUCCESS : FAT32_ERROR;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Validate that the buffer contains a plausible FAT32 BPB.
///
/// @details Checks the boot signature word (0xAA55), confirms that the
///          FAT16-era fatSize16 field is zero, that the FAT32 fatSize32 field
///          is non-zero, that the root entry count is zero (as required by
///          FAT32), that bytes per sector is at least the minimum cluster size,
///          and that sectors per cluster is non-zero.
///
/// @param bpb A pointer to a Fat32BiosParameterBlock to validate.
///
/// @return FAT32_SUCCESS if the BPB looks valid, FAT32_INVALID_FILESYSTEM
///   otherwise.
///
int fat32ValidateBpb(const Fat32BiosParameterBlock *bpb) {
  if (bpb->signatureWord != 0xAA55) {
    return FAT32_INVALID_FILESYSTEM;
  }

  if ((bpb->fatSize16 != 0)
      || (bpb->fatSize32 == 0)
      || (bpb->rootEntryCount != 0)
  ) {
    return FAT32_INVALID_FILESYSTEM;
  }

  if ((bpb->bytesPerSector < FAT32_CLUSTER_SIZE_MIN)
      || (bpb->sectorsPerCluster == 0)
  ) {
    return FAT32_INVALID_FILESYSTEM;
  }

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Allocate a Fat32DriverState and populate it from a validated BPB.
///
/// @param filesystemState A pointer to the FilesystemState that owns this
///   driver.  Used to anchor the absolute LBA calculations against startLba.
/// @param bpb A pointer to a validated Fat32BiosParameterBlock.
///
/// @return A pointer to a fully-populated Fat32DriverState on success, or
///   NULL if the allocation failed.
///
Fat32DriverState* fat32CreateDriverState(
    FilesystemState *filesystemState,
    const Fat32BiosParameterBlock *bpb) {
  Fat32DriverState *driverState =
    (Fat32DriverState *) calloc(1, sizeof(Fat32DriverState));
  if (driverState == NULL) {
    return NULL;
  }

  driverState->filesystemState     = filesystemState;
  driverState->bytesPerSector      = bpb->bytesPerSector;
  driverState->sectorsPerCluster   = bpb->sectorsPerCluster;
  driverState->bytesPerCluster     =
    (uint32_t) bpb->sectorsPerCluster * (uint32_t) bpb->bytesPerSector;
  driverState->reservedSectorCount = bpb->reservedSectorCount;
  driverState->numberOfFats        = bpb->numberOfFats;
  driverState->fatSizeInSectors    = bpb->fatSize32;
  driverState->rootDirectoryCluster = bpb->rootCluster;
  driverState->fsInfoSector        = bpb->fsInfoSector;

  // The FAT region begins immediately after the reserved sectors.
  driverState->fatStartSector =
    filesystemState->startLba + bpb->reservedSectorCount;

  // The data region begins after all copies of the FAT.
  driverState->dataStartSector = driverState->fatStartSector
    + ((uint32_t) bpb->numberOfFats * bpb->fatSize32);

  // Compute the total number of usable data clusters.
  uint32_t totalSectors = (bpb->totalSectors32 != 0)
    ? bpb->totalSectors32
    : (uint32_t) bpb->totalSectors16;
  uint32_t dataSectors = totalSectors
    - (driverState->dataStartSector - filesystemState->startLba);
  driverState->totalDataClusters = dataSectors / bpb->sectorsPerCluster;

  return driverState;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Initialize the FAT32 filesystem driver.
///
/// @details Reads and validates the BIOS Parameter Block (BPB) from the
///          partition's boot sector, allocates and populates a
///          Fat32DriverState, and installs it in the FilesystemState.  The
///          caller must have already called getPartitionInfo so that startLba
///          and endLba are correct, and must have allocated blockBuffer to at
///          least blockSize bytes.
///
/// @param filesystemState A pointer to a FilesystemState whose blockDevice,
///   startLba, endLba, blockBuffer, and blockSize fields are already valid.
///
/// @return FAT32_SUCCESS on success, or one of the FAT32 error codes on
///   failure.
///
int fat32Initialize(FilesystemState *filesystemState) {
  // Read the boot sector into the pre-allocated block buffer.
  int returnValue = fat32ReadBootSector(filesystemState);
  if (returnValue != FAT32_SUCCESS) {
    return returnValue;
  }

  Fat32BiosParameterBlock *bpb =
    (Fat32BiosParameterBlock *) filesystemState->blockBuffer;

  returnValue = fat32ValidateBpb(bpb);
  if (returnValue != FAT32_SUCCESS) {
    return returnValue;
  }

  Fat32DriverState *driverState = fat32CreateDriverState(filesystemState, bpb);
  if (driverState == NULL) {
    return FAT32_NO_MEMORY;
  }

  filesystemState->driverState = driverState;

  return FAT32_SUCCESS;
}

