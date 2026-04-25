///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.01.2026
///
/// @file              BlockStorage.h
///
/// @brief             Definitions needed in support of block storage and
///                    access.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef BLOCK_STORAGE_H
#define BLOCK_STORAGE_H

#include "stdint.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @struct BlockStorageDevice
///
/// @brief The collection of data and functions needed to interact with a block
/// storage device.
///
/// @param context The device-specific context to pass to the functions.
/// @param readBlocks Function pointer for the function to read a given number
///   of blocks from the storage device.
/// @param writeBlocks Function pointer for the function to write a given number
///   of blocks to the storage device.
/// @param blockSize The size, in bytes, of the physical blocks on the device.
/// @param blockBitShift The number of bits to shift to convert filesystem-level
///   blocks to physical blocks.
/// @param partitionNumber The one-based partition index that is to be used by
///   a filesystem.
typedef struct BlockStorageDevice {
  void *context;
  int (*readBlocks)(void *context, uint32_t startBlock,
    uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer);
  int (*writeBlocks)(void *context, uint32_t startBlock,
    uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer);
  int (*schedReadBlocks)(void *context, uint32_t startBlock,
    uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer);
  int (*schedWriteBlocks)(void *context, uint32_t startBlock,
    uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer);
  uint16_t blockSize;
  uint8_t blockBitShift;
  uint8_t partitionNumber;
} BlockStorageDevice;

/// @struct FileBlockMetadata
///
/// @brief Block-level metadata for a file.
///
/// @param blockDevice A pointer to the BlockStorageDevice where the file
///   resides.
/// @param startBlock The block (LBA) on the block device where the file
///   begins.
/// @param numBlocks The number of blocks that the file occupies on the block
///   device.
typedef struct FileBlockMetadata {
  BlockStorageDevice *blockDevice;
  uint32_t            startBlock;
  uint32_t            numBlocks;
} FileBlockMetadata;

#ifdef __cplusplus
}
#endif

#endif // BLOCK_STORAGE_H

