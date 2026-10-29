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

// Doxygen marker
/// @file

// Custom includes
#include "Logger.h"
#include "Scheduler.h"
#include "SdCard.h"
#include "NanoOs.h"
#include "Processes.h"
#include "../user/NanoOsLibC.h"

// Must come last
#include "../user/NanoOsStdio.h"

/// @fn int sdCardGetReadWriteArgs(SdCardState *sdCardState,
///   uint32_t startBlock, uint32_t numBlocks,
///   uint32_t *startSdBlock, uint32_t *numSdBlocks)
///
/// @brief Get the startSdBlock and numSdBlocks parameters for a read or write
/// operation on the SD card.
///
/// @param sdCardState A pointer to the SdCardState object maintained by the
///   HAL.
/// @param startBlock The block to start at in terms of the caller's context.
/// @param numBlocks The number of blocks in terms of the caller's context.
/// @param startSdBlock A pointer to the uint32_t variable that will hold the
///   first block of the SD card to read from or write to.
/// @param numSdBlocks A pointer to the uint32_t variable that will hold the
///   number of blocks on the SD card to read or write.
///
/// @return Returns 0 on success, -EINVAL on failure.
int sdCardGetReadWriteArgs(SdCardState *sdCardState,
  uint32_t startBlock, uint32_t numBlocks,
  uint32_t *startSdBlock, uint32_t *numSdBlocks
) {
  *startSdBlock = startBlock << sdCardState->bsDevice->blockBitShift;
  *numSdBlocks = numBlocks << sdCardState->bsDevice->blockBitShift;
  if ((*startSdBlock + *numSdBlocks) > sdCardState->numBlocks) {
    logError("Invalid R/W range\n");
    return -EINVAL;
  }

  return 0;
}

