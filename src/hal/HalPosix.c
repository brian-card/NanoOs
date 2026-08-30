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

/// @file HalArduinoSamD21x18ASim.c
///
/// @brief HAL implementation for a Posix simulator.

#ifdef __x86_64__

#include <string.h>

#include "HalPosix.h"
#include "HalCommon.h"
#include "SdCardPosix.h"
#include "user/NanoOsErrno.h"
#include "kernel/Commands.h"
#include "kernel/Filesystem.h"
#include "kernel/Logger.h"
#include "kernel/NanoOs.h"
#include "kernel/Scheduler.h"
#include "kernel/Processes.h"

// Must come last
#include "user/NanoOsStdio.h"

// Types and prototypes from files that we can't directly include.
typedef struct NanoOsApi NanoOsApi;
extern NanoOsApi nanoOsApi;
extern NanoOsApi *NANO_OS_API;

#ifdef __cplusplus
extern "C"
{
#endif
void* callOverlayFunctionFromFile(const void *overlayDir, const void *overlay,
  const char *function, void *args);
#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// Forward declarations for all POSIX platform functions (defined in
// HalArduinoSamD21x18ASimImpl.c), now with va_list signatures.
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
int32_t posixSetSpiSpeed(va_list args);
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
  NanoOsOverlayMap **overlayMap, size_t *overlaySize, StaticLogs **staticLogs,
  NanoOsOverlayMap **contiguousFilesystem, size_t *contiguousFilesystemSize);

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

/// @def _numBlockDevices
///
/// @brief Number of BlockDevices that can be managed by the HAL.
///
/// @note This is a #define rather than a const uint32_t so that it doesn't
/// need its own KEEP_IN_FLASH treatment - it's folded into an immediate
/// value at each use site instead of occupying storage that could land in
/// .rodata.
#define _numBlockDevices \
  ((uint32_t) (sizeof(blockDevices) / sizeof(blockDevices[0])))

/// @var posixBlockDevicesOnline
///
/// @brief Bitmask array of online block devices.
static uint32_t posixBlockDevicesOnline[] = {
  0x00000000,
};

/// @var _sdCardName
///
/// @brief Process name assigned to the SD card process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _sdCardName[] KEEP_IN_FLASH = "SD card";

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
    logError("Could not start SD card process.\n");
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = SCHEDULER_STATE->firstUserPid;
  processDescriptor->name = _sdCardName;
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
    logError("Could not restart SD card process.\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = _sdCardName;
  processDescriptor->userId = ROOT_USER_ID;

  BlockDevice *sdDevice
    = (BlockDevice*) coroutineResume(processDescriptor->mainThread, NULL);
  if (sdDevice == NULL) {
    logError("SD card restart returned NULL.\n");
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
  [HAL_SPI_SET_SPEED]      = posixSetSpiSpeed,
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

/// @var _logBuffer
///
/// @brief Statically allocated buffer for formatting log messages.
static char _logBuffer[128];

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
  halImpl.platform.callFileOverlay = callOverlayFunctionFromFile;
  halImpl.platform.execCommand = execOverlayCommand;
  halImpl.platform.restartRootFilesystem = restartContiguousFilesystem;
  halImpl.platform.initRootStorage = halCommonInitRootFilesystem,
  halImpl.platform.restartShell = restartOverlayShell;

  // Uncomment these lines to switch to using the built-in shell:
  // halImpl.platform.execCommand = execBuiltinCommand;
  // halImpl.platform.restartShell = restartBuiltinShell;

  halImpl.uart.numSupported = 2;
  halImpl.uart.online       = posixUartsOnline;

  halImpl.dio.numSupported = 0;
  halImpl.dio.online       = posixDiosOnline;

  halImpl.spi.numSupported = 0;
  halImpl.spi.online       = posixSpisOnline;

  halImpl.timer.numSupported = 2;
  halImpl.timer.online       = posixTimersOnline;

  halImpl.blockDevice.numSupported = _numBlockDevices;
  halImpl.blockDevice.online       = posixBlockDevicesOnline;

  halImpl.memory.logBuffer      = _logBuffer;
  halImpl.memory.logBufferSize  = sizeof(_logBuffer);
#ifdef NANO_OS_STRINGS_STRIPPED
  halImpl.memory.stringsPresent = false;
#else
  halImpl.memory.stringsPresent = true;
#endif // NANO_OS_STRINGS_STRIPPED

  // Perform POSIX-specific hardware setup and retrieve the overlay mapping.
  int32_t result
    = halPosixImplInit(resetBuffer,
      &halImpl.memory.overlayMap,
      &halImpl.memory.overlaySize,
      &halImpl.memory.staticLogs,
      &halImpl.memory.contiguousFilesystem,
      &halImpl.memory.contiguousFilesystemSize);
  if (result != 0) {
    halImpl.memory.overlayMap               = NULL;
    halImpl.memory.overlaySize              = 0;
    halImpl.memory.contiguousFilesystem     = NULL;
    halImpl.memory.contiguousFilesystemSize = 0;
    halImpl.memory.staticLogs               = NULL;
    return result;
  }
  if (halImpl.memory.staticLogs != NULL) {
    memset(halImpl.memory.staticLogs, 0, sizeof(StaticLogs));
  }

  NANO_OS_API = &nanoOsApi;

  return halCommonInit();
}

#endif // __x86_64__
