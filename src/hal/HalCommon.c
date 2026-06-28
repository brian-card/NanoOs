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

/// @file HalCommon.c
///
/// @brief HAL routines that are common to multiple implementations.

#include "HalCommon.h"
#include "../kernel/Fat32Filesystem.h"

// Must come last
#include "../user/NanoOsStdio.h"

/// @var halFunctions
///
/// @brief Array of pointers to per-subsystem function pointer arrays.  Each
/// platform init sets halFunctions[subsystem] to point at a static array of
/// HalFunction entries.  Subsystems not supported by a platform leave their
/// entry NULL.
HalFunction *halFunctions[HAL_NUM_SUBSYSTEMS] = {NULL};

/// @var halFunctionCounts
///
/// @brief Number of valid function slots in each subsystem's array.  This is
/// used to sanity check the function parameter of the callHal function.
static const uint32_t halFunctionCounts[HAL_NUM_SUBSYSTEMS] = {
  [HAL_MEMORY]       = HAL_MEMORY_NUM_FNS,
  [HAL_UART]         = HAL_UART_NUM_FNS,
  [HAL_DIO]          = HAL_DIO_NUM_FNS,
  [HAL_SPI]          = HAL_SPI_NUM_FNS,
  [HAL_CLOCK]        = HAL_CLOCK_NUM_FNS,
  [HAL_POWER]        = HAL_POWER_NUM_FNS,
  [HAL_TIMER]        = HAL_TIMER_NUM_FNS,
  [HAL_BLOCK_DEVICE] = HAL_BLOCK_DEVICE_NUM_FNS,
};

/// @fn int32_t callHal(HalSubsystem subsystem, uint32_t function, ...)
///
/// @brief Dispatch a call to the registered platform-specific HAL function.
///
/// @param subsystem The HalSubsystem index into halFunctions.
/// @param function The function-name enum index for the given subsystem.
/// @param ... Arguments to forward to the platform function via va_list.
///
/// @return Returns the value returned by the platform function, or -ENOTSUP if
/// no function has been registered for the given subsystem/function pair.
int32_t callHal(HalSubsystem subsystem, uint32_t function, ...) {
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if ((subsystem >= HAL_NUM_SUBSYSTEMS)
    || (halFunctions[subsystem] == NULL)
    || (function >= halFunctionCounts[subsystem])
    || (halFunctions[subsystem][function] == NULL)
  ) {
    return -ENOTSUP;
  } else if (processDescriptor != NULL) {
    if ((processDescriptor->privilegeLevel != PRIVILEGE_LEVEL_KERNEL) 
      && (findHalCapability(processDescriptor->halCapabilities,
        processDescriptor->numHalCapabilities, subsystem, function) == NULL)
    ) {
      return -EACCES;
    }
  }
  va_list args;
  va_start(args, function);
  int32_t returnValue = halFunctions[subsystem][function](args);
  va_end(args);
  return returnValue;
}

// ---------------------------------------------------------------------------
// Forward declarations for typed wrappers (defined later in this file).
// ---------------------------------------------------------------------------
static int32_t halMemoryProcessStackSize(bool debug, size_t *returnValue);
static int32_t halMemoryMemoryManagerStackSize(bool debug, size_t *returnValue);
static int32_t halMemoryBottomOfHeap(bool debug, void **returnValue);
static int32_t halMemoryNumExtraSchedulerStacks(bool debug, uint8_t *returnValue);
static int32_t halMemoryNumExtraConsoleStacks(bool debug, uint8_t *returnValue);

static int32_t halUartInit(void);
static int32_t halUartConfigure(int32_t deviceId, uint32_t baud);
static int32_t halUartPoll(int32_t deviceId);
static int32_t halUartWrite(int32_t deviceId, const uint8_t *data,
  ssize_t length, ssize_t *returnValue);
static int32_t halUartIsConsole(int32_t deviceId, bool *returnValue);

static int32_t halDioInit(void);
static int32_t halDioConfigure(int32_t deviceId, bool output);
static int32_t halDioWrite(int32_t deviceId, bool high);

static int32_t halSpiInit(void);
static int32_t halSpiConfigure(int32_t deviceId,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud);
static int32_t halSpiStartTransfer(int32_t deviceId);
static int32_t halSpiEndTransfer(int32_t deviceId);
static int32_t halSpiTransfer8(int32_t deviceId, uint8_t data);
static int32_t halSpiTransferBytes(int32_t deviceId,
  uint8_t *data, uint32_t length);

static int32_t halClockInit(void);
static int32_t halClockSetSystemTime(struct timespec *ts);
static int32_t halClockGetElapsedMilliseconds(int64_t startTime,
  int64_t *returnValue);
static int32_t halClockGetElapsedMicroseconds(int64_t startTime,
  int64_t *returnValue);
static int32_t halClockGetElapsedNanoseconds(int64_t startTime,
  int64_t *returnValue);

static int32_t halPowerEnterMode(HalPowerMode powerMode);

static int32_t halTimerInit(void);
static int32_t halTimerInitDevice(int32_t deviceId);
static int32_t halTimerConfigOneShot(int32_t deviceId,
  uint64_t nanoseconds, void (*callback)(void));
static int32_t halTimerConfiguredNanoseconds(int32_t deviceId,
  uint64_t *returnValue);
static int32_t halTimerRemainingNanoseconds(int32_t deviceId,
  uint64_t *returnValue);
static int32_t halTimerCancel(int32_t deviceId);
static int32_t halTimerCancelAndGet(int32_t deviceId,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void));

static int32_t halBlockDeviceInit(void);
static int32_t halBlockDeviceGet(int32_t deviceId, BlockDevice **returnValue);
static int32_t halBlockDeviceRestart(ProcessDescriptor *processDescriptor);

// ---------------------------------------------------------------------------
// Common subsystem instances — function pointers set to typed wrappers above;
// data members (numSupported, online, overlayMap, etc.) set by platform init.
// ---------------------------------------------------------------------------

HalMemory halCommonMemory = {
  .processStackSize        = halMemoryProcessStackSize,
  .memoryManagerStackSize  = halMemoryMemoryManagerStackSize,
  .bottomOfHeap            = halMemoryBottomOfHeap,
  .numExtraSchedulerStacks = halMemoryNumExtraSchedulerStacks,
  .numExtraConsoleStacks   = halMemoryNumExtraConsoleStacks,
  .overlayMap              = NULL,
  .overlaySize             = 0,
};

HalUart halCommonUart = {
  .numSupported = 0,
  .online       = NULL,
  .init         = halUartInit,
  .configure    = halUartConfigure,
  .poll         = halUartPoll,
  .write        = halUartWrite,
  .isConsole    = halUartIsConsole,
};

HalDio halCommonDio = {
  .numSupported = 0,
  .online       = NULL,
  .init         = halDioInit,
  .configure    = halDioConfigure,
  .write        = halDioWrite,
};

HalSpi halCommonSpi = {
  .numSupported  = 0,
  .online        = NULL,
  .init          = halSpiInit,
  .configure     = halSpiConfigure,
  .startTransfer = halSpiStartTransfer,
  .endTransfer   = halSpiEndTransfer,
  .transfer8     = halSpiTransfer8,
  .transferBytes = halSpiTransferBytes,
};

HalClock halCommonClock = {
  .init                   = halClockInit,
  .setSystemTime          = halClockSetSystemTime,
  .getElapsedMilliseconds = halClockGetElapsedMilliseconds,
  .getElapsedMicroseconds = halClockGetElapsedMicroseconds,
  .getElapsedNanoseconds  = halClockGetElapsedNanoseconds,
};

HalPower halCommonPower = {
  .enterMode = halPowerEnterMode,
};

HalTimer halCommonTimer = {
  .numSupported        = 0,
  .online              = NULL,
  .init                = halTimerInit,
  .initDevice          = halTimerInitDevice,
  .configOneShot       = halTimerConfigOneShot,
  .configuredNanoseconds = halTimerConfiguredNanoseconds,
  .remainingNanoseconds  = halTimerRemainingNanoseconds,
  .cancel              = halTimerCancel,
  .cancelAndGet        = halTimerCancelAndGet,
};

HalBlockDevice halCommonBlockDevice = {
  .numSupported = 0,
  .online       = NULL,
  .init         = halBlockDeviceInit,
  .get          = halBlockDeviceGet,
  .restart      = halBlockDeviceRestart,
};

static Hal halCommonHal = {
  .memory      = &halCommonMemory,
  .uart        = &halCommonUart,
  .dio         = &halCommonDio,
  .spi         = &halCommonSpi,
  .clock       = &halCommonClock,
  .power       = &halCommonPower,
  .timer       = &halCommonTimer,
  .blockDevice = &halCommonBlockDevice,

  .initRootStorage = halCommonInitRootFilesystem,
};

/// @var HAL
///
/// @brief Global pointer to the active HAL instance.
const Hal *HAL = &halCommonHal;

// ---------------------------------------------------------------------------
// Typed wrapper implementations — each calls callHal with the subsystem and
// function enum values, forwarding all typed parameters as varargs.
// ---------------------------------------------------------------------------

static int32_t halMemoryProcessStackSize(bool debug, size_t *returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_PROCESS_STACK_SIZE,
    debug, returnValue);
}

static int32_t halMemoryMemoryManagerStackSize(bool debug,
  size_t *returnValue
) {
  return callHal(HAL_MEMORY, HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE,
    debug, returnValue);
}

static int32_t halMemoryBottomOfHeap(bool debug, void **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_BOTTOM_OF_HEAP, debug, returnValue);
}

static int32_t halMemoryNumExtraSchedulerStacks(bool debug,
  uint8_t *returnValue
) {
  return callHal(HAL_MEMORY, HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS,
    debug, returnValue);
}

static int32_t halMemoryNumExtraConsoleStacks(bool debug,
  uint8_t *returnValue
) {
  return callHal(HAL_MEMORY, HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS,
    debug, returnValue);
}

static int32_t halUartInit(void) {
  return callHal(HAL_UART, HAL_UART_INIT);
}

static int32_t halUartConfigure(int32_t deviceId, uint32_t baud) {
  return callHal(HAL_UART, HAL_UART_CONFIGURE, deviceId, baud);
}

static int32_t halUartPoll(int32_t deviceId) {
  return callHal(HAL_UART, HAL_UART_POLL, deviceId);
}

static int32_t halUartWrite(int32_t deviceId, const uint8_t *data,
  ssize_t length, ssize_t *returnValue
) {
  return callHal(HAL_UART, HAL_UART_WRITE, deviceId, data, length,
    returnValue);
}

static int32_t halUartIsConsole(int32_t deviceId, bool *returnValue) {
  return callHal(HAL_UART, HAL_UART_IS_CONSOLE, deviceId, returnValue);
}

static int32_t halDioInit(void) {
  return callHal(HAL_DIO, HAL_DIO_INIT);
}

static int32_t halDioConfigure(int32_t deviceId, bool output) {
  return callHal(HAL_DIO, HAL_DIO_CONFIGURE, deviceId, output);
}

static int32_t halDioWrite(int32_t deviceId, bool high) {
  return callHal(HAL_DIO, HAL_DIO_WRITE, deviceId, high);
}

static int32_t halSpiInit(void) {
  return callHal(HAL_SPI, HAL_SPI_INIT);
}

static int32_t halSpiConfigure(int32_t deviceId,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud
) {
  return callHal(HAL_SPI, HAL_SPI_CONFIGURE,
    deviceId, (int) cs, (int) sck, (int) copi, (int) cipo, baud);
}

static int32_t halSpiStartTransfer(int32_t deviceId) {
  return callHal(HAL_SPI, HAL_SPI_START_TRANSFER, deviceId);
}

static int32_t halSpiEndTransfer(int32_t deviceId) {
  return callHal(HAL_SPI, HAL_SPI_END_TRANSFER, deviceId);
}

static int32_t halSpiTransfer8(int32_t deviceId, uint8_t data) {
  return callHal(HAL_SPI, HAL_SPI_TRANSFER8, deviceId, (int) data);
}

static int32_t halSpiTransferBytes(int32_t deviceId,
  uint8_t *data, uint32_t length
) {
  return callHal(HAL_SPI, HAL_SPI_TRANSFER_BYTES, deviceId, data, length);
}

static int32_t halClockInit(void) {
  return callHal(HAL_CLOCK, HAL_CLOCK_INIT);
}

static int32_t halClockSetSystemTime(struct timespec *ts) {
  return callHal(HAL_CLOCK, HAL_CLOCK_SET_SYSTEM_TIME, ts);
}

static int32_t halClockGetElapsedMilliseconds(int64_t startTime,
  int64_t *returnValue
) {
  return callHal(HAL_CLOCK, HAL_CLOCK_GET_ELAPSED_MILLISECONDS,
    startTime, returnValue);
}

static int32_t halClockGetElapsedMicroseconds(int64_t startTime,
  int64_t *returnValue
) {
  return callHal(HAL_CLOCK, HAL_CLOCK_GET_ELAPSED_MICROSECONDS,
    startTime, returnValue);
}

static int32_t halClockGetElapsedNanoseconds(int64_t startTime,
  int64_t *returnValue
) {
  return callHal(HAL_CLOCK, HAL_CLOCK_GET_ELAPSED_NANOSECONDS,
    startTime, returnValue);
}

static int32_t halPowerEnterMode(HalPowerMode powerMode) {
  return callHal(HAL_POWER, HAL_POWER_ENTER_MODE, (int) powerMode);
}

static int32_t halTimerInit(void) {
  return callHal(HAL_TIMER, HAL_TIMER_INIT);
}

static int32_t halTimerInitDevice(int32_t deviceId) {
  return callHal(HAL_TIMER, HAL_TIMER_INIT_DEVICE, deviceId);
}

static int32_t halTimerConfigOneShot(int32_t deviceId,
  uint64_t nanoseconds, void (*callback)(void)
) {
  return callHal(HAL_TIMER, HAL_TIMER_CONFIG_ONE_SHOT,
    deviceId, nanoseconds, callback);
}

static int32_t halTimerConfiguredNanoseconds(int32_t deviceId,
  uint64_t *returnValue
) {
  return callHal(HAL_TIMER, HAL_TIMER_CONFIGURED_NANOSECONDS,
    deviceId, returnValue);
}

static int32_t halTimerRemainingNanoseconds(int32_t deviceId,
  uint64_t *returnValue
) {
  return callHal(HAL_TIMER, HAL_TIMER_REMAINING_NANOSECONDS,
    deviceId, returnValue);
}

static int32_t halTimerCancel(int32_t deviceId) {
  return callHal(HAL_TIMER, HAL_TIMER_CANCEL, deviceId);
}

static int32_t halTimerCancelAndGet(int32_t deviceId,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void)
) {
  return callHal(HAL_TIMER, HAL_TIMER_CANCEL_AND_GET,
    deviceId, configuredNanoseconds, remainingNanoseconds, callback);
}

static int32_t halBlockDeviceInit(void) {
  return callHal(HAL_BLOCK_DEVICE, HAL_BLOCK_DEVICE_INIT);
}

static int32_t halBlockDeviceGet(int32_t deviceId, BlockDevice **returnValue) {
  return callHal(HAL_BLOCK_DEVICE, HAL_BLOCK_DEVICE_GET,
    deviceId, returnValue);
}

static int32_t halBlockDeviceRestart(ProcessDescriptor *processDescriptor) {
  return callHal(HAL_BLOCK_DEVICE, HAL_BLOCK_DEVICE_RESTART,
    processDescriptor);
}

// ---------------------------------------------------------------------------
// Common HAL helper implementations.
// ---------------------------------------------------------------------------

/// @fn BlockDevice* halCommonInitRootSdSpiStorage(
///   SdCardSpiArgs *sdCardSpiArgs)
///
/// @brief Common routine for initializing root storage using an SD card over
/// SPI.
///
/// @param sdCardSpiArgs A pointer to an SdCardSpiArgs structure that contains
///   the values to pass to runSdCardSpi.
///
/// @return Returns the initialized BlockDevice pointer on success, NULL on
/// failure.
BlockDevice* halCommonInitRootSdSpiStorage(
  SdCardSpiArgs *sdCardSpiArgs
) {
  ProcessDescriptor *allProcesses = SCHEDULER_STATE->allProcesses;

  // Create the SD card process.
  ProcessDescriptor *processDescriptor
    = &allProcesses[SCHEDULER_STATE->firstUserPid - 1];
  if (processCreate(
    processDescriptor, runSdCardSpi, sdCardSpiArgs)
    != processSuccess
  ) {
    printString("Could not start SD card process\n");
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = SCHEDULER_STATE->firstUserPid;
  processDescriptor->name = "SD card";
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_KERNEL;
  processDescriptor->restartFunction = HAL->blockDevice->restart;
  processDescriptor->restartArgs = (void*)(intptr_t)0;
  BlockDevice *sdDevice = (BlockDevice*) coroutineResume(
    allProcesses[SCHEDULER_STATE->firstUserPid - 1].mainThread, NULL);
  sdDevice->partitionNumber = 1;
  SCHEDULER_STATE->firstUserPid++;
  SCHEDULER_STATE->firstShellPid = SCHEDULER_STATE->firstUserPid;

  return sdDevice;
}

/// @fn int32_t halCommonInitRootFilesystem(void)
///
/// @brief Common initialization for the root filesystem process.
///
/// @return Returns 0 on success, -errno on failure.
int32_t halCommonInitRootFilesystem() {
  if (SCHEDULER_STATE == NULL) {
    return -EBUSY;
  }

  BlockDevice *rootBlockDevice = NULL;
  HAL->blockDevice->get(0, &rootBlockDevice);

  if (rootBlockDevice == NULL) {
    if (HAL->blockDevice == NULL) {
      printString("ERROR! HAL->blockDevice is NULL\n");
      return -ENODEV;
    }

    if (HAL->blockDevice->init() != 0) {
      printString("ERROR! HAL->blockDevice->init() failed\n");
      return -ENODEV;
    }

    HAL->blockDevice->get(0, &rootBlockDevice);
  }

  if (rootBlockDevice == NULL) {
    printString("ERROR! No rootBlockDevice available\n");
    return -ENODEV;
  }

  // Allocate the filesystem process.
  SCHEDULER_STATE->rootFsPid = SCHEDULER_STATE->firstUserPid;
  ProcessDescriptor *allProcesses = SCHEDULER_STATE->allProcesses;
  ProcessDescriptor *processDescriptor
    = &allProcesses[SCHEDULER_STATE->rootFsPid - 1];
  if (processCreate(processDescriptor, dummyProcess, NULL) != processSuccess) {
    printString("Could not allocate filesystem process\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = SCHEDULER_STATE->rootFsPid;
  processDescriptor->name = "filesystem";
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  processDescriptor->restartFunction = restartFilesystem;
  // DO NOT resume the process yet.  Let the scheduler take care of that.

  SCHEDULER_STATE->firstUserPid = SCHEDULER_STATE->rootFsPid + 1;
  SCHEDULER_STATE->firstShellPid = SCHEDULER_STATE->firstUserPid;

  // The filesystem runs as an overlay, which increases the depth of the call
  // stack.  Double the stack size for it.
  Thread *thread = threadProvision(NULL, dummyProcess, NULL);
  if (thread == NULL) {
    printString("Could not increase filesystem process's stack size.\n");
    return -ENOMEM;
  }
  if (threadSetStackEnd(
    processDescriptor->mainThread, threadStackEnd(thread)) != processSuccess
  ) {
    printString("Could not set filesystem process's stack size.\n");
  }

  return 0;
}

/// @fn int32_t restartFilesystem(ProcessDescriptor *processDescriptor)
///
/// @brief Restart the filesystem process using the existing root block device.
///
/// @param processDescriptor A pointer to the ProcessDescriptor of the
///   filesystem process to restart.
///
/// @return Returns 0 on success, -errno on failure.
int32_t restartFilesystem(ProcessDescriptor *processDescriptor) {
  BlockDevice *rootBlockDevice = NULL;
  HAL->blockDevice->get(0, &rootBlockDevice);
  if (rootBlockDevice == NULL) {
    return -ENODEV;
  }

  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = rootBlockDevice;
  fs.blockSize = fs.blockDevice->blockSize;
  fs.driverInit = fat32Initialize;
  fs.driverFopen = fat32Fopen;
  fs.driverFread = fat32Fread;
  fs.driverFwrite = fat32Fwrite;
  fs.driverFclose = fat32Fclose;
  fs.driverRemove = fat32Remove;
  fs.driverFseek = fat32Fseek;
  fs.driverGetFileBlockMetadata = fat32GetFileBlockMetadata;
  fs.driverGetFilename = fat32GetFilename;

  //// BlockOverlayArgs blockOverlayArgs = {
  ////   .blockDevice = rootBlockDevice,
  ////   .startBlock = 1,
  ////   .args = &fs,
  //// };
  //// if (processCreate(processDescriptor, runBlockOverlay, &blockOverlayArgs)
  ////   != processSuccess
  //// ) {
  ////   printString("Could not restart filesystem process.\n");
  ////   return -ENOMEM;
  //// }
  if (processCreate(processDescriptor, runFilesystem, &fs)
    != processSuccess
  ) {
    printString("Could not restart filesystem process.\n");
    return -ENOMEM;
  }

  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = "filesystem";
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  processDescriptor->restartFunction = restartFilesystem;
  processDescriptor->callOverlayFunction = callOverlayFunctionFromBlockDevice;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  // Let the filesystem process initialize before we return.
  while (fs.driverState == NULL) {
    SCHEDULER_STATE->runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
  }

  return 0;
}

/// @fn int32_t halCommonInit(void)
///
/// @brief Initialization function common to multiple HAL implementations.
/// Uses the global HAL pointer to call subsystem init and configure functions.
///
/// @return Returns 0 on success, -errno on failure.
int32_t halCommonInit(void) {
  if (HAL == NULL) {
    return -EINVAL;
  }

  int32_t ii = 0;
  int32_t defaultUart = -1;

  if (HAL->uart != NULL) {
    if (HAL->uart->init() < 0) {
      return -ENOTTY;
    }
    int32_t numUarts = HAL->uart->numSupported;
    if (numUarts <= 0) {
      // Nothing we can do.
      return -ENOTTY;
    }

    for (ii = 0; ii < numUarts; ii++) {
      if (!online(HAL->uart, ii)) {
        continue;
      }

      if (HAL->uart->configure(ii, 1000000) == 0) {
        if (defaultUart < 0) {
          defaultUart = ii;
        }
      } else {
        setOffline(HAL->uart, ii);
      }
    }
  }

  if (HAL->dio != NULL) {
    if (HAL->dio->init() != 0) {
      HAL->uart->write(defaultUart,
        (uint8_t*) "WARNING: Failed to initialize DIO subsystem\n",
        strlen("WARNING: Failed to initialize DIO subsystem\n"), NULL);
    }
  }

  if (HAL->spi != NULL) {
    if (HAL->spi->init() != 0) {
      HAL->uart->write(defaultUart,
        (uint8_t*) "WARNING: Failed to initialize SPI subsystem\n",
        strlen("WARNING: Failed to initialize SPI subsystem\n"), NULL);
    }
  }

  if (HAL->clock != NULL) {
    if (HAL->clock->init() != 0) {
      HAL->uart->write(defaultUart,
        (uint8_t*) "WARNING: Failed to initialize clock subsystem\n",
        strlen("WARNING: Failed to initialize clock subsystem\n"), NULL);
    }
  }

  if (HAL->timer != NULL)  {
    do {
      if (HAL->timer->init() != 0) {
        HAL->uart->write(defaultUart,
          (uint8_t*) "WARNING: Failed to initialize timer subsystem\n",
          strlen("WARNING: Failed to initialize timer subsystem\n"), NULL);
        break;
      }

      uint32_t timerOnline = HAL->timer->online[0];
      for (ii = 0; ii < (int32_t) HAL->timer->numSupported; ii++) {
        if (online(HAL->timer, ii) == false) {
          continue;
        }

        if (HAL->timer->initDevice(ii) < 0) {
          setOffline(HAL->timer, ii);
        }
      }

      if (HAL->timer->online[0] != timerOnline) {
        if (HAL->uart != NULL) {
          HAL->uart->write(defaultUart,
            (uint8_t*) "WARNING: Did not initialize all timers\n",
            strlen("WARNING: Did not initialize all timers\n"), NULL);
        }
      }
    } while (0);
  }

  return 0;
}
