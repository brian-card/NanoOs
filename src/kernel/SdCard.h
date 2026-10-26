///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              1.13.2025
///
/// @file              SdCard.h
///
/// @brief             SD card functionality for NanoOs.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
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
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef SD_CARD_H
#define SD_CARD_H

#include "stdbool.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct BlockDevice BlockDevice;

/// @struct SdCardState
///
/// @brief State maintained by the HAL for an SD card.
///
/// @param context A pointer to any implementation-specific context.
/// @param blockSize The number of bytes per block on the SD card as presented
///   to the host.
/// @param numBlocks The total number of blocks available on the SD card.
/// @param sdCardVersion The version of the card (1 or 2).
/// @param blockAddressed true if the card takes block (LBA) addresses -
///   high-capacity SDHC/SDXC; false if it takes byte addresses - standard-
///   capacity SDSC.  Determined from the CMD58 OCR CCS bit, not from the
///   version, since a v2 card can still be standard capacity.
/// @param bsDevice A pointer to the BlockDevice that abstracts this
///   card.
typedef struct SdCardState {
  void *context;
  uint16_t blockSize;
  uint32_t numBlocks;
  int sdCardVersion;
  bool blockAddressed;
  BlockDevice *bsDevice;
} SdCardState;

int sdCardGetReadWriteArgs(SdCardState *sdCardState,
  uint32_t startBlock, uint32_t numBlocks,
  uint32_t *startSdBlock, uint32_t *numSdBlocks);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SD_CARD_H
