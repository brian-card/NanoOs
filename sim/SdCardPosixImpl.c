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

// Standard library includes
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Simulator includes
#include "SdCardPosix.h"

//// #define printDebug(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define printDebug(fmt, ...) {}

/// @fn int sdCardRead(int devFd, void *buffer, size_t start, size_t len)
///
/// @brief POSIX implementation of read command for the SD card simulator.
///
/// @param devFd The file descriptor of the device file to read from.
/// @param buffer A pointer to the buffer to read data into.
/// @param start The position, in bytes, of the start of the read.
/// @param len The number of bytes to read.
///
/// @return Returns 0 on success, errno on failure.
int sdCardRead(int devFd, void *buffer, size_t start, size_t len) {
  int returnValue = 0;

  printDebug("sdCardRead: Calling lseek\n");
  if (lseek(devFd, start, SEEK_SET) < 0) {
    printDebug("sdCardReadBlocksCommandHandler: lseek FAILED\n");
    returnValue = errno;
    goto exit;
  }

  printDebug("sdCardRead: Calling read\n");
  if (read(devFd, buffer, len) <= 0) {
    printDebug("sdCardReadBlocksCommandHandler: read FAILED\n");
    returnValue = errno;
  }

exit:
  return returnValue;
}

/// @fn int sdCardWrite(int devFd, const void *buffer, size_t start, size_t len)
///
/// @brief POSIX implementation of write command for the SD card simulator.
///
/// @param devFd The file descriptor of the device file to write to.
/// @param buffer A pointer to the buffer to write data from.
/// @param start The position, in bytes, of the start of the write.
/// @param len The number of bytes to write.
///
/// @return Returns 0 on success, errno on failure.
int sdCardWrite(int devFd, const void *buffer, size_t start, size_t len) {
  int returnValue = 0;

  if (lseek(devFd, start, SEEK_SET) < 0) {
    returnValue = errno;
    goto exit;
  }

  if (write(devFd, buffer, len) <= 0) {
    returnValue = errno;
  }

exit:
  return returnValue;
}

/// @fn const char* sdCardInit(SdCardState *sdCardState,
///   BlockStorageDevice *sdDevice, const char *sdCardDevicePath)
///
/// @brief Initialize the components of an SdCardState object.
///
/// @param sdCardState A pointer to the SdCardState object to initialize.
/// @param sdDevice A pointer to the generic BlockStorageDevice that will be
///   handed off to the filesystem process.
/// @param sdCardDevicePath A C string holding the path to the device node on
///   the host OS where NanoOs's block device simulation lives.
///
/// @return Returns the string representation of the errno produced as a result
///   of the call to open.
const char* sdCardInit(SdCardState *sdCardState,
  BlockStorageDevice *sdDevice, const char *sdCardDevicePath
) {
  errno = 0;

  sdCardState->bsDevice = sdDevice;
  sdCardState->blockSize = 512;
  sdCardState->numBlocks = 204800; // 100 MB
  sdCardState->sdCardVersion = 2;
  sdCardState->context = (void*) ((intptr_t) open(sdCardDevicePath, O_RDWR));

  return strerror(errno);
}

