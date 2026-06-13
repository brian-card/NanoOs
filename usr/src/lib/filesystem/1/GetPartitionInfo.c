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

/// @file GetPartitionInfo.c
///
/// @brief Overlay implementation of GetPartitionInfo.

// Partition table constants
#define PARTITION_TABLE_OFFSET 0x1BE
#define PARTITION_ENTRY_SIZE 16

#define PARTITION_TYPE_FAT16_LBA 0x0E
#define PARTITION_TYPE_FAT16_LBA_EXTENDED 0x1E
#define PARTITION_TYPE_FAT32_LBA 0x0C
#define PARTITION_TYPE_LINUX 0x83

#define PARTITION_LBA_OFFSET 8
#define PARTITION_SECTORS_OFFSET 12

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFilesystem.h"

void* GetPartitionInfo(void *args) {
  FilesystemState *fs = (FilesystemState*) args;
  if (fs->blockDevice->partitionNumber == 0) {
    printDebugString("getPartitionInfo: Partition number is 0\n");
    fs->args = ((void*) -1);
    return fs;
  }
  
  printDebugString("getPartitionInfo: Reading block 0\n");
  if (fs->blockDevice->readBlocks(fs->blockDevice->context, 0, 1,
      fs->blockSize, fs->blockBuffer) != 0
  ) {
    printDebugString("getPartitionInfo: Failed to read block 0\n");
    fs->args = ((void*) -2);
    return fs;
  }
  printDebugString("getPartitionInfo: Got block 0\n");
  
  uint8_t *partitionTable = fs->blockBuffer + PARTITION_TABLE_OFFSET;
  uint8_t *entry
    = partitionTable
    + ((fs->blockDevice->partitionNumber - 1)
    * PARTITION_ENTRY_SIZE);
  uint8_t type = entry[4];
  
  if ((type == PARTITION_TYPE_FAT16_LBA)
    || (type == PARTITION_TYPE_FAT16_LBA_EXTENDED)
    || (type == PARTITION_TYPE_FAT32_LBA)
    || (type == PARTITION_TYPE_LINUX)
  ) {
    uint32_t lbaValue, sectorsValue;
    
    // Read LBA offset using readBytes for alignment safety
    printDebugString("getPartitionInfo: Reading LBA offset\n");
    readBytes(&lbaValue, &entry[PARTITION_LBA_OFFSET]);
    fs->startLba = lbaValue;
    
    // Read number of sectors using readBytes for alignment safety
    printDebugString("getPartitionInfo: Reading partition sectors\n");
    readBytes(&sectorsValue, &entry[PARTITION_SECTORS_OFFSET]);
    fs->endLba = fs->startLba + sectorsValue - 1;
    
    printDebugString("getPartitionInfo: Returing good status\n");
    fs->args = ((void*) 0);
    return fs;
  }
  
  printDebugString("getPartitionInfo: Invalid partition type\n");
  fs->args = ((void*) -3);
  return fs;
}

