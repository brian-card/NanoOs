///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.18.2025
///
/// @file              SdCardSpi.h
///
/// @brief             SPI-based SD card functionality for NanoOs.
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

#ifndef SD_CARD_SPI_H
#define SD_CARD_SPI_H

// Custom includes
#include "SdCard.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct SdCardSpiArgs {
  int spiCsDio;
  int spiCopiDio;
  int spiCipoDio;
  int spiSckDio;
} SdCardSpiArgs;

int sdSpiInit(SdCardSpiArgs *sdCardSpiArgs, SdCardState *sdCardState);
int sdSpiReadBlocks(SdCardState *sdCardState,
  uint32_t startBlock, uint32_t numBlocks, uint8_t *buffer);
int sdSpiWriteBlocks(SdCardState *sdCardState,
  uint32_t startBlock, uint32_t numBlocks, uint8_t *buffer);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SD_CARD_SPI_H
