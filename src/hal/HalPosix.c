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

/// @file HalPosix.cpp
///
/// @brief HAL implementation for a Posix simulator.

#ifdef __x86_64__


#include "HalPosix.h"
#include "user/NanoOsErrno.h"
#include "kernel/NanoOs.h"
/*
#include "kernel/SdCardPosix.h"
#include "kernel/ExFatTask.h"
#include "kernel/MemoryManager.h"
#include "kernel/Tasks.h"
*/

uintptr_t posixProcessStackSize(void);
uintptr_t posixMemoryManagerStackSize(bool debug);
void* posixBottomOfHeap(void);
int posixGetNumSerialPorts(void);
int posixSetNumSerialPorts(int numSerialPorts);
int posixInitSerialPort(int port, int32_t baud);
int posixPollSerialPort(int port);
ssize_t posixWriteSerialPort(int port, const uint8_t *data, ssize_t length);
int posixGetNumDios(void);
int posixConfigureDio(int dio, bool output);
int posixWriteDio(int dio, bool high);
int posixInitSpiDevice(int spi,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud);
int posixStartSpiTransfer(int spi);
int posixEndSpiTransfer(int spi);
int posixSpiTransfer8(int spi, uint8_t data);
int posixSetSystemTime(struct timespec *now);
int64_t posixGetElapsedMilliseconds(int64_t startTime);
int64_t posixGetElapsedMicroseconds(int64_t startTime);
int64_t posixGetElapsedNanoseconds(int64_t startTime);
int posixShutdown(HalShutdownType shutdownType);
int posixInitRootStorage(SchedulerState *schedulerState);
int posixGetNumTimers(void);
int posixSetNumTimers(int numTimers);
int posixInitTimer(int timer);
int posixConfigOneShotTimer(int timer,
    uint64_t nanoseconds, void (*callback)(void));
uint64_t posixConfiguredTimerNanoseconds(int timer);
uint64_t posixRemainingTimerNanoseconds(int timer);
int posixCancelTimer(int timer);
int posixCancelAndGetTimer(int timer,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void));

/// @var posixHal
///
/// @brief The implementation of the Hal interface for the Arduino Nano 33 Iot.
static Hal posixHal = {
  // Memory definitions.
  .processStackSize = posixProcessStackSize,
  .memoryManagerStackSize = posixMemoryManagerStackSize,
  .bottomOfHeap = posixBottomOfHeap,
  
  // Overlay definitions.
  .overlayMap = NULL,
  .overlaySize = 0,
  
  // Serial port functionality.
  .getNumSerialPorts = posixGetNumSerialPorts,
  .setNumSerialPorts = posixSetNumSerialPorts,
  .initSerialPort = posixInitSerialPort,
  .pollSerialPort = posixPollSerialPort,
  .writeSerialPort = posixWriteSerialPort,
  
  // Digital IO pin functionality.
  .getNumDios = posixGetNumDios,
  .configureDio = posixConfigureDio,
  .writeDio = posixWriteDio,
  
  // SPI functionality.
  .initSpiDevice = posixInitSpiDevice,
  .startSpiTransfer = posixStartSpiTransfer,
  .endSpiTransfer = posixEndSpiTransfer,
  .spiTransfer8 = posixSpiTransfer8,
  
  // System time functionality.
  .setSystemTime = posixSetSystemTime,
  .getElapsedMilliseconds = posixGetElapsedMilliseconds,
  .getElapsedMicroseconds = posixGetElapsedMicroseconds,
  .getElapsedNanoseconds = posixGetElapsedNanoseconds,
  
  // Hardware power
  .shutdown = posixShutdown,
  
  // Root storage configuration.
  .initRootStorage = posixInitRootStorage,
  
  // Hardware timers.
  .getNumTimers = posixGetNumTimers,
  .setNumTimers = posixSetNumTimers,
  .initTimer = posixInitTimer,
  .configOneShotTimer = posixConfigOneShotTimer,
  .configuredTimerNanoseconds = posixConfiguredTimerNanoseconds,
  .remainingTimerNanoseconds = posixRemainingTimerNanoseconds,
  .cancelTimer = posixCancelTimer,
  .cancelAndGetTimer = posixCancelAndGetTimer,
};

const Hal* halPosixImplInit(jmp_buf resetBuffer, const char *sdCardDevicePath,
  void **overlayMap, uintptr_t *overlaySize);

const Hal* halPosixInit(jmp_buf resetBuffer, const char *sdCardDevicePath) {
  if (halPosixImplInit(resetBuffer, sdCardDevicePath,
    (void**) &posixHal.overlayMap, &posixHal.overlaySize) != 0
  ) {
    return NULL;
  }

  return &posixHal;
}

#endif // __x86_64__

