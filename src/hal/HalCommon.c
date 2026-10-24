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

// Standard C includes
#include <stdarg.h>
#include <stdint.h>

// Unix includes
#include "sys/types.h"

// Must come first
#include "../user/NanoOsApi.h"

#define gid_t C_gid_t
#define uid_t C_uid_t
#define pid_t C_pid_t
#include "HalCommon.h"
#undef gid_t
#undef uid_t
#undef pid_t
// See src/include/sys/types.h: clear the guard flags the real library's
// (renamed) typedefs above set, so that header's own gid_t/uid_t/pid_t,
// reached later in this file, don't wrongly defer to them.
#undef __gid_t_defined
#undef _GID_T_DECLARED
#undef __uid_t_defined
#undef _UID_T_DECLARED
#undef __pid_t_defined
#undef _PID_T_DECLARED

// NanoOs includes
#include "../kernel/BootTrace.h"
#include "../kernel/Console.h"
#include "../kernel/Coroutines.h"
#include "../kernel/MemoryManager.h"
#include "../kernel/NanoOs.h"
#include "../kernel/Overlay.h"
#include "../kernel/OverlayFunctions.h"
#include "../kernel/Processes.h"
#include "../kernel/Scheduler.h"
#include "../user/NanoOsFcntl.h"
#include "../user/NanoOsLibC.h"
#include "../user/NanoOsPwd.h"
#include "../user/NanoOsSched.h"
#include "../user/NanoOsSignal.h"
#include "../user/NanoOsTermios.h"
#include "../user/NanoOsUnistd.h"

// Must come last
#include "../user/NanoOsStdio.h"

// Functionality needed from Scheduler.c
void runSchedulerQueues(PrivilegeLevel privilegeLevelBound);

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
static const uint32_t halFunctionCounts[HAL_NUM_SUBSYSTEMS] KEEP_IN_FLASH = {
  [HAL_PLATFORM]     = HAL_PLATFORM_NUM_FNS,
  [HAL_MEMORY]       = HAL_MEMORY_NUM_FNS,
  [HAL_UART]         = HAL_UART_NUM_FNS,
  [HAL_DIO]          = HAL_DIO_NUM_FNS,
  [HAL_SPI]          = HAL_SPI_NUM_FNS,
  [HAL_CLOCK]        = HAL_CLOCK_NUM_FNS,
  [HAL_POWER]        = HAL_POWER_NUM_FNS,
  [HAL_TIMER]        = HAL_TIMER_NUM_FNS,
  [HAL_BLOCK_DEVICE] = HAL_BLOCK_DEVICE_NUM_FNS,
};

/// @fn int callHal(HalSubsystem subsystem, uint32_t function,
///   int32_t deviceId, ...)
///
/// @brief Dispatch a call to the registered platform-specific HAL function.
///
/// @param subsystem The HalSubsystem index into halFunctions.
/// @param function The function-name enum index for the given subsystem.
/// @param deviceId The device within the subsystem that the call acts on, or
///   0 for a function that takes no device.
/// @param ... Arguments to forward to the platform function via va_list.
///
/// @return Returns the value returned by the platform function, -ENOTSUP if
/// no function has been registered for the given subsystem/function pair, or
/// -EACCES if the running process may not call the function on deviceId.
int callHal(HalSubsystem subsystem, uint32_t function, int32_t deviceId, ...) {
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if ((subsystem >= HAL_NUM_SUBSYSTEMS)
    || (halFunctions[subsystem] == NULL)
    || (function >= halFunctionCounts[subsystem])
    || (halFunctions[subsystem][function] == NULL)
  ) {
    return -ENOTSUP;
  } else if (processDescriptor != NULL) {
    // Only the scheduler gets an unconditional pass here.  Every other
    // process -- including the other PRIVILEGE_LEVEL_KERNEL ones (console,
    // memory manager, the SD-over-SPI card process) -- needs a real,
    // explicit grant in its own halCapabilities array, for the device too.
    uint8_t bit = halCapabilityBitBase[subsystem] + (uint8_t) function;
    if ((processDescriptor->processId != schedulerPid)
      && (((processDescriptor->halCapabilityBitmap[bit >> 3]
          & (1 << (bit & 7))) == 0)
        || (((uint32_t) deviceId) >= 8)
        || ((processDescriptor->halDeviceMasks[subsystem]
          & (1 << deviceId)) == 0))
    ) {
      return -EACCES;
    }
  }
  va_list args;
  va_start(args, deviceId);
  int returnValue = halFunctions[subsystem][function](deviceId, args);
  va_end(args);
  return returnValue;
}

// ---------------------------------------------------------------------------
// Forward declarations for typed wrappers (defined later in this file).
// ---------------------------------------------------------------------------
static int halPlatformCallFileOverlay(HalCallFileOverlayFn *returnValue);
static int halPlatformExecCommand(HalExecCommandFn *returnValue);
static int halPlatformRestartRootFilesystem(
  HalRestartRootFilesystemFn *returnValue);
static int halPlatformRestartShell(HalRestartShellFn *returnValue);
static int halPlatformStartProcesses(HalStartProcessesFn *returnValue);
static int halPlatformInvokePendingCallbacks(void);
static int halPlatformLaunchProcesses(HalLaunchProcessesFn *returnValue);

static int halMemoryProcessStackSize(bool debug, size_t *returnValue);
static int halMemoryMemoryManagerStackSize(bool debug, size_t *returnValue);
static int halMemoryBottomOfHeap(bool debug, void **returnValue);
static int halMemoryNumExtraSchedulerStacks(bool debug, uint8_t *returnValue);
static int halMemoryNumExtraConsoleStacks(bool debug, uint8_t *returnValue);
static int halMemoryOverlayMap(NanoOsOverlayMap **returnValue);
static int halMemoryContiguousFilesystem(NanoOsOverlayMap **returnValue);
static int halMemoryStaticLogs(StaticLogs **returnValue);
static int halMemoryLogBuffer(char **returnValue);
static int halMemoryLogEntries(LogEntry **returnValue);
static int halMemoryLogMessages(ProcessMessage **returnValue);
static int halMemoryAllProcesses(ProcessDescriptor **returnValue);
static int halMemoryReadyQueues(ProcessQueue ***returnValue);
static int halMemoryWaitingQueue(ProcessQueue **returnValue);
static int halMemoryTimedWaitingQueue(ProcessQueue **returnValue);
static int halMemoryFreeQueue(ProcessQueue **returnValue);
static int halMemoryProcessErrorNumbers(int **returnValue);
static int halMemoryProcessStorage(void ****returnValue);

static int halUartInit(void);
static int halUartConfigure(int32_t deviceId, uint32_t baud);
static int halUartRegisterCallback(int32_t deviceId,
  ProcessDescriptor *processDescriptor, int64_t messageType, void *priv);
static int halUartPoll(int32_t deviceId);
static int halUartRead(int32_t deviceId, uint8_t *data,
  ssize_t length, ssize_t *returnValue);
static int halUartWrite(int32_t deviceId, const uint8_t *data,
  ssize_t length, ssize_t *returnValue);
static int halUartIsConsole(int32_t deviceId, bool *returnValue);

static int halDioInit(void);
static int halDioConfigure(int32_t deviceId, bool output);
static int halDioWrite(int32_t deviceId, bool high);

static int halSpiInit(void);
static int halSpiConfigure(int32_t deviceId,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud);
static int halSpiSetSpeed(int32_t deviceId, uint32_t baud);
static int halSpiStartTransfer(int32_t deviceId);
static int halSpiEndTransfer(int32_t deviceId);
static int halSpiTransfer8(int32_t deviceId, uint8_t data);
static int halSpiTransferBytes(int32_t deviceId,
  uint8_t *data, uint32_t length);
static int halSpiTransferUntil(int32_t deviceId, uint8_t data,
  uint8_t mask, uint8_t value, bool equal, uint16_t maxTransfers);

static int halClockInit(void);
static int halClockSetSystemTime(struct timespec *ts);
static int halClockGetElapsedMilliseconds(int64_t startTime,
  int64_t *returnValue);
static int halClockGetElapsedMicroseconds(int64_t startTime,
  int64_t *returnValue);
static int halClockGetElapsedNanoseconds(int64_t startTime,
  int64_t *returnValue);

static int halPowerEnterMode(HalPowerMode powerMode);

static int halTimerInit(void);
static int halTimerInitDevice(int32_t deviceId);
static int halTimerConfigOneShot(int32_t deviceId,
  uint64_t nanoseconds, void (*callback)(void));
static int halTimerConfiguredNanoseconds(int32_t deviceId,
  uint64_t *returnValue);
static int halTimerRemainingNanoseconds(int32_t deviceId,
  uint64_t *returnValue);
static int halTimerCancel(int32_t deviceId);
static int halTimerCancelAndGet(int32_t deviceId,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void));

static int halBlockDeviceInit(void);
static int halBlockDeviceGet(int32_t deviceId, BlockDevice **returnValue);
static int halBlockDeviceRestart(ProcessDescriptor *processDescriptor);

// ---------------------------------------------------------------------------
// Common subsystem instances — function pointers set to typed wrappers
// above; data members (numSupported, online, overlayMap, etc.) set by
// platform init code, which writes to halImpl's pointers directly since it
// needs a mutable view.  The read-only HAL pointer below points at the same
// halImpl instance for general use.
//
// halImpl starts with every subsystem pointer NULL.  Each platform init
// points halImpl's members at the instances below for the subsystems that
// platform supports and leaves the rest NULL.
// ---------------------------------------------------------------------------

HalPlatform halImplPlatform = {
  .callFileOverlay        = halPlatformCallFileOverlay,
  .execCommand            = halPlatformExecCommand,
  .restartRootFilesystem  = halPlatformRestartRootFilesystem,
  .restartShell           = halPlatformRestartShell,
  .startProcesses         = halPlatformStartProcesses,
  .invokePendingCallbacks = halPlatformInvokePendingCallbacks,
  .launchProcesses        = halPlatformLaunchProcesses,
};

HalMemory halImplMemory = {
  .processStackSize        = halMemoryProcessStackSize,
  .memoryManagerStackSize  = halMemoryMemoryManagerStackSize,
  .bottomOfHeap            = halMemoryBottomOfHeap,
  .numExtraSchedulerStacks = halMemoryNumExtraSchedulerStacks,
  .numExtraConsoleStacks   = halMemoryNumExtraConsoleStacks,
  .overlayMap              = halMemoryOverlayMap,
  .overlaySize             = 0,
  .contiguousFilesystem    = halMemoryContiguousFilesystem,
  .stringsPresent          = false,
  .staticLogs              = halMemoryStaticLogs,
  .logBuffer               = halMemoryLogBuffer,
  .logBufferSize           = 0,
  .logEntries              = halMemoryLogEntries,
  .logMessages             = halMemoryLogMessages,
  .allProcesses            = halMemoryAllProcesses,
  .readyQueues             = halMemoryReadyQueues,
  .waitingQueue            = halMemoryWaitingQueue,
  .timedWaitingQueue       = halMemoryTimedWaitingQueue,
  .freeQueue               = halMemoryFreeQueue,
  .processErrorNumbers     = halMemoryProcessErrorNumbers,
  .processStorage          = halMemoryProcessStorage,
};

HalUart halImplUart = {
  .numSupported           = 0,
  .online                 = NULL,
  .pollOnly               = NULL,
  .init                   = halUartInit,
  .configure              = halUartConfigure,
  .registerCallback       = halUartRegisterCallback,
  .poll                   = halUartPoll,
  .read                   = halUartRead,
  .write                  = halUartWrite,
  .isConsole              = halUartIsConsole,
};

HalDio halImplDio = {
  .numSupported = 0,
  .online       = NULL,
  .init         = halDioInit,
  .configure    = halDioConfigure,
  .write        = halDioWrite,
};

HalSpi halImplSpi = {
  .numSupported  = 0,
  .online        = NULL,
  .init          = halSpiInit,
  .configure     = halSpiConfigure,
  .setSpeed      = halSpiSetSpeed,
  .startTransfer = halSpiStartTransfer,
  .endTransfer   = halSpiEndTransfer,
  .transfer8     = halSpiTransfer8,
  .transferBytes = halSpiTransferBytes,
  .transferUntil = halSpiTransferUntil,
};

HalClock halImplClock = {
  .init                   = halClockInit,
  .setSystemTime          = halClockSetSystemTime,
  .getElapsedMilliseconds = halClockGetElapsedMilliseconds,
  .getElapsedMicroseconds = halClockGetElapsedMicroseconds,
  .getElapsedNanoseconds  = halClockGetElapsedNanoseconds,
};

HalPower halImplPower = {
  .enterMode = halPowerEnterMode,
};

HalTimer halImplTimer = {
  .numSupported          = 0,
  .online                = NULL,
  .init                  = halTimerInit,
  .initDevice            = halTimerInitDevice,
  .configOneShot         = halTimerConfigOneShot,
  .configuredNanoseconds = halTimerConfiguredNanoseconds,
  .remainingNanoseconds  = halTimerRemainingNanoseconds,
  .cancel                = halTimerCancel,
  .cancelAndGet          = halTimerCancelAndGet,
};

HalBlockDevice halImplBlockDevice = {
  .numSupported = 0,
  .online       = NULL,
  .init         = halBlockDeviceInit,
  .get          = halBlockDeviceGet,
  .restart      = halBlockDeviceRestart,
};

Hal halImpl = {
  .platform    = NULL,
  .memory      = NULL,
  .uart        = NULL,
  .dio         = NULL,
  .spi         = NULL,
  .clock       = NULL,
  .power       = NULL,
  .timer       = NULL,
  .blockDevice = NULL,
};

/// @var HAL
///
/// @brief Global, read-only pointer to the active, root HAL instance.
const Hal *HAL = &halImpl;

/// @var namedProcessTable
///
/// @brief See the declaration in HalCommon.h.  Defaults to NULL; a platform
/// that wants named-process lookup points this at its own storage.
NamedProcessEntry *namedProcessTable = NULL;

/// @var namedProcessTableCapacity
///
/// @brief See the declaration in HalCommon.h.
uint8_t namedProcessTableCapacity = 0;

/// @var filesystemIpcCapabilities
///
/// @brief See the declaration in HalCommon.h.  Defaults to NULL; a platform
/// that starts a filesystem process points this at its own storage.
IpcCapability *filesystemIpcCapabilities = NULL;

/// @var numFilesystemIpcCapabilities
///
/// @brief See the declaration in HalCommon.h.
size_t numFilesystemIpcCapabilities = 0;

/// @var loggerIpcCapabilities
///
/// @brief See the declaration in HalCommon.h.  Defaults to NULL; a platform
/// that starts a logger points this at its own storage.
IpcCapability *loggerIpcCapabilities = NULL;

/// @var numLoggerIpcCapabilities
///
/// @brief See the declaration in HalCommon.h.
size_t numLoggerIpcCapabilities = 0;

/// @var sdCardHalCapabilities
///
/// @brief See the declaration in HalCommon.h.  Defaults to NULL; a platform
/// that starts an SD-over-SPI card process points this at its own storage.
HalCapability *sdCardHalCapabilities = NULL;

/// @var numSdCardHalCapabilities
///
/// @brief See the declaration in HalCommon.h.
size_t numSdCardHalCapabilities = 0;

/// @fn int findProcessByName(const char *name, ProcessId *returnValue)
///
/// @brief Look up a process's PID by the name it was registered under.
///
/// @param name The name to search for.
/// @param returnValue A pointer to a ProcessId that will hold the result on
///   success.
///
/// @return Returns 0 on success, -errno on failure.
int findProcessByName(const char *name, ProcessId *returnValue) {
  if ((name == NULL) || (returnValue == NULL)) {
    return -EINVAL;
  }

  for (uint8_t ii = 0; ii < namedProcessTableCapacity; ii++) {
    if (namedProcessTable[ii].name == NULL) {
      break;
    }
    if (strcmp(namedProcessTable[ii].name, name) == 0) {
      *returnValue = namedProcessTable[ii].pid;
      return 0;
    }
  }

  return -ENOENT;
}

/// @fn int registerProcessName(const char *name, ProcessId pid)
///
/// @brief Register a process's PID under a name, so it can later be found
/// with findProcessByName.
///
/// @param name The name to register.  Only its pointer is stored, so it
///   must remain valid for the life of the system (a KEEP_IN_FLASH string
///   literal, as used elsewhere in the HAL, satisfies this).
/// @param pid The ProcessId to associate with name.
///
/// @return Returns 0 on success, -errno on failure.
int registerProcessName(const char *name, ProcessId pid) {
  if (name == NULL) {
    return -EINVAL;
  }

  for (uint8_t ii = 0; ii < namedProcessTableCapacity; ii++) {
    if (namedProcessTable[ii].name == NULL) {
      namedProcessTable[ii].name = name;
      namedProcessTable[ii].pid = pid;
      return 0;
    }
  }

  return -ENOSPC;
}

// ---------------------------------------------------------------------------
// Typed wrapper implementations — each calls callHal with the subsystem and
// function enum values, forwarding all typed parameters as varargs.
// ---------------------------------------------------------------------------

static int halMemoryProcessStackSize(bool debug, size_t *returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_PROCESS_STACK_SIZE, 0,
    debug, returnValue);
}

static int halMemoryMemoryManagerStackSize(bool debug,
  size_t *returnValue
) {
  return callHal(HAL_MEMORY, HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE, 0,
    debug, returnValue);
}

static int halMemoryBottomOfHeap(bool debug, void **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_BOTTOM_OF_HEAP, 0, debug, returnValue);
}

static int halMemoryNumExtraSchedulerStacks(bool debug,
  uint8_t *returnValue
) {
  return callHal(HAL_MEMORY, HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS, 0,
    debug, returnValue);
}

static int halMemoryNumExtraConsoleStacks(bool debug,
  uint8_t *returnValue
) {
  return callHal(HAL_MEMORY, HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS, 0,
    debug, returnValue);
}

static int halMemoryOverlayMap(NanoOsOverlayMap **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_OVERLAY_MAP, 0, returnValue);
}

static int halMemoryContiguousFilesystem(NanoOsOverlayMap **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_CONTIGUOUS_FILESYSTEM, 0, returnValue);
}

static int halMemoryStaticLogs(StaticLogs **returnValue) {
  if (loggerStaticLogsStale) {
    *returnValue = NULL;
    return 0;
  }
  return callHal(HAL_MEMORY, HAL_MEMORY_STATIC_LOGS, 0, returnValue);
}

static int halMemoryLogBuffer(char **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_LOG_BUFFER, 0, returnValue);
}

static int halMemoryLogEntries(LogEntry **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_LOG_ENTRIES, 0, returnValue);
}

static int halMemoryLogMessages(ProcessMessage **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_LOG_MESSAGES, 0, returnValue);
}

static int halMemoryAllProcesses(ProcessDescriptor **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_ALL_PROCESSES, 0, returnValue);
}

static int halMemoryReadyQueues(ProcessQueue ***returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_READY_QUEUES, 0, returnValue);
}

static int halMemoryWaitingQueue(ProcessQueue **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_WAITING_QUEUE, 0, returnValue);
}

static int halMemoryTimedWaitingQueue(ProcessQueue **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_TIMED_WAITING_QUEUE, 0, returnValue);
}

static int halMemoryFreeQueue(ProcessQueue **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_FREE_QUEUE, 0, returnValue);
}

static int halMemoryProcessErrorNumbers(int **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_PROCESS_ERROR_NUMBERS, 0, returnValue);
}

static int halMemoryProcessStorage(void ****returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_PROCESS_STORAGE, 0, returnValue);
}

static int halPlatformCallFileOverlay(HalCallFileOverlayFn *returnValue) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_CALL_FILE_OVERLAY, 0, returnValue);
}

static int halPlatformExecCommand(HalExecCommandFn *returnValue) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_EXEC_COMMAND, 0, returnValue);
}

static int halPlatformRestartRootFilesystem(
  HalRestartRootFilesystemFn *returnValue
) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_RESTART_ROOT_FILESYSTEM, 0,
    returnValue);
}

static int halPlatformRestartShell(HalRestartShellFn *returnValue) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_RESTART_SHELL, 0, returnValue);
}

static int halPlatformStartProcesses(HalStartProcessesFn *returnValue) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_START_PROCESSES, 0, returnValue);
}

/// @var halPlatformCallbacksAnyPending
///
/// @brief Whether or not there are currently ANY pending callbacks.
static volatile bool halPlatformCallbacksAnyPending = false;

static int halPlatformInvokePendingCallbacks(void) {
  // The scheduler calls this on every pass and there's almost never anything
  // pending, so don't pay for the dispatch just to have the platform check.
  if (halPlatformCallbacksAnyPending == false) {
    return 0;
  }

  return callHal(HAL_PLATFORM, HAL_PLATFORM_INVOKE_PENDING_CALLBACKS, 0);
}

static int halPlatformLaunchProcesses(HalLaunchProcessesFn *returnValue) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_LAUNCH_PROCESSES, 0, returnValue);
}

static int halUartInit(void) {
  return callHal(HAL_UART, HAL_UART_INIT, 0);
}

static int halUartConfigure(int32_t deviceId, uint32_t baud) {
  return callHal(HAL_UART, HAL_UART_CONFIGURE, deviceId, baud);
}

static int halUartRegisterCallback(int32_t deviceId,
  ProcessDescriptor *processDescriptor, int64_t messageType, void *priv
) {
  return callHal(HAL_UART, HAL_UART_REGISTER_CALLBACK, deviceId,
    processDescriptor, messageType, priv);
}

static int halUartPoll(int32_t deviceId) {
  return callHal(HAL_UART, HAL_UART_POLL, deviceId);
}

static int halUartRead(int32_t deviceId, uint8_t *data,
  ssize_t length, ssize_t *returnValue
) {
  return callHal(HAL_UART, HAL_UART_READ, deviceId, data, length,
    returnValue);
}

static int halUartWrite(int32_t deviceId, const uint8_t *data,
  ssize_t length, ssize_t *returnValue
) {
  return callHal(HAL_UART, HAL_UART_WRITE, deviceId, data, length,
    returnValue);
}

static int halUartIsConsole(int32_t deviceId, bool *returnValue) {
  return callHal(HAL_UART, HAL_UART_IS_CONSOLE, deviceId, returnValue);
}

static int halDioInit(void) {
  return callHal(HAL_DIO, HAL_DIO_INIT, 0);
}

static int halDioConfigure(int32_t deviceId, bool output) {
  return callHal(HAL_DIO, HAL_DIO_CONFIGURE, deviceId, output);
}

static int halDioWrite(int32_t deviceId, bool high) {
  return callHal(HAL_DIO, HAL_DIO_WRITE, deviceId, high);
}

static int halSpiInit(void) {
  return callHal(HAL_SPI, HAL_SPI_INIT, 0);
}

static int halSpiConfigure(int32_t deviceId,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud
) {
  return callHal(HAL_SPI, HAL_SPI_CONFIGURE,
    deviceId, (int) cs, (int) sck, (int) copi, (int) cipo, baud);
}

static int halSpiSetSpeed(int32_t deviceId, uint32_t baud) {
  return callHal(HAL_SPI, HAL_SPI_SET_SPEED, deviceId, baud);
}

static int halSpiStartTransfer(int32_t deviceId) {
  return callHal(HAL_SPI, HAL_SPI_START_TRANSFER, deviceId);
}

static int halSpiEndTransfer(int32_t deviceId) {
  return callHal(HAL_SPI, HAL_SPI_END_TRANSFER, deviceId);
}

static int halSpiTransfer8(int32_t deviceId, uint8_t data) {
  return callHal(HAL_SPI, HAL_SPI_TRANSFER8, deviceId, (int) data);
}

static int halSpiTransferBytes(int32_t deviceId,
  uint8_t *data, uint32_t length
) {
  return callHal(HAL_SPI, HAL_SPI_TRANSFER_BYTES, deviceId, data, length);
}

static int halSpiTransferUntil(int32_t deviceId, uint8_t data,
  uint8_t mask, uint8_t value, bool equal, uint16_t maxTransfers
) {
  return callHal(HAL_SPI, HAL_SPI_TRANSFER_UNTIL, deviceId, (int) data,
    (int) mask, (int) value, (int) equal, (int) maxTransfers);
}

static int halClockInit(void) {
  return callHal(HAL_CLOCK, HAL_CLOCK_INIT, 0);
}

static int halClockSetSystemTime(struct timespec *ts) {
  return callHal(HAL_CLOCK, HAL_CLOCK_SET_SYSTEM_TIME, 0, ts);
}

static int halClockGetElapsedMilliseconds(int64_t startTime,
  int64_t *returnValue
) {
  return callHal(HAL_CLOCK, HAL_CLOCK_GET_ELAPSED_MILLISECONDS, 0,
    startTime, returnValue);
}

static int halClockGetElapsedMicroseconds(int64_t startTime,
  int64_t *returnValue
) {
  return callHal(HAL_CLOCK, HAL_CLOCK_GET_ELAPSED_MICROSECONDS, 0,
    startTime, returnValue);
}

static int halClockGetElapsedNanoseconds(int64_t startTime,
  int64_t *returnValue
) {
  return callHal(HAL_CLOCK, HAL_CLOCK_GET_ELAPSED_NANOSECONDS, 0,
    startTime, returnValue);
}

static int halPowerEnterMode(HalPowerMode powerMode) {
  return callHal(HAL_POWER, HAL_POWER_ENTER_MODE, 0, (int) powerMode);
}

static int halTimerInit(void) {
  return callHal(HAL_TIMER, HAL_TIMER_INIT, 0);
}

static int halTimerInitDevice(int32_t deviceId) {
  return callHal(HAL_TIMER, HAL_TIMER_INIT_DEVICE, deviceId);
}

static int halTimerConfigOneShot(int32_t deviceId,
  uint64_t nanoseconds, void (*callback)(void)
) {
  return callHal(HAL_TIMER, HAL_TIMER_CONFIG_ONE_SHOT,
    deviceId, nanoseconds, callback);
}

static int halTimerConfiguredNanoseconds(int32_t deviceId,
  uint64_t *returnValue
) {
  return callHal(HAL_TIMER, HAL_TIMER_CONFIGURED_NANOSECONDS,
    deviceId, returnValue);
}

static int halTimerRemainingNanoseconds(int32_t deviceId,
  uint64_t *returnValue
) {
  return callHal(HAL_TIMER, HAL_TIMER_REMAINING_NANOSECONDS,
    deviceId, returnValue);
}

static int halTimerCancel(int32_t deviceId) {
  return callHal(HAL_TIMER, HAL_TIMER_CANCEL, deviceId);
}

static int halTimerCancelAndGet(int32_t deviceId,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void)
) {
  return callHal(HAL_TIMER, HAL_TIMER_CANCEL_AND_GET,
    deviceId, configuredNanoseconds, remainingNanoseconds, callback);
}

static int halBlockDeviceInit(void) {
  return callHal(HAL_BLOCK_DEVICE, HAL_BLOCK_DEVICE_INIT, 0);
}

static int halBlockDeviceGet(int32_t deviceId, BlockDevice **returnValue) {
  return callHal(HAL_BLOCK_DEVICE, HAL_BLOCK_DEVICE_GET,
    deviceId, returnValue);
}

static int halBlockDeviceRestart(ProcessDescriptor *processDescriptor) {
  return callHal(HAL_BLOCK_DEVICE, HAL_BLOCK_DEVICE_RESTART,
    (int32_t) (intptr_t) processDescriptor->restartArgs, processDescriptor);
}

// ---------------------------------------------------------------------------
// Common HAL helper implementations.
// ---------------------------------------------------------------------------

/// @var _sdCardName
///
/// @brief Process name assigned to the SD card process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _sdCardName[] KEEP_IN_FLASH = "SD card";

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
  if (HAL->blockDevice == NULL) {
    return NULL;
  }

  // Create the SD card process.  Its PID is intentionally not exposed as a
  // well-known global: nothing outside this function and the BlockDevice it
  // returns needs to address it directly.
  ProcessId sdCardPid = reserveProcessSlot();
  ProcessDescriptor *processDescriptor = &allProcesses[sdCardPid - 1];
  if (processCreate(
    processDescriptor, runSdCardSpi, sdCardSpiArgs)
    != processSuccess
  ) {
    logError("Could not start SD card process\n");
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = sdCardPid;
  processDescriptor->name = _sdCardName;
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_KERNEL;
  processDescriptor->restartFunction = HAL->blockDevice->restart;
  processDescriptor->restartArgs = (void*)(intptr_t)0;
  if (sdCardHalCapabilities != NULL) {
    setProcessHalCapabilities(processDescriptor, sdCardHalCapabilities,
      numSdCardHalCapabilities);
  }
  BlockDevice *sdDevice = (BlockDevice*) coroutineResume(
    processDescriptor->mainThread, NULL);
  if (sdDevice == NULL) {
    return NULL;
  }
  sdDevice->partitionNumber = 1;

  return sdDevice;
}

/// @var _filesystemName
///
/// @brief Process name assigned to the filesystem process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _filesystemName[] KEEP_IN_FLASH = "filesystem";

/// @var _mainFunctionName
///
/// @brief Name of the exported entry-point function a contiguous filesystem
/// overlay must provide.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _mainFunctionName[] KEEP_IN_FLASH = "main";

/// @var _restartFilesystemFailedMessage
///
/// @brief Message printed when restartBuiltinFilesystem or
/// restartOverlayFilesystem fails to recreate the filesystem process.
///
/// @note This stays a raw printString, not logError: it fires while the
/// filesystem is being brought up (or has just failed to come up), and
/// logError's own strings are only reachable once the filesystem is
/// available. If the filesystem is what's failing, they may never be.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _restartFilesystemFailedMessage[] KEEP_IN_FLASH
  = "Could not restart filesystem process.\n";

/// @var _noRootBlockDeviceMessage
///
/// @brief Message printed when restartContiguousFilesystem can't find a
/// root block device. See _restartFilesystemFailedMessage for why
/// this stays a raw printString.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _noRootBlockDeviceMessage[] KEEP_IN_FLASH
  = "Could not get root block device for filesystem\n";

/// @var _noMainFunctionMessage
///
/// @brief Message printed when restartContiguousFilesystem can't find the
/// overlay's main entry point. See _restartFilesystemFailedMessage
/// for why this stays a raw printString.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _noMainFunctionMessage[] KEEP_IN_FLASH
  = "Could not find main function for filesystem process\n";

/// @var _restartContiguousFilesystemFailedMessage
///
/// @brief Message printed when restartContiguousFilesystem fails to
/// recreate the filesystem process. See
/// _restartFilesystemFailedMessage for why this stays a raw
/// printString.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _restartContiguousFilesystemFailedMessage[] KEEP_IN_FLASH
  = "Could not restart filesystem process\n";

/// @var _contiguousFilesystemReadFailedMessage
///
/// @brief Message printed when restartContiguousFilesystem can't read the
/// contiguous filesystem binary from the block device. See
/// _restartFilesystemFailedMessage for why this stays a raw printString.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _contiguousFilesystemReadFailedMessage[] KEEP_IN_FLASH
  = "Could not read filesystem binary from block device\n";

/// @var _contiguousFilesystemVersionMessage
///
/// @brief Message printed when restartContiguousFilesystem finds a filesystem
/// binary built for a different overlay version. See
/// _restartFilesystemFailedMessage for why this stays a raw printString.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _contiguousFilesystemVersionMessage[] KEEP_IN_FLASH
  = "Filesystem binary was built for a different overlay version\n";

/// @var _numExtraFilesystemStacks
///
/// @brief The number of extra stacks to add for the filesystem.
static int _numExtraFilesystemStacks = 0;

/// @fn int halCommonInitRootFilesystem(void)
///
/// @brief Common initialization for the root filesystem process.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInitRootFilesystem(void) {
  if (schedulerIsInitialized() == false) {
    return -EBUSY;
  } else if (HAL->blockDevice == NULL) {
    return -ENODEV;
  }

  BlockDevice *rootBlockDevice = NULL;
  HAL->blockDevice->get(0, &rootBlockDevice);

  if (rootBlockDevice == NULL) {
    if (HAL->blockDevice->init() != 0) {
      logError("HAL->blockDevice->init() failed\n");
      return -ENODEV;
    }

    HAL->blockDevice->get(0, &rootBlockDevice);
  }

  if (rootBlockDevice == NULL) {
    logError("No rootBlockDevice available\n");
    return -ENODEV;
  }

  // Allocate the filesystem process.
  rootFilesystemPid = reserveProcessSlot();
  ProcessDescriptor *processDescriptor
    = &allProcesses[rootFilesystemPid - 1];
  if (processCreate(processDescriptor, dummyProcess, NULL) != processSuccess) {
    logError("Could not allocate filesystem process\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = rootFilesystemPid;
  processDescriptor->name = _filesystemName;
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  HalRestartRootFilesystemFn restartRootFilesystem = NULL;
  if (HAL->platform != NULL) {
    HAL->platform->restartRootFilesystem(&restartRootFilesystem);
  }
  processDescriptor->restartFunction = restartRootFilesystem;
  // DO NOT resume the process yet.  Let the scheduler take care of that.

  // The filesystem runs as an overlay, which increases the depth of the call
  // stack relative to a kernel process.  Increase the stack space for the
  // process as required for the platform.
  for (int ii = 0; ii < _numExtraFilesystemStacks; ii++) {
    Thread *thread = threadProvision(NULL, dummyProcess, NULL);
    if (thread == NULL) {
      logError("Could not increase filesystem process's stack size.\n");
      return -ENOMEM;
    }
    if (threadSetStackEnd(
      processDescriptor->mainThread, threadStackEnd(thread)) != processSuccess
    ) {
      logError("Could not set filesystem process's stack size.\n");
    }
  }

  registerProcessName(_filesystemName, rootFilesystemPid);

  // Grant the filesystem process its IPC capabilities, if this platform has
  // any to grant (see FILESYSTEM_IPC_CAPABILITIES_INITIALIZER).  The logger
  // entry ([4]) is intentionally left at its default 0 here: the logger
  // doesn't exist yet at this point in bring-up.  halCommonInitLogger
  // patches it in once/if the logger actually starts.
  if (filesystemIpcCapabilities != NULL) {
    filesystemIpcCapabilities[0].destinationPid = schedulerPid;
    filesystemIpcCapabilities[1].destinationPid = consolePid;
    filesystemIpcCapabilities[2].destinationPid = memoryManagerPid;
    // The SD card process's PID is never made well-known (see
    // halCommonInitRootSdSpiStorage): it's always the slot immediately
    // before the filesystem's own, by construction of reserveProcessSlot.
    filesystemIpcCapabilities[3].destinationPid = rootFilesystemPid - 1;
    processDescriptor->ipcCapabilities = filesystemIpcCapabilities;
    processDescriptor->numIpcCapabilities = numFilesystemIpcCapabilities;
    processDescriptor->ipcCapabilitiesDynamic = false;
  }

  return 0;
}

/// @var _loggerName
///
/// @brief Process name assigned to the logger process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _loggerName[] KEEP_IN_FLASH = "logger";

/// @var _couldNotCreateLoggerProcess
///
/// @brief Message printed when the logger process itself could not be
/// created.  Since the logger isn't available yet, this can't be logged via
/// logError and must be printed directly instead.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _couldNotCreateLoggerProcess[] KEEP_IN_FLASH
  = "Could not create logger process\n";

/// @fn int halCommonInitLogger(void)
///
/// @brief Common initialization for the logger process.  Only meant to be
/// called by a platform's startProcesses implementation, and only when
/// HAL->memory->stringsPresent is false: platforms whose OS image keeps its
/// .rodata can format log messages immediately and have no need for a
/// separate logger process.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInitLogger(void) {
  if (schedulerIsInitialized() == false) {
    return -EBUSY;
  }

  loggerPid = reserveProcessSlot();
  ProcessDescriptor *processDescriptor = &allProcesses[loggerPid - 1];
  if (processCreate(processDescriptor, dummyProcess, NULL) != processSuccess) {
    printString(_couldNotCreateLoggerProcess);
    loggerPid = 0; // Invalid PID
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = loggerPid;
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->name = _loggerName;
  // The logger is an executive process, but we're going to start it in
  // supervisor mode until the system comes up far enough to launch it.
  // restartLogger will take care of fixing the level once it launches
  // successfully.
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
  registerProcessName(_loggerName, loggerPid);
  logDebug("Initialized logger process\n");

  // Grant the logger process its IPC capabilities, if this platform has any
  // to grant (see LOGGER_IPC_CAPABILITIES_INITIALIZER).
  if (loggerIpcCapabilities != NULL) {
    loggerIpcCapabilities[0].destinationPid = schedulerPid;
    loggerIpcCapabilities[1].destinationPid = consolePid;
    loggerIpcCapabilities[2].destinationPid = memoryManagerPid;
    loggerIpcCapabilities[3].destinationPid = rootFilesystemPid;
    loggerIpcCapabilities[4].destinationPid = loggerPid;
    processDescriptor->ipcCapabilities = loggerIpcCapabilities;
    processDescriptor->numIpcCapabilities = numLoggerIpcCapabilities;
    processDescriptor->ipcCapabilitiesDynamic = false;
  }

  // The logger didn't exist yet when the filesystem's own IPC capabilities
  // were set up (see halCommonInitRootFilesystem).  Now that it does, patch
  // the filesystem's logger entry ([4]) in too.
  if ((filesystemIpcCapabilities != NULL) && (rootFilesystemPid > 0)) {
    filesystemIpcCapabilities[4].destinationPid = loggerPid;
  }

  return 0;
}

/// @fn int halCommonInitFileCache(uint8_t depth)
///
/// @brief Allocate fileCache from the scheduler's heap with room for depth
/// overlays of HAL->memory->overlaySize bytes each.  A no-op if fileCache
/// already exists.
///
/// @param depth The number of overlays the cache can hold.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInitFileCache(uint8_t depth) {
  if (fileCache != NULL) {
    return 0;
  } else if (depth == 0) {
    return -EINVAL;
  } else if (HAL->memory == NULL) {
    return -ENODEV;
  }

  size_t fifoSize = sizeof(OverlayFifo)
    + ((depth - 1) * sizeof(OverlayFifoNode));
  size_t overlaySize = HAL->memory->overlaySize;
  uint8_t *allocation = (uint8_t*) schedMalloc(
    fifoSize + (depth * overlaySize));
  if (allocation == NULL) {
    logError("Could not allocate the overlay file cache\n");
    return -ENOMEM;
  }

  OverlayFifo *overlayFifo = (OverlayFifo*) allocation;
  memset(overlayFifo, 0, fifoSize);
  overlayFifo->size = depth;
  uint8_t *overlayMemory = allocation + fifoSize;
  for (uint8_t ii = 0; ii < depth; ii++) {
    overlayFifo->nodes[ii].memory = overlayMemory;
    overlayMemory += overlaySize;
  }
  fileCache = overlayFifo;

  return 0;
}

/// @var _consoleProcessName
///
/// @brief The name the console process is registered under.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _consoleProcessName[] KEEP_IN_FLASH = CONSOLE_PROCESS_NAME;

static int configSysGrantHardware(void);
static int configSysStartShells(void);
static int configSysQueueDevices(void);

/// @fn int halCommonLaunchProcesses(void)
///
/// @brief Register the console's name, grant the devices from
/// /etc/config.sys their hardware, start the shells on the console ports, and
/// queue the devices, then arm every
/// supervisor- and user-level process that was created during scheduler
/// startup but not yet queued, and push each one onto its ready queue.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonLaunchProcesses(void) {
  if ((HAL->memory == NULL) || (HAL->platform == NULL)) {
    return -ENODEV;
  }

  ProcessQueue **readyQueues = NULL;
  HAL->memory->readyQueues(&readyQueues);
  if (readyQueues == NULL) {
    return -ENODEV;
  }

  registerProcessName(_consoleProcessName, consolePid);
  int returnValue = configSysGrantHardware();
  bootTraceValue("hardware granted", returnValue);
  int shellStatus = configSysStartShells();
  bootTraceValue("shells started, status", shellStatus);
  bootTraceValue("number of shells", (int) schedulerGetNumShells());
  if (shellStatus != 0) {
    returnValue = shellStatus;
  }
  // Devices must not run until their console ports and capabilities exist.
  int queueStatus = configSysQueueDevices();
  bootTraceValue("devices queued, status", queueStatus);
  if (queueStatus != 0) {
    returnValue = queueStatus;
  }
  ProcessId firstShellPid = schedulerGetFirstShellPid();
  uint8_t numShells = schedulerGetNumShells();
  for (ProcessId pid = 1; pid <= numProcesses; pid++) {
    ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
    if ((processDescriptor->readyQueue != NULL)
      || (processDescriptor->privilegeLevel < PRIVILEGE_LEVEL_SUPERVISOR)
    ) {
      continue;
    }

    HAL->platform->callFileOverlay(&processDescriptor->callOverlayFunction);
    if (pid == loggerPid) {
      processDescriptor->restartFunction = restartLogger;
    } else if ((pid >= firstShellPid) && (pid - firstShellPid < numShells)) {
      HAL->platform->restartShell(&processDescriptor->restartFunction);
    } else {
      processDescriptor->restartFunction = NULL;
    }

    processDescriptor->readyQueue
      = readyQueues[processDescriptor->privilegeLevel];
    if (processQueuePush(processDescriptor->readyQueue, processDescriptor)
      != 0
    ) {
      returnValue = -ENOMEM;
    }
  }

  return returnValue;
}

/// @def CONFIG_SYS_LINE_SIZE
///
/// @brief Size of the buffer used to read one line of /etc/config.sys.
#define CONFIG_SYS_LINE_SIZE 128

/// @struct ConfigSysDevice
///
/// @brief A process started from a DEVICE line in /etc/config.sys.
///
/// @param next The next device in the list, or NULL.
/// @param pid The ProcessId of the slot the device runs in.
/// @param path The full path to the device's overlay on the filesystem.
/// @param argv The NULL-terminated array of arguments for the device.
typedef struct ConfigSysDevice {
  struct ConfigSysDevice *next;
  ProcessId               pid;
  char                   *path;
  char                  **argv;
} ConfigSysDevice;

/// @struct ConfigSysConsole
///
/// @brief A console port and the program to run on it, from a CONSOLE line in
/// /etc/config.sys.
///
/// @param next The next console in the list, or NULL.
/// @param port The name of the port: UART<n> or the path of a DEVICE.
/// @param program The program to run on the port while no user is logged in.
typedef struct ConfigSysConsole {
  struct ConfigSysConsole *next;
  char                    *port;
  ShellProgram             program;
} ConfigSysConsole;

/// @struct ConfigSys
///
/// @brief What /etc/config.sys configured.  Allocated only when the file is
/// read, so that platforms without one spend a single pointer of .bss on it.
///
/// @param devices List of the devices from DEVICE lines.
/// @param hardware List of the HARDWARE lines, each parsed like a DEVICE line:
///   the device's path followed by the hardware it gets.
/// @param consoles List of the consoles from CONSOLE lines, in file order.
typedef struct ConfigSys {
  ConfigSysDevice  *devices;
  ConfigSysDevice  *hardware;
  ConfigSysConsole *consoles;
} ConfigSys;

/// @var _configSys
///
/// @brief What /etc/config.sys configured, or NULL if it hasn't been read.
static ConfigSys *_configSys = NULL;

/// @var _configSysPath
///
/// @brief Path to the system configuration file.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _configSysPath[] KEEP_IN_FLASH = "/etc/config.sys";

/// @var _configSysReadMode
///
/// @brief Mode used to open /etc/config.sys.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _configSysReadMode[] KEEP_IN_FLASH = "r";

/// @var _configSysDeviceKeyword
///
/// @brief Keyword of a /etc/config.sys line that starts a device process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _configSysDeviceKeyword[] KEEP_IN_FLASH = "DEVICE";

/// @var _configSysConsoleKeyword
///
/// @brief Keyword of a /etc/config.sys line that runs a program on a console
/// port.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _configSysConsoleKeyword[] KEEP_IN_FLASH = "CONSOLE";

/// @var _configSysHardwareKeyword
///
/// @brief Keyword of a /etc/config.sys line that grants a device hardware.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _configSysHardwareKeyword[] KEEP_IN_FLASH = "HARDWARE";

/// @var _configSysUartPrefix
///
/// @brief Prefix of a CONSOLE line's port when the port is a UART.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _configSysUartPrefix[] KEEP_IN_FLASH = "UART";

/// @var _configSysRemKeyword
///
/// @brief Keyword of a /etc/config.sys comment line.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _configSysRemKeyword[] KEEP_IN_FLASH = "REM";

/// @fn static inline bool configSysIsSpace(char c)
///
/// @brief Determine whether a character separates fields in /etc/config.sys.
///
/// @param c The character to check.
///
/// @return Returns true if c is a space or a tab, false otherwise.
static inline bool configSysIsSpace(char c) {
  return (c == ' ') || (c == '\t');
}

/// @fn static char* configSysTrim(char *string)
///
/// @brief Remove leading and trailing whitespace, including line endings,
/// from a string in place.
///
/// @param string The string to trim.
///
/// @return Returns a pointer to the first non-whitespace character of string.
static char* configSysTrim(char *string) {
  while (configSysIsSpace(*string)) {
    string++;
  }

  char *end = string + strlen(string);
  while ((end > string) && (configSysIsSpace(end[-1])
    || (end[-1] == '\r') || (end[-1] == '\n'))
  ) {
    end--;
  }
  *end = '\0';

  return string;
}

/// @fn static void configSysToUpper(char *string)
///
/// @brief Convert the ASCII letters of a string to uppercase in place.
///
/// @param string The string to convert.
///
/// @return This function returns no value.
static void configSysToUpper(char *string) {
  for (; *string != '\0'; string++) {
    if ((*string >= 'a') && (*string <= 'z')) {
      *string -= 'a' - 'A';
    }
  }
}

/// @fn static ConfigSysDevice* configSysParseDevice(char *value)
///
/// @brief Build a ConfigSysDevice from the value of a DEVICE line: the path to
/// the device's overlay followed by its arguments.
///
/// @param value The trimmed text after the '=' of a DEVICE line.
///
/// @return Returns a newly-allocated ConfigSysDevice on success, NULL on
/// failure.
static ConfigSysDevice* configSysParseDevice(char *value) {
  size_t numArgs = 0;
  for (char *cursor = value; *cursor != '\0';) {
    while (configSysIsSpace(*cursor)) {
      cursor++;
    }
    if (*cursor != '\0') {
      numArgs++;
    }
    while ((*cursor != '\0') && !configSysIsSpace(*cursor)) {
      cursor++;
    }
  }
  if (numArgs == 0) {
    return NULL;
  }

  ConfigSysDevice *device = (ConfigSysDevice*) schedMalloc(
    sizeof(ConfigSysDevice) + ((numArgs + 1) * sizeof(char*))
    + strlen(value) + 1);
  if (device == NULL) {
    return NULL;
  }
  device->next = NULL;
  device->pid = 0;
  device->argv = (char**) &device[1];
  char *strings = (char*) &device->argv[numArgs + 1];
  strcpy(strings, value);

  size_t argIndex = 0;
  for (char *cursor = strings; *cursor != '\0';) {
    while (configSysIsSpace(*cursor)) {
      *cursor++ = '\0';
    }
    if (*cursor != '\0') {
      device->argv[argIndex++] = cursor;
    }
    while ((*cursor != '\0') && !configSysIsSpace(*cursor)) {
      cursor++;
    }
  }
  device->argv[argIndex] = NULL;

  device->path = device->argv[0];
  char *name = strrchr(device->path, '/');
  if (name != NULL) {
    device->argv[0] = name + 1;
  }

  return device;
}

/// @fn static ProcessDescriptor* configSysNextSlot(void)
///
/// @brief Find an unused user-level process slot to run a device in.
///
/// @return Returns a pointer to the slot's ProcessDescriptor on success, NULL
/// if no slot is available.
static ProcessDescriptor* configSysNextSlot(void) {
  ProcessId firstPid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  for (ProcessId pid = firstPid; pid <= numProcesses; pid++) {
    ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
    if ((processDescriptor->privilegeLevel == PRIVILEGE_LEVEL_USER)
      && (processDescriptor->readyQueue == NULL)
    ) {
      return processDescriptor;
    }
  }

  return NULL;
}

/// @fn int restartConfigSysDevice(ProcessDescriptor *processDescriptor)
///
/// @brief Implementation of restartFunction to launch a device from
/// /etc/config.sys, and to re-launch it if it exits.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that manages the
///   device's process.
///
/// @return Returns 0 on success, -errno on failure.
int restartConfigSysDevice(ProcessDescriptor *processDescriptor) {
  ConfigSysDevice *device = (_configSys != NULL) ? _configSys->devices : NULL;
  while ((device != NULL) && (device->pid != processDescriptor->processId)) {
    device = device->next;
  }
  if (device == NULL) {
    return -ENOENT;
  }

  int returnValue = schedulerRunExecutiveOverlay(processDescriptor,
    device->path, device->argv);
  if (returnValue == -EBUSY) {
    return -EAGAIN;
  } else if (returnValue < 0) {
    logError("Could not start device %s: %s\n",
      device->path, strerror(-returnValue));
  }

  return returnValue;
}

/// @fn static int configSysAddDevice(char *value, int lineNumber)
///
/// @brief Assign a process slot to the device described by the value of a
/// DEVICE line.  The slot is not queued until the whole file has been read.
///
/// @param value The trimmed text after the '=' of a DEVICE line.
/// @param lineNumber The line of /etc/config.sys the DEVICE line is on.
///
/// @return Returns 0 on success, -errno on failure.
static int configSysAddDevice(char *value, int lineNumber) {
  ProcessDescriptor *processDescriptor = configSysNextSlot();
  if (processDescriptor == NULL) {
    logError("No process slot for the device on line %d of %s\n",
      lineNumber, _configSysPath);
    return -ENOSPC;
  }

  ConfigSysDevice *device = configSysParseDevice(value);
  if (device == NULL) {
    logError("Could not load the device on line %d of %s\n",
      lineNumber, _configSysPath);
    return -ENOMEM;
  }
  device->pid = processDescriptor->processId;
  ConfigSysDevice **tail = &_configSys->devices;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = device;

  HAL->platform->callFileOverlay(&processDescriptor->callOverlayFunction);
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  schedulerSetBaseHalCapabilities(processDescriptor);
  processDescriptor->restartFunction = restartConfigSysDevice;

  return 0;
}

/// @fn static int configSysAddConsole(char *value, int lineNumber)
///
/// @brief Record the port and program of a CONSOLE line.  The port is resolved
/// when the shells are started, after the whole file has been read.
///
/// @param value The trimmed text after the '=' of a CONSOLE line.
/// @param lineNumber The line of /etc/config.sys the CONSOLE line is on.
///
/// @return Returns 0 on success, -errno on failure.
static int configSysAddConsole(char *value, int lineNumber) {
  char *program = value;
  while ((*program != '\0') && !configSysIsSpace(*program)) {
    program++;
  }
  if (*program == '\0') {
    logError("No program for the console on line %d of %s\n",
      lineNumber, _configSysPath);
    return -EINVAL;
  }
  *program++ = '\0';

  ConfigSysDevice *parsed = configSysParseDevice(configSysTrim(program));
  ConfigSysConsole *console = (ConfigSysConsole*) schedMalloc(
    sizeof(ConfigSysConsole) + strlen(value) + 1);
  if ((parsed == NULL) || (console == NULL)) {
    logError("Could not load the console on line %d of %s\n",
      lineNumber, _configSysPath);
    schedFree(parsed);
    schedFree(console);
    return -ENOMEM;
  }
  console->next = NULL;
  console->port = (char*) &console[1];
  strcpy(console->port, value);
  console->program.path = parsed->path;
  console->program.argv = parsed->argv;

  ConfigSysConsole **tail = &_configSys->consoles;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = console;

  return 0;
}

/// @fn static int configSysParseUart(const char *name)
///
/// @brief Parse the name of a UART: UART<n>, in any case.
///
/// @param name The name to parse.
///
/// @return Returns the ID of the UART on success, -ENOENT if the name doesn't
/// name a UART, -EINVAL if it names one badly.
static int configSysParseUart(const char *name) {
  size_t prefixLength = 0;
  for (; _configSysUartPrefix[prefixLength] != '\0'; prefixLength++) {
    char c = name[prefixLength];
    if ((c >= 'a') && (c <= 'z')) {
      c -= 'a' - 'A';
    }
    if (c != _configSysUartPrefix[prefixLength]) {
      return -ENOENT;
    }
  }
  if ((name[prefixLength] < '0') || (name[prefixLength] > '9')) {
    return -ENOENT;
  }

  unsigned int uartId = 0;
  const char *digit = &name[prefixLength];
  for (; (*digit >= '0') && (*digit <= '9'); digit++) {
    uartId = (uartId * 10) + (unsigned int) (*digit - '0');
    if (uartId > UINT8_MAX) {
      return -EINVAL;
    }
  }
  if (*digit != '\0') {
    return -EINVAL;
  }

  return (int) uartId;
}

/// @fn static ConfigSysDevice* configSysFindDevice(const char *path)
///
/// @brief Find the device a DEVICE line loaded from its path.
///
/// @param path The path of the device's overlay.
///
/// @return Returns a pointer to the ConfigSysDevice on success, NULL if no
/// DEVICE line has the path.
static ConfigSysDevice* configSysFindDevice(const char *path) {
  ConfigSysDevice *device = (_configSys != NULL) ? _configSys->devices : NULL;
  while ((device != NULL) && (strcmp(device->path, path) != 0)) {
    device = device->next;
  }
  return device;
}

/// @fn static int configSysConsolePort(const char *port)
///
/// @brief Find the index of the console port a CONSOLE line names, adding a
/// forwarding port for the device if the port is a DEVICE's path.
///
/// @param port The name of the port from the CONSOLE line.
/// @param outputPid A pointer to the ProcessId that will hold the ID of the
///   device's process if the port is a device, or PROCESS_ID_NOT_SET if not.
///
/// @return Returns the index of the port on success, -errno on failure.
static int configSysConsolePort(const char *port, ProcessId *outputPid) {
  *outputPid = PROCESS_ID_NOT_SET;
  int uartId = configSysParseUart(port);
  if (uartId >= 0) {
    int portId = schedulerGetUartConsolePort((uint8_t) uartId);
    return (portId >= 0) ? portId : -ENODEV;
  } else if (uartId != -ENOENT) {
    return uartId;
  }

  ConfigSysDevice *device = configSysFindDevice(port);
  if (device == NULL) {
    return -ENOENT;
  }
  *outputPid = device->pid;
  return schedulerAddForwardingPort(device->pid);
}

/// @fn static int configSysAddHardware(char *value, int lineNumber)
///
/// @brief Record the device and hardware of a HARDWARE line.  The grants are
/// made after the whole file has been read.
///
/// @param value The trimmed text after the '=' of a HARDWARE line.
/// @param lineNumber The line of /etc/config.sys the HARDWARE line is on.
///
/// @return Returns 0 on success, -errno on failure.
static int configSysAddHardware(char *value, int lineNumber) {
  ConfigSysDevice *hardware = configSysParseDevice(value);
  if ((hardware == NULL) || (hardware->argv[1] == NULL)) {
    logError("Invalid hardware on line %d of %s\n",
      lineNumber, _configSysPath);
    schedFree(hardware);
    return (hardware == NULL) ? -ENOMEM : -EINVAL;
  }

  ConfigSysDevice **tail = &_configSys->hardware;
  while (*tail != NULL) {
    tail = &(*tail)->next;
  }
  *tail = hardware;

  return 0;
}

/// @fn static int configSysGrantUart(ProcessDescriptor *processDescriptor,
///   int uartId)
///
/// @brief Grant a process every UART function a driver of the UART uses.
///
/// @param processDescriptor A pointer to the process's ProcessDescriptor.
/// @param uartId The ID of the UART.
///
/// @return Returns 0 on success, -errno on failure.
static int configSysGrantUart(ProcessDescriptor *processDescriptor,
  int uartId
) {
  if (uartId >= 8) {
    return -EINVAL;
  }

  for (uint8_t function = HAL_UART_CONFIGURE; function < HAL_UART_NUM_FNS;
    function++
  ) {
    int returnValue = addProcessHalCapability(processDescriptor,
      (((uint16_t) HAL_UART) << 8) | function, (uint8_t) (1 << uartId));
    if (returnValue != 0) {
      return returnValue;
    }
  }

  return 0;
}

/// @fn static int configSysGrantHardware(void)
///
/// @brief Grant each device the hardware its HARDWARE lines name.
///
/// @return Returns 0 on success, -errno if a HARDWARE line could not be used.
static int configSysGrantHardware(void) {
  int returnValue = 0;
  ConfigSysDevice *first = (_configSys != NULL) ? _configSys->hardware : NULL;
  for (ConfigSysDevice *hardware = first; hardware != NULL;
    hardware = hardware->next
  ) {
    ConfigSysDevice *device = configSysFindDevice(hardware->path);
    if (device == NULL) {
      logError("No device %s for hardware in %s\n",
        hardware->path, _configSysPath);
      returnValue = -ENOENT;
      continue;
    }

    for (char **resource = &hardware->argv[1]; *resource != NULL; resource++) {
      int grantStatus = configSysParseUart(*resource);
      if (grantStatus >= 0) {
        grantStatus = configSysGrantUart(&allProcesses[device->pid - 1],
          grantStatus);
      }
      if (grantStatus != 0) {
        logError("Could not grant %s to %s: %s\n",
          *resource, hardware->path, strerror(-grantStatus));
        returnValue = grantStatus;
      }
    }
  }

  return returnValue;
}

/// @fn static int configSysStartShells(void)
///
/// @brief Start a shell on each console port that /etc/config.sys names, or
/// getty on every console port if it names none that can be used, and give
/// the unused shell process slots back to user processes.
///
/// @return Returns 0 on success, -errno if a CONSOLE line could not be used.
static int configSysStartShells(void) {
  uint8_t maxShells = schedulerGetNumShells();
  uint32_t portsUsed = 0;
  uint8_t numShells = 0;
  int returnValue = 0;

  ConfigSysConsole *first = (_configSys != NULL) ? _configSys->consoles : NULL;
  for (ConfigSysConsole *console = first; console != NULL;
    console = console->next
  ) {
    ProcessId outputPid = PROCESS_ID_NOT_SET;
    int portId = configSysConsolePort(console->port, &outputPid);
    if (portId < 0) {
      logError("Could not use console port %s from %s: %s\n",
        console->port, _configSysPath, strerror(-portId));
      returnValue = portId;
      continue;
    } else if ((portsUsed & (((uint32_t) 1) << portId)) != 0) {
      logError("Console port %s is in %s more than once\n",
        console->port, _configSysPath);
      returnValue = -EEXIST;
      continue;
    } else if (numShells == maxShells) {
      logError("Too many consoles in %s\n", _configSysPath);
      returnValue = -ENOSPC;
      break;
    }

    int bindStatus = schedulerBindShell(numShells, (uint8_t) portId,
      &console->program, outputPid);
    if (bindStatus == 0) {
      portsUsed |= ((uint32_t) 1) << portId;
      numShells++;
    } else {
      returnValue = bindStatus;
    }
  }

  if (numShells == 0) {
    int numConsolePorts = schedulerGetNumConsolePorts();
    for (int portId = 0;
      (portId < numConsolePorts) && (numShells < maxShells);
      portId++
    ) {
      int bindStatus = schedulerBindShell(numShells, (uint8_t) portId, NULL,
        PROCESS_ID_NOT_SET);
      if (bindStatus != 0) {
        returnValue = bindStatus;
        break;
      }
      numShells++;
    }
  }

  schedulerSetNumShells(numShells);
  return returnValue;
}

/// @fn static int configSysQueueDevices(void)
///
/// @brief Queue the process slots of the devices from /etc/config.sys.  Each
/// slot's dummy process runs once as an executive process, and when it exits,
/// restartConfigSysDevice launches the device in its place.
///
/// @return Returns 0 on success, -errno on failure.
static int configSysQueueDevices(void) {
  ProcessQueue **readyQueues = NULL;
  HAL->memory->readyQueues(&readyQueues);
  if (readyQueues == NULL) {
    return -ENODEV;
  }

  int returnValue = 0;
  ConfigSysDevice *first = (_configSys != NULL) ? _configSys->devices : NULL;
  for (ConfigSysDevice *device = first; device != NULL;
    device = device->next
  ) {
    ProcessDescriptor *processDescriptor = &allProcesses[device->pid - 1];
    if (processDescriptor->readyQueue != NULL) {
      continue;
    }
    processDescriptor->readyQueue = readyQueues[PRIVILEGE_LEVEL_EXECUTIVE];
    if (processQueuePush(processDescriptor->readyQueue, processDescriptor)
      != 0
    ) {
      returnValue = -ENOMEM;
    }
  }

  return returnValue;
}

/// @fn static int configSysProcessLine(char *line, int lineNumber)
///
/// @brief Act on one line of /etc/config.sys.
///
/// @param line The line, including any line ending.
/// @param lineNumber The line's number within /etc/config.sys.
///
/// @return Returns 0 on success, -errno on failure.
static int configSysProcessLine(char *line, int lineNumber) {
  char *keyword = line;
  char *value = strchr(keyword, '=');
  if (value != NULL) {
    *value++ = '\0';
    value = configSysTrim(value);
  }
  keyword = configSysTrim(keyword);
  char *end = keyword;
  while ((*end != '\0') && !configSysIsSpace(*end)) {
    end++;
  }
  bool hasArgument = (*end != '\0');
  *end = '\0';
  configSysToUpper(keyword);

  if ((*keyword == '\0') || (*keyword == ';')
    || (strcmp(keyword, _configSysRemKeyword) == 0)
  ) {
    return 0;
  } else if ((value == NULL) || hasArgument) {
    logError("Invalid line %d in %s\n", lineNumber, _configSysPath);
    return -EINVAL;
  } else if (strcmp(keyword, _configSysDeviceKeyword) == 0) {
    return configSysAddDevice(value, lineNumber);
  } else if (strcmp(keyword, _configSysConsoleKeyword) == 0) {
    return configSysAddConsole(value, lineNumber);
  } else if (strcmp(keyword, _configSysHardwareKeyword) == 0) {
    return configSysAddHardware(value, lineNumber);
  }

  logWarn("Unrecognized command on line %d of %s\n",
    lineNumber, _configSysPath);
  return 0;
}

/// @fn int halCommonLoadConfigSys(void)
///
/// @brief Read /etc/config.sys, assigning a process slot to each DEVICE line
/// and recording each HARDWARE and CONSOLE line, for halCommonLaunchProcesses
/// to apply.
/// Only meant to be called by a platform's launchProcesses implementation,
/// after the root filesystem is running and before halCommonLaunchProcesses.
///
/// @return Returns 0 on success or if there is no /etc/config.sys, -errno on
/// failure.
int halCommonLoadConfigSys(void) {
  if ((HAL->memory == NULL) || (HAL->platform == NULL)) {
    return -ENODEV;
  }

  bootTrace("opening config.sys");
  FILE *configSys = schedFopen(_configSysPath, _configSysReadMode);
  if (configSys == NULL) {
    bootTrace("no config.sys");
    return 0;
  }

  if (_configSys == NULL) {
    _configSys = (ConfigSys*) schedMalloc(sizeof(ConfigSys));
    if (_configSys == NULL) {
      schedFclose(configSys);
      return -ENOMEM;
    }
    memset(_configSys, 0, sizeof(ConfigSys));
  }

  char *line = (char*) schedMalloc(CONFIG_SYS_LINE_SIZE);
  if (line == NULL) {
    schedFclose(configSys);
    return -ENOMEM;
  }

  int returnValue = 0;
  int lineNumber = 0;
  bool discarding = false;
  while (schedFgets(line, CONFIG_SYS_LINE_SIZE, configSys) == line) {
    size_t length = strlen(line);
    if ((length == CONFIG_SYS_LINE_SIZE - 1) && (line[length - 1] != '\n')) {
      if (discarding == false) {
        lineNumber++;
        logError("Line %d of %s is too long\n", lineNumber, _configSysPath);
        returnValue = -EOVERFLOW;
        discarding = true;
      }
      continue;
    } else if (discarding) {
      // The end of a line that was too long.
      discarding = false;
      continue;
    }

    lineNumber++;
    int lineStatus = configSysProcessLine(line, lineNumber);
    if (lineStatus != 0) {
      returnValue = lineStatus;
    }
  }

  schedFree(line);
  schedFclose(configSys);

  return returnValue;
}

/// @var _builtinFilesystemInitDriver
///
/// @brief File-local variable to hold the value to set fs.driverInit to in
/// restartBuiltinFilesystem.
static FilesystemDriverInit _builtinFilesystemInitDriver;

/// @var _builtinFilesystemCommandHandlers
///
/// @brief File-local variable to hold the value to set fs.commandHandlers to
/// in restartBuiltinFilesystem.
static const FilesystemCommandHandler *_builtinFilesystemCommandHandlers;

/// @var filesystemHalCapabilities
///
/// @brief Array of HalCapability items granted specifically to the filesystem
/// process.  It's the only process that needs direct access to
/// HAL_MEMORY_ALL_PROCESSES: it captures the pointer at startup to access
/// attributes (specifically environment variables) of processes at runtime.
///
/// @note findHalCapability() does an early-terminating linear search that
/// assumes this array is sorted in ascending order of subsystemFunction (i.e.
/// by (subsystem << 8) | function). Keep new entries in the correct sorted
/// position, not just appended at the end.
HalCapability filesystemHalCapabilities[] = {
  {
    .subsystemFunction = (((uint16_t) HAL_MEMORY) << 8)
      | HAL_MEMORY_STATIC_LOGS,
    .deviceIds =         0x01, // Device 0: no device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_MEMORY) << 8)
      | HAL_MEMORY_LOG_BUFFER,
    .deviceIds =         0x01, // Device 0: no device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_MEMORY) << 8)
      | HAL_MEMORY_ALL_PROCESSES,
    .deviceIds =         0x01, // Device 0: no device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_UART) << 8)
      | HAL_UART_WRITE,
    .deviceIds =         0x03, // Bitmask for device IDs 0 and 1
  },
  {
    .subsystemFunction = (((uint16_t) HAL_UART) << 8)
      | HAL_UART_IS_CONSOLE,
    .deviceIds =         0x03, // Bitmask for device IDs 0 and 1
  },
  {
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8)
      | HAL_CLOCK_GET_ELAPSED_MILLISECONDS,
    .deviceIds =         0x01, // Device 0: no device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8)
      | HAL_CLOCK_GET_ELAPSED_MICROSECONDS,
    .deviceIds =         0x01, // Device 0: no device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8)
      | HAL_CLOCK_GET_ELAPSED_NANOSECONDS,
    .deviceIds =         0x01, // Device 0: no device for this function
  },
};


/// @fn int restartBuiltinFilesystem(ProcessDescriptor *processDescriptor)
///
/// @brief Restart the filesystem process built into the OS image using the
/// existing root block device.
///
/// @param processDescriptor A pointer to the ProcessDescriptor of the
///   filesystem process to restart.
///
/// @return Returns 0 on success, -errno on failure.
int restartBuiltinFilesystem(ProcessDescriptor *processDescriptor) {
  if ((HAL->blockDevice == NULL) || (HAL->memory == NULL)) {
    return -ENODEV;
  }

  BlockDevice *rootBlockDevice = NULL;
  HAL->blockDevice->get(0, &rootBlockDevice);
  if (rootBlockDevice == NULL) {
    return -ENODEV;
  }

  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = rootBlockDevice;
  fs.blockSize = fs.blockDevice->blockSize;
  fs.numProcesses = HAL->memory->numProcesses;
  ProcessDescriptor *processes = NULL;
  HAL->memory->allProcesses(&processes);
  fs.allProcesses = processes;

  fs.driverInit = _builtinFilesystemInitDriver;
  fs.commandHandlers = _builtinFilesystemCommandHandlers;

  if (processCreate(processDescriptor, runFilesystem, &fs)
    != processSuccess
  ) {
    printString(_restartFilesystemFailedMessage);
    return -ENOMEM;
  }

  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = _filesystemName;
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  setProcessHalCapabilities(processDescriptor, filesystemHalCapabilities,
    sizeof(filesystemHalCapabilities) / sizeof(filesystemHalCapabilities[0]));
  processDescriptor->restartFunction = restartBuiltinFilesystem;
  processDescriptor->callOverlayFunction = callOverlayFunctionFromBlockDevice;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  // Let the filesystem process initialize before we return.  fs.driverState
  // starts at NULL (memset above) meaning "hasn't run yet"; runFilesystem
  // writes the (void*) 1 placeholder into it the moment it starts (before
  // it's actually parsed a partition or initialized a driver), then
  // overwrites it with the real outcome once initialization genuinely
  // finishes -- a real driverState pointer on success, or (void*) 2 on
  // failure.  Waiting on driverState != NULL would return here far too
  // early (right after the placeholder is written), so we wait through
  // both the "hasn't run yet" and "started but not finished" states
  // instead.
  while ((fs.driverState == NULL)
    || (fs.driverState == (void*) ((intptr_t) 1))
  ) {
    runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
  }

  return 0;
}

/// @fn int restartOverlayFilesystem(ProcessDescriptor *processDescriptor)
///
/// @brief Restart the filesystem process run as a regular overlay using the
/// existing root block device.
///
/// @param processDescriptor A pointer to the ProcessDescriptor of the
///   filesystem process to restart.
///
/// @return Returns 0 on success, -errno on failure.
int restartOverlayFilesystem(ProcessDescriptor *processDescriptor) {
  if ((HAL->blockDevice == NULL) || (HAL->memory == NULL)) {
    return -ENODEV;
  }

  BlockDevice *rootBlockDevice = NULL;
  HAL->blockDevice->get(0, &rootBlockDevice);
  if (rootBlockDevice == NULL) {
    return -ENODEV;
  }

  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = rootBlockDevice;
  fs.blockSize = fs.blockDevice->blockSize;
  fs.numProcesses = HAL->memory->numProcesses;
  ProcessDescriptor *processes = NULL;
  HAL->memory->allProcesses(&processes);
  fs.allProcesses = processes;

  BlockOverlayArgs blockOverlayArgs = {
    .blockDevice = rootBlockDevice,
    .startBlock = 1,
    .args = &fs,
  };
  if (processCreate(processDescriptor, runBlockOverlay, &blockOverlayArgs)
    != processSuccess
  ) {
    printString(_restartFilesystemFailedMessage);
    return -ENOMEM;
  }

  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = _filesystemName;
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  setProcessHalCapabilities(processDescriptor, filesystemHalCapabilities,
    sizeof(filesystemHalCapabilities) / sizeof(filesystemHalCapabilities[0]));
  processDescriptor->restartFunction = restartOverlayFilesystem;
  processDescriptor->callOverlayFunction = callOverlayFunctionFromBlockDevice;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  // Let the filesystem process initialize before we return.  See the
  // comment in restartBuiltinFilesystem above for why this waits through
  // both NULL and the placeholder value.
  while ((fs.driverState == NULL)
    || (fs.driverState == (void*) ((intptr_t) 1))
  ) {
    runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
  }

  return 0;
}

/// @fn int restartContiguousFilesystem(
///   ProcessDescriptor *processDescriptor)
///
/// @brief Restart the filesystem process run as a dedicated contiguous overlay
/// process using the existing root block device.
///
/// @param processDescriptor A pointer to the ProcessDescriptor of the
///   filesystem process to restart.
///
/// @return Returns 0 on success, -errno on failure.
int restartContiguousFilesystem(ProcessDescriptor *processDescriptor) {
  BlockDevice *rootBlockDevice = NULL;
  if ((HAL->blockDevice != NULL) && (HAL->memory != NULL)) {
    HAL->blockDevice->get(0, &rootBlockDevice);
  }
  if (rootBlockDevice == NULL) {
    printString(_noRootBlockDeviceMessage);
    processDescriptor->restartFunction = NULL;
    return -ENODEV;
  }

  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = rootBlockDevice;
  fs.blockSize = fs.blockDevice->blockSize;
  fs.numProcesses = HAL->memory->numProcesses;
  ProcessDescriptor *processes = NULL;
  HAL->memory->allProcesses(&processes);
  fs.allProcesses = processes;

  NanoOsOverlayMap *overlayMap = NULL;
  HAL->memory->contiguousFilesystem(&overlayMap);
  // HAL->memory->contiguousFilesystem can fail if the calling process doesn't
  // have the HAL capabilities to run it, which would leave overlayMap NULL.  It
  // *SHOULD'T* fail because only the scheduler should be running this function
  // and the scheduler is the one privileged process, but don't take any chances
  // here.  Read the binary into memory as long as overlayMap isn't NULL.
  if ((overlayMap == NULL)
    || rootBlockDevice->schedReadBlocks(
    rootBlockDevice->context,
    /* startBlock= */ 1,
    /* numBlocks= */ HAL->memory->contiguousFilesystemSize
      / (size_t) rootBlockDevice->blockSize,
    rootBlockDevice->blockSize,
    (uint8_t*) overlayMap) != 0
  ) {
    printString(_contiguousFilesystemReadFailedMessage);
    processDescriptor->restartFunction = NULL;
    return -EIO;
  } else if ((overlayMap->header.magic != NANO_OS_OVERLAY_MAGIC)
    || (overlayMap->header.version != NANO_OS_OVERLAY_VERSION)
  ) {
    printString(_contiguousFilesystemVersionMessage);
    processDescriptor->restartFunction = NULL;
    return -ENOEXEC;
  }
  overlayMap->header.osApi = NANO_OS_API;

  OverlayFunction filesystemMain = NULL;
  for (uint16_t ii = 0; ii < overlayMap->numExports; ii++) {
    if (strcmp(overlayMap->exports[ii].name, _mainFunctionName) == 0) {
      filesystemMain = overlayMap->exports[ii].fn;
      break;
    }
  }
  if (filesystemMain == NULL) {
    printString(_noMainFunctionMessage);
    processDescriptor->restartFunction = NULL;
    return -ENOTSUP;
  }

  if (processCreate(processDescriptor, filesystemMain, &fs) != processSuccess) {
    printString(_restartContiguousFilesystemFailedMessage);
    return -ENOMEM;
  }

  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = _filesystemName;
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  setProcessHalCapabilities(processDescriptor, filesystemHalCapabilities,
    sizeof(filesystemHalCapabilities) / sizeof(filesystemHalCapabilities[0]));
  processDescriptor->restartFunction = restartContiguousFilesystem;
  processDescriptor->callOverlayFunction = NULL;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  // Let the filesystem process initialize before we return.  See the
  // comment in restartBuiltinFilesystem above for why this waits through
  // both NULL and the placeholder value.
  while ((fs.driverState == NULL)
    || (fs.driverState == (void*) ((intptr_t) 1))
  ) {
    runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
  }

  return 0;
}

// We want to utilize the HAL UART callback storage that is provided by the HAL
// implementation, so provide those declarations here.
extern HalUartCallback halUartCallbacks[];
extern ProcessMessage halUartCallbackMessages[];
extern volatile bool halUartCallbacksPending[];

/// @fn int halCommonUartRegisterCallback(int32_t deviceId, va_list args)
///
/// @brief Common helper function to register a callback to be called when a
/// UART interrupt fires.
///
/// @param deviceId The device ID that callHal checked the call against.
/// @param args A va_list that captures the parameters that were passed to
///   HAL->uart->registerCallback.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonUartRegisterCallback(int32_t deviceId, va_list args) {
  ProcessDescriptor *processDescriptor = va_arg(args, ProcessDescriptor*);
  int64_t messageType = va_arg(args, int64_t);
  void *priv = va_arg(args, void*);

  if ((deviceId < 0) || (((uint32_t) deviceId) >= HAL->uart->numSupported)) {
    return -ENODEV;
  }

  if (pollOnly(HAL->uart, deviceId)) {
    // We can't configure this device with a callback.  It's poll-only.
    return -ENOTSUP;
  }

  HalUartCallback *halUartCallback = &halUartCallbacks[deviceId];
  halUartCallback->processDescriptor = processDescriptor;
  halUartCallback->messageType = messageType;
  halUartCallback->priv = priv;

  return 0;
}

/// @fn void halCommonUartMarkReady(int32_t deviceId)
///
/// @brief Mark a callback as being ready.
///
/// @param deviceId The device ID of the UART to mark the callback ready for.
///   The callback must have been previously registered with
///   halCommonUartRegisterCallback.
///
/// @return This function returns no value.
void halCommonUartMarkReady(int32_t deviceId) {
  // *DO NOT* check HAL permissions on the device ID this time.  This function
  // is called from an interrupt and can happen within the context of *ANY*
  // process.
  //
  // Also, *DO NOT* do parameter validation in this function.  This function is
  // intended to be called from within a dedicated interrupt vector that has the
  // deviceId hardcoded.  We don't want to waste time validating the same
  // parameter every single time.  If the caller doesn't write their vector with
  // the correct deviceId, that's on them.
  HalUartCallback *halUartCallback = &halUartCallbacks[deviceId];
  if (halUartCallback->processDescriptor == NULL) {
    // Nothing registered.  Nothing to mark.
    return;
  }

  halUartCallbacksPending[deviceId] = true;
  halPlatformCallbacksAnyPending = true;
}

/// @fn int halCommonPlatformLaunchProcesses(int32_t deviceId, va_list args)
///
/// @brief Platform launchProcesses implementation for platforms that use
/// halCommonLaunchProcesses unchanged.
///
/// @param deviceId The device ID that callHal checked the call against.
/// @param args A va_list holding a HalLaunchProcessesFn* to populate.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonPlatformLaunchProcesses(int32_t deviceId, va_list args) {
  (void) deviceId;
  HalLaunchProcessesFn *returnValue = va_arg(args, HalLaunchProcessesFn*);
  if (returnValue != NULL) {
    *returnValue = halCommonLaunchProcesses;
  }
  return 0;
}

/// @fn int halCommonPlatformInvokePendingCallbacks(int32_t deviceId,
///   va_list args)
///
/// @brief Invoke any pending UART callbacks.  Only called through
/// halPlatformInvokePendingCallbacks, which has already found
/// halPlatformCallbacksAnyPending set.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonPlatformInvokePendingCallbacks(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;

  halPlatformCallbacksAnyPending = false;
  if (HAL->uart == NULL) {
    return 0;
  }

  // BUG:  Look, I know this is cheating.  HAL->uart->numSupported is a uint32_t
  // and the smallest platform we run on has 16-bit integers, so this is data
  // loss.  We can't affort to run the for loop with non-register-width
  // integers, though.  This function is intended to be run every scheduler loop
  // and on systems without native 32-bit integer support, it's just too slow
  // to do 32-bit math.  For now, I'm going to assume that we'll never run on
  // a system with more that 65,535 UARTs.  *IF WE EVER DO* in some sort of
  // weird, alternate universe, the fix for this would be to break numSupported
  // into a high 16-bits and a low 16-bits and run a double for loop.  At the
  // moment, the cost of doing that is considered unacceptable, so we'll just
  // accept the data loss and (infinitesimally) small risk.
  unsigned int numDevices = (unsigned int) HAL->uart->numSupported;
  for (unsigned int ii = 0; ii < numDevices; ii++) {
    if (halUartCallbacksPending[ii] == false) {
      // Nothing to notify
      continue;
    }

    HalUartCallback *halUartCallback = &halUartCallbacks[ii];
    if (halUartCallback->processDescriptor == NULL) {
      // Not an error, but there's no callback configured.  Bail.
      continue;
    }

    ProcessMessage *processMessage =  &halUartCallbackMessages[ii];
    if (processMessageInUse(processMessage)) {
      // The recipient hasn't processed the last interrupt.  That's OK.  When
      // they do a read, they'll get everything in the buffer.  We need to mark
      // halPlatformCallbacksAnyPending true again so that we don't take the
      // early exit the next time this function is called.
      halPlatformCallbacksAnyPending = true;
      continue;
    }

    halUartCallbacksPending[ii] = false;

    processMessageInit(processMessage,
      halUartCallback->messageType, halUartCallback->priv,
      sizeof(void*), false);
    if (processMessageQueuePush(
      halUartCallback->processDescriptor, processMessage) != processSuccess
    ) {
      processMessageRelease(processMessage);

      // processMessageQueuePush will fail if the process is no longer running.
      // Clean up the callback if so.
      if (processRunning(halUartCallback->processDescriptor) == false) {
        memset(halUartCallback, 0, sizeof(*halUartCallback));
        // Don't mark the callback pending again.
        continue;
      }

      // Mark the callback pending again since we weren't able to process it.
      halUartCallbacksPending[ii] = true;
      halPlatformCallbacksAnyPending = true;
    }
  }

  return 0;
}

/// @fn int halCommonInit(
///   FilesystemDriverInit builtinFilesystemInitDriver,
///   const FilesystemCommandHandler *builtinFilesystemCommandHandlers,
///   int numExtraFilesystemStacks)
///
/// @brief Initialization function common to multiple HAL implementations.
/// Uses the global HAL pointer to call subsystem init and configure functions.
///
/// @param builtinFilesystemInitDriver The function to use to initialize the
///   built-in filesystem.
/// @param builtinFilesystemCommandHandlers The command handlers to use for the
///   built-in filesystem.
/// @param numExtraFilesystemStacks The number of extra stacks to allocate for
///   the filesystem.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInit(
  FilesystemDriverInit builtinFilesystemInitDriver,
  const FilesystemCommandHandler *builtinFilesystemCommandHandlers,
  int numExtraFilesystemStacks
) {
  if (HAL == NULL) {
    return -EINVAL;
  }

  int32_t ii = 0;
  int32_t defaultUart = -1;

  if ((HAL->uart == NULL) || (HAL->uart->init() < 0)) {
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

  if ((HAL->dio != NULL) && (HAL->dio->init() != 0)) {
    logWarn("Failed to initialize DIO subsystem\n");
  }

  if ((HAL->spi != NULL) && (HAL->spi->init() != 0)) {
    logWarn("Failed to initialize SPI subsystem\n");
  }

  if ((HAL->clock != NULL) && (HAL->clock->init() != 0)) {
    logWarn("Failed to initialize clock subsystem\n");
  }

  do {
    if (HAL->timer == NULL) {
      break;
    } else if (HAL->timer->init() != 0) {
      logWarn("Failed to initialize timer subsystem\n");
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
      logWarn("Did not initialize all timers\n");
    }
  } while (0);

  if (HAL->memory != NULL) {
    NanoOsOverlayMap *overlayMap = NULL;
    HAL->memory->overlayMap(&overlayMap);
    if ((overlayMap != NULL) && (HAL->memory->overlaySize > 0)) {
      memset(overlayMap, 0, HAL->memory->overlaySize);
    }

    char *logBuffer = NULL;
    HAL->memory->logBuffer(&logBuffer);
    if ((logBuffer != NULL) && (HAL->memory->logBufferSize > 0)) {
      memset(logBuffer, 0, HAL->memory->logBufferSize);
    }
  }

  _builtinFilesystemInitDriver = builtinFilesystemInitDriver;
  _builtinFilesystemCommandHandlers = builtinFilesystemCommandHandlers;
  _numExtraFilesystemStacks = numExtraFilesystemStacks;

  return 0;
}

