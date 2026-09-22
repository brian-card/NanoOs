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

// Must come before HalCommon.h: struct stat and struct dirent (from
// kernel/Filesystem.h) declare fields of type uid_t/gid_t, so those typedefs
// need to be established under their real names here, first and unwrapped.
// Otherwise, reached fresh for the first time down HalCommon.h's own include
// chain below, they would fall inside the uid_t/gid_t/pid_t rename bracket
// immediately below and end up referring to types (C_uid_t/C_gid_t) that
// the rename never actually defines.
#include "kernel/Filesystem.h"

#include "HalPosix.h"

// HalCommon.h transitively includes, via kernel/Logger.h's own chain, the
// real <stdlib.h>/<sys/types.h>, which would otherwise typedef
// uid_t/gid_t/pid_t in this same translation unit with types conflicting
// with the ones kernel/Filesystem.h (included above) already established.
// Renaming them here for the real header's own definitions -- reached only
// inside this one #include -- mirrors the identical hazard every file that
// includes "stdio.h" already guards against (see e.g. Console.c, Logger.c,
// Commands.c) and the one HalCommon.c itself already guards against around
// its own #include "HalCommon.h".
#define gid_t C_gid_t
#define uid_t C_uid_t
#define pid_t C_pid_t
#include "HalCommon.h"
#undef gid_t
#undef uid_t
#undef pid_t

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
int posixProcessStackSize(va_list args);
int posixMemoryManagerStackSize(va_list args);
int posixBottomOfHeap(va_list args);
int posixNumExtraSchedulerStacks(va_list args);
int posixNumExtraConsoleStacks(va_list args);

int posixInitUart(va_list args);
int posixConfigureUart(va_list args);
int posixPollUart(va_list args);
int posixWriteUart(va_list args);
int posixIsUartConsole(va_list args);

int posixInitDio(va_list args);
int posixConfigureDio(va_list args);
int posixWriteDio(va_list args);

int posixInitSpi(va_list args);
int posixConfigureSpiDevice(va_list args);
int posixSetSpiSpeed(va_list args);
int posixStartSpiTransfer(va_list args);
int posixEndSpiTransfer(va_list args);
int posixSpiTransfer8(va_list args);
int posixSpiTransferBytes(va_list args);

int posixTimeInit(va_list args);
int posixSetSystemTime(va_list args);
int posixGetElapsedMilliseconds(va_list args);
int posixGetElapsedMicroseconds(va_list args);
int posixGetElapsedNanoseconds(va_list args);

int posixEnterPowerMode(va_list args);

int posixInitTimer(va_list args);
int posixInitTimerDevice(va_list args);
int posixConfigOneShotTimer(va_list args);
int posixConfiguredTimerNanoseconds(va_list args);
int posixRemainingTimerNanoseconds(va_list args);
int posixCancelTimer(va_list args);
int posixCancelAndGetTimer(va_list args);

int halPosixImplInit(jmp_buf resetBuffer,
  NanoOsOverlayMap **overlayMap, size_t *overlaySize, StaticLogs **staticLogs,
  NanoOsOverlayMap **contiguousFilesystem, size_t *contiguousFilesystemSize);

static int posixCallFileOverlay(va_list args);
static int posixExecCommand(va_list args);
static int posixRestartRootFilesystem(va_list args);
static int posixRestartShell(va_list args);
static int posixStartProcesses(va_list args);

static int posixOverlayMap(va_list args);
static int posixContiguousFilesystem(va_list args);
static int posixStaticLogs(va_list args);
static int posixLogBuffer(va_list args);
static int posixLogEntries(va_list args);
static int posixLogMessages(va_list args);
static int posixAllProcesses(va_list args);
static int posixReadyQueues(va_list args);
static int posixWaitingQueue(va_list args);
static int posixTimedWaitingQueue(va_list args);
static int posixFreeQueue(va_list args);
static int posixProcessErrorNumbers(va_list args);
static int posixProcessStorage(va_list args);

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

/// @def NUM_PROCESSES
///
/// @brief Value to indicate the maximum number of processes that can be run
/// concurrently.
#define NUM_PROCESSES 10

/// @struct HalProcessQueue
///
/// @brief Structure to manage an individual process queue.  This is the
/// implementation that backs the ProcessQueue structure in the kernel.
///
/// @param name The string name of the queue for use in error messages.
/// @param head The index of the head of the queue.
/// @param tail The index of the tail of the queue.
/// @param numElements The number of elements currently in the queue.
/// @param processes The array of pointers to ProcessDescriptors from the
///   allProcesses array.  This is a variable-length array in the kernel.  It's
///   declared with definitive size here since this is the actual
///   implementationn backing.
typedef struct HalProcessQueue {
  const char        *name;
  uint8_t            head;
  uint8_t            tail;
  uint8_t            numElements;
  ProcessDescriptor *processes[NUM_PROCESSES];
} HalProcessQueue;

/// @var _kernelReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_KERNEL ready
/// queue.
static HalProcessQueue _kernelReadyQueue;

/// @var _executiveReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_EXECUTIVE ready
/// queue.
static HalProcessQueue _executiveReadyQueue;

/// @var _supervisorReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_SUPERVISOR ready
/// queue.
static HalProcessQueue _supervisorReadyQueue;

/// @var _userReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_USER ready queue.
static HalProcessQueue _userReadyQueue;

/// @var _readyQueues
///
/// @brief HAL implementation backing for the ready queues.
static ProcessQueue *_readyQueues[NUM_READY_QUEUES] = {
  (ProcessQueue*) &_kernelReadyQueue,
  (ProcessQueue*) &_executiveReadyQueue,
  (ProcessQueue*) &_supervisorReadyQueue,
  (ProcessQueue*) &_userReadyQueue,
};

/// @var _waitingQueue
///
/// @brief HAL implementation backing for the waiting queue.
static HalProcessQueue _waitingQueue;

/// @var _timedWaitingQueue
///
/// @brief HAL implementation backing for the timed waiting queue.
static HalProcessQueue _timedWaitingQueue;

/// @var _freeQueue
///
/// @brief HAL implementation backing for the free queue.
static HalProcessQueue _freeQueue;

/// @var _processErrorNumbers
///
/// @brief Process-specific storage for each process's errno value.
static int _processErrorNumbers[NUM_PROCESSES + 1];

/// @var _processStorageBase
///
/// @brief File-local, first-level variable to hold the per-process storage.
static void *_processStorageBase[NUM_PROCESSES][NUM_PROCESS_STORAGE_KEYS];

/// @var _processStorage
///
/// @brief File-local, second-level variable to hold the per-process storage.
static void **_processStorage[NUM_PROCESSES];

/// @var _logBuffer
///
/// @brief Statically allocated buffer for formatting log messages.
static char _logBuffer[128];

/// @def NUM_LOG_ENTRIES
///
/// @brief The number of LogEntry objects held in our local array.
#define NUM_LOG_ENTRIES 10

/// @var _logEntries
///
/// @brief Local array of LogEntry objects to use in communication with the
/// logger process.
static LogEntry _logEntries[NUM_LOG_ENTRIES];

/// @var _logMessages
///
/// @brief Private pool of ProcessMessage objects used to deliver log entries to
/// the logger process.  One per _logEntries slot.
static ProcessMessage _logMessages[NUM_LOG_ENTRIES];

/// @var _allProcesses
///
/// @brief Statically allocated buffer of ProcessDescriptors to hold the
/// metadata for all processes on the system, including the scheduler.
static ProcessDescriptor _allProcesses[NUM_PROCESSES];

/// @var _namedProcesses
///
/// @brief This platform's storage for HalCommon.c's named-process lookup
/// table.  Sized for the processes posixDoStartProcesses can register
/// (filesystem, logger).
static NamedProcessEntry _namedProcesses[2];

/// @var _filesystemIpcCapabilities
///
/// @brief This platform's storage for the filesystem process's IPC
/// capabilities.  See FILESYSTEM_IPC_CAPABILITIES_INITIALIZER.
static IpcCapability _filesystemIpcCapabilities[]
  = FILESYSTEM_IPC_CAPABILITIES_INITIALIZER;

/// @var _loggerIpcCapabilities
///
/// @brief This platform's storage for the logger process's IPC
/// capabilities.  See LOGGER_IPC_CAPABILITIES_INITIALIZER.
static IpcCapability _loggerIpcCapabilities[]
  = LOGGER_IPC_CAPABILITIES_INITIALIZER;

/// @var _sdCardName
///
/// @brief Process name assigned to the SD card process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _sdCardName[] KEEP_IN_FLASH = "SD card";

int posixInitBlockDevice(va_list args) {
  (void) args;
  if (schedulerIsInitialized() == false) {
    return -EBUSY;
  }

  ProcessId sdCardPid = reserveProcessSlot();
  ProcessDescriptor *processDescriptor = &allProcesses[sdCardPid - 1];
  if (processCreate(
    processDescriptor, runSdCardPosix, (void*) _sdCardDevicePath)
    != processSuccess
  ) {
    logError("Could not start SD card process.\n");
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = sdCardPid;
  processDescriptor->name = _sdCardName;
  processDescriptor->userId = ROOT_USER_ID;
  BlockDevice *sdDevice = (BlockDevice*) coroutineResume(
    allProcesses[sdCardPid - 1].mainThread, NULL);
  sdDevice->partitionNumber = 1;
  blockDevices[0] = sdDevice;
  setOnline(HAL->blockDevice, 0);

  return 0;
}

int posixGetBlockDevice(va_list args) {
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

int posixRestartBlockDevice(va_list args) {
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

/// @var _overlayMap
///
/// @brief Runtime-determined overlay memory address, set by halPosixImplInit.
static NanoOsOverlayMap *_overlayMap = NULL;

/// @var _contiguousFilesystem
///
/// @brief Runtime-determined contiguous filesystem memory address, set by
/// halPosixImplInit.
static NanoOsOverlayMap *_contiguousFilesystem = NULL;

/// @var _staticLogs
///
/// @brief Runtime-determined static logs memory address, set by
/// halPosixImplInit.
static StaticLogs *_staticLogs = NULL;

static int posixCallFileOverlay(va_list args) {
  HalCallFileOverlayFn *returnValue = va_arg(args, HalCallFileOverlayFn*);
  if (returnValue != NULL) {
    *returnValue = callOverlayFunctionFromFile;
  }
  return 0;
}

static int posixExecCommand(va_list args) {
  HalExecCommandFn *returnValue = va_arg(args, HalExecCommandFn*);
  if (returnValue != NULL) {
    // Uncomment to switch to using the built-in shell:
    // *returnValue = execBuiltinCommand;
    *returnValue = execOverlayCommand;
  }
  return 0;
}

/// @fn static int posixDoStartProcesses(void)
///
/// @brief Start every process specific to the POSIX platform: the root
/// filesystem (and, transitively, the SD card process), plus the logger if
/// this build's .rodata was stripped.
///
/// @return Returns 0 on success, -errno on failure.
static int posixDoStartProcesses(void) {
  int returnValue = halCommonInitRootFilesystem();
  if (HAL->memory.stringsPresent == false) {
    int loggerStatus = halCommonInitLogger();
    if ((returnValue == 0) && (loggerStatus != 0)) {
      returnValue = loggerStatus;
    }
  }
  return returnValue;
}

static int posixStartProcesses(va_list args) {
  HalStartProcessesFn *returnValue = va_arg(args, HalStartProcessesFn*);
  if (returnValue != NULL) {
    *returnValue = posixDoStartProcesses;
  }
  return 0;
}

static int posixRestartRootFilesystem(va_list args) {
  HalRestartRootFilesystemFn *returnValue
    = va_arg(args, HalRestartRootFilesystemFn*);
  if (returnValue != NULL) {
    *returnValue = restartContiguousFilesystem;
  }
  return 0;
}

static int posixRestartShell(va_list args) {
  HalRestartShellFn *returnValue = va_arg(args, HalRestartShellFn*);
  if (returnValue != NULL) {
    // Uncomment to switch to using the built-in shell:
    // *returnValue = restartBuiltinShell;
    *returnValue = restartOverlayShell;
  }
  return 0;
}

static int posixOverlayMap(va_list args) {
  NanoOsOverlayMap **returnValue = va_arg(args, NanoOsOverlayMap**);
  if (returnValue != NULL) {
    *returnValue = _overlayMap;
  }
  return 0;
}

static int posixContiguousFilesystem(va_list args) {
  NanoOsOverlayMap **returnValue = va_arg(args, NanoOsOverlayMap**);
  if (returnValue != NULL) {
    *returnValue = _contiguousFilesystem;
  }
  return 0;
}

static int posixStaticLogs(va_list args) {
  StaticLogs **returnValue = va_arg(args, StaticLogs**);
  if (returnValue != NULL) {
    *returnValue = _staticLogs;
  }
  return 0;
}

static int posixLogBuffer(va_list args) {
  char **returnValue = va_arg(args, char**);
  if (returnValue != NULL) {
    *returnValue = _logBuffer;
  }
  return 0;
}

static int posixLogEntries(va_list args) {
  LogEntry **returnValue = va_arg(args, LogEntry**);
  if (returnValue != NULL) {
    *returnValue = _logEntries;
  }
  return 0;
}

static int posixLogMessages(va_list args) {
  ProcessMessage **returnValue = va_arg(args, ProcessMessage**);
  if (returnValue != NULL) {
    *returnValue = _logMessages;
  }
  return 0;
}

static int posixAllProcesses(va_list args) {
  ProcessDescriptor **returnValue = va_arg(args, ProcessDescriptor**);
  if (returnValue != NULL) {
    *returnValue = _allProcesses;
  }
  return 0;
}

static int posixReadyQueues(va_list args) {
  ProcessQueue ***returnValue = va_arg(args, ProcessQueue***);
  if (returnValue != NULL) {
    *returnValue = _readyQueues;
  }
  return 0;
}

static int posixWaitingQueue(va_list args) {
  ProcessQueue **returnValue = va_arg(args, ProcessQueue**);
  if (returnValue != NULL) {
    *returnValue = (ProcessQueue*) &_waitingQueue;
  }
  return 0;
}

static int posixTimedWaitingQueue(va_list args) {
  ProcessQueue **returnValue = va_arg(args, ProcessQueue**);
  if (returnValue != NULL) {
    *returnValue = (ProcessQueue*) &_timedWaitingQueue;
  }
  return 0;
}

static int posixFreeQueue(va_list args) {
  ProcessQueue **returnValue = va_arg(args, ProcessQueue**);
  if (returnValue != NULL) {
    *returnValue = (ProcessQueue*) &_freeQueue;
  }
  return 0;
}

static int posixProcessErrorNumbers(va_list args) {
  int **returnValue = va_arg(args, int**);
  if (returnValue != NULL) {
    *returnValue = _processErrorNumbers;
  }
  return 0;
}

static int posixProcessStorage(va_list args) {
  void ****returnValue = va_arg(args, void****);
  if (returnValue != NULL) {
    *returnValue = _processStorage;
  }
  return 0;
}

static HalFunction posixPlatformFunctions[HAL_PLATFORM_NUM_FNS] = {
  [HAL_PLATFORM_CALL_FILE_OVERLAY]       = posixCallFileOverlay,
  [HAL_PLATFORM_EXEC_COMMAND]            = posixExecCommand,
  [HAL_PLATFORM_RESTART_ROOT_FILESYSTEM] = posixRestartRootFilesystem,
  [HAL_PLATFORM_RESTART_SHELL]           = posixRestartShell,
  [HAL_PLATFORM_START_PROCESSES]         = posixStartProcesses,
};

static HalFunction posixMemoryFunctions[HAL_MEMORY_NUM_FNS] = {
  [HAL_MEMORY_PROCESS_STACK_SIZE]         = posixProcessStackSize,
  [HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = posixMemoryManagerStackSize,
  [HAL_MEMORY_BOTTOM_OF_HEAP]             = posixBottomOfHeap,
  [HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = posixNumExtraSchedulerStacks,
  [HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = posixNumExtraConsoleStacks,
  [HAL_MEMORY_OVERLAY_MAP]                = posixOverlayMap,
  [HAL_MEMORY_CONTIGUOUS_FILESYSTEM]      = posixContiguousFilesystem,
  [HAL_MEMORY_STATIC_LOGS]                = posixStaticLogs,
  [HAL_MEMORY_LOG_BUFFER]                 = posixLogBuffer,
  [HAL_MEMORY_LOG_ENTRIES]                = posixLogEntries,
  [HAL_MEMORY_LOG_MESSAGES]               = posixLogMessages,
  [HAL_MEMORY_ALL_PROCESSES]              = posixAllProcesses,
  [HAL_MEMORY_READY_QUEUES]               = posixReadyQueues,
  [HAL_MEMORY_WAITING_QUEUE]              = posixWaitingQueue,
  [HAL_MEMORY_TIMED_WAITING_QUEUE]        = posixTimedWaitingQueue,
  [HAL_MEMORY_FREE_QUEUE]                 = posixFreeQueue,
  [HAL_MEMORY_PROCESS_ERROR_NUMBERS]      = posixProcessErrorNumbers,
  [HAL_MEMORY_PROCESS_STORAGE]            = posixProcessStorage,
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
  [HAL_SPI_SET_SPEED]      = posixSetSpiSpeed,
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

int halPosixInit(jmp_buf resetBuffer, const char *sdCardDevicePath) {
  _sdCardDevicePath = sdCardDevicePath;

  // Wire up per-subsystem function arrays.
  halFunctions[HAL_PLATFORM]     = posixPlatformFunctions;
  halFunctions[HAL_MEMORY]       = posixMemoryFunctions;
  halFunctions[HAL_UART]         = posixUartFunctions;
  halFunctions[HAL_DIO]          = posixDioFunctions;
  halFunctions[HAL_SPI]          = posixSpiFunctions;
  halFunctions[HAL_CLOCK]        = posixClockFunctions;
  halFunctions[HAL_POWER]        = posixPowerFunctions;
  halFunctions[HAL_TIMER]        = posixTimerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = posixBlockDeviceFunctions;

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

  memset(_namedProcesses, 0, sizeof(_namedProcesses));
  namedProcessTable = _namedProcesses;
  namedProcessTableCapacity
    = sizeof(_namedProcesses) / sizeof(_namedProcesses[0]);

  filesystemIpcCapabilities = _filesystemIpcCapabilities;
  numFilesystemIpcCapabilities
    = sizeof(_filesystemIpcCapabilities) / sizeof(_filesystemIpcCapabilities[0]);
  loggerIpcCapabilities = _loggerIpcCapabilities;
  numLoggerIpcCapabilities
    = sizeof(_loggerIpcCapabilities) / sizeof(_loggerIpcCapabilities[0]);

  halImpl.memory.logBufferSize  = sizeof(_logBuffer);
  halImpl.memory.numLogEntries  = NUM_LOG_ENTRIES;
  memset(&_logEntries, 0, sizeof(_logEntries));
  memset(&_logMessages, 0, sizeof(_logMessages));
#ifdef NANO_OS_STRINGS_STRIPPED
  halImpl.memory.stringsPresent = false;
#else
  halImpl.memory.stringsPresent = true;
#endif // NANO_OS_STRINGS_STRIPPED

  memset(_allProcesses, 0, sizeof(_allProcesses));
  halImpl.memory.numProcesses        = NUM_PROCESSES;
  for (int ii = 0; ii < NUM_READY_QUEUES; ii++) {
    memset(_readyQueues[ii], 0, sizeof(HalProcessQueue));
  }
  memset(&_waitingQueue, 0, sizeof(HalProcessQueue));
  memset(&_timedWaitingQueue, 0, sizeof(HalProcessQueue));
  memset(&_freeQueue, 0, sizeof(HalProcessQueue));
  memset(_processStorageBase, 0,
    NUM_PROCESSES * NUM_PROCESS_STORAGE_KEYS * sizeof(void*));
  for (int ii = 0; ii < NUM_PROCESSES; ii++) {
    _processStorage[ii] = _processStorageBase[ii];
  }

  // Perform POSIX-specific hardware setup and retrieve the overlay mapping.
  int32_t result
    = halPosixImplInit(resetBuffer,
      &_overlayMap,
      &halImpl.memory.overlaySize,
      &_staticLogs,
      &_contiguousFilesystem,
      &halImpl.memory.contiguousFilesystemSize);
  if (result != 0) {
    _overlayMap                             = NULL;
    halImpl.memory.overlaySize              = 0;
    _contiguousFilesystem                   = NULL;
    halImpl.memory.contiguousFilesystemSize = 0;
    _staticLogs                             = NULL;
    return result;
  }
  if (_staticLogs != NULL) {
    memset(_staticLogs, 0, sizeof(StaticLogs));
  }

  NANO_OS_API = &nanoOsApi;

  // POSIX/sim always runs its filesystem as a contiguous overlay process
  // (see restartContiguousFilesystem, selected below), never the builtin
  // path, so there's no driver to hand halCommonInit here.
  return halCommonInit(
    /* builtinFilesystemInitDriver= */ NULL,
    /* builtinFilesystemCommandHandlers= */ NULL);
}

#endif // __x86_64__
