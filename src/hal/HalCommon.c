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

/// @fn int callHal(HalSubsystem subsystem, uint32_t function, ...)
///
/// @brief Dispatch a call to the registered platform-specific HAL function.
///
/// @param subsystem The HalSubsystem index into halFunctions.
/// @param function The function-name enum index for the given subsystem.
/// @param ... Arguments to forward to the platform function via va_list.
///
/// @return Returns the value returned by the platform function, or -ENOTSUP if
/// no function has been registered for the given subsystem/function pair.
int callHal(HalSubsystem subsystem, uint32_t function, ...) {
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
    // explicit grant in its own halCapabilities array.
    if ((processDescriptor->processId != schedulerPid)
      && (findHalCapability(processDescriptor->halCapabilities,
        processDescriptor->numHalCapabilities, subsystem, function) == NULL)
    ) {
      return -EACCES;
    }
  }
  va_list args;
  va_start(args, function);
  int returnValue = halFunctions[subsystem][function](args);
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
static int halUartPoll(int32_t deviceId);
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
// Common HAL instance — function pointers set to typed wrappers above; data
// members (numSupported, online, overlayMap, etc.) set by platform init code,
// which writes to halImpl directly since it needs a mutable view.  The
// read-only HAL pointer below points at this same instance for general use.
// ---------------------------------------------------------------------------

Hal halImpl = {
  .platform = {
    .callFileOverlay       = halPlatformCallFileOverlay,
    .execCommand           = halPlatformExecCommand,
    .restartRootFilesystem = halPlatformRestartRootFilesystem,
    .restartShell          = halPlatformRestartShell,
    .startProcesses        = halPlatformStartProcesses,
  },
  .memory = {
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
  },
  .uart = {
    .numSupported = 0,
    .online       = NULL,
    .init         = halUartInit,
    .configure    = halUartConfigure,
    .poll         = halUartPoll,
    .write        = halUartWrite,
    .isConsole    = halUartIsConsole,
  },
  .dio = {
    .numSupported = 0,
    .online       = NULL,
    .init         = halDioInit,
    .configure    = halDioConfigure,
    .write        = halDioWrite,
  },
  .spi = {
    .numSupported  = 0,
    .online        = NULL,
    .init          = halSpiInit,
    .configure     = halSpiConfigure,
    .setSpeed      = halSpiSetSpeed,
    .startTransfer = halSpiStartTransfer,
    .endTransfer   = halSpiEndTransfer,
    .transfer8     = halSpiTransfer8,
    .transferBytes = halSpiTransferBytes,
  },
  .clock = {
    .init                   = halClockInit,
    .setSystemTime          = halClockSetSystemTime,
    .getElapsedMilliseconds = halClockGetElapsedMilliseconds,
    .getElapsedMicroseconds = halClockGetElapsedMicroseconds,
    .getElapsedNanoseconds  = halClockGetElapsedNanoseconds,
  },
  .power = {
    .enterMode = halPowerEnterMode,
  },
  .timer = {
    .numSupported          = 0,
    .online                = NULL,
    .init                  = halTimerInit,
    .initDevice            = halTimerInitDevice,
    .configOneShot         = halTimerConfigOneShot,
    .configuredNanoseconds = halTimerConfiguredNanoseconds,
    .remainingNanoseconds  = halTimerRemainingNanoseconds,
    .cancel                = halTimerCancel,
    .cancelAndGet          = halTimerCancelAndGet,
  },
  .blockDevice = {
    .numSupported = 0,
    .online       = NULL,
    .init         = halBlockDeviceInit,
    .get          = halBlockDeviceGet,
    .restart      = halBlockDeviceRestart,
  },
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
  return callHal(HAL_MEMORY, HAL_MEMORY_PROCESS_STACK_SIZE,
    debug, returnValue);
}

static int halMemoryMemoryManagerStackSize(bool debug,
  size_t *returnValue
) {
  return callHal(HAL_MEMORY, HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE,
    debug, returnValue);
}

static int halMemoryBottomOfHeap(bool debug, void **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_BOTTOM_OF_HEAP, debug, returnValue);
}

static int halMemoryNumExtraSchedulerStacks(bool debug,
  uint8_t *returnValue
) {
  return callHal(HAL_MEMORY, HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS,
    debug, returnValue);
}

static int halMemoryNumExtraConsoleStacks(bool debug,
  uint8_t *returnValue
) {
  return callHal(HAL_MEMORY, HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS,
    debug, returnValue);
}

static int halMemoryOverlayMap(NanoOsOverlayMap **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_OVERLAY_MAP, returnValue);
}

static int halMemoryContiguousFilesystem(NanoOsOverlayMap **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_CONTIGUOUS_FILESYSTEM, returnValue);
}

static int halMemoryStaticLogs(StaticLogs **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_STATIC_LOGS, returnValue);
}

static int halMemoryLogBuffer(char **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_LOG_BUFFER, returnValue);
}

static int halMemoryLogEntries(LogEntry **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_LOG_ENTRIES, returnValue);
}

static int halMemoryLogMessages(ProcessMessage **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_LOG_MESSAGES, returnValue);
}

static int halMemoryAllProcesses(ProcessDescriptor **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_ALL_PROCESSES, returnValue);
}

static int halMemoryReadyQueues(ProcessQueue ***returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_READY_QUEUES, returnValue);
}

static int halMemoryWaitingQueue(ProcessQueue **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_WAITING_QUEUE, returnValue);
}

static int halMemoryTimedWaitingQueue(ProcessQueue **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_TIMED_WAITING_QUEUE, returnValue);
}

static int halMemoryFreeQueue(ProcessQueue **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_FREE_QUEUE, returnValue);
}

static int halMemoryProcessErrorNumbers(int **returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_PROCESS_ERROR_NUMBERS, returnValue);
}

static int halMemoryProcessStorage(void ****returnValue) {
  return callHal(HAL_MEMORY, HAL_MEMORY_PROCESS_STORAGE, returnValue);
}

static int halPlatformCallFileOverlay(HalCallFileOverlayFn *returnValue) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_CALL_FILE_OVERLAY, returnValue);
}

static int halPlatformExecCommand(HalExecCommandFn *returnValue) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_EXEC_COMMAND, returnValue);
}

static int halPlatformRestartRootFilesystem(
  HalRestartRootFilesystemFn *returnValue
) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_RESTART_ROOT_FILESYSTEM,
    returnValue);
}

static int halPlatformRestartShell(HalRestartShellFn *returnValue) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_RESTART_SHELL, returnValue);
}

static int halPlatformStartProcesses(HalStartProcessesFn *returnValue) {
  return callHal(HAL_PLATFORM, HAL_PLATFORM_START_PROCESSES, returnValue);
}

static int halUartInit(void) {
  return callHal(HAL_UART, HAL_UART_INIT);
}

static int halUartConfigure(int32_t deviceId, uint32_t baud) {
  return callHal(HAL_UART, HAL_UART_CONFIGURE, deviceId, baud);
}

static int halUartPoll(int32_t deviceId) {
  return callHal(HAL_UART, HAL_UART_POLL, deviceId);
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
  return callHal(HAL_DIO, HAL_DIO_INIT);
}

static int halDioConfigure(int32_t deviceId, bool output) {
  return callHal(HAL_DIO, HAL_DIO_CONFIGURE, deviceId, output);
}

static int halDioWrite(int32_t deviceId, bool high) {
  return callHal(HAL_DIO, HAL_DIO_WRITE, deviceId, high);
}

static int halSpiInit(void) {
  return callHal(HAL_SPI, HAL_SPI_INIT);
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

static int halClockInit(void) {
  return callHal(HAL_CLOCK, HAL_CLOCK_INIT);
}

static int halClockSetSystemTime(struct timespec *ts) {
  return callHal(HAL_CLOCK, HAL_CLOCK_SET_SYSTEM_TIME, ts);
}

static int halClockGetElapsedMilliseconds(int64_t startTime,
  int64_t *returnValue
) {
  return callHal(HAL_CLOCK, HAL_CLOCK_GET_ELAPSED_MILLISECONDS,
    startTime, returnValue);
}

static int halClockGetElapsedMicroseconds(int64_t startTime,
  int64_t *returnValue
) {
  return callHal(HAL_CLOCK, HAL_CLOCK_GET_ELAPSED_MICROSECONDS,
    startTime, returnValue);
}

static int halClockGetElapsedNanoseconds(int64_t startTime,
  int64_t *returnValue
) {
  return callHal(HAL_CLOCK, HAL_CLOCK_GET_ELAPSED_NANOSECONDS,
    startTime, returnValue);
}

static int halPowerEnterMode(HalPowerMode powerMode) {
  return callHal(HAL_POWER, HAL_POWER_ENTER_MODE, (int) powerMode);
}

static int halTimerInit(void) {
  return callHal(HAL_TIMER, HAL_TIMER_INIT);
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
  return callHal(HAL_BLOCK_DEVICE, HAL_BLOCK_DEVICE_INIT);
}

static int halBlockDeviceGet(int32_t deviceId, BlockDevice **returnValue) {
  return callHal(HAL_BLOCK_DEVICE, HAL_BLOCK_DEVICE_GET,
    deviceId, returnValue);
}

static int halBlockDeviceRestart(ProcessDescriptor *processDescriptor) {
  return callHal(HAL_BLOCK_DEVICE, HAL_BLOCK_DEVICE_RESTART,
    processDescriptor);
}

// ---------------------------------------------------------------------------
// Common HAL helper implementations.
// ---------------------------------------------------------------------------

/// @fn ProcessId reserveProcessSlot(void)
///
/// @brief Reserve the next available process slot, advancing the
/// scheduler's firstUserPid/firstShellPid bookkeeping accordingly.  Shared
/// by every platform-specific process the HAL starts during scheduler
/// bring-up (SD card, root filesystem, logger, and future ones) so that
/// slot allocation isn't duplicated ad hoc at each call site.
///
/// @return Returns the ProcessId of the reserved slot.
ProcessId reserveProcessSlot(void) {
  ProcessId pid = SCHEDULER_STATE->firstUserPid;
  SCHEDULER_STATE->firstUserPid = pid + 1;
  SCHEDULER_STATE->firstShellPid = SCHEDULER_STATE->firstUserPid;
  return pid;
}

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
  processDescriptor->restartFunction = HAL->blockDevice.restart;
  processDescriptor->restartArgs = (void*)(intptr_t)0;
  if (sdCardHalCapabilities != NULL) {
    processDescriptor->halCapabilities = sdCardHalCapabilities;
    processDescriptor->numHalCapabilities = numSdCardHalCapabilities;
  }
  BlockDevice *sdDevice = (BlockDevice*) coroutineResume(
    processDescriptor->mainThread, NULL);
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

/// @fn int halCommonInitRootFilesystem(void)
///
/// @brief Common initialization for the root filesystem process.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInitRootFilesystem(void) {
  if (SCHEDULER_STATE == NULL) {
    return -EBUSY;
  }

  BlockDevice *rootBlockDevice = NULL;
  HAL->blockDevice.get(0, &rootBlockDevice);

  if (rootBlockDevice == NULL) {
    if (HAL->blockDevice.init() != 0) {
      logError("HAL->blockDevice.init() failed\n");
      return -ENODEV;
    }

    HAL->blockDevice.get(0, &rootBlockDevice);
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
  halImpl.platform.restartRootFilesystem(&restartRootFilesystem);
  processDescriptor->restartFunction = restartRootFilesystem;
  // DO NOT resume the process yet.  Let the scheduler take care of that.

  // The filesystem runs as an overlay, which increases the depth of the call
  // stack.  Double the stack size for it.
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
/// HAL->memory.stringsPresent is false: platforms whose OS image keeps its
/// .rodata can format log messages immediately and have no need for a
/// separate logger process.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInitLogger(void) {
  if (SCHEDULER_STATE == NULL) {
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
  HAL->platform.callFileOverlay(&processDescriptor->callOverlayFunction);
  // The logger is an executive process, but we're going to start it in
  // supervisor mode until the system comes up far enough to launch it.
  // restartLogger will take care of fixing the level once it launches
  // successfully.
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
  processDescriptor->restartFunction = restartLogger;
  registerProcessName(_loggerName, loggerPid);
  logDebug("Initialized logger process\n");

  // Grant the logger process its IPC capabilities, if this platform has any
  // to grant (see LOGGER_IPC_CAPABILITIES_INITIALIZER).
  if (loggerIpcCapabilities != NULL) {
    loggerIpcCapabilities[0].destinationPid = schedulerPid;
    loggerIpcCapabilities[1].destinationPid = memoryManagerPid;
    loggerIpcCapabilities[2].destinationPid = rootFilesystemPid;
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
  BlockDevice *rootBlockDevice = NULL;
  HAL->blockDevice.get(0, &rootBlockDevice);
  if (rootBlockDevice == NULL) {
    return -ENODEV;
  }

  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = rootBlockDevice;
  fs.blockSize = fs.blockDevice->blockSize;

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
    SCHEDULER_STATE->runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
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
  BlockDevice *rootBlockDevice = NULL;
  HAL->blockDevice.get(0, &rootBlockDevice);
  if (rootBlockDevice == NULL) {
    return -ENODEV;
  }

  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = rootBlockDevice;
  fs.blockSize = fs.blockDevice->blockSize;

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
  processDescriptor->restartFunction = restartOverlayFilesystem;
  processDescriptor->callOverlayFunction = callOverlayFunctionFromBlockDevice;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  // Let the filesystem process initialize before we return.  See the
  // comment in restartBuiltinFilesystem above for why this waits through
  // both NULL and the placeholder value.
  while ((fs.driverState == NULL)
    || (fs.driverState == (void*) ((intptr_t) 1))
  ) {
    SCHEDULER_STATE->runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
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
  HAL->blockDevice.get(0, &rootBlockDevice);
  if (rootBlockDevice == NULL) {
    printString(_noRootBlockDeviceMessage);
    processDescriptor->restartFunction = NULL;
    return -ENODEV;
  }

  FilesystemState fs;
  memset(&fs, 0, sizeof(fs));
  fs.blockDevice = rootBlockDevice;
  fs.blockSize = fs.blockDevice->blockSize;

  NanoOsOverlayMap *overlayMap = NULL;
  HAL->memory.contiguousFilesystem(&overlayMap);
  // HAL->memory.contiguousFilesystem can fail if the calling process doesn't
  // have the HAL capabilities to run it, which would leave overlayMap NULL.  It
  // *SHOULD'T* fail because only the scheduler should be running this function
  // and the scheduler is the one privileged process, but don't take any chances
  // here.  Read the binary into memory as long as overlayMap isn't NULL.
  if ((overlayMap == NULL)
    || rootBlockDevice->schedReadBlocks(
    rootBlockDevice->context,
    /* startBlock= */ 1,
    /* numBlocks= */ HAL->memory.contiguousFilesystemSize
      / (size_t) rootBlockDevice->blockSize,
    rootBlockDevice->blockSize,
    (uint8_t*) overlayMap) != 0
  ) {
    printString(_contiguousFilesystemReadFailedMessage);
    processDescriptor->restartFunction = NULL;
    return -EIO;
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
  processDescriptor->restartFunction = restartContiguousFilesystem;
  processDescriptor->callOverlayFunction = NULL;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  // Let the filesystem process initialize before we return.  See the
  // comment in restartBuiltinFilesystem above for why this waits through
  // both NULL and the placeholder value.
  while ((fs.driverState == NULL)
    || (fs.driverState == (void*) ((intptr_t) 1))
  ) {
    SCHEDULER_STATE->runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
  }

  return 0;
}

/// @fn int halCommonInit(
///   FilesystemDriverInit builtinFilesystemInitDriver,
///   const FilesystemCommandHandler *builtinFilesystemCommandHandlers)
///
/// @brief Initialization function common to multiple HAL implementations.
/// Uses the global HAL pointer to call subsystem init and configure functions.
///
/// @return Returns 0 on success, -errno on failure.
int halCommonInit(
  FilesystemDriverInit builtinFilesystemInitDriver,
  const FilesystemCommandHandler *builtinFilesystemCommandHandlers
) {
  if (HAL == NULL) {
    return -EINVAL;
  }

  int32_t ii = 0;
  int32_t defaultUart = -1;

  if (HAL->uart.init() < 0) {
    return -ENOTTY;
  }
  int32_t numUarts = HAL->uart.numSupported;
  if (numUarts <= 0) {
    // Nothing we can do.
    return -ENOTTY;
  }

  for (ii = 0; ii < numUarts; ii++) {
    if (!online(HAL->uart, ii)) {
      continue;
    }

    if (HAL->uart.configure(ii, 1000000) == 0) {
      if (defaultUart < 0) {
        defaultUart = ii;
      }
    } else {
      setOffline(HAL->uart, ii);
    }
  }

  if (HAL->dio.init() != 0) {
    logWarn("Failed to initialize DIO subsystem\n");
  }

  if (HAL->spi.init() != 0) {
    logWarn("Failed to initialize SPI subsystem\n");
  }

  if (HAL->clock.init() != 0) {
    logWarn("Failed to initialize clock subsystem\n");
  }

  do {
    if (HAL->timer.init() != 0) {
      logWarn("Failed to initialize timer subsystem\n");
      break;
    }

    uint32_t timerOnline = HAL->timer.online[0];
    for (ii = 0; ii < (int32_t) HAL->timer.numSupported; ii++) {
      if (online(HAL->timer, ii) == false) {
        continue;
      }

      if (HAL->timer.initDevice(ii) < 0) {
        setOffline(HAL->timer, ii);
      }
    }

    if (HAL->timer.online[0] != timerOnline) {
      logWarn("Did not initialize all timers\n");
    }
  } while (0);

  NanoOsOverlayMap *overlayMap = NULL;
  HAL->memory.overlayMap(&overlayMap);
  if ((overlayMap != NULL) && (HAL->memory.overlaySize > 0)) {
    memset(overlayMap, 0, HAL->memory.overlaySize);
  }

  char *logBuffer = NULL;
  HAL->memory.logBuffer(&logBuffer);
  if ((logBuffer != NULL) && (HAL->memory.logBufferSize > 0)) {
    memset(logBuffer, 0, HAL->memory.logBufferSize);
  }

  _builtinFilesystemInitDriver = builtinFilesystemInitDriver;
  _builtinFilesystemCommandHandlers = builtinFilesystemCommandHandlers;

  return 0;
}

