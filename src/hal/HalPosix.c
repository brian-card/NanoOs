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

#include <string.h>

#include "HalPosix.h"
#include "HalCommon.h"
#include "SdCardPosix.h"
#include "user/NanoOsErrno.h"
#include "kernel/Filesystem.h"
#include "kernel/NanoOs.h"
#include "kernel/Scheduler.h"
#include "kernel/Processes.h"

// Must come last
#include "user/NanoOsStdio.h"

size_t posixProcessStackSize(bool debug);
size_t posixMemoryManagerStackSize(bool debug);
void* posixBottomOfHeap(bool debug);
uint8_t posixNumExtraSchedulerStacks(bool debug);
uint8_t posixNumExtraConsoleStacks(bool debug);
static HalMemory posixMemoryHal = {
  .processStackSize = posixProcessStackSize,
  .memoryManagerStackSize = posixMemoryManagerStackSize,
  .bottomOfHeap = posixBottomOfHeap,
  .numExtraSchedulerStacks = posixNumExtraSchedulerStacks,
  .numExtraConsoleStacks = posixNumExtraConsoleStacks,
  .overlayMap = NULL,
  .overlaySize = 0,
};

static uint32_t posixUartsOnline[] = {
  0x00000002,
};
int32_t posixInitUart(void);
int32_t posixConfigureUart(int32_t port, uint32_t baud);
int posixPollUart(int32_t port);
ssize_t posixWriteUart(int32_t port, const uint8_t *data, ssize_t length);
bool posixIsUartConsole(int32_t port);

static HalUart posixUartHal = {
  .numSupported = 2,
  .online = posixUartsOnline,
  .init = posixInitUart,
  .configure = posixConfigureUart,
  .poll = posixPollUart,
  .write = posixWriteUart,
  .isConsole = posixIsUartConsole,
};

static uint32_t posixDiosOnline[] = {
  0x00000000,
};
int32_t posixInitDio(void);
int32_t posixConfigureDio(int32_t deviceId, bool output);
int32_t posixWriteDio(int32_t deviceId, bool high);
static HalDio posixDioHal = {
  .numSupported = 0,
  .online = posixDiosOnline,
  .init = posixInitDio,
  .configure = posixConfigureDio,
  .write = posixWriteDio,
};

static uint32_t posixSpisOnline[] = {
  0x00000000,
};
int32_t posixInitSpi(void);
int32_t posixConfigureSpiDevice(int32_t deviceId,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud);
int32_t posixStartSpiTransfer(int32_t deviceId);
int32_t posixEndSpiTransfer(int32_t deviceId);
int32_t posixSpiTransfer8(int32_t deviceId, uint8_t data);
int32_t posixSpiTransferBytes(int32_t deviceId, uint8_t *data, uint32_t length);
static HalSpi posixSpiHal = {
  .numSupported = 0,
  .online = posixSpisOnline,
  .init = posixInitSpi,
  .configure = posixConfigureSpiDevice,
  .startTransfer = posixStartSpiTransfer,
  .endTransfer = posixEndSpiTransfer,
  .transfer8 = posixSpiTransfer8,
  .transferBytes = posixSpiTransferBytes,
};

int32_t posixTimeInit(void);
int32_t posixSetSystemTime(struct timespec *now);
int64_t posixGetElapsedMilliseconds(int64_t startTime);
int64_t posixGetElapsedMicroseconds(int64_t startTime);
int64_t posixGetElapsedNanoseconds(int64_t startTime);
static HalClock posixClockHal = {
  .init = posixTimeInit,
  .setSystemTime = posixSetSystemTime,
  .getElapsedMilliseconds = posixGetElapsedMilliseconds,
  .getElapsedMicroseconds = posixGetElapsedMicroseconds,
  .getElapsedNanoseconds = posixGetElapsedNanoseconds,
};

int posixShutdown(HalShutdownType shutdownType);
static HalPower posixPowerHal = {
  .shutdown = posixShutdown,
};

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
static HalTimer posixTimerHal = {
  .getNum = posixGetNumTimers,
  .setNum = posixSetNumTimers,
  .init = posixInitTimer,
  .configOneShot = posixConfigOneShotTimer,
  .configuredNanoseconds = posixConfiguredTimerNanoseconds,
  .remainingNanoseconds = posixRemainingTimerNanoseconds,
  .cancel = posixCancelTimer,
  .cancelAndGet = posixCancelAndGetTimer,
};

/// @var _sdCardDevicePath
///
/// @brief Path to the device node to connect to for the SdCardSim process.
static const char *_sdCardDevicePath = NULL;

int posixInitRootStorage(SchedulerState *schedulerState) {
  ProcessDescriptor *allProcesses = schedulerState->allProcesses;
  
  // Create the SD card process.
  ProcessDescriptor *processDescriptor
    = &allProcesses[schedulerState->firstUserPid - 1];
  if (processCreate(
    processDescriptor, runSdCardPosix, (void*) _sdCardDevicePath)
    != processSuccess
  ) {
    printString("Could not start SD card process.\n");
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->pid = schedulerState->firstUserPid;
  processDescriptor->name = "SD card";
  processDescriptor->userId = ROOT_USER_ID;
  BlockStorageDevice *sdDevice = (BlockStorageDevice*) coroutineResume(
    allProcesses[schedulerState->firstUserPid - 1].mainThread, NULL);
  sdDevice->partitionNumber = 1;
  schedulerState->firstUserPid++;
  schedulerState->firstShellPid = schedulerState->firstUserPid;
  
  return halCommonInitRootFilesystem(sdDevice);
}


/// @var posixHal
///
/// @brief The implementation of the Hal interface for the Arduino Nano 33 Iot.
static Hal posixHal = {
  .memory = &posixMemoryHal,
  .uart = &posixUartHal,
  .dio = &posixDioHal,
  .spi = &posixSpiHal,
  .clock = &posixClockHal,
  .power = &posixPowerHal,
  .timer = &posixTimerHal,
  
  // Root storage configuration.
  .initRootStorage = posixInitRootStorage,
};

const Hal* halPosixImplInit(jmp_buf resetBuffer, Hal *hal);

const Hal* halPosixInit(jmp_buf resetBuffer, const char *sdCardDevicePath) {
  _sdCardDevicePath = sdCardDevicePath;
  if (halPosixImplInit(resetBuffer, &posixHal) != 0) {
    return NULL;
  }

  return &posixHal;
}

#endif // __x86_64__

