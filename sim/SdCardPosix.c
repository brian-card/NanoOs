////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file

// NanoOs includes
#include "BlockDevice.h"
#include "Hal.h"
#include "Logger.h"
#include "NanoOs.h"
#include "NanoOsLibC.h"
#include "Processes.h"

// Simulator includes
#include "SdCardPosix.h"

// Must come last
#include "../user/NanoOsStdio.h"

// Implementation prototypes.
const char* sdCardInit(SdCardState *sdCardState,
  BlockDevice *sdDevice, const char *sdCardDevicePath);
int sdCardRead(int devFd, void *buffer, size_t start, size_t len);
int sdCardWrite(int devFd, const void *buffer, size_t start, size_t len);

/// @var _sdCardState
///
/// @brief The state of the simulated SD card.
static SdCardState _sdCardState;

/// @var _sdDevice
///
/// @brief The BlockDevice that abstracts the simulated SD card.
static BlockDevice _sdDevice;

/// @fn BlockDevice* sdCardPosixInit(const char *sdCardDevicePath)
///
/// @brief Open the file that backs the simulated SD card.  Calling it again
/// after a success returns the same BlockDevice.
///
/// @param sdCardDevicePath The path on the host to the SD card image.
///
/// @return Returns the BlockDevice for the card on success, NULL on failure.
BlockDevice* sdCardPosixInit(const char *sdCardDevicePath) {
  if (_sdDevice.readBlocks != NULL) {
    return &_sdDevice;
  }

  const char *openError
    = sdCardInit(&_sdCardState, &_sdDevice, sdCardDevicePath);
  if (((intptr_t) _sdCardState.context) < 0) {
    logError("Failed to open sdCardDevicePath \"%s\"\n", sdCardDevicePath);
    logError("Error returned: %s\n", openError);
    return NULL;
  }

  _sdDevice.context = (void*) ((intptr_t) 0);
  _sdDevice.blockSize = _sdCardState.blockSize;
  _sdDevice.blockBitShift = 0;
  _sdDevice.partitionNumber = 1;
  _sdDevice.writeBlocks = HAL->blockDevice->writeBlocks;
  _sdDevice.readBlocks = HAL->blockDevice->readBlocks;

  return &_sdDevice;
}

/// @fn int sdCardPosixTransfer(int32_t deviceId, va_list args, bool isWrite)
///
/// @brief Read or write blocks on the simulated SD card.
///
/// @param deviceId The zero-based ID of the block device.
/// @param args The startBlock, numBlocks, blockSize, and buffer arguments of
///   HalBlockDevice's readBlocks/writeBlocks.
/// @param isWrite true to write the blocks, false to read them.
///
/// @return Returns 0 on success, -errno on failure.
static int sdCardPosixTransfer(int32_t deviceId, va_list args, bool isWrite) {
  uint32_t startBlock = va_arg(args, uint32_t);
  uint32_t numBlocks = va_arg(args, uint32_t);
  (void) va_arg(args, int);
  uint8_t *buffer = va_arg(args, uint8_t*);
  if ((deviceId != 0) || (_sdDevice.readBlocks == NULL)) {
    return -ENODEV;
  }

  uint32_t startSdBlock = 0, numSdBlocks = 0;
  int returnValue = sdCardGetReadWriteArgs(&_sdCardState,
    startBlock, numBlocks, &startSdBlock, &numSdBlocks);
  if (returnValue != 0) {
    return returnValue;
  }

  int devFd = (int) ((intptr_t) _sdCardState.context);
  size_t start = ((size_t) _sdCardState.blockSize) * startSdBlock;
  size_t length = ((size_t) _sdCardState.blockSize) * numSdBlocks;
  if (isWrite) {
    returnValue = sdCardWrite(devFd, buffer, start, length);
    // An SPI card clocks 0xFF back into the buffer while the data goes out.
    memset(buffer, 0xFF, length);
  } else {
    returnValue = sdCardRead(devFd, buffer, start, length);
  }

  return (returnValue == 0) ? 0 : -EIO;
}

/// @fn int sdCardPosixReadBlocks(int32_t deviceId, va_list args)
///
/// @brief HAL_BLOCK_DEVICE_READ_BLOCKS implementation for the simulator.
///
/// @param deviceId The zero-based ID of the block device.
/// @param args The startBlock, numBlocks, blockSize, and buffer arguments.
///
/// @return Returns 0 on success, -errno on failure.
int sdCardPosixReadBlocks(int32_t deviceId, va_list args) {
  return sdCardPosixTransfer(deviceId, args, false);
}

/// @fn int sdCardPosixWriteBlocks(int32_t deviceId, va_list args)
///
/// @brief HAL_BLOCK_DEVICE_WRITE_BLOCKS implementation for the simulator.
///
/// @param deviceId The zero-based ID of the block device.
/// @param args The startBlock, numBlocks, blockSize, and buffer arguments.
///
/// @return Returns 0 on success, -errno on failure.
int sdCardPosixWriteBlocks(int32_t deviceId, va_list args) {
  return sdCardPosixTransfer(deviceId, args, true);
}
