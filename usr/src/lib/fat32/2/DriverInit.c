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

/// @file DriverInit.c
///
/// @brief Overlay implementation of DriverInit for the FAT32 filesystem driver.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFat32.h"

void* DriverInit(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  BlockDevice *blockDevice = filesystemState->blockDevice;

  // Read the partition's boot sector into the pre-allocated block buffer.
  int returnValue = blockDevice->readBlocks(
    blockDevice->context,
    filesystemState->startLba,
    1,
    blockDevice->blockSize,
    filesystemState->blockBuffer);
  if (returnValue != 0) {
    filesystemState->args = ((void*) FAT32_ERROR);
    return filesystemState;
  }

  Fat32BiosParameterBlock *bpb =
    (Fat32BiosParameterBlock *) filesystemState->blockBuffer;

  // --- Copy multi-byte BPB fields into aligned local variables ---
  uint16_t bpbSignatureWord;
  uint16_t bpbFatSize16;
  uint32_t bpbFatSize32;
  uint16_t bpbRootEntryCount;
  uint16_t bpbBytesPerSector;
  uint16_t bpbReservedSectorCount;
  uint32_t bpbRootCluster;
  uint16_t bpbFsInfoSector;
  uint32_t bpbTotalSectors32;
  uint16_t bpbTotalSectors16;

  memcpy(&bpbSignatureWord,      &bpb->signatureWord,      sizeof(uint16_t));
  memcpy(&bpbFatSize16,          &bpb->fatSize16,          sizeof(uint16_t));
  memcpy(&bpbFatSize32,          &bpb->fatSize32,          sizeof(uint32_t));
  memcpy(&bpbRootEntryCount,     &bpb->rootEntryCount,     sizeof(uint16_t));
  memcpy(&bpbBytesPerSector,     &bpb->bytesPerSector,     sizeof(uint16_t));
  memcpy(&bpbReservedSectorCount,&bpb->reservedSectorCount,sizeof(uint16_t));
  memcpy(&bpbRootCluster,        &bpb->rootCluster,        sizeof(uint32_t));
  memcpy(&bpbFsInfoSector,       &bpb->fsInfoSector,       sizeof(uint16_t));
  memcpy(&bpbTotalSectors32,     &bpb->totalSectors32,     sizeof(uint32_t));
  memcpy(&bpbTotalSectors16,     &bpb->totalSectors16,     sizeof(uint16_t));

  // --- Validate the BPB ---
  if (bpbSignatureWord != 0xAA55) {
    filesystemState->args = ((void*) FAT32_INVALID_FILESYSTEM);
    return filesystemState;
  }
  if ((bpbFatSize16 != 0)
      || (bpbFatSize32 == 0)
      || (bpbRootEntryCount != 0)
  ) {
    filesystemState->args = ((void*) FAT32_INVALID_FILESYSTEM);
    return filesystemState;
  }
  if ((bpbBytesPerSector < FAT32_SECTOR_SIZE)
      || (bpb->sectorsPerCluster == 0)
      || ((bpb->sectorsPerCluster & (bpb->sectorsPerCluster - 1)) != 0)
  ) {
    filesystemState->args = ((void*) FAT32_INVALID_FILESYSTEM);
    return filesystemState;
  }

  // --- Allocate and populate the driver state ---
  Fat32DriverState *driverState =
    (Fat32DriverState *) calloc(1, sizeof(Fat32DriverState));
  if (driverState == NULL) {
    filesystemState->args = ((void*) FAT32_NO_MEMORY);
    return filesystemState;
  }

  driverState->filesystemState     = filesystemState;
  driverState->bytesPerSector      = bpbBytesPerSector;
  driverState->sectorsPerCluster   = bpb->sectorsPerCluster;
  driverState->bytesPerCluster     =
    (uint32_t) bpb->sectorsPerCluster * (uint32_t) bpbBytesPerSector;
  driverState->reservedSectorCount = bpbReservedSectorCount;
  driverState->numberOfFats        = bpb->numberOfFats;
  driverState->fatSizeInSectors    = bpbFatSize32;
  driverState->rootDirectoryCluster = bpbRootCluster;
  driverState->fsInfoSector        = bpbFsInfoSector;

  driverState->fatStartSector =
    filesystemState->startLba + bpbReservedSectorCount;

  driverState->dataStartSector = driverState->fatStartSector
    + ((uint32_t) bpb->numberOfFats * bpbFatSize32);

  uint32_t totalSectors = (bpbTotalSectors32 != 0)
    ? bpbTotalSectors32
    : (uint32_t) bpbTotalSectors16;
  uint32_t dataSectors = totalSectors
    - (driverState->dataStartSector - filesystemState->startLba);
  driverState->totalDataClusters =
    dataSectors / bpb->sectorsPerCluster;

  filesystemState->driverState = driverState;

  filesystemState->args = ((void*) FAT32_SUCCESS);
  return filesystemState;
}

