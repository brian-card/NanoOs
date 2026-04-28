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
#include "kernel/ExFatFilesystem.h"
#include "kernel/ExFatTask.h"
#include "kernel/Filesystem.h"
#include "kernel/NanoOs.h"
#include "kernel/Scheduler.h"
#include "kernel/Tasks.h"

// Must come last
#include "user/NanoOsStdio.h"

uintptr_t posixProcessStackSize(void);
uintptr_t posixMemoryManagerStackSize(bool debug);
void* posixBottomOfHeap(void);

int posixGetNumUarts(void);
int posixSetNumUarts(int numUarts);
int posixInitUart(int port, int32_t baud);
int posixPollUart(int port);
ssize_t posixWriteUart(int port, const uint8_t *data, ssize_t length);
static HalUart posixUartHal = {
  .getNumUarts = posixGetNumUarts,
  .setNumUarts = posixSetNumUarts,
  .initUart = posixInitUart,
  .pollUart = posixPollUart,
  .writeUart = posixWriteUart,
};

int posixGetNumDios(void);
int posixConfigureDio(int dio, bool output);
int posixWriteDio(int dio, bool high);
static HalDio posixDioHal = {
  .getNumDios = posixGetNumDios,
  .configureDio = posixConfigureDio,
  .writeDio = posixWriteDio,
};

int posixInitSpiDevice(int spi,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud);
int posixStartSpiTransfer(int spi);
int posixEndSpiTransfer(int spi);
int posixSpiTransfer8(int spi, uint8_t data);
int posixSpiTransferBytes(int spi, uint8_t *data, uint32_t length);
static HalSpi posixSpiHal = {
  .initSpiDevice = posixInitSpiDevice,
  .startSpiTransfer = posixStartSpiTransfer,
  .endSpiTransfer = posixEndSpiTransfer,
  .spiTransfer8 = posixSpiTransfer8,
  .spiTransferBytes = posixSpiTransferBytes,
};

int posixSetSystemTime(struct timespec *now);
int64_t posixGetElapsedMilliseconds(int64_t startTime);
int64_t posixGetElapsedMicroseconds(int64_t startTime);
int64_t posixGetElapsedNanoseconds(int64_t startTime);
static HalClock posixClockHal = {
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
  .getNumTimers = posixGetNumTimers,
  .setNumTimers = posixSetNumTimers,
  .initTimer = posixInitTimer,
  .configOneShotTimer = posixConfigOneShotTimer,
  .configuredTimerNanoseconds = posixConfiguredTimerNanoseconds,
  .remainingTimerNanoseconds = posixRemainingTimerNanoseconds,
  .cancelTimer = posixCancelTimer,
  .cancelAndGetTimer = posixCancelAndGetTimer,
};

/// @var _sdCardDevicePath
///
/// @brief Path to the device node to connect to for the SdCardSim task.
static const char *_sdCardDevicePath = NULL;

int posixInitRootStorage(SchedulerState *schedulerState) {
  TaskDescriptor *allTasks = schedulerState->allTasks;
  
  // Create the SD card task.
  TaskDescriptor *taskDescriptor
    = &allTasks[schedulerState->firstUserTaskId - 1];
  if (taskCreate(
    taskDescriptor, runSdCardPosix, (void*) _sdCardDevicePath)
    != taskSuccess
  ) {
    printString("Could not start SD card task.\n");
  }
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);
  taskDescriptor->taskId = schedulerState->firstUserTaskId;
  taskDescriptor->name = "SD card";
  taskDescriptor->userId = ROOT_USER_ID;
  BlockStorageDevice *sdDevice = (BlockStorageDevice*) coroutineResume(
    allTasks[schedulerState->firstUserTaskId - 1].taskHandle, NULL);
  sdDevice->partitionNumber = 1;
  schedulerState->firstUserTaskId++;
  schedulerState->firstShellTaskId = schedulerState->firstUserTaskId;
  
  return halCommonInitRootFilesystem(sdDevice);
}


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
  
  .uartHal = &posixUartHal,
  .dioHal = &posixDioHal,
  .spiHal = &posixSpiHal,
  .clockHal = &posixClockHal,
  .powerHal = &posixPowerHal,
  .timerHal = &posixTimerHal,
  
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

