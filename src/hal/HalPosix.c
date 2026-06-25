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

/// @file HalPosix.c
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

// ---------------------------------------------------------------------------
// Forward declarations for all POSIX platform functions (defined in
// HalPosixImpl.c), now with va_list signatures.
// ---------------------------------------------------------------------------
int32_t posixProcessStackSize(va_list args);
int32_t posixMemoryManagerStackSize(va_list args);
int32_t posixBottomOfHeap(va_list args);
int32_t posixNumExtraSchedulerStacks(va_list args);
int32_t posixNumExtraConsoleStacks(va_list args);

int32_t posixInitUart(va_list args);
int32_t posixConfigureUart(va_list args);
int32_t posixPollUart(va_list args);
int32_t posixWriteUart(va_list args);
int32_t posixIsUartConsole(va_list args);

int32_t posixInitDio(va_list args);
int32_t posixConfigureDio(va_list args);
int32_t posixWriteDio(va_list args);

int32_t posixInitSpi(va_list args);
int32_t posixConfigureSpiDevice(va_list args);
int32_t posixStartSpiTransfer(va_list args);
int32_t posixEndSpiTransfer(va_list args);
int32_t posixSpiTransfer8(va_list args);
int32_t posixSpiTransferBytes(va_list args);

int32_t posixTimeInit(va_list args);
int32_t posixSetSystemTime(va_list args);
int32_t posixGetElapsedMilliseconds(va_list args);
int32_t posixGetElapsedMicroseconds(va_list args);
int32_t posixGetElapsedNanoseconds(va_list args);

int32_t posixEnterPowerMode(va_list args);

int32_t posixInitTimer(va_list args);
int32_t posixInitTimerDevice(va_list args);
int32_t posixConfigOneShotTimer(va_list args);
int32_t posixConfiguredTimerNanoseconds(va_list args);
int32_t posixRemainingTimerNanoseconds(va_list args);
int32_t posixCancelTimer(va_list args);
int32_t posixCancelAndGetTimer(va_list args);

int32_t halPosixImplInit(jmp_buf resetBuffer,
  NanoOsOverlayMap **overlayMap, size_t *overlaySize);

// ---------------------------------------------------------------------------
// Per-platform online bitmask arrays — pointers are installed on halCommon*
// instances at init time.
// ---------------------------------------------------------------------------

static uint32_t posixUartsOnline[] = {
  0x00000002,
};

static uint32_t posixDiosOnline[] = {
  0x00000000,
};

static uint32_t posixSpisOnline[] = {
  0x00000000,
};

static uint32_t posixTimersOnline[] = {
  0x00000003,
};

/// @var _sdCardDevicePath
///
/// @brief Path to the device node to connect to for the SdCardSim process.
static const char *_sdCardDevicePath = NULL;

/// @var blockDevices
///
/// @brief Array of BlockDevice pointers that are managed by the driver
/// processes.
static BlockDevice *blockDevices[] = {
  NULL,
};

/// @var _numBlockDevices
///
/// @brief Number of BlockDevices that can be managed by the HAL.
static const uint32_t _numBlockDevices
  = sizeof(blockDevices) / sizeof(blockDevices[0]);

/// @var posixBlockDevicesOnline
///
/// @brief Bitmask array of online block devices.
static uint32_t posixBlockDevicesOnline[] = {
  0x00000000,
};

int32_t posixInitBlockDevice(va_list args) {
  (void) args;
  if (SCHEDULER_STATE == NULL) {
    return -EBUSY;
  }

  ProcessDescriptor *allProcesses = SCHEDULER_STATE->allProcesses;

  // Create the SD card process.
  ProcessDescriptor *processDescriptor
    = &allProcesses[SCHEDULER_STATE->firstUserPid - 1];
  if (processCreate(
    processDescriptor, runSdCardPosix, (void*) _sdCardDevicePath)
    != processSuccess
  ) {
    printString("Could not start SD card process.\n");
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = SCHEDULER_STATE->firstUserPid;
  processDescriptor->name = "SD card";
  processDescriptor->userId = ROOT_USER_ID;
  BlockDevice *sdDevice = (BlockDevice*) coroutineResume(
    allProcesses[SCHEDULER_STATE->firstUserPid - 1].mainThread, NULL);
  sdDevice->partitionNumber = 1;
  SCHEDULER_STATE->firstUserPid++;
  SCHEDULER_STATE->firstShellPid = SCHEDULER_STATE->firstUserPid;
  blockDevices[0] = sdDevice;
  setOnline(HAL->blockDevice, 0);

  return 0;
}

int32_t posixGetBlockDevice(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  BlockDevice **returnValue = va_arg(args, BlockDevice**);

  if (!online(HAL->blockDevice, deviceId)) {
    if (returnValue != NULL) {
      *returnValue = NULL;
    }
    return -ENODEV;
  }

  if (returnValue != NULL) {
    *returnValue = blockDevices[deviceId];
  }
  return 0;
}

int32_t posixRestartBlockDevice(va_list args) {
  ProcessDescriptor *processDescriptor = va_arg(args, ProcessDescriptor*);
  int32_t deviceId = (int32_t) (intptr_t) processDescriptor->restartArgs;

  if (processCreate(
    processDescriptor, runSdCardPosix, (void*) _sdCardDevicePath)
    != processSuccess
  ) {
    printString("Could not restart SD card process.\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = "SD card";
  processDescriptor->userId = ROOT_USER_ID;

  BlockDevice *sdDevice
    = (BlockDevice*) coroutineResume(processDescriptor->mainThread, NULL);
  if (sdDevice == NULL) {
    printString("SD card restart returned NULL.\n");
    return -ENODEV;
  }
  sdDevice->partitionNumber = 1;
  blockDevices[deviceId] = sdDevice;
  setOnline(HAL->blockDevice, deviceId);

  return 0;
}

static HalFunction posixMemoryFunctions[HAL_MEMORY_NUM_FNS] = {
  [HAL_MEMORY_PROCESS_STACK_SIZE]         = posixProcessStackSize,
  [HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = posixMemoryManagerStackSize,
  [HAL_MEMORY_BOTTOM_OF_HEAP]             = posixBottomOfHeap,
  [HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = posixNumExtraSchedulerStacks,
  [HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = posixNumExtraConsoleStacks,
};

static HalFunction posixUartFunctions[HAL_UART_NUM_FNS] = {
  [HAL_UART_INIT]       = posixInitUart,
  [HAL_UART_CONFIGURE]  = posixConfigureUart,
  [HAL_UART_POLL]       = posixPollUart,
  [HAL_UART_WRITE]      = posixWriteUart,
  [HAL_UART_IS_CONSOLE] = posixIsUartConsole,
};

static HalFunction posixDioFunctions[HAL_DIO_NUM_FNS] = {
  [HAL_DIO_INIT]      = posixInitDio,
  [HAL_DIO_CONFIGURE] = posixConfigureDio,
  [HAL_DIO_WRITE]     = posixWriteDio,
};

static HalFunction posixSpiFunctions[HAL_SPI_NUM_FNS] = {
  [HAL_SPI_INIT]           = posixInitSpi,
  [HAL_SPI_CONFIGURE]      = posixConfigureSpiDevice,
  [HAL_SPI_START_TRANSFER] = posixStartSpiTransfer,
  [HAL_SPI_END_TRANSFER]   = posixEndSpiTransfer,
  [HAL_SPI_TRANSFER8]      = posixSpiTransfer8,
  [HAL_SPI_TRANSFER_BYTES] = posixSpiTransferBytes,
};

static HalFunction posixClockFunctions[HAL_CLOCK_NUM_FNS] = {
  [HAL_CLOCK_INIT]                    = posixTimeInit,
  [HAL_CLOCK_SET_SYSTEM_TIME]         = posixSetSystemTime,
  [HAL_CLOCK_GET_ELAPSED_MILLISECONDS] = posixGetElapsedMilliseconds,
  [HAL_CLOCK_GET_ELAPSED_MICROSECONDS] = posixGetElapsedMicroseconds,
  [HAL_CLOCK_GET_ELAPSED_NANOSECONDS]  = posixGetElapsedNanoseconds,
};

static HalFunction posixPowerFunctions[HAL_POWER_NUM_FNS] = {
  [HAL_POWER_ENTER_MODE] = posixEnterPowerMode,
};

static HalFunction posixTimerFunctions[HAL_TIMER_NUM_FNS] = {
  [HAL_TIMER_INIT]                  = posixInitTimer,
  [HAL_TIMER_INIT_DEVICE]           = posixInitTimerDevice,
  [HAL_TIMER_CONFIG_ONE_SHOT]       = posixConfigOneShotTimer,
  [HAL_TIMER_CONFIGURED_NANOSECONDS] = posixConfiguredTimerNanoseconds,
  [HAL_TIMER_REMAINING_NANOSECONDS]  = posixRemainingTimerNanoseconds,
  [HAL_TIMER_CANCEL]                = posixCancelTimer,
  [HAL_TIMER_CANCEL_AND_GET]        = posixCancelAndGetTimer,
};

static HalFunction posixBlockDeviceFunctions[HAL_BLOCK_DEVICE_NUM_FNS] = {
  [HAL_BLOCK_DEVICE_INIT]    = posixInitBlockDevice,
  [HAL_BLOCK_DEVICE_GET]     = posixGetBlockDevice,
  [HAL_BLOCK_DEVICE_RESTART] = posixRestartBlockDevice,
};

int32_t halPosixInit(jmp_buf resetBuffer, const char *sdCardDevicePath) {
  _sdCardDevicePath = sdCardDevicePath;

  // Wire up per-subsystem function arrays.
  halFunctions[HAL_MEMORY]       = posixMemoryFunctions;
  halFunctions[HAL_UART]         = posixUartFunctions;
  halFunctions[HAL_DIO]          = posixDioFunctions;
  halFunctions[HAL_SPI]          = posixSpiFunctions;
  halFunctions[HAL_CLOCK]        = posixClockFunctions;
  halFunctions[HAL_POWER]        = posixPowerFunctions;
  halFunctions[HAL_TIMER]        = posixTimerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = posixBlockDeviceFunctions;

  // Set per-platform data members on the common subsystem instances.
  halCommonUart.numSupported = 2;
  halCommonUart.online       = posixUartsOnline;

  halCommonDio.numSupported = 0;
  halCommonDio.online       = posixDiosOnline;

  halCommonSpi.numSupported = 0;
  halCommonSpi.online       = posixSpisOnline;

  halCommonTimer.numSupported = 2;
  halCommonTimer.online       = posixTimersOnline;

  halCommonBlockDevice.numSupported = _numBlockDevices;
  halCommonBlockDevice.online       = posixBlockDevicesOnline;

  // Perform POSIX-specific hardware setup and retrieve the overlay mapping.
  NanoOsOverlayMap *overlayMap = NULL;
  size_t overlaySize = 0;
  int32_t result = halPosixImplInit(resetBuffer, &overlayMap, &overlaySize);
  if (result != 0) {
    return result;
  }

  halCommonMemory.overlayMap  = overlayMap;
  halCommonMemory.overlaySize = overlaySize;

  return halCommonInit();
}

#endif // __x86_64__
