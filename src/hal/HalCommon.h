///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.28.2026
///
/// @file              HalCommon.h
///
/// @brief             Header for routines that are common to multiple HAL
///                    implementations.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include <stdarg.h>
#include <string.h>

#include "../kernel/Hal.h"
// Deliberately *NOT* including MemoryManager.h here.  The HAL has to be
// operational prior to the memory manager and really should be completely
// independent of it.
#include "../kernel/Console.h"
#include "../kernel/Filesystem.h"
#include "../kernel/Logger.h"
#include "../kernel/NanoOs.h"
#include "../kernel/Processes.h"
#include "../kernel/Scheduler.h"
#include "../kernel/SdCardSpi.h"
#include "../user/NanoOsErrno.h"


/// @typedef HalFunction
///
/// @brief Type for all HAL implementation functions in the dispatch table.
typedef int (*HalFunction)(va_list args);

/// @struct NamedProcessEntry
///
/// @brief One entry in a platform's table of named processes.
///
/// @param name The process's registered name, or NULL if this slot is
///   unused.
/// @param pid The ProcessId registered under name.
typedef struct NamedProcessEntry {
  const char *name;
  ProcessId   pid;
} NamedProcessEntry;

/// @def FILESYSTEM_IPC_CAPABILITIES_INITIALIZER
///
/// @brief Initializer for a platform's own array of IpcCapability entries
/// granted to the root filesystem process.  destinationPid fields start at
/// 0 and are patched in by halCommonInitRootFilesystem/halCommonInitLogger
/// once the processes they refer to exist.  Kept as a macro (rather than a
/// single shared array) so each platform that starts a filesystem process
/// gets its own storage -- see filesystemIpcCapabilities below -- while
/// still having exactly one place that defines what the capabilities are.
#define FILESYSTEM_IPC_CAPABILITIES_INITIALIZER { \
  { \
    .destinationPid = 0, /* Scheduler PID */ \
    .signature      = SCHEDULER_COMMAND_SIGNATURE, \
    .messageTypes   = (((uint16_t) 1) << SCHEDULER_REPLACE_OVERLAY) \
  }, \
  { \
    .destinationPid = 0, /* Console PID */ \
    .signature      = CONSOLE_COMMAND_SIGNATURE, \
    .messageTypes \
      = (((uint16_t) 1) << CONSOLE_GET_BUFFER) \
      | (((uint16_t) 1) << CONSOLE_WRITE_BUFFER) \
      | (((uint16_t) 1) << CONSOLE_RELEASE_BUFFER) \
  }, \
  { \
    .destinationPid = 0, /* Memory manager PID */ \
    .signature      = MEMORY_MANAGER_COMMAND_SIGNATURE, \
    .messageTypes \
      = (((uint16_t) 1) << MEMORY_MANAGER_REALLOC) \
      | (((uint16_t) 1) << MEMORY_MANAGER_FREE) \
  }, \
  { \
    .destinationPid = 0, /* SD card PID (== rootFilesystemPid - 1) */ \
    .signature      = SD_CARD_COMMAND_SIGNATURE, \
    .messageTypes \
      = (((uint16_t) 1) << SD_CARD_READ_BLOCKS) \
      | (((uint16_t) 1) << SD_CARD_WRITE_BLOCKS) \
  }, \
  { \
    .destinationPid = 0, /* Logger PID, patched by halCommonInitLogger */ \
    .signature      = LOGGER_COMMAND_SIGNATURE, \
    .messageTypes   = (((uint16_t) 1) << LOGGER_LOG_MESSAGE) \
  }, \
}

/// @def LOGGER_IPC_CAPABILITIES_INITIALIZER
///
/// @brief Initializer for a platform's own array of IpcCapability entries
/// granted to the logger process.  See
/// FILESYSTEM_IPC_CAPABILITIES_INITIALIZER for why this is a macro rather
/// than a single shared array.
#define LOGGER_IPC_CAPABILITIES_INITIALIZER { \
  { \
    .destinationPid = 0, /* Scheduler PID */ \
    .signature      = SCHEDULER_COMMAND_SIGNATURE, \
    .messageTypes \
      = (((uint16_t) 1) << SCHEDULER_GET_HOSTNAME) \
      | (((uint16_t) 1) << SCHEDULER_REPLACE_OVERLAY) \
  }, \
  { \
    .destinationPid = 0, /* Memory manager PID */ \
    .signature      = MEMORY_MANAGER_COMMAND_SIGNATURE, \
    .messageTypes \
      = (((uint16_t) 1) << MEMORY_MANAGER_REALLOC) \
      | (((uint16_t) 1) << MEMORY_MANAGER_FREE) \
  }, \
  { \
    .destinationPid = 0, /* Filesystem PID */ \
    .signature      = FILESYSTEM_COMMAND_SIGNATURE, \
    .messageTypes \
      = (((uint16_t) 1) << FILESYSTEM_OPEN_FILE) \
      | (((uint16_t) 1) << FILESYSTEM_CLOSE_FILE) \
      | (((uint16_t) 1) << FILESYSTEM_READ_FILE) \
      | (((uint16_t) 1) << FILESYSTEM_WRITE_FILE) \
      | (((uint16_t) 1) << FILESYSTEM_REMOVE_FILE) \
      | (((uint16_t) 1) << FILESYSTEM_SEEK_FILE) \
      | (((uint16_t) 1) << FILESYSTEM_GET_FILE_BLOCK_METADATA) \
      | (((uint16_t) 1) << FILESYSTEM_END_OF_FILE) \
  }, \
}

/// @def SD_CARD_HAL_CAPABILITIES_INITIALIZER
///
/// @brief Initializer for a platform's own array of HalCapability entries
/// granted to the SD-over-SPI card process (see
/// halCommonInitRootSdSpiStorage).  Kept as a macro, same as the
/// IpcCapability initializers above, so each platform that actually starts
/// this process gets its own storage -- see sdCardHalCapabilities below --
/// rather than every platform carrying it whether or not it's used.
///
/// Includes HAL_UART_WRITE/HAL_UART_IS_CONSOLE even though runSdCardSpi
/// never calls HAL->uart directly: logError/logWarn (used throughout
/// SdCardSpi.c) fall back to writing straight to the UART -- via
/// printString_() -- whenever the logger process isn't up yet or
/// stringsPresent is true, and that fallback runs in the calling process's
/// own context.  See the identical note on memoryManagerHalCapabilities in
/// Scheduler.c.  LOG_FALLBACK_HAL_CAPABILITIES (Hal.h) and
/// HAL_CLOCK_GET_ELAPSED_NANOSECONDS are included for that same fallback:
/// writing to the UART is only the last step of it, and without the rest
/// logMessage() can't get the buffer it formats into.
///
/// HAL_TIMER_CANCEL must stay the LAST entry: initializeSchedulerState() in
/// Scheduler.c indexes this array by its last position
/// (sdCardHalCapabilities[numSdCardHalCapabilities - 1]) to patch in the
/// preemption timer's device ID once it's known, the same way it does for
/// baseUserHalCapabilities.  The SD card process needs this so its own
/// command handlers can cancel a stale preemption timer left armed from
/// whoever ran before them, right before touching real SPI hardware.
#define SD_CARD_HAL_CAPABILITIES_INITIALIZER { \
  LOG_FALLBACK_HAL_CAPABILITIES, \
  { \
    .subsystemFunction = (((uint16_t) HAL_UART) << 8) | HAL_UART_WRITE, \
    .deviceIds =         0x03, /* Bitmask for device IDs 0 and 1 */ \
  }, \
  { \
    .subsystemFunction = (((uint16_t) HAL_UART) << 8) | HAL_UART_IS_CONSOLE, \
    .deviceIds =         0x03, /* Bitmask for device IDs 0 and 1 */ \
  }, \
  { \
    .subsystemFunction = (((uint16_t) HAL_SPI) << 8) | HAL_SPI_CONFIGURE, \
    .deviceIds =         0x01, /* Bitmask for device ID 0 */ \
  }, \
  { \
    .subsystemFunction = (((uint16_t) HAL_SPI) << 8) | HAL_SPI_START_TRANSFER, \
    .deviceIds =         0x01, /* Bitmask for device ID 0 */ \
  }, \
  { \
    .subsystemFunction = (((uint16_t) HAL_SPI) << 8) | HAL_SPI_END_TRANSFER, \
    .deviceIds =         0x01, /* Bitmask for device ID 0 */ \
  }, \
  { \
    .subsystemFunction = (((uint16_t) HAL_SPI) << 8) | HAL_SPI_TRANSFER8, \
    .deviceIds =         0x01, /* Bitmask for device ID 0 */ \
  }, \
  { \
    .subsystemFunction = (((uint16_t) HAL_SPI) << 8) | HAL_SPI_TRANSFER_BYTES, \
    .deviceIds =         0x01, /* Bitmask for device ID 0 */ \
  }, \
  { \
    .subsystemFunction = (((uint16_t) HAL_SPI) << 8) | HAL_SPI_SET_SPEED, \
    .deviceIds =         0x01, /* Bitmask for device ID 0 */ \
  }, \
  { \
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8) \
      | HAL_CLOCK_GET_ELAPSED_NANOSECONDS, \
    .deviceIds =         0x00, /* No device for this function */ \
  }, \
  { \
    .subsystemFunction = (((uint16_t) HAL_TIMER) << 8) | HAL_TIMER_CANCEL, \
    .deviceIds =         0, /* To be filled in by initializeSchedulerState() */ \
  }, \
}

#ifdef __cplusplus
extern "C"
{
#endif

extern HalFunction *halFunctions[HAL_NUM_SUBSYSTEMS];

/// @var halImpl
///
/// @brief The single, mutable, backing instance of the root HAL.  HAL
/// initialization code (platform init functions) must write to this instance
/// directly to populate the HAL's subsystems, since the read-only HAL pointer
/// exported from Hal.h cannot be written through.
extern Hal halImpl;

/// @var namedProcessTable
///
/// @brief Pointer to the platform-owned array of NamedProcessEntry slots.
/// HalCommon.c owns the lookup/registration logic but not the storage: each
/// platform declares its own array, sized to what it actually needs, and
/// points this at it during its own init (mirrors the HalUart.online /
/// HalDio.online pattern of a common field pointing at platform-owned
/// storage). NULL (the default) means the platform has no named-process
/// table at all, which findProcessByName/registerProcessName handle safely.
extern NamedProcessEntry *namedProcessTable;

/// @var namedProcessTableCapacity
///
/// @brief The number of slots in namedProcessTable, set by the platform
/// alongside namedProcessTable itself.
extern uint8_t namedProcessTableCapacity;

/// @var filesystemIpcCapabilities
///
/// @brief Pointer to the platform-owned array of IpcCapability entries for
/// the root filesystem process, built from
/// FILESYSTEM_IPC_CAPABILITIES_INITIALIZER.  NULL (the default) means this
/// platform never starts a filesystem process at all.  Same
/// platform-owned-storage pattern as namedProcessTable.
extern IpcCapability *filesystemIpcCapabilities;

/// @var numFilesystemIpcCapabilities
///
/// @brief The number of entries in filesystemIpcCapabilities.
extern size_t numFilesystemIpcCapabilities;

/// @var loggerIpcCapabilities
///
/// @brief Pointer to the platform-owned array of IpcCapability entries for
/// the logger process, built from LOGGER_IPC_CAPABILITIES_INITIALIZER.
/// NULL (the default) means this platform never starts a logger.
extern IpcCapability *loggerIpcCapabilities;

/// @var numLoggerIpcCapabilities
///
/// @brief The number of entries in loggerIpcCapabilities.
extern size_t numLoggerIpcCapabilities;

/// @var sdCardHalCapabilities
///
/// @brief Pointer to the platform-owned array of HalCapability entries for
/// the SD-over-SPI card process, built from
/// SD_CARD_HAL_CAPABILITIES_INITIALIZER.  NULL (the default) means this
/// platform never starts an SD-over-SPI card process at all -- most don't;
/// only the specific boards that call halCommonInitRootSdSpiStorage do.
extern HalCapability *sdCardHalCapabilities;

/// @var numSdCardHalCapabilities
///
/// @brief The number of entries in sdCardHalCapabilities.
extern size_t numSdCardHalCapabilities;

int callHal(HalSubsystem subsystem, uint32_t function, ...);
ProcessId reserveProcessSlot(void);
int findProcessByName(const char *name, ProcessId *returnValue);
int registerProcessName(const char *name, ProcessId pid);
BlockDevice* halCommonInitRootSdSpiStorage(SdCardSpiArgs *sdCardSpiArgs);
int halCommonInitRootFilesystem(void);
int halCommonInitLogger(void);
int restartBuiltinFilesystem(ProcessDescriptor *processDescriptor);
int restartOverlayFilesystem(ProcessDescriptor *processDescriptor);
int restartContiguousFilesystem(ProcessDescriptor *processDescriptor);
int halCommonInit(
  FilesystemDriverInit builtinFilesystemInitDriver,
  const FilesystemCommandHandler *builtinFilesystemCommandHandlers);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_COMMON_H

