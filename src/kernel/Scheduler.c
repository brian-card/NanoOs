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

/// @file Scheduler.c
///
/// @brief Implementation of scheduler logic.

// Unix includes
#include "sys/types.h"

// Kernel space includes
#include "Commands.h"
#include "Console.h"
#include "Hal.h"
#include "Logger.h"
#include "NanoOs.h"
#include "OverlayFunctions.h"
#include "Processes.h"
#include "Scheduler.h"
#include "SdCard.h"

// User space includes
#include "../user/NanoOsLibC.h"
#include "../user/NanoOsUnistd.h"

// Must come last
#include "../user/NanoOsStdio.h"

// Support prototypes.
void runScheduler(void);
int schedulerLoadOverlay(ProcessDescriptor *processDescriptor, char **envp);
int schedulerDumpMemoryAllocations();
int schedulerDumpOpenFiles();
void removeProcess(
  ProcessDescriptor *processDescriptor, const char *errorMessage);
void forceYield(void);

/// @def NUM_STANDARD_FILE_DESCRIPTORS
///
/// @brief The number of file descriptors a process usually starts out with.
#define NUM_STANDARD_FILE_DESCRIPTORS 3

/// @def STDIN_FILE_DESCRIPTOR_INDEX
///
/// @brief Index into a ProcessDescriptor's fileDescriptors array that holds the
/// FileDescriptor object that maps to the process's stdin FILE stream.
#define STDIN_FILE_DESCRIPTOR_INDEX 0

/// @def STDOUT_FILE_DESCRIPTOR_INDEX
///
/// @brief Index into a ProcessDescriptor's fileDescriptors array that holds the
/// FileDescriptor object that maps to the process's stdout FILE stream.
#define STDOUT_FILE_DESCRIPTOR_INDEX 1

/// @def STDERR_FILE_DESCRIPTOR_INDEX
///
/// @brief Index into a ProcessDescriptor's fileDescriptors array that holds the
/// FileDescriptor object that maps to the process's stderr FILE stream.
#define STDERR_FILE_DESCRIPTOR_INDEX 2

/// @var _functionInProgress
///
/// @brief Function that's already in progress that keeps another function from
/// running.
const char *_functionInProgress = NULL;

/// @var schedulerThread
///
/// @brief Pointer to the main process handle that's allocated before the
/// scheduler is started.
Thread *schedulerThread = NULL;

/// @var allProcesses
///
/// @brief Pointer to the allProcesses array that is part of the
/// SchedulerState object maintained by the scheduler process.  This is needed
/// in order to do lookups from process IDs to process object pointers.
static ProcessDescriptor *allProcesses = NULL;

/// @var SCHEDULER_STATE
///
/// @brief Global pointer to the SchedulerState managed by the scheduler process.
SchedulerState *SCHEDULER_STATE = NULL;

/// @var standardKernelFileDescriptors
///
/// @brief The array of file descriptors that all kernel processes use.
static FileDescriptor standardKernelFileDescriptors[
  NUM_STANDARD_FILE_DESCRIPTORS
] = {
  {
    // stdin
    // Kernel processes do not read from stdin, so clear out both pipes.
    .lastOwner = 0,
    .inputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
  {
    // stdout
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .lastOwner = 0,
    .inputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
  {
    // stderr
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .lastOwner = 0,
    .inputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
};

/// @var standardKernelFileDescriptorsPointers
///
/// @brief The array of file descriptor pointers that all kernel processes use.
static FileDescriptor *standardKernelFileDescriptorsPointers[
  NUM_STANDARD_FILE_DESCRIPTORS
] = {
  &standardKernelFileDescriptors[0],
  &standardKernelFileDescriptors[1],
  &standardKernelFileDescriptors[2],
};

/// @var standardUserFileDescriptors
///
/// @brief Pointer to the array of FileDescriptor objects (declared in the
/// startScheduler function on the scheduler's stack) that all processes start
/// out with.
static FileDescriptor standardUserFileDescriptors[
  NUM_STANDARD_FILE_DESCRIPTORS
] = {
  {
    // stdin
    // Uni-directional FileDescriptor, so clear the output pipe and direct the
    // input pipe to the console.
    .lastOwner = 0,
    .inputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
  {
    // stdout
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .lastOwner = 0,
    .inputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
  {
    // stderr
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .lastOwner = 0,
    .inputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = PROCESS_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
};

/// @var baseExecutiveHalCapabilities
///
/// @brief Array of HalCapability items that describe what a process can do
/// with the HAL.
HalCapability baseExecutiveHalCapabilities[] = {
  {
    .subsystemFunction = (((uint16_t) HAL_UART) << 8) | HAL_UART_WRITE,
    .deviceIds =         0x01, // Bitmask for device ID 0
  },
  {
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8)
      | HAL_CLOCK_GET_ELAPSED_MILLISECONDS,
    .deviceIds =         0x00, // No device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8)
      | HAL_CLOCK_GET_ELAPSED_MICROSECONDS,
    .deviceIds =         0x00, // No device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8)
      | HAL_CLOCK_GET_ELAPSED_NANOSECONDS,
    .deviceIds =         0x00, // No device for this function
  },
};

/// @var baseUserHalCapabilities
///
/// @brief Array of HalCapability items that describe what a process can do
/// with the HAL.
HalCapability baseUserHalCapabilities[] = {
#ifdef NANO_OS_DEBUG
  {
    .subsystemFunction = (((uint16_t) HAL_UART) << 8) | HAL_UART_WRITE,
    .deviceIds =         0x01, // Bitmask for device ID 0
  },
#endif // NANO_OS_DEBUG
  {
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8)
      | HAL_CLOCK_GET_ELAPSED_MILLISECONDS,
    .deviceIds =         0x00, // No device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8)
      | HAL_CLOCK_GET_ELAPSED_MICROSECONDS,
    .deviceIds =         0x00, // No device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_CLOCK) << 8)
      | HAL_CLOCK_GET_ELAPSED_NANOSECONDS,
    .deviceIds =         0x00, // No device for this function
  },
  {
    .subsystemFunction = (((uint16_t) HAL_TIMER) << 8) | HAL_TIMER_CANCEL,
    .deviceIds =         0, // To be filled in by scheduler
  },
};

/// @var baseSchedulerIpcCapabilities
///
/// @brief Array of IpcCapability items that describe what messages the
/// scheduler process can send to other processes.
///
/// @note These capabilites only matter when the scheduler is sending a message
/// to a process via a path that ultimately funnels through
/// sendProcessMessageToProcess.  Usually, the scheduler uses its own message
/// infrastructure that bypasses capability checks.
IpcCapability baseSchedulerIpcCapabilities[] = {
  {
    .destinationPid = 0, // SD card PID to be set by scheduler
    .signature      = SD_CARD_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << SD_CARD_READ_BLOCKS)
      | (((uint16_t) 1) << SD_CARD_WRITE_BLOCKS)
  },
  {
    .destinationPid = 0, // Logger PID to be set by scheduler
    .signature      = LOGGER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << LOGGER_LOG_MESSAGE)
  },
};

/// @var baseConsoleIpcCapabilities
///
/// @brief Array of IpcCapability items that describe what messages the console
/// process can send to other processes.
IpcCapability baseConsoleIpcCapabilities[] = {
  {
    .destinationPid = 0, // Scheduler PID to be set by scheduler
    .signature      = SCHEDULER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << SCHEDULER_SEND_SIGNAL)
  },
  {
    .destinationPid = 0, // Memory manager PID to be set by scheduler
    .signature      = MEMORY_MANAGER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << MEMORY_MANAGER_REALLOC)
      | (((uint16_t) 1) << MEMORY_MANAGER_FREE)
      | (((uint16_t) 1) << MEMORY_MANAGER_GET_FREE_MEMORY)
      | (((uint16_t) 1) << MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS)
  },
};

/// @var baseMemoryManagerIpcCapabilities
///
/// @brief Array of IpcCapability items that describe what messages the memory
/// manager process can send to other processes.
IpcCapability baseMemoryManagerIpcCapabilities[] = {
  {
    .destinationPid = 0, // Console PID to be set by scheduler
    .signature      = CONSOLE_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << CONSOLE_WRITE_VALUE)
      | (((uint16_t) 1) << CONSOLE_RELEASE_PORT)
  },
};

/// @var baseFilesystemIpcCapabilities
///
/// @brief Array of IpcCapability items that describe what messages the
/// filesystem process can send to other processes.
IpcCapability baseFilesystemIpcCapabilities[] = {
  {
    .destinationPid = 0, // Scheduler PID to be set by scheduler
    .signature      = SCHEDULER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << SCHEDULER_REPLACE_OVERLAY)
  },
  {
    .destinationPid = 0, // Console PID to be set by scheduler
    .signature      = CONSOLE_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << CONSOLE_GET_BUFFER)
      | (((uint16_t) 1) << CONSOLE_WRITE_BUFFER)
      | (((uint16_t) 1) << CONSOLE_RELEASE_BUFFER)
  },
  {
    .destinationPid = 0, // Memory manager PID to be set by scheduler
    .signature      = MEMORY_MANAGER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << MEMORY_MANAGER_REALLOC)
      | (((uint16_t) 1) << MEMORY_MANAGER_FREE)
  },
  {
    .destinationPid = 0, // SD card PID to be set by scheduler
    .signature      = SD_CARD_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << SD_CARD_READ_BLOCKS)
      | (((uint16_t) 1) << SD_CARD_WRITE_BLOCKS)
  },
};

/// @var baseLoggerIpcCapabilities
///
/// @brief Array of IpcCapability items that describe what messages the logger
/// process can send to other processes.
IpcCapability baseLoggerIpcCapabilities[] = {
  {
    .destinationPid = 0, // Scheduler PID to be set by scheduler
    .signature      = SCHEDULER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << SCHEDULER_GET_HOSTNAME)
      | (((uint16_t) 1) << SCHEDULER_REPLACE_OVERLAY)
  },
  {
    .destinationPid = 0, // Memory manager PID to be set by scheduler
    .signature      = MEMORY_MANAGER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << MEMORY_MANAGER_REALLOC)
      | (((uint16_t) 1) << MEMORY_MANAGER_FREE)
  },
  {
    .destinationPid = 0, // Filesystem PID to be set by scheduler
    .signature      = FILESYSTEM_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << FILESYSTEM_OPEN_FILE)
      | (((uint16_t) 1) << FILESYSTEM_CLOSE_FILE)
      | (((uint16_t) 1) << FILESYSTEM_READ_FILE)
      | (((uint16_t) 1) << FILESYSTEM_WRITE_FILE)
      | (((uint16_t) 1) << FILESYSTEM_REMOVE_FILE)
      | (((uint16_t) 1) << FILESYSTEM_SEEK_FILE)
      | (((uint16_t) 1) << FILESYSTEM_GET_FILE_BLOCK_METADATA)
      | (((uint16_t) 1) << FILESYSTEM_END_OF_FILE)
  },
};

/// @var baseSupervisorIpcCapabilities
///
/// @brief Array of IpcCapability items that describe what messages a process
/// with supervisor privilege can send to other processes.
IpcCapability baseSupervisorIpcCapabilities[] = {
  {
    .destinationPid = 0, // Scheduler PID to be set by scheduler
    .signature      = SCHEDULER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << SCHEDULER_KILL_PROCESS)
      | (((uint16_t) 1) << SCHEDULER_GET_NUM_RUNNING_PROCESSES)
      | (((uint16_t) 1) << SCHEDULER_GET_PROCESS_INFO)
      | (((uint16_t) 1) << SCHEDULER_SET_PROCESS_USER)
      | (((uint16_t) 1) << SCHEDULER_GET_HOSTNAME)
      | (((uint16_t) 1) << SCHEDULER_EXECVE)
      | (((uint16_t) 1) << SCHEDULER_SPAWN)
      | (((uint16_t) 1) << SCHEDULER_SEND_SIGNAL)
      | (((uint16_t) 1) << SCHEDULER_REPLACE_OVERLAY)
      | (((uint16_t) 1) << SCHEDULER_SHUTDOWN)
  },
  {
    .destinationPid = 0, // Console PID to be set by scheduler
    .signature      = CONSOLE_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << CONSOLE_WRITE_VALUE)
      | (((uint16_t) 1) << CONSOLE_GET_BUFFER)
      | (((uint16_t) 1) << CONSOLE_WRITE_BUFFER)
      | (((uint16_t) 1) << CONSOLE_RELEASE_PORT)
      | (((uint16_t) 1) << CONSOLE_GET_OWNED_PORT)
      | (((uint16_t) 1) << CONSOLE_GET_ECHO)
      | (((uint16_t) 1) << CONSOLE_SET_ECHO)
      | (((uint16_t) 1) << CONSOLE_WAIT_FOR_INPUT)
      | (((uint16_t) 1) << CONSOLE_RELEASE_BUFFER)
  },
  {
    .destinationPid = 0, // Memory manager PID to be set by scheduler
    .signature      = MEMORY_MANAGER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << MEMORY_MANAGER_REALLOC)
      | (((uint16_t) 1) << MEMORY_MANAGER_FREE)
      | (((uint16_t) 1) << MEMORY_MANAGER_GET_FREE_MEMORY)
      | (((uint16_t) 1) << MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS)
  },
  {
    .destinationPid = 0, // Filesystem PID to be set by scheduler
    .signature      = FILESYSTEM_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << FILESYSTEM_OPEN_FILE)
      | (((uint16_t) 1) << FILESYSTEM_CLOSE_FILE)
      | (((uint16_t) 1) << FILESYSTEM_READ_FILE)
      | (((uint16_t) 1) << FILESYSTEM_WRITE_FILE)
      | (((uint16_t) 1) << FILESYSTEM_REMOVE_FILE)
      | (((uint16_t) 1) << FILESYSTEM_SEEK_FILE)
      | (((uint16_t) 1) << FILESYSTEM_DUMP_OPEN_FILES)
      | (((uint16_t) 1) << FILESYSTEM_GET_FILE_BLOCK_METADATA)
  },
};

/// @var baseUserIpcCapabilities
///
/// @brief Array of IpcCapability items that describe what messages a process
/// with user privilege can send to other processes.
IpcCapability baseUserIpcCapabilities[] = {
  {
    .destinationPid = 0, // Scheduler PID to be set by scheduler
    .signature      = SCHEDULER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << SCHEDULER_GET_NUM_RUNNING_PROCESSES)
      | (((uint16_t) 1) << SCHEDULER_GET_PROCESS_INFO)
      | (((uint16_t) 1) << SCHEDULER_GET_HOSTNAME)
      | (((uint16_t) 1) << SCHEDULER_REPLACE_OVERLAY)
  },
  {
    .destinationPid = 0, // Console PID to be set by scheduler
    .signature      = CONSOLE_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << CONSOLE_GET_BUFFER)
      | (((uint16_t) 1) << CONSOLE_WRITE_BUFFER)
      | (((uint16_t) 1) << CONSOLE_RELEASE_PORT)
      | (((uint16_t) 1) << CONSOLE_RELEASE_BUFFER)
  },
  {
    .destinationPid = 0, // Memory manager PID to be set by scheduler
    .signature      = MEMORY_MANAGER_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << MEMORY_MANAGER_REALLOC)
      | (((uint16_t) 1) << MEMORY_MANAGER_FREE)
      | (((uint16_t) 1) << MEMORY_MANAGER_GET_FREE_MEMORY)
      | (((uint16_t) 1) << MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS)
  },
  {
    .destinationPid = 0, // Filesystem PID to be set by scheduler
    .signature      = FILESYSTEM_COMMAND_SIGNATURE,
    .messageTypes
      = (((uint16_t) 1) << FILESYSTEM_OPEN_FILE)
      | (((uint16_t) 1) << FILESYSTEM_CLOSE_FILE)
      | (((uint16_t) 1) << FILESYSTEM_READ_FILE)
      | (((uint16_t) 1) << FILESYSTEM_WRITE_FILE)
      | (((uint16_t) 1) << FILESYSTEM_REMOVE_FILE)
      | (((uint16_t) 1) << FILESYSTEM_SEEK_FILE)
      | (((uint16_t) 1) << FILESYSTEM_DUMP_OPEN_FILES)
      | (((uint16_t) 1) << FILESYSTEM_GET_FILE_BLOCK_METADATA)
  },
};

void* schedRealloc(void *ptr, size_t size);
void* schedMalloc(size_t size);

/// @fn int addProcessIpcCapability(ProcessDescriptor *processDescriptor,
///   ProcessId destinationPid, int64_t signature, uint32_t messageType)
///
/// @brief Do an in-order array insertion into a process's ipcCapabilities
/// array.
///
/// @param processDescriptor A pointer to the ProcessDescriptor with the
///   ipcCapabilities array to update.
/// @param destinationPid The ProcessId that the IPC message is destined for.
/// @param messageType The numeric message that is to be sent to the
///   destination.
///
/// @return Returns 0 on success, -errno on failure.
int addProcessIpcCapability(ProcessDescriptor *processDescriptor,
  ProcessId destinationPid, int64_t signature, uint32_t messageType
) {
  IpcCapability *capability = NULL;
  size_t ii = 0;
  if (processDescriptor->numIpcCapabilities > 0) {
    for (ii = 0; ii < processDescriptor->numIpcCapabilities; ii++) {
      capability = &processDescriptor->ipcCapabilities[ii];
      if ((capability->destinationPid == destinationPid)
        && (capability->signature == signature)
      ) {
        break;
      }
    }
  }

  if (ii < processDescriptor->numIpcCapabilities) {
    // Add the message type to the existing capability's messageTypes.
    capability->messageTypes |= ((uint16_t) 1) << messageType;
    logDebug("Added capability to send message type %ld from process %ld "
      "to process %ld\n",
      (long int) messageType,
      (long int) processDescriptor->processId,
      (long int) destinationPid);
    // We're done.
    return 0;
  }

  logDebug("Extinding ipcCapabilities array\n");

  // If we made it this far then the destination PID doesn't exist in the
  // process's capabilities yet, so we'll have to extend the array.
  if (processDescriptor->ipcCapabilitiesDynamic == true) {
    // Resize the existing array.  This is the expected case.
    logDebug("Extinding exiting array\n");
    void *check = schedRealloc(processDescriptor->ipcCapabilities,
      sizeof(IpcCapability) * (processDescriptor->numIpcCapabilities + 1));
    if (check == NULL) {
      logError("realloc returned NULL when trying to allocate "
        "%d IpcCapability objects\n",
        processDescriptor->numIpcCapabilities + 1);
      return -ENOMEM;
    }
    processDescriptor->ipcCapabilities = (IpcCapability*) check;
  } else {
    // Array is one of the statically-allocated ones.  We'll have to make a
    // copy first.
    logDebug("Copying to new array\n");
    void *copy = schedMalloc(
      sizeof(IpcCapability) * (processDescriptor->numIpcCapabilities + 1));
    if (copy == NULL) {
      logError("malloc returned NULL when trying to allocate "
        "%d IpcCapability objects\n",
        processDescriptor->numIpcCapabilities + 1);
      return -ENOMEM;
    }
    memcpy(copy, processDescriptor->ipcCapabilities,
      sizeof(IpcCapability) * processDescriptor->numIpcCapabilities);
    processDescriptor->ipcCapabilities = (IpcCapability*) copy;
    processDescriptor->ipcCapabilitiesDynamic = true;
  }

  // Find the place in the array that we need to insert the capability.
  for (ii = 0; ii < processDescriptor->numIpcCapabilities; ii++) {
    capability = &processDescriptor->ipcCapabilities[ii];
    if (capability->destinationPid > destinationPid) {
      // This is the expected stop case.
      break;
    } else if ((capability->destinationPid == destinationPid)
      && (capability->signature > signature)
    ) {
      break;
    }
  }

  logDebug("Adding new capability to send message type %ld from process "
    "%ld to process %ld\n",
    (long int) messageType,
    (long int) processDescriptor->processId,
    (long int) destinationPid);

  // Move all the capabilities from this point on down by one.
  for (; ii < processDescriptor->numIpcCapabilities; ii++) {
    processDescriptor->ipcCapabilities[ii + 1]
      = processDescriptor->ipcCapabilities[ii];
  }

  // capability still points to the spot we need to update, so set the members
  // of that poniter.
  capability->destinationPid = destinationPid;
  capability->signature = signature;
  capability->messageTypes = ((uint16_t) 1) << messageType;
  logDebug("capability->destinationPid = %ld, capability->messageTypes = "
    "0x%lx\n",
    (long int) capability->destinationPid,
    (unsigned long int) capability->messageTypes);
  processDescriptor->numIpcCapabilities++;

  return 0;
}

/// @fn int removeProcessIpcCapability(ProcessDescriptor *processDescriptor,
///   ProcessId destinationPid, int64_t signature, uint32_t messageType)
///
/// @brief Remove a capability from a process's ipcCapabilities array.
///
/// @param processDescriptor A pointer to the ProcessDescriptor with the
///   ipcCapabilities array to update.
/// @param destinationPid The ProcessId that the IPC message is destined for.
/// @param messageType The numeric message that is to be sent to the
///   destination.
///
/// @return Returns 0 on success, -errno on failure.
int removeProcessIpcCapability(ProcessDescriptor *processDescriptor,
  ProcessId destinationPid, int64_t signature, uint32_t messageType
) {
  IpcCapability *capability = NULL;
  if (processDescriptor->numIpcCapabilities > 0) {
    for (size_t ii = 0; ii < processDescriptor->numIpcCapabilities; ii++) {
      capability = &processDescriptor->ipcCapabilities[ii];
      if ((capability->destinationPid == destinationPid)
        && (capability->signature == signature)
      ) {
        break;
      }
    }
  }

  if (capability != NULL) {
    // Exclude the message type from the existing capability's messageTypes.
    capability->messageTypes &= ~(((uint16_t) 1) << messageType);
  }
  // else this destinationPid isn't even in the process's ipcCapabilities array,
  // so there's nothing to update.

  return 0;
}

/// @fn void runSchedulerQueues(PrivilegeLevel privilegeLevelBound)
///
/// @brief Make one pass through all the process queues less than the provided
/// bound.
///
/// @param privilegeLevelBound The upper bound for the privilege level queues.
///   The queues less than this value will be run.
///
/// @return This function returns no value.
void runSchedulerQueues(PrivilegeLevel privilegeLevelBound) {
  ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;

  for (PrivilegeLevel ii = PRIVILEGE_LEVEL_KERNEL;
    ii < privilegeLevelBound;
    ii++
  ) {
    SCHEDULER_STATE->currentReady = &SCHEDULER_STATE->ready[ii];
    uint8_t queueSize = SCHEDULER_STATE->currentReady->numElements;
    for (uint8_t jj = 0; jj < queueSize; jj++) {
      runScheduler();
    }
  }

  SCHEDULER_STATE->currentReady = currentReady;
}

/// @fn int processQueuePush(
///   ProcessQueue *processQueue, ProcessDescriptor *processDescriptor)
///
/// @brief Push a pointer to a ProcessDescriptor onto a ProcessQueue.
///
/// @param processQueue A pointer to a ProcessQueue to push the pointer to.
/// @param processDescriptor A pointer to a ProcessDescriptor to push onto the
///   queue.
///
/// @return Returns 0 on success, ENOMEM on failure.
int processQueuePush(
  ProcessQueue *processQueue, ProcessDescriptor *processDescriptor
) {
  if ((processQueue == NULL)
    || (processQueue->numElements >= SCHEDULER_NUM_PROCESSES)
  ) {
    logError("Could not push process %d onto %s queue:\n",
      processDescriptor->processId, processQueue->name);
    return ENOMEM;
  }

  processQueue->processes[processQueue->tail] = processDescriptor;
  processQueue->tail++;
  processQueue->tail %= SCHEDULER_NUM_PROCESSES;
  processQueue->numElements++;
  processDescriptor->processQueue = processQueue;

  return 0;
}

/// @fn ProcessDescriptor* processQueuePop(ProcessQueue *processQueue)
///
/// @brief Pop a pointer to a ProcessDescriptor from a ProcessQueue.
///
/// @param processQueue A pointer to a ProcessQueue to pop the pointer from.
///
/// @return Returns a pointer to a ProcessDescriptor on success, NULL on
/// failure.
ProcessDescriptor* processQueuePop(ProcessQueue *processQueue) {
  ProcessDescriptor *processDescriptor = NULL;
  if ((processQueue == NULL) || (processQueue->numElements == 0)) {
    return processDescriptor; // NULL
  }

  processDescriptor = processQueue->processes[processQueue->head];
  processQueue->head++;
  processQueue->head %= SCHEDULER_NUM_PROCESSES;
  processQueue->numElements--;
  processDescriptor->processQueue = NULL;

  return processDescriptor;
}

/// @fn int processQueueRemove(
///   ProcessQueue *processQueue, ProcessDescriptor *processDescriptor)
///
/// @brief Remove a pointer to a ProcessDescriptor from a ProcessQueue.
///
/// @param processQueue A pointer to a ProcessQueue to remove the pointer from.
/// @param processDescriptor A pointer to a ProcessDescriptor to remove from the
///   queue.
///
/// @return Returns 0 on success, ENOMEM on failure.
int processQueueRemove(
  ProcessQueue *processQueue, ProcessDescriptor *processDescriptor
) {
  int returnValue = EINVAL;
  if ((processQueue == NULL) || (processQueue->numElements == 0)) {
    // Nothing to do.
    return returnValue; // EINVAL
  }

  ProcessDescriptor *poppedDescriptor = NULL;
  for (uint8_t ii = 0; ii < processQueue->numElements; ii++) {
    poppedDescriptor = processQueuePop(processQueue);
    if (poppedDescriptor == processDescriptor) {
      returnValue = ENOERR;
      processDescriptor->processQueue = NULL;
      break;
    }
    // This is not what we're looking for.  Put it back.
    processQueuePush(processQueue, poppedDescriptor);
  }

  return returnValue;
}

/// @fn ProcessDescriptor* schedulerGetProcessById(unsigned int pid)
///
/// @brief Look up a process for a running command given its process ID.
///
/// @note This function is meant to be called from outside of the scheduler's
/// running state.  That's why there's no SchedulerState pointer in the
/// parameters.
///
/// @param pid The integer ID for the process.
///
/// @return Returns the found process descriptor on success, NULL on failure.
ProcessDescriptor* schedulerGetProcessById(unsigned int pid) {
  ProcessDescriptor *processDescriptor = NULL;
  if ((pid > 0) && (pid <= NANO_OS_NUM_PROCESSES)) {
    processDescriptor = &allProcesses[pid - 1];
  }

  return processDescriptor;
}

/// @fn void* dummyProcess(void *args)
///
/// @brief Dummy process that's loaded at startup to prepopulate the process
/// array with processes.
///
/// @param args Any arguments passed to this function.  Ignored.
///
/// @return This function always returns NULL.
void* dummyProcess(void *args) {
  (void) args;
  return NULL;
}

/// @fn int schedulerSendProcessMessageToProcess(
///   ProcessDescriptor *processDescriptor, ProcessMessage *processMessage)
///
/// @brief Get an available ProcessMessage, populate it with the specified data,
/// and push it onto a destination process's queue.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that manages
///   the process to send a message to.
/// @param processMessage A pointer to the message to send to the destination
///   process.
///
/// @return Returns processSuccess on success, processError on failure.
int schedulerSendProcessMessageToProcess(
  ProcessDescriptor *processDescriptor, ProcessMessage *processMessage
) {
  int returnValue = processSuccess;
  if ((processDescriptor == NULL)
    || (processDescriptor->mainThread == NULL)
  ) {
    logError("Attempt to send processMessage to NULL process.\n");
    returnValue = processError;
    goto exit;
  } else if (processMessage == NULL) {
    logError(
      "Attempt to send NULL processMessage to process %ld.\n",
      (long int) processDescriptor->processId);
    returnValue = processError;
    goto exit;
  }

  // Sanity checks
  if (processCorrupted(processDescriptor)) {
    logError("Process %ld is corrupted\n",
      (long int) processDescriptor->processId);
    returnValue = processError;
    goto exit;
  }
  if (processRunning(processDescriptor) == false) {
    logError("Process %ld is not running\n",
      (long int) processDescriptor->processId);
    returnValue = processError;
    goto exit;
  }

  returnValue = processMessageQueuePush(processDescriptor, processMessage);
  if (returnValue != processSuccess) {
    logError("Could not push message onto process %ld's message queue\n",
      (long int) processDescriptor->processId);
    // returnValue is already set.  Don't modify it.
    goto exit;
  }

  while (processMessageDone(processMessage) == false) {
    runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
  }

exit:
  return returnValue;
}

/// @fn int schedulerSendProcessMessageToPid(SchedulerState *schedulerState,
///   unsigned int pid, ProcessMessage *processMessage)
///
/// @brief Look up a process by its PID and send a message to it.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param pid The ID of the process to send the message to.
/// @param processMessage A pointer to the message to send to the destination
///   process.
///
/// @return Returns processSuccess on success, processError on failure.
int schedulerSendProcessMessageToPid(SchedulerState *schedulerState,
  unsigned int pid, ProcessMessage *processMessage
) {
  int returnValue = processError;
  if ((pid <= 0) || (pid > NANO_OS_NUM_PROCESSES)) {
    // Not a valid PID.  Fail.
    logError("%d is not a valid PID.\n", pid);
    return returnValue; // processError
  }

  ProcessDescriptor *processDescriptor = &schedulerState->allProcesses[pid - 1];
  // If processDescriptor is NULL, it will be detected as not running by
  // schedulerSendProcessMessageToProcess, so there's no real point in
  //  checking for NULL here.
  return schedulerSendProcessMessageToProcess(
    processDescriptor, processMessage);
}

/// @fn int schedulerInitSendMessageToProcess(
///   ProcessDescriptor *processDescriptor, int64_t type,
///   void *data, size_t size)
///
/// @brief Send a ProcessMessage to another process identified by its ProcessDescriptor.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that holds the
///   metadata for the process.
/// @param type The type of the message to send to the destination process.
/// @param data A pointer to the data to send, cast to a void*.
/// @param size The number of bytes the data at the data pointer consumes.
///
/// @return Returns processSuccess on success, a different process status
/// on failure.
int schedulerInitSendMessageToProcess(ProcessDescriptor *processDescriptor,
  int64_t type, void *data, size_t size
) {
  ProcessMessage processMessage;
  memset(&processMessage, 0, sizeof(processMessage));

  // These messages are always waiting for done from the caller, so hardcode
  // the waiting parameter to true here.
  processMessageInit(&processMessage, type, data, size, true);

  int returnValue = schedulerSendProcessMessageToProcess(
    processDescriptor, &processMessage);

  return returnValue;
}

/// @fn int schedulerInitSendMessageToPid(
///   int pid, int64_t type, void *data, size_t size)
///
/// @brief Send a ProcessMessage to another process identified by its PID. Looks
/// up the process's ProcessDescriptor by its PID and then calls
/// schedulerInitSendMessageToProcess.
///
/// @param pid The process ID of the destination process.
/// @param type The type of the message to send to the destination process.
/// @param data A pointer to the data to send, cast to a void*.
/// @param size The number of bytes the data at the data pointer consumes.
///
/// @return Returns processSuccess on success, a different process status
/// on failure.
int schedulerInitSendMessageToPid(
  int pid, int64_t type, void *data, size_t size
) {
  int returnValue = processError;
  if ((pid <= 0) || (pid > NANO_OS_NUM_PROCESSES)) {
    // Not a valid PID.  Fail.
    logError("%d is not a valid PID.\n", pid);
    return returnValue; // processError
  }

  ProcessDescriptor *processDescriptor
    = &SCHEDULER_STATE->allProcesses[pid - 1];
  returnValue = schedulerInitSendMessageToProcess(
    processDescriptor, type, data, size);
  return returnValue;
}

/// @fn void* schedulerResumeReallocMessage(void *ptr, size_t size)
///
/// @brief Send a MEMORY_MANAGER_REALLOC command to the memory manager process
/// by resuming it with the message and get a reply.
///
/// @param ptr The pointer to send to the process.
/// @param size The size to send to the process.
///
/// @return Returns the data pointer returned in the reply.
void* schedulerResumeReallocMessage(void *ptr, size_t size) {
  void *returnValue = NULL;
  
  ReallocMessage reallocMessage;
  reallocMessage.ptr = ptr;
  reallocMessage.size = size;
  
  if (schedulerInitSendMessageToPid(SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_REALLOC,
    &reallocMessage, sizeof(reallocMessage)
    ) != processSuccess
  ) {
    // Nothing we can do.
    return returnValue; // NULL
  }
  // The handler set the pointer back in the structure we sent it, so grab it
  // out of the structure we already have.
  returnValue = reallocMessage.ptr;

  // The message that was sent to us is the one that we allocated on the stack,
  // so, there's no reason to call processMessageRelease here.
  
  return returnValue;
}

/// @fn void* schedRealloc(void *ptr, size_t size)
///
/// @brief Reallocate a provided pointer to a new size.
///
/// @param ptr A pointer to the original block of dynamic memory.  If this value
///   is NULL, new memory will be allocated.
/// @param size The new size desired for the memory block at ptr.  If this value
///   is 0, the provided pointer will be freed.
///
/// @return Returns a pointer to size-adjusted memory on success, NULL on
/// failure or free.
void* schedRealloc(void *ptr, size_t size) {
  return schedulerResumeReallocMessage(ptr, size);
}

/// @fn void* schedMalloc(size_t size)
///
/// @brief Allocate but do not clear memory.
///
/// @param size The size of the block of memory to allocate in bytes.
///
/// @return Returns a pointer to newly-allocated memory of the specified size
/// on success, NULL on failure.
void* schedMalloc(size_t size) {
  return schedulerResumeReallocMessage(NULL, size);
}

/// @fn void* schedCalloc(size_t nmemb, size_t size)
///
/// @brief Allocate memory and clear all the bytes to 0.
///
/// @param nmemb The number of elements to allocate in the memory block.
/// @param size The size of each element to allocate in the memory block.
///
/// @return Returns a pointer to zeroed newly-allocated memory of the specified
/// size on success, NULL on failure.
void* schedCalloc(size_t nmemb, size_t size) {
  size_t totalSize = nmemb * size;
  logDebug("Calling schedulerResumeReallocMessage\n");
  void *returnValue = schedulerResumeReallocMessage(NULL, totalSize);
  logDebug("Returned from schedulerResumeReallocMessage\n");
  
  if (returnValue != NULL) {
    memset(returnValue, 0, totalSize);
  }
  return returnValue;
}

/// @fn void schedFree(void *ptr)
///
/// @brief Free a piece of memory using mechanisms available to the scheduler.
///
/// @param ptr The pointer to the memory to free.
///
/// @return This function returns no value.
void schedFree(void *ptr) {
  // No need to check the return value here.  There's nothing we can do if we
  // fail to send the message for some reason.
  MemoryManagerFreeArgs memoryManagerFreeArgs = {
    .ptr = ptr,
  };
  schedulerInitSendMessageToPid(SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_FREE,
    &memoryManagerFreeArgs, sizeof(memoryManagerFreeArgs));
  return;
}

/// @fn int assignMemory(void *ptr, ProcessId pid) {
///
/// @brief Assign a piece of memory to a specific process.
///
/// @param ptr The pointer to the memory to assign.
/// @param pid The ID of the process to assign the memory to.
///
/// @return Returns 0 on success, -errno on failure.
int assignMemory(void *ptr, ProcessId pid) {
  AssignMemoryArgs assignMemoryArgs = {
    .ptr = ptr,
    .pid = pid,
  };

  int returnValue = 0;
  if (schedulerInitSendMessageToPid(SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_ASSIGN_MEMORY,
    &assignMemoryArgs, sizeof(assignMemoryArgs)) != processSuccess
  ) {
    // Nothing we can do.
    returnValue = -ENOMEM;
  }

  return returnValue;
}

/// @fn int schedulerAssignPortToPid(uint8_t consolePort, ProcessId owner)
///
/// @brief Assign a console port to a process ID.
///
/// @param consolePort The ID of the consolePort to assign.
/// @param owner The ID of the process to assign the port to.
///
/// @return Returns processSuccess on success, processError on failure.
int schedulerAssignPortToPid(uint8_t consolePort, ProcessId owner) {
  ConsolePortPidUnion consolePortPidUnion;
  consolePortPidUnion.consolePortPidAssociation.consolePort
    = consolePort;
  consolePortPidUnion.consolePortPidAssociation.pid = owner;

  int returnValue = schedulerInitSendMessageToPid(
    SCHEDULER_STATE->consolePid,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_ASSIGN_PORT,
    (void*) ((uintptr_t) consolePortPidUnion.nanoOsMessageData), /* size= */ 0);

  return returnValue;
}

/// @fn int schedulerSetPortShell(uint8_t consolePort, ProcessId shell)
///
/// @brief Assign a console port to a process ID.
///
/// @param consolePort The ID of the consolePort to set the shell for.
/// @param shell The ID of the shell process for the port.
///
/// @return Returns processSuccess on success, processError on failure.
int schedulerSetPortShell(uint8_t consolePort, ProcessId shell) {
  int returnValue = processError;

  if (shell >= NANO_OS_NUM_PROCESSES) {
    logError(
      "schedulerSetPortShell called with invalid shell PID %ld\n",
      (long int) shell);
    return returnValue; // processError
  }

  ConsolePortPidUnion consolePortPidUnion;
  consolePortPidUnion.consolePortPidAssociation.consolePort
    = consolePort;
  consolePortPidUnion.consolePortPidAssociation.pid = shell;

  returnValue = schedulerInitSendMessageToPid(
    SCHEDULER_STATE->consolePid,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_SET_PORT_SHELL,
    (void*) ((uintptr_t) consolePortPidUnion.nanoOsMessageData), /* size= */ 0);

  return returnValue;
}

/// @fn int schedulerGetNumConsolePorts(void)
///
/// @brief Get the number of ports the console is running.
///
/// @return Returns the number of ports the console is running on success, -1
/// on failure.
int schedulerGetNumConsolePorts(void) {
  int returnValue = -1;

  ConsoleGetNumPortsArgs consoleGetNumPortsArgs = {
    .numPorts = 0,
  };
  if (schedulerInitSendMessageToPid(
    SCHEDULER_STATE->consolePid,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_GET_NUM_PORTS,
    /* data= */ &consoleGetNumPortsArgs,
    /* size= */ sizeof(consoleGetNumPortsArgs)) != processSuccess
  ) {
    logError("Could not send CONSOLE_GET_NUM_PORTS to console\n");
    return returnValue; // -1
  }

  returnValue = consoleGetNumPortsArgs.numPorts;

  return returnValue;
}

/// @fn ProcessId schedulerGetNumRunningProcesses(struct timespec *timeout)
///
/// @brief Get the number of running processes from the scheduler.
///
/// @param timeout A pointer to a struct timespec with the end time for the
///   timeout.
///
/// @return Returns the number of running processes on success, 0 on failure.
/// There is no way for the number of running processes to exceed the maximum
/// value of a ProcessId type, so it's used here as the return type.
ProcessId schedulerGetNumRunningProcesses(struct timespec *timeout) {
  ProcessMessage *processMessage = NULL;
  int waitStatus = processSuccess;
  ProcessId numProcessDescriptors = 0;

  SchedulerGetNumRunningProcessesArgs schedulerGetNumRunningProcessesArgs = {
    .returnValue = 0,
    .errorNumber = 0,
  };
  processMessage = initSendProcessMessageToPid(
    SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_GET_NUM_RUNNING_PROCESSES,
    &schedulerGetNumRunningProcessesArgs,
    sizeof(schedulerGetNumRunningProcessesArgs), true);
  if (processMessage == NULL) {
    logError("Could not communicate with scheduler.\n");
    goto exit;
  }

  waitStatus = processMessageWaitForDone(processMessage, timeout);
  if (waitStatus != processSuccess) {
    if (waitStatus == processTimedout) {
      logError("Command to get the number of running processes timed out.\n");
    } else {
      logError("Command to get the number of running processes failed.\n");
    }

    // Without knowing how many processes there are, we can't continue.  Bail.
    goto releaseMessage;
  }

  numProcessDescriptors = schedulerGetNumRunningProcessesArgs.returnValue;
  if (numProcessDescriptors == 0) {
    logError("Number of running processes returned from the scheduler is 0.\n");
    errno = schedulerGetNumRunningProcessesArgs.errorNumber;
    goto releaseMessage;
  }

releaseMessage:
  if (processMessageRelease(processMessage) != processSuccess) {
    logError("Could not release message sent to scheduler for "
      "getting the number of running processes.\n");
  }

exit:
  return numProcessDescriptors;
}

/// @fn ProcessInfo* schedulerGetProcessInfo(void)
///
/// @brief Get information about all processes running in the system from the
/// scheduler.
///
/// @return Returns a populated, dynamically-allocated ProcessInfo object on
/// success, NULL on failure.
ProcessInfo* schedulerGetProcessInfo(void) {
  ProcessMessage *processMessage = NULL;
  int waitStatus = processSuccess;

  // We don't know where our messages to the scheduler will be in its queue, so
  // we can't assume they will be processed immediately, but we can't wait
  // forever either.  Set a 100 ms timeout.
  struct timespec timeout = {0};
  timespec_get(&timeout, TIME_UTC);
  timeout.tv_nsec += 100000000;

  // Because the scheduler runs on the main thread, it doesn't have the
  // ability to yield.  That means it can't do anything that requires a
  // synchronus message exchange, i.e. allocating memory.  So, we need to
  // allocate memory from the current process and then pass that back to the
  // scheduler to populate.  That means we first need to know how many processes
  // are running so that we know how much space to allocate.  So, get that
  // first.
  ProcessId numProcessDescriptors = schedulerGetNumRunningProcesses(&timeout);

  // We need numProcessDescriptors rows.
  ProcessInfo *processInfo = (ProcessInfo*) malloc(sizeof(ProcessInfo)
    + ((numProcessDescriptors - 1) * sizeof(ProcessInfoElement)));
  if (processInfo == NULL) {
    logError("Could not allocate memory for processInfo in getProcessInfo.\n");
    goto exit;
  }

  // It is possible, although unlikely, that an additional process is started
  // between the time we made the call above and the time that our message gets
  // handled below.  We allocated our return value based upon the size that was
  // returned above and, if we're not careful, it will be possible to overflow
  // the array.  Initialize processInfo->numProcesses so that
  // schedulerGetProcessInfoCommandHandler knows the maximum number of
  // ProcessInfoElements it can populated.
  processInfo->numProcesses = numProcessDescriptors;

  SchedulerGetProcessInfoArgs schedulerGetProcessInfoArgs = {
    .processInfo = processInfo,
    .returnValue = 0,
    .errorNumber = 0,
  };
  processMessage
    = initSendProcessMessageToPid(SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_GET_PROCESS_INFO,
    &schedulerGetProcessInfoArgs,
    sizeof(schedulerGetProcessInfoArgs), true);

  if (processMessage == NULL) {
    logError("Could not send scheduler message to get process info.\n");
    goto freeMemory;
  }

  waitStatus = processMessageWaitForDone(processMessage, &timeout);
  if (waitStatus != processSuccess) {
    if (waitStatus == processTimedout) {
      logError("Command to get process information timed out.\n");
    } else {
      logError("Command to get process information failed.\n");
    }

    // Without knowing the data for the processes, we can't display them.  Bail.
    goto releaseMessage;
  }

  if (schedulerGetProcessInfoArgs.returnValue != 0) {
    errno = schedulerGetProcessInfoArgs.errorNumber;
    logError("Scheduler returned status: %s\n", strerror(errno));
    goto releaseMessage;
  }

  if (processMessageRelease(processMessage) != processSuccess) {
    logError("Could not release message sent to scheduler for "
      "getting the number of running processes.\n");
  }

  return processInfo;

releaseMessage:
  if (processMessageRelease(processMessage) != processSuccess) {
    logError("Could not release message sent to scheduler for "
      "getting the number of running processes.\n");
  }

freeMemory:
  free(processInfo); processInfo = NULL;

exit:
  return processInfo;
}

/// @fn int schedulerKillProcess(ProcessId pid)
///
/// @brief Do all the inter-process communication with the scheduler required
/// to kill a running process.
///
/// @param pid The ID of the process to kill.
///
/// @return Returns 0 on success, 1 on failure.
int schedulerKillProcess(ProcessId pid) {
  SchedulerKillProcessArgs schedulerKillProcessArgs = {
    .pid = pid,
    .returnValue = 0,
    .errorNumber = 0,
  };
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_KILL_PROCESS,
    &schedulerKillProcessArgs, sizeof(schedulerKillProcessArgs), true);
  if (processMessage == NULL) {
    logError("Could not communicate with scheduler.\n");
    return 1;
  }

  // We don't know where our message to the scheduler will be in its queue, so
  // we can't assume it will be processed immediately, but we can't wait forever
  // either.  Set a 100 ms timeout.
  struct timespec ts = { 0, 0 };
  timespec_get(&ts, TIME_UTC);
  int64_t timeout = (((int64_t) ts.tv_sec) * ((int64_t) 1000000000))
    + ts.tv_nsec;
  timeout += 100000000;
  ts.tv_sec = timeout / ((int64_t) 1000000000);
  ts.tv_nsec = timeout % ((int64_t) 1000000000);

  int waitStatus = processMessageWaitForDone(processMessage, &ts);
  int returnValue = 0;
  if (waitStatus == processSuccess) {
    returnValue = schedulerKillProcessArgs.returnValue;
    if (returnValue == 0) {
      logInfo("Termination of process %d successful.\n", pid);
    } else {
      logError("Process termination returned status \"%s\".\n",
        strerror(schedulerKillProcessArgs.errorNumber));
      errno = schedulerKillProcessArgs.errorNumber;
    }
  } else {
    returnValue = 1;
    if (waitStatus == processTimedout) {
      logError("Command to kill PID %d timed out.\n", pid);
    } else {
      logError("Command to kill PID %d returned status %d.\n", pid, waitStatus);
    }
  }

  if (processMessageRelease(processMessage) != processSuccess) {
    returnValue = 1;
    logError("Could not release message sent to scheduler for kill command.\n");
  }

  return returnValue;
}

/// @fn int schedulerSendSignal(ProcessId pid, int signal)
///
/// @brief Send a signal to a process.
///
/// @param pid The process ID of the process to send the signal to.
/// @param signal The integer signal to send to the process.
///
/// @return On success, 0 is returned.  On failure, -1 is returned and errno is
/// set appropriately.
int schedulerSendSignal(ProcessId pid, int signal) {
  int returnValue = -1;

  SchedulerSendSignalArgs sendSignalArgs = {
    .pid = pid,
    .signal = signal,
    .returnValue = 0,
    .errorNumber = 0,
  };
  ProcessMessage *processMessage
    = initSendProcessMessageToPid(
    SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_SEND_SIGNAL,
    /* data= */ &sendSignalArgs, /* size= */ sizeof(sendSignalArgs), true);
  if (processMessage == NULL) {
    logError("Could not communicate with scheduler.\n");
    errno = EOTHER;
    return returnValue; // -1
  }

  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

  returnValue = sendSignalArgs.returnValue;
  errno = sendSignalArgs.errorNumber;

  return returnValue;
}

/// @fn int schedulerReplaceOverlay(const void *overlayNamespace,
///   FileBlockMetadata *overlay)
///
/// @brief Replace the overlay of the running process with a new one.
///
/// @param overlayNamespace The namespace (directory or block device ID) that
///   the overlay is in.
/// @param overlay A pointer to the FileBlockMetadata to use as the new overlay.
///
/// @return Returns 0 on success, -errno on failure.  On success, the new
/// overlay will be loaded when this function returns.
int schedulerReplaceOverlay(const void *overlayNamespace,
  FileBlockMetadata *overlay
) {
  int returnValue = -EOTHER;
  if (overlay == NULL) {
    returnValue = -EINVAL;
    return returnValue;
  }

  SchedulerReplaceOverlayArgs schedulerReplaceOverlayArgs = {
    .overlayNamespace = (void*) overlayNamespace,
    .overlay = overlay,
    .returnValue = 0,
  };
  ProcessMessage *processMessage
    = initSendProcessMessageToPid(
    SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_REPLACE_OVERLAY,
    /* data= */ &schedulerReplaceOverlayArgs,
    /* size= */ sizeof(schedulerReplaceOverlayArgs),
    true);
  if (processMessage == NULL) {
    logError("Could not communicate with scheduler.\n");
    return returnValue; // -EOTHER
  }

  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

  returnValue = schedulerReplaceOverlayArgs.returnValue;

  return returnValue;
}

/// @fn int schedulerSetProcessUser(UserId userId)
///
/// @brief Set the user ID of the current process to the specified user ID.
///
/// @return Returns 0 on success, -1 on failure.
int schedulerSetProcessUser(UserId userId) {
  int returnValue = -1;
  SchedulerSetProcessUserArgs schedulerSetProcessUserArgs = {
    .userId = userId,
    .returnValue = returnValue,
    .errorNumber = 0,
  };
  ProcessMessage *processMessage
    = initSendProcessMessageToPid(
    SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_SET_PROCESS_USER,
    &schedulerSetProcessUserArgs, sizeof(schedulerSetProcessUserArgs), true);
  if (processMessage == NULL) {
    logError("Could not communicate with scheduler.\n");
    return returnValue; // -1
  }

  processMessageWaitForDone(processMessage, NULL);
  returnValue = schedulerSetProcessUserArgs.returnValue;
  processMessageRelease(processMessage);

  if (returnValue != 0) {
    errno = schedulerSetProcessUserArgs.errorNumber;
    logError("Scheduler returned \"%s\" for setProcessUser.\n",
      strerror(errno));
  }

  return returnValue;
}

/// @fn FileDescriptor* schedulerGetFileDescriptor(FILE *stream)
///
/// @brief Get the IoPipe object for a process given a pointer to the FILE
///   stream to write to.
///
/// @param stream A pointer to the desired FILE output stream (stdout or
///   stderr).
///
/// @return Returns the appropriate FileDescriptor object for the current
/// process on success, NULL on failure.
FileDescriptor* schedulerGetFileDescriptor(FILE *stream) {
  FileDescriptor *returnValue = NULL;
  uintptr_t fdIndex = (uintptr_t) stream;
  ProcessId runningProcessIndex = getRunningPid() - 1;

  if (fdIndex <= allProcesses[runningProcessIndex].numFileDescriptors) {
    returnValue
      = allProcesses[runningProcessIndex].fileDescriptors[fdIndex - 1];
  } else {
    logError("Received request for unknown stream %d.\n",
      (int) (intptr_t) stream);
  }

  return returnValue;
}

/// @fn char* schedulerGetHostname(void)
///
/// @brief Get the hostname that's read during startup.
///
/// @return Returns the hostname that's read during startup on success, NULL on
/// failure.
const char* schedulerGetHostname(void) {
  SchedulerGetHostnameArgs schedulerGetHostnameArgs = {
    .hostname = NULL,
    .errorNumber = 0,
  };
  ProcessMessage *processMessage
    = initSendProcessMessageToPid(
    SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_GET_HOSTNAME,
    &schedulerGetHostnameArgs, sizeof(schedulerGetHostnameArgs), true);
  if (processMessage == NULL) {
    logError("Could not communicate with scheduler.\n");
    return schedulerGetHostnameArgs.hostname; // NULL
  }

  processMessageWaitForDone(processMessage, NULL);
  if (schedulerGetHostnameArgs.errorNumber != 0) {
    errno = schedulerGetHostnameArgs.errorNumber;
  }
  processMessageRelease(processMessage);

  return schedulerGetHostnameArgs.hostname;
}

/// @fn int schedulerExecve(const char *pathname,
///   char *const argv[], char *const envp[])
///
/// @brief NanoOs implementation of Unix execve function.
///
/// @param pathname The full, absolute path on disk to the program to run.
/// @param argv The NULL-terminated array of arguments for the command.  argv[0]
///   must be valid and should be the name of the program.
/// @param envp The NULL-terminated array of environment variables in
///   "name=value" format.  This array may be NULL.
///
/// @return This function will not return to the caller on success.  On failure,
/// -1 will be returned and the value of errno will be set to indicate the
/// reason for the failure.
int schedulerExecve(const char *pathname,
  char *const argv[], char *const envp[]
) {
  if ((pathname == NULL) || (argv == NULL) || (argv[0] == NULL)) {
    errno = EFAULT;
    return -1;
  }

  ExecArgs *execArgs = (ExecArgs*) calloc(1, sizeof(ExecArgs));
  if (execArgs == NULL) {
    logError("Allocating execArgs failed\n");
    errno = ENOMEM;
    return -1;
  }

  execArgs->pathname = (char*) malloc(strlen(pathname) + 1);
  if (execArgs->pathname == NULL) {
    logError("Allocating execArgs->pathname failed\n");
    errno = ENOMEM;
    goto freeExecArgs;
  }
  strcpy(execArgs->pathname, pathname);

  size_t argvLen = 0;
  for (; argv[argvLen] != NULL; argvLen++);
  argvLen++; // Account for the terminating NULL element
  execArgs->argv = (char**) calloc(1, argvLen * sizeof(char*));
  if (execArgs->argv == NULL) {
    logError("Allocating execArgs->argv failed\n");
    errno = ENOMEM;
    goto freeExecArgs;
  }

  // argvLen is guaranteed to always be at least 1, so it's safe to run to
  // (argvLen - 1) here.
  size_t ii = 0;
  for (; ii < (argvLen - 1); ii++) {
    // We know that argv[ii] isn't NULL because of the calculation for argvLen
    // above, so it's safe to use strlen.
    execArgs->argv[ii] = (char*) malloc(strlen(argv[ii]) + 1);
    if (execArgs->argv[ii] == NULL) {
      logError("Allocating execArgs->argv[%zu] failed\n", ii);
      errno = ENOMEM;
      goto freeExecArgs;
    }
    strcpy(execArgs->argv[ii], argv[ii]);
  }
  execArgs->argv[ii] = NULL; // NULL-terminate the array

  if (envp != NULL) {
    size_t envpLen = 0;
    for (; envp[envpLen] != NULL; envpLen++);
    envpLen++; // Account for the terminating NULL element
    execArgs->envp = (char**) calloc(1, envpLen * sizeof(char*));
    if (execArgs->envp == NULL) {
      logError("Allocating execArgs->envp failed\n");
      errno = ENOMEM;
      goto freeExecArgs;
    }

    // envpLen is guaranteed to always be at least 1, so it's safe to run to
    // (envpLen - 1) here.
    for (ii = 0; ii < (envpLen - 1); ii++) {
      // We know that envp[ii] isn't NULL because of the calculation for envpLen
      // above, so it's safe to use strlen.
      execArgs->envp[ii] = (char*) malloc(strlen(envp[ii]) + 1);
      if (execArgs->envp[ii] == NULL) {
        logError("Allocating execArgs->envp[%zu] failed\n", ii);
        errno = ENOMEM;
        goto freeExecArgs;
      }
      strcpy(execArgs->envp[ii], envp[ii]);
    }
    execArgs->envp[ii] = NULL; // NULL-terminate the array
  } else {
    execArgs->envp = NULL;
  }

  execArgs->schedulerState = NULL; // Set by the scheduler

  SchedulerExecveArgs schedulerExecveArgs = {
    .execArgs = execArgs,
    .errorNumber = 0,
  };
  ProcessMessage *processMessage
    = initSendProcessMessageToPid(
    SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_EXECVE,
    &schedulerExecveArgs, sizeof(schedulerExecveArgs), true);
  if (processMessage == NULL) {
    // The only way this should be possible is if all available messages are
    // in use, so use ENOMEM as the errno.
    errno = ENOMEM;
    goto freeExecArgs;
  }

  processMessageWaitForDone(processMessage, NULL);

  // If we got this far then the exec failed for some reason.  The error will
  // be in the data portion of the message we sent to the scheduler.
  errno = schedulerExecveArgs.errorNumber;
  processMessageRelease(processMessage);

freeExecArgs:
  execArgs = execArgsDestroy(execArgs);

  return -1;
}

////////////////////////////////////////////////////////////////////////////////
// Scheduler command handlers and support functions
////////////////////////////////////////////////////////////////////////////////

/// @fn int closeProcessFileDescriptors(ProcessDescriptor *processDescriptor)
///
/// @brief Helper function to close out the file descriptors owned by a process
/// when it exits or is killed.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that holds the
///   fileDescriptors array to close.
///
/// @return Returns 0 on success, -errno on failure.
int closeProcessFileDescriptors(ProcessDescriptor *processDescriptor) {
  ProcessMessage processMessage;
  memset(&processMessage, 0, sizeof(processMessage));

  if (_functionInProgress != NULL) {
    logError("Cannot execute because 0x%lx is already in progress\n",
      (long unsigned int) ((intptr_t) _functionInProgress));
    return -EBUSY;
  }

  int returnValue = 0;
  _functionInProgress = __func__;

  FileDescriptor **fileDescriptors = processDescriptor->fileDescriptors;
  if ((fileDescriptors == NULL)
    || (fileDescriptors == standardKernelFileDescriptorsPointers)
  ) {
    // Nothing to do.
    goto exit; // return 0
  }

  uint8_t numFileDescriptors = processDescriptor->numFileDescriptors;
  for (uint8_t ii = 0; ii < numFileDescriptors; ii++) {
    FileDescriptor *fileDescriptor = fileDescriptors[ii];
    if (fileDescriptor == NULL) {
      // This file descriptor was previously closed.  Move on.
      continue;
    }

    if (fileDescriptor->pipeEnd != NULL) {
      // Clear the pid of the waiting process's stdin file descriptor.
      fileDescriptor->pipeEnd->pipeEnd = NULL;
      fileDescriptor->pipeEnd->inputChannel.pid = PROCESS_ID_NOT_SET;

      ProcessId waitingOutputPid = fileDescriptor->outputChannel.pid;
      if (waitingOutputPid != PROCESS_ID_NOT_SET) {
        ProcessDescriptor *waitingProcessDescriptor
          = &SCHEDULER_STATE->allProcesses[waitingOutputPid - 1];
        if (processState(waitingProcessDescriptor) == PROCESS_STATE_WAIT) {
          // Send an empty message to the waiting process so that it will
          // become unblocked.
          if (processMessageInit(&processMessage,
            fileDescriptor->outputChannel.messageType, NULL, 0, true
            ) != processSuccess
          ) {
            // Nothing we can do.
            returnValue = -EOTHER;
            goto exit;
          }
          if (processMessageQueuePush(waitingProcessDescriptor,
            &processMessage) != processSuccess
          ) {
            // Nothing we can do.
            returnValue = -EOTHER;
            goto exit;
          }
          ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;
          int64_t startTime = 0;
          HAL->clock.getElapsedMicroseconds(0, &startTime);
          // schedulerKillProcess times out after 100 milliseconds, so
          // timeout after 50 milliseconds.
          int64_t elapsedUs = 0;
          while ((processMessageDone(&processMessage) == false)
            && (HAL->clock.getElapsedMicroseconds(startTime, &elapsedUs),
              elapsedUs < 50000)
          ) {
            for (int ii = 0; ii < NUM_PRIVILEGE_LEVELS; ii++) {
              SCHEDULER_STATE->currentReady = &SCHEDULER_STATE->ready[ii];
              uint8_t queueSize = SCHEDULER_STATE->currentReady->numElements;
              for (uint8_t jj = 0; jj < queueSize; jj++) {
                runScheduler();
              }
            }
          }
          SCHEDULER_STATE->currentReady = currentReady;
        }
      }
      removeProcessIpcCapability(processDescriptor,
        waitingOutputPid, CONSOLE_COMMAND_SIGNATURE, CONSOLE_RETURNING_INPUT);
    }

    fileDescriptors[ii]->refCount--;
    if (fileDescriptors[ii]->refCount == 0) {
      if (fileDescriptors[ii]->pipeEnd != NULL) {
        fileDescriptors[ii]->pipeEnd->pipeEnd = NULL;
      }
      schedFree(fileDescriptors[ii]); fileDescriptors[ii] = NULL;
    }
  }

  // schedFree will pull an available message.  Release the one we've been
  // using so that we're guaranteed it will be successful.
  schedFree(fileDescriptors); processDescriptor->fileDescriptors = NULL;
  processDescriptor->numFileDescriptors = 0;

exit:
  _functionInProgress = NULL;
  return returnValue;
}

/// @fn FILE* schedFopen(const char *pathname, const char *mode)
///
/// @brief Version of fopen for the scheduler.
///
/// @param pathname A pointer to the C string with the full path to the file to
///   open.
/// @param mode A pointer to the C string that defines the way to open the file.
///
/// @return Returns a pointer to the opened file on success, NULL on failure.
FILE* schedFopen(const char *pathname, const char *mode) {
  FILE *returnValue = NULL;
  if (SCHEDULER_STATE->rootFsPid == 0) {
    return returnValue; // NULL
  }

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    FilesystemFopenArgs fopenArgs = {
      .pathname = (char*) pathname,
      .mode = (char*) mode,
      .fd = 0, // We don't care
    };
    logDebug("schedFopen: Sending message\n");
    if (schedulerInitSendMessageToPid(SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_OPEN_FILE,
      &fopenArgs, sizeof(fopenArgs)
      ) != processSuccess
    ) {
      // Nothing we can do.
      return returnValue; // NULL
    }

    returnValue = fopenArgs.returnValue;

    _functionInProgress = NULL;
  } else {
    logError("Cannot execute because 0x%lx is already in progress\n",
      (long unsigned int) ((intptr_t) _functionInProgress));
    errno = EBUSY;
  }

  return returnValue;
}

/// @fn int schedFclose(FILE *stream)
///
/// @brief Version of fclose for the scheduler.
///
/// @param stream A pointer to the FILE object that was previously opened.
///
/// @return Returns 0 on success, EOF on failure.  On failure, the value of
/// errno is also set to the appropriate error.
int schedFclose(FILE *stream) {
  int returnValue = 0;
  if (SCHEDULER_STATE->rootFsPid == 0) {
    errno = ENODEV;
    return EOF;
  }

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    FilesystemFcloseArgs fcloseArgs;
    fcloseArgs.stream = stream;
    fcloseArgs.returnValue = 0;

    if (schedulerInitSendMessageToPid(SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_CLOSE_FILE,
      &fcloseArgs, sizeof(fcloseArgs)
      ) != processSuccess
    ) {
      // Nothing we can do.
      errno = EOTHER;
      return EOF;
    }

    if (fcloseArgs.returnValue != 0) {
      errno = -fcloseArgs.returnValue;
      returnValue = EOF;
    }

    _functionInProgress = NULL;
  } else {
    logError("Cannot execute because 0x%lx is already in progress\n",
      (long unsigned int) ((intptr_t) _functionInProgress));
    errno = EBUSY;
    returnValue = EOF;
  }

  return returnValue;
}

/// @fn int schedRemove(const char *pathname)
///
/// @brief Version of remove for the scheduler.
///
/// @param pathname A pointer to the C string with the full path to the file to
///   remove.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int schedRemove(const char *pathname) {
  int returnValue = 0;
  if (SCHEDULER_STATE->rootFsPid == 0) {
    errno = ENODEV;
    return -1;
  }

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    FilesystemRemoveArgs filesystemRemoveArgs = {
      .pathname = (char*) pathname,
      .returnValue = 0,
    };

    if (schedulerInitSendMessageToPid(SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_REMOVE_FILE,
      &filesystemRemoveArgs, sizeof(filesystemRemoveArgs)
      ) != processSuccess
    ) {
      // Nothing we can do.
      errno = EOTHER;
      return -1;
    }

    if (filesystemRemoveArgs.returnValue != 0) {
      // returnValue holds a negative errno.  Set errno for the current process
      // and return -1 like we're supposed to.
      errno = -filesystemRemoveArgs.returnValue;
      returnValue = -1;
    }

    _functionInProgress = NULL;
  } else {
    logError("Cannot execute because 0x%lx is already in progress\n",
      (long unsigned int) ((intptr_t) _functionInProgress));
    errno = EBUSY;
    returnValue = -1;
  }

  return returnValue;
}

/// @fn size_t schedFread(void *ptr, size_t size, size_t nmemb, FILE *stream)
///
/// @brief Version of fread for the scheduler.
///
/// @param ptr A pointer to the buffer to read data into.
/// @param size The size, in bytes, of each item that is to be read in.
/// @param nmemb The number of items to read from the file.
/// @param stream A pointer to the open FILE to read data in from.
///
/// @return Returns the number of items successfully read in.
size_t schedFread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  if (SCHEDULER_STATE->rootFsPid == 0) {
    return 0;
  }

  FilesystemIoCommandArgs filesystemIoCommandArgs = {
    .file = stream,
    .buffer = ptr,
    .length = size * nmemb
  };

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    if (schedulerInitSendMessageToPid(SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_READ_FILE,
      &filesystemIoCommandArgs, sizeof(filesystemIoCommandArgs)
      ) != processSuccess
    ) {
      // Nothing we can do.
      return 0;
    }

    _functionInProgress = NULL;
  } else {
    logError("Cannot execute because 0x%lx is already in progress\n",
      (long unsigned int) ((intptr_t) _functionInProgress));
    errno = EBUSY;
    return 0;
  }

  return filesystemIoCommandArgs.length / size;
}

/// @fn size_t schedFwrite(void *ptr, size_t size, size_t nmemb, FILE *stream)
///
/// @brief Version of fwrite for the scheduler.
///
/// @param ptr A pointer to the buffer to write data from.
/// @param size The size, in bytes, of each item that is to be written out.
/// @param nmemb The number of items to written to the file.
/// @param stream A pointer to the open FILE to write data out to.
///
/// @return Returns the number of items successfully written out.
size_t schedFwrite(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  if (SCHEDULER_STATE->rootFsPid == 0) {
    return 0;
  }

  FilesystemIoCommandArgs filesystemIoCommandArgs = {
    .file = stream,
    .buffer = ptr,
    .length = size * nmemb
  };

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    if (schedulerInitSendMessageToPid(SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_WRITE_FILE,
      &filesystemIoCommandArgs, sizeof(filesystemIoCommandArgs)
      ) != processSuccess
    ) {
      // Nothing we can do.
      return 0;
    }

    _functionInProgress = NULL;
  } else {
    logError("Cannot execute because 0x%lx is already in progress\n",
      (long unsigned int) ((intptr_t) _functionInProgress));
    errno = EBUSY;
    return 0;
  }

  return filesystemIoCommandArgs.length / size;
}

/// @fn int schedFgets(char *buffer, int size, FILE *stream)
///
/// @brief Version of fgets for the scheduler.
///
/// @param buffer The character buffer to read the file data into.
/// @param size The size of the buffer provided, in bytes.
/// @param stream A pointer to the FILE object that was previously opened.
///
/// @return Returns a pointer to the provided buffer on success, NULL on
/// failure.
char* schedFgets(char *buffer, int size, FILE *stream) {
  if (SCHEDULER_STATE->rootFsPid == 0) {
    return NULL;
  }

  char *returnValue = NULL;

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    FilesystemIoCommandArgs filesystemIoCommandArgs = {
      .file = stream,
      .buffer = buffer,
      .length = (uint32_t) size - 1
    };

    if (schedulerInitSendMessageToPid(SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_READ_FILE,
      &filesystemIoCommandArgs, sizeof(filesystemIoCommandArgs)
      ) != processSuccess
    ) {
      // Nothing we can do.
      return NULL;
    }
    if (filesystemIoCommandArgs.length > 0) {
      buffer[filesystemIoCommandArgs.length] = '\0';
      returnValue = buffer;
    }

    _functionInProgress = NULL;
  } else {
    logError("Cannot execute because 0x%lx is already in progress\n",
      (long unsigned int) ((intptr_t) _functionInProgress));
    errno = EBUSY;
    // returnValue is already NULL.
  }

  return returnValue;
}

/// @fn int schedFputs(const char *s, FILE *stream)
///
/// @brief Version of fputs for the scheduler.
///
/// @param s A pointer to the C string to write to the file.
/// @param stream A pointer to the FILE object that was previously opened.
///
/// @return Returns 0 on success, EOF on failure.  On failure, the value of
/// errno is also set to the appropriate error.
int schedFputs(const char *s, FILE *stream) {
  if (SCHEDULER_STATE->rootFsPid == 0) {
    errno = ENODEV;
    return EOF;
  }

  int returnValue = 0;

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    FilesystemIoCommandArgs filesystemIoCommandArgs = {
      .file = stream,
      .buffer = (void*) s,
      .length = (uint32_t) strlen(s)
    };

    if (schedulerInitSendMessageToPid(SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_WRITE_FILE,
      &filesystemIoCommandArgs, sizeof(filesystemIoCommandArgs)
      ) != processSuccess
    ) {
      // Nothing we can do.
      return EOF;
    }
    if (filesystemIoCommandArgs.length == 0) {
      returnValue = EOF;
    }

    _functionInProgress = NULL;
  } else {
    logError("Cannot execute because 0x%lx is already in progress\n",
      (long unsigned int) ((intptr_t) _functionInProgress));
    errno = EBUSY;
    returnValue = EOF;
  }

  return returnValue;
}

/// @fn int schedGetFileBlockMetadataFromFile(FILE *stream,
///   FileBlockMetadata *metadata)
///
/// @brief Get the block-level metadata for a given file.
///
/// @param stream A pointer to a previously-opened FILE.
/// @param metadata A pointer to a FileBlockMetadata structure the caller wants
///   populated.
///
/// @return Returns 0 on success, -errno on failure.
int schedGetFileBlockMetadataFromFile(
  FILE *stream, FileBlockMetadata *metadata
) {
  if (SCHEDULER_STATE->rootFsPid == 0) {
    return -ENODEV;
  }

  if ((stream == NULL) || (metadata == NULL)) {
    return -EINVAL;
  }

  GetFileBlockMetadataArgs args = {
    .stream = stream,
    .metadata = metadata,
  };

  if (schedulerInitSendMessageToPid(SCHEDULER_STATE->rootFsPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_GET_FILE_BLOCK_METADATA,
    &args, sizeof(args)
    ) != processSuccess
  ) {
    // Nothing we can do.
    return -EIO;
  }

  return 0;
}

/// @var _readMode
///
/// @brief fopen() mode string used to open a file for reading.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _readMode[] KEEP_IN_FLASH = "r";

/// @fn int schedGetFileBlockMetadataFromPath(const char *path,
///   FileBlockMetadata *metadata)
///
/// @brief Get the block-level metadata for a given path.
///
/// @param path A string representing a path to a file on the filesystem.
/// @param metadata A pointer to a FileBlockMetadata structure the caller wants
///   populated.
///
/// @return Returns 0 on success, -errno on failure.
int schedGetFileBlockMetadataFromPath(
  const char *path, FileBlockMetadata *metadata
) {
  if ((path == NULL) || (metadata == NULL)) {
    return -EINVAL;
  }

  FILE *stream = schedFopen(path, _readMode);
  if (stream == NULL) {
    logError("Could not open file \"%s\"\n", path);
    return -EIO;
  }
  int returnValue = schedGetFileBlockMetadataFromFile(stream, metadata);
  schedFclose(stream); stream = NULL;

  return returnValue;
}

/// @var _mainOverlayPath
///
/// @brief Path suffix used to locate an overlay namespace's main overlay
/// file.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _mainOverlayPath[] KEEP_IN_FLASH = "/main";

/// @var _overlayExt
///
/// @brief Local copy of OVERLAY_EXT (defined in Overlay.h) kept in this
/// translation unit's own storage.  If OVERLAY_EXT's definition ever
/// changes, this picks up the change automatically since it's initialized
/// from the macro rather than duplicating the literal.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _overlayExt[] KEEP_IN_FLASH = OVERLAY_EXT;

/// @fn int loadProcessDescriptorOverlayMetadata(ProcessDescriptor *processDescriptor)
///
/// @brief Load the FileBlockMetadata for a ProcessDescriptor's overlay.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to load the
///   FileBlockMetadata for.
///
/// @return Returns 0 on success, -errno on failure.
int loadProcessDescriptorOverlayMetadata(ProcessDescriptor *processDescriptor) {
  if (processDescriptor->overlayNamespace == NULL) {
    // Nothing to do
    return 0;
  }

  char *overlayPath = (char*) schedMalloc(
    strlen((char*) processDescriptor->overlayNamespace) + OVERLAY_EXT_LEN + 6);
  if (overlayPath == NULL) {
    // Fail.
    logError("malloc failure for overlayPath.\n");
    return -ENOMEM;
  }
  strcpy(overlayPath, (char*) processDescriptor->overlayNamespace);
  strcat(overlayPath, _mainOverlayPath);
  strcat(overlayPath, _overlayExt);

  int returnValue
    = schedGetFileBlockMetadataFromPath(overlayPath,
      &processDescriptor->overlay);
  if ((returnValue == -EIO) && (errno == EBUSY)) {
    returnValue = -EBUSY;
  }
  schedFree(overlayPath);

  return returnValue;
}

/// @fn int schedulerKillProcessCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Kill a process identified by its process ID.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received that contains
///   the information about the process to kill.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerKillProcessCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  int returnValue = 0;

  UserId callingUserId = processMessageFrom(processMessage)->userId;
  SchedulerKillProcessArgs *schedulerKillProcessArgs
    = (SchedulerKillProcessArgs*) processMessageData(processMessage);

  ProcessId pid = schedulerKillProcessArgs->pid;
  logInfo("Killing process %ld\n", (long int) pid);
  int processIndex = pid - 1;

  bool selfKill = false;
  if (processPid(processMessageFrom(processMessage)) == pid) {
    selfKill = true;
  }

  if ((pid >= schedulerState->firstUserPid)
    && (pid <= NANO_OS_NUM_PROCESSES)
    && (processRunning(&allProcesses[processIndex]))
  ) {
    if ((allProcesses[processIndex].userId == callingUserId)
      || (callingUserId == ROOT_USER_ID)
    ) {
      ProcessDescriptor *processDescriptor = &allProcesses[processIndex];
      // Regardless of whether or not we succeed at terminating it, we have
      // to remove it from its queue.  We don't know which queue it's on,
      // though.  The fact that we're killing it makes it likely that it's hung.
      // The most likely reason is that it's waiting on something with an
      // infinite timeout, so it's most likely to be on the waiting queue.  The
      // second most likely reason is that it's in an infinite loop, so the
      // ready queue is the second-most-likely place it could be.  The least-
      // likely place for it to be would be the timed waiting queue with a very
      // long timeout.  So, attempt to remove from the queues in that order.
      if (processQueueRemove(&schedulerState->waiting, processDescriptor) != 0
      ) {
        if (processQueueRemove(processDescriptor->readyQueue,
          processDescriptor) != 0
        ) {
          processQueueRemove(&schedulerState->timedWaiting, processDescriptor);
        }
      }

      if (selfKill == false) {
        // Tell the console to release the port for us.  We will forward it
        // the message we acquired above, which it will use to send to the
        // correct shell to unblock it.  We need to do this before terminating
        // the process because, in the event the process we're terminating is
        // one of the shell process slots, the message won't get released
        // because there's no shell blocking waiting for the message.
        ConsoleReleasePidPortArgs consoleReleasePidPortArgs = {
          .processId = pid,
        };
        if (schedulerInitSendMessageToPid(
          SCHEDULER_STATE->consolePid,
          CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_PID_PORT,
          /* data= */ &consoleReleasePidPortArgs,
          /* size= */ sizeof(consoleReleasePidPortArgs)) != processSuccess
        ) {
          logError(
            "Could not send CONSOLE_RELEASE_PID_PORT message "
            "to console process\n");
          schedulerKillProcessArgs->returnValue = 1;
          schedulerKillProcessArgs->errorNumber = EBUSY;
        }

        // Close the file descriptors before we terminate the process so that
        // anything that gets sent to the process's queue gets cleaned up when
        // we terminate it.
        if (closeProcessFileDescriptors(processDescriptor) != 0) {
          // DO NOT mark the message done or release it.  Return an error status
          // immediately so that we push the message back onto our queue and
          // try it again later.
          return -EBUSY;
        }

        MemoryManagerFreeProcessMemoryArgs memoryManagerFreeProcessMemoryArgs
        = {
          .pid = pid,
          .returnValue = 0,
        };
        if (schedulerInitSendMessageToPid(
          SCHEDULER_STATE->memoryManagerPid,
          MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_FREE_PROCESS_MEMORY,
          &memoryManagerFreeProcessMemoryArgs,
          sizeof(memoryManagerFreeProcessMemoryArgs)) != processSuccess
        ) {
          logError(
            "Could not send MEMORY_MANAGER_FREE_PROCESS_MEMORY"
            "message to memory manager process\n");
          schedulerKillProcessArgs->returnValue = 1;
          schedulerKillProcessArgs->errorNumber = EBUSY;
        }

        // MEMORY_MANAGER_FREE_PROCESS_MEMORY will have freed envp if it
        // existed, so make sure it's NULL now.
        processDescriptor->envp = NULL;
        processDescriptor->name = NULL;
        processDescriptor->userId = NO_USER_ID;
      }

      // Terminate the process and make sure its message queue gets flushed.
      if (processTerminate(processDescriptor, false) == processSuccess) {
        threadSetContext(processDescriptor->mainThread,
          processDescriptor);

        // It's likely (i.e. almost certain) that the killed process was a user
        // process that was killed by a user process.  That would mean that we
        // were in the middle of processing a user process queue, the number of
        // items in which was captured before the runScheduler loop was started.
        // (See the logic at the end of startScheduler.)  Rather than pushing
        // the killed process onto the free queue, push it back onto its ready
        // queue so that we don't try to pop a process from an empty queue.
        // runScheduler will do the cleanup and put the process onto the free
        // queue again once it picks back up again.
        processQueuePush(processDescriptor->readyQueue, processDescriptor);
      } else {
        // Tell the caller that we've failed.
        logError("Failed to terminate process; marking message 0x%lx done\n",
          (unsigned long int) (uintptr_t) processMessage);
        schedulerKillProcessArgs->returnValue = 1;
        schedulerKillProcessArgs->errorNumber = EOTHER;

        // Do *NOT* push the process back onto the free queue in this case.
        // If we couldn't terminate it, it's not valid to try and reuse it for
        // another process.
      }

      if (processMessageSetDone(processMessage) != processSuccess) {
        logError("Could not mark message done in "
          "schedulerKillProcessCommandHandler.\n");
      }
    } else {
      // Tell the caller that we've failed.
      schedulerKillProcessArgs->returnValue = 1;
      schedulerKillProcessArgs->errorNumber = EACCES;
      if (processMessageSetDone(processMessage) != processSuccess) {
        logError("Could not mark message done in "
          "schedulerKillProcessCommandHandler.\n");
      }
    }
  } else {
    // Tell the caller that we've failed.
    schedulerKillProcessArgs->returnValue = 1;
    schedulerKillProcessArgs->errorNumber = EINVAL;
    if (processMessageSetDone(processMessage) != processSuccess) {
      logError("Could not mark message done in "
        "schedulerKillProcessCommandHandler.\n");
    }
  }

  if ((processMessageWaiting(processMessage) == false) || (selfKill == true)) {
    processMessageRelease(processMessage);
  }
  // else DO NOT release the message since that's done by the caller.

  return returnValue;
}

/// @fn int schedulerGetNumProcessDescriptorsCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Get the number of processes that are currently running in the system.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.  This will be
///   reused for the reply.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerGetNumProcessDescriptorsCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  int returnValue = 0;
  SchedulerGetNumRunningProcessesArgs *schedulerGetNumRunningProcessesArgs
    = (SchedulerGetNumRunningProcessesArgs*) processMessageData(processMessage);

  uint8_t numProcessDescriptors = 0;
  for (int ii = 1; ii <= NANO_OS_NUM_PROCESSES; ii++) {
    if (processRunning(&schedulerState->allProcesses[ii - 1])) {
      numProcessDescriptors++;
    }
  }
  schedulerGetNumRunningProcessesArgs->returnValue = numProcessDescriptors;
  schedulerGetNumRunningProcessesArgs->errorNumber = 0;

  processMessageSetDone(processMessage);

  // DO NOT release the message since the caller is waiting on the response.

  return returnValue;
}

/// @fn int schedulerGetProcessInfoCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Fill in a provided array with information about the currently-running
/// processes.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.  This will be
///   reused for the reply.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerGetProcessInfoCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  int returnValue = 0;

  SchedulerGetProcessInfoArgs *schedulerGetProcessInfoArgs =
    (SchedulerGetProcessInfoArgs*) processMessageData(processMessage);
  int maxProcesses = schedulerGetProcessInfoArgs->processInfo->numProcesses;
  ProcessInfoElement *processes
    = schedulerGetProcessInfoArgs->processInfo->processes;

  int idx = 0;
  for (int ii = 1;
    (ii <= NANO_OS_NUM_PROCESSES) && (idx < maxProcesses);
    ii++
  ) {
    if (processRunning(&schedulerState->allProcesses[ii - 1]) == false) {
      continue;
    }

    processes[idx].pid = (int) schedulerState->allProcesses[ii - 1].processId;
    processes[idx].name = schedulerState->allProcesses[ii - 1].name;
    processes[idx].userId = schedulerState->allProcesses[ii - 1].userId;
    idx++;
  }

  // It's possible that a process completed between the time that processInfo
  // was allocated and now, so set the value of numProcesses to the value of
  // idx.
  schedulerGetProcessInfoArgs->processInfo->numProcesses = idx;
  schedulerGetProcessInfoArgs->returnValue = 0;
  schedulerGetProcessInfoArgs->errorNumber = 0;

  processMessageSetDone(processMessage);

  // DO NOT release the message since the caller is waiting on the response.

  return returnValue;
}

/// @fn int schedulerSetProcessUserCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Get the number of processes that are currently running in the system.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.
///   This will be reused for the reply.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerSetProcessUserCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  int returnValue = 0;
  SchedulerSetProcessUserArgs *schedulerSetProcessUserArgs
    = (SchedulerSetProcessUserArgs*) processMessageData(processMessage);
  ProcessId callingPid = processPid(processMessageFrom(processMessage));

  if ((callingPid > 0) && (callingPid <= NANO_OS_NUM_PROCESSES)) {
    if ((schedulerState->allProcesses[callingPid - 1].userId == -1)
      || (schedulerSetProcessUserArgs->userId == -1)
    ) {
      schedulerState->allProcesses[callingPid - 1].userId
        = schedulerSetProcessUserArgs->userId;
      schedulerSetProcessUserArgs->returnValue = 0;
      schedulerSetProcessUserArgs->errorNumber = 0;
    } else {
      schedulerSetProcessUserArgs->returnValue = -1;
      schedulerSetProcessUserArgs->errorNumber = EACCES;
    }
  }

  processMessageSetDone(processMessage);

  // DO NOT release the message since the caller is waiting on the response.

  return returnValue;
}

/// @fn int schedulerGetHostnameCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Get the hostname that's read when the scheduler starts.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerGetHostnameCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  int returnValue = 0;

  SchedulerGetHostnameArgs *schedulerGetHostnameArgs
    = (SchedulerGetHostnameArgs*) processMessageData(processMessage);

  schedulerGetHostnameArgs->hostname = schedulerState->hostname;
  schedulerGetHostnameArgs->errorNumber = 0;

  processMessageSetDone(processMessage);
  return returnValue;
}

/// @fn int schedulerExecveCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Exec a new program in place of a running program.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerExecveCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  int returnValue = 0;
  if (processMessage == NULL) {
    // This should be impossible, but there's nothing to do.  Return good
    // status.
    return returnValue; // 0
  }

  ProcessDescriptor *processDescriptor = processMessageFrom(processMessage);

  SchedulerExecveArgs *schedulerExecveArgs
    = (SchedulerExecveArgs*) processMessageData(processMessage);
  ExecArgs *execArgs = schedulerExecveArgs->execArgs;
  if (execArgs == NULL) {
    logError("execArgs provided was NULL.\n");
    schedulerExecveArgs->errorNumber = EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  execArgs->callingPid = processPid(processMessageFrom(processMessage));

  char *pathname = execArgs->pathname;
  if (pathname == NULL) {
    // Invalid
    logError("pathname provided was NULL.\n");
    schedulerExecveArgs->errorNumber = EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **argv = execArgs->argv;
  if (argv == NULL) {
    // Invalid
    logError("argv provided was NULL.\n");
    schedulerExecveArgs->errorNumber = EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  } else if (argv[0] == NULL) {
    // Invalid
    logError("argv[0] provided was NULL.\n");
    schedulerExecveArgs->errorNumber = EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **envp = execArgs->envp;

  // The arguments provided to this command are going to replace the ones that
  // spawned the original.  We need to free the original envp if there was one.
  if (processDescriptor->envp != NULL) {
    for (int ii = 0; processDescriptor->envp[ii] != NULL; ii++)  {
      schedFree(processDescriptor->envp[ii]);
    }
    schedFree(processDescriptor->envp);
    processDescriptor->envp = NULL;
  }

  if (assignMemory(execArgs, 0) != 0) {
    logWarn("Could not protect execArgs memory.\nUndefined behavior.\n");
  }

  if (assignMemory(pathname, 0) != 0) {
    logWarn("Could not protect pathname memory.\nUndefined behavior.\n");
  }

  if (assignMemory(argv, 0) != 0) {
    logWarn("Could not protect argv memory.\nUndefined behavior.\n");
  }
  for (int ii = 0; argv[ii] != NULL; ii++) {
    if (assignMemory(argv[ii], 0) != 0) {
      logWarn("Could not protect argv[lld] memory.\nUndefined behavior.\n",
        (long int) ii);
    }
  }

  if (envp != NULL) {
    if (assignMemory(envp, 0) != 0) {
      logWarn("Could not protect envp memory.\nUndefined behavior.\n");
    }
    for (int ii = 0; envp[ii] != NULL; ii++) {
      if (assignMemory(envp[ii], 0) != 0) {
        logWarn("Could not protect envp[%ld] memory.\n"
          "Undefined behavior.\n", (long int) ii);
      }
    }
  }

  if (assignMemory(processDescriptor->fileDescriptors, 0) != 0) {
    logWarn("Could not protect fileDescriptors memory.\nUndefined behavior.\n");
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    if (processDescriptor->fileDescriptors[ii] == NULL) {
      continue;
    }
    if (assignMemory(processDescriptor->fileDescriptors[ii], 0) != 0) {
      logWarn("Could not protect fileDescriptors[%ld] memory.\n"
        "Undefined behavior.\n",
        (long int) ii);
    }
  }

  // The process should be blocked in processMessageQueueWaitForType waiting
  // on a condition with an infinite timeout.  So, it *SHOULD* be on the
  // waiting queue.  Take no chances, though.
  if (processQueueRemove(&schedulerState->waiting, processDescriptor) != 0) {
    if (processQueueRemove(&schedulerState->timedWaiting, processDescriptor)
      != 0
    ) {
      processQueueRemove(processDescriptor->readyQueue, processDescriptor);
    }
  }

  // Kill and clear out the calling process.  We're reusing this process,
  // though, and if we're using pipes, something may have already sent us a
  // message that the replacement is expected to process.  So, keep the message
  // queue (set the second argument to true).
  processTerminate(processDescriptor, true);
  threadSetContext(processDescriptor->mainThread, processDescriptor);

  // We don't want to wait for the memory manager to release the memory.  Make
  // it do it immediately.
  MemoryManagerFreeProcessMemoryArgs memoryManagerFreeProcessMemoryArgs = {
    .pid = processDescriptor->processId,
    .returnValue = 0,
  };
  if (schedulerInitSendMessageToPid(
    SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_FREE_PROCESS_MEMORY,
    &memoryManagerFreeProcessMemoryArgs,
    sizeof(memoryManagerFreeProcessMemoryArgs))
  ) {
    logWarn("Could not release memory for process %ld\nMemory leak.\n",
      (long int) processDescriptor->processId);
  }

  execArgs->schedulerState = schedulerState;
  if (processCreate(processDescriptor, HAL->platform.execCommand, execArgs)
    == processError
  ) {
    logError("Could not configure process handle for new command.\n");
  }

  if (assignMemory(execArgs, processDescriptor->processId) != 0) {
    logWarn("Could not assign execArgs to exec process.\n"
      "Undefined behavior.\n");
  }

  if (assignMemory(pathname, processDescriptor->processId) != 0) {
    logWarn("Could not assign pathname to exec process.\n"
      "Undefined behavior.\n");
  }

  if (assignMemory(argv, processDescriptor->processId) != 0) {
    logWarn("Could not assign argv to exec process.\n"
      "Undefined behavior.\n");
  }
  for (int ii = 0; argv[ii] != NULL; ii++) {
    if (assignMemory(argv[ii], processDescriptor->processId) != 0) {
      logWarn("Could not assign argv[%d] to exec process.\n"
        "Undefined behavior.\n", ii);
    }
  }

  if (envp != NULL) {
    if (assignMemory(envp, processDescriptor->processId) != 0) {
      logWarn("Could not assign envp to exec process.\n"
        "Undefined behavior.\n");
    }
    for (int ii = 0; envp[ii] != NULL; ii++) {
      if (assignMemory(envp[ii], processDescriptor->processId) != 0) {
        logWarn("Could not assign envp[%d] to exec process.\n"
          "Undefined behavior.\n", ii);
      }
    }
  }

  if (assignMemory(processDescriptor->fileDescriptors,
    processDescriptor->processId) != 0
  ) {
    logWarn("Could not assign fileDescriptors to scheduler.\n"
      "Undefined behavior.\n");
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    if (processDescriptor->fileDescriptors[ii] == NULL) {
      continue;
    }
    if (assignMemory(processDescriptor->fileDescriptors[ii],
      processDescriptor->processId) != 0
    ) {
      logWarn("Could not assign fileDescriptors[%d] to scheduler.\n"
        "Undefined behavior.\n", ii);
    }
  }

  if (processDescriptor->fileDescriptors[STDOUT_FILE_DESCRIPTOR_INDEX]->pipeEnd
    != NULL
  ) {
    addProcessIpcCapability(processDescriptor,
      processDescriptor->fileDescriptors[
        STDOUT_FILE_DESCRIPTOR_INDEX]->pipeEnd->lastOwner,
        CONSOLE_COMMAND_SIGNATURE, CONSOLE_RETURNING_INPUT);
  }

  if (HAL->platform.execCommand == execOverlayCommand) {
    processDescriptor->overlayNamespace = pathname;
  }
  returnValue = loadProcessDescriptorOverlayMetadata(processDescriptor);
  if (returnValue == -EBUSY) {
    // We're in the middle of a filesystem operation already and can't access
    // a file right now.  Return error status and try again later.  DO NOT
    // set the message done or alter its value.
    return returnValue; // -EBUSY
  } else if (returnValue != 0) {
    schedulerExecveArgs->errorNumber = returnValue;
    returnValue = 0; // Don't retry this command
    processMessageSetDone(processMessage);
    return returnValue; // 0
  }
  processDescriptor->envp = envp;
  processDescriptor->name = argv[0];

  /*
   * This shouldn't be necessary.  In hindsight, perhaps I shouldn't be
   * assigning a port to a process at all.  That's not the way Unix works.  I
   * should probably remove the ability to exclusively assign a port to a
   * process at some point in the future.  Delete this if I haven't found a
   * good reason to continue granting exclusive access to a process by then.
   * Leaving it uncommented in an if (false) so that compilation will fail
   * if/when I delete the functionality.
   *
   * JBC 14-Nov-2025
   */
  if (false) {
    if (schedulerAssignPortToPid(
      /*commandDescriptor->consolePort*/ 255, processDescriptor->processId)
      != processSuccess
    ) {
      logWarn("Could not assign console port to process.\n");
    }
  }

  // Resume the thread so that it picks up all the pointers it needs before
  // we release the message we were sent.
  processResume(processDescriptor, NULL);

  // Put the process on the ready queue.
  processQueuePush(processDescriptor->readyQueue, processDescriptor);

  processMessageRelease(processMessage);

  return returnValue;
}

/// @fn int schedulerSpawnCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Spawn a program in a new process.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerSpawnCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  int returnValue = 0;
  if (processMessage == NULL) {
    // This should be impossible, but there's nothing to do.  Return good
    // status.
    return returnValue; // 0
  }

  SchedulerSpawnArgs *schedulerSpawnArgs
    = (SchedulerSpawnArgs*) processMessageData(processMessage);
  SpawnArgs *spawnArgs = schedulerSpawnArgs->spawnArgs;
  if (spawnArgs == NULL) {
    logError("spawnArgs provided was NULL.\n");
    schedulerSpawnArgs->errorNumber = EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }

  char *pathname = spawnArgs->path;
  if (pathname == NULL) {
    // Invalid
    logError("pathname provided was NULL.\n");
    schedulerSpawnArgs->errorNumber = EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **argv = spawnArgs->argv;
  if (argv == NULL) {
    // Invalid
    logError("argv provided was NULL.\n");
    schedulerSpawnArgs->errorNumber = EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  } else if (argv[0] == NULL) {
    // Invalid
    logError("argv[0] provided was NULL.\n");
    schedulerSpawnArgs->errorNumber = EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **envp = spawnArgs->envp;

  ProcessDescriptor *processDescriptor = processQueuePop(&schedulerState->free);
  if (processDescriptor == NULL) {
    logError("Out of process slots to launch process.\n");
    schedulerSpawnArgs->errorNumber = EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  *spawnArgs->newPid = processDescriptor->processId;

  // Initialize the new process.
  threadSetContext(processDescriptor->mainThread, processDescriptor);

  ExecArgs *execArgs = (ExecArgs*) schedMalloc(sizeof(ExecArgs));
  if (execArgs == NULL) {
    logError("Out of memory for ExecArgs.\n");
    schedulerSpawnArgs->errorNumber = ENOMEM;
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  execArgs->callingPid = processPid(processMessageFrom(processMessage));
  execArgs->pathname = spawnArgs->path;
  execArgs->argv = spawnArgs->argv;
  execArgs->envp = spawnArgs->envp;
  execArgs->schedulerState = schedulerState;

  processDescriptor->userId
    = allProcesses[processPid(processMessageFrom(processMessage)) - 1].userId;

  processDescriptor->numFileDescriptors = NUM_STANDARD_FILE_DESCRIPTORS;
  // Use calloc for processDescriptor->fileDescriptors in case we fail to
  // allocate one of the FileDescriptor pointers later and have to free the
  // elements of the array.  It's safe to pass NULL to free().
  processDescriptor->fileDescriptors = (FileDescriptor**) schedCalloc(1,
    NUM_STANDARD_FILE_DESCRIPTORS * sizeof(FileDescriptor*));
  if (processDescriptor->fileDescriptors == NULL) {
    logError("Could not allocate file descriptor array for new command\n");
    schedulerSpawnArgs->errorNumber = ENOMEM;
    schedFree(execArgs);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    processDescriptor->fileDescriptors[ii]
      = (FileDescriptor*) schedMalloc(sizeof(FileDescriptor));
    if (processDescriptor->fileDescriptors[ii] == NULL) {
      logError("Could not allocate memory for file descriptor %d "
        "for new process\n", ii);
      schedulerSpawnArgs->errorNumber = ENOMEM;
      for (int jj = 0; jj < ii; jj++) {
        schedFree(processDescriptor->fileDescriptors[jj]);
      }
      schedFree(processDescriptor->fileDescriptors);
      schedFree(execArgs);
      processMessageSetDone(processMessage);
      return returnValue; // 0; Don't retry this command
    }
    memcpy(
      processDescriptor->fileDescriptors[ii],
      &standardUserFileDescriptors[ii],
      sizeof(FileDescriptor)
    );
    processDescriptor->fileDescriptors[ii]->lastOwner
      = processDescriptor->processId;
  }

  if (spawnArgs->fileActions != NULL) {
    // Take care of the dup2 file actions.
    for (uint8_t ii = 0; ii < spawnArgs->fileActions->numDup2; ii++) {
      Dup2 *dup2 = &spawnArgs->fileActions->dup2[ii];
      if (dup2->fd >= processDescriptor->numFileDescriptors) {
        // This is technically legal in Unix, but we're not going to support it.
        // We're handling a spawn call here, so the only things that it makes
        // sense to dup are stdin, stdout, and stderr.
        schedFree(dup2->dup);
        continue;
      }

      // If we made it this far then we need to free the FileDescriptor that's
      // at the specified fd index and set it to the one provided.
      schedFree(processDescriptor->fileDescriptors[dup2->fd]);
      processDescriptor->fileDescriptors[dup2->fd] = dup2->dup;
      dup2->dup->lastOwner = processDescriptor->processId;

      // The dup2->dup FileDescriptor almost certainly has a non-NULL pipeEnd
      // pointer since we're handling dup2 logic, but guard anyway.
      if (dup2->dup->pipeEnd != NULL) {
        if (dup2->fd == STDIN_FILENO) {
          // We need to set the pid of the outputChannel of the other end of
          // the pipe to our ID.
          dup2->dup->pipeEnd->outputChannel.pid = processDescriptor->processId;
          addProcessIpcCapability(
            &allProcesses[dup2->dup->pipeEnd->lastOwner - 1],
            processDescriptor->processId, CONSOLE_COMMAND_SIGNATURE,
            CONSOLE_RETURNING_INPUT);
        } else if ((dup2->fd == STDOUT_FILENO) || (dup2->fd == STDERR_FILENO)) {
          // We need to set the pid of the inputChannel of the other end of
          // the pipe to our ID.
          dup2->dup->pipeEnd->inputChannel.pid = processDescriptor->processId;
          addProcessIpcCapability(processDescriptor,
            dup2->dup->pipeEnd->lastOwner, CONSOLE_COMMAND_SIGNATURE,
            CONSOLE_RETURNING_INPUT);
        }
      }
    }

    schedFree(spawnArgs->fileActions); spawnArgs->fileActions = NULL;
  }

  schedFree(spawnArgs); spawnArgs = NULL;

  if (processCreate(processDescriptor, HAL->platform.execCommand, execArgs)
    == processError
  ) {
    logError("Could not configure process handle for new command.\n");
  }

  if (assignMemory(execArgs, processDescriptor->processId) != 0) {
    logWarn("Could not assign execArgs to spawn process.\n"
      "Undefined behavior.\n");
  }

  if (assignMemory(pathname, processDescriptor->processId) != 0) {
    logWarn("Could not assign pathname to spawn process.\n"
      "Undefined behavior.\n");
  }

  if (assignMemory(argv, processDescriptor->processId) != 0) {
    logWarn("Could not assign argv to spawn process.\nUndefined behavior.\n");
  }
  for (int ii = 0; argv[ii] != NULL; ii++) {
    if (assignMemory(argv[ii], processDescriptor->processId) != 0) {
      logWarn("Could not assign argv[%d] to spawn process.\n"
        "Undefined behavior.\n", ii);
    }
  }

  if (envp != NULL) {
    if (assignMemory(envp, processDescriptor->processId) != 0) {
      logWarn("Could not assign envp to spawn process.\n"
        "Undefined behavior.\n");
    }
    for (int ii = 0; envp[ii] != NULL; ii++) {
      if (assignMemory(envp[ii], processDescriptor->processId) != 0) {
        logWarn("Could not assign envp[%d] to spawn process.\n"
          "Undefined behavior.\n", ii);
      }
    }
  }

  if (assignMemory(processDescriptor->fileDescriptors,
    processDescriptor->processId) != 0
  ) {
    logWarn("Could not assign fileDescriptors to spawn process.\n"
      "Undefined behavior.\n");
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    if (processDescriptor->fileDescriptors[ii] == NULL) {
      continue;
    }
    if (assignMemory(processDescriptor->fileDescriptors[ii],
      processDescriptor->processId) != 0
    ) {
      logWarn("Could not assign fileDescriptors[%d] to spawn process.\n"
        "Undefined behavior.\n", ii);
    }
  }

  if (HAL->platform.execCommand == execOverlayCommand) {
    processDescriptor->overlayNamespace = pathname;
  }
  returnValue = loadProcessDescriptorOverlayMetadata(processDescriptor);
  if (returnValue == -EBUSY) {
    // We're in the middle of a filesystem operation already and can't access
    // a file right now.  Return error status and try again later.  DO NOT
    // set the message done or alter its value.
    return returnValue; // -EBUSY
  } else if (returnValue != 0) {
    schedulerSpawnArgs->errorNumber = returnValue;
    returnValue = 0; // Don't retry this command
    processMessageSetDone(processMessage);

    // We have to terminate the process because something may have pushed a
    // message onto its message queue.  Set the second parameter to false to
    // make sure that the message queue is purged.
    processTerminate(processDescriptor, false);
    threadSetContext(processDescriptor->mainThread, processDescriptor);
    return returnValue; // 0
  }
  processDescriptor->envp = envp;
  processDescriptor->name = argv[0];

  // Resume the thread so that it picks up all the pointers it needs before
  // we release the message we were sent.
  processResume(processDescriptor, NULL);

  // Put the process on the ready queue.
  processQueuePush(processDescriptor->readyQueue, processDescriptor);

  schedulerSpawnArgs->errorNumber = 0;
  processMessageSetDone(processMessage);

  return returnValue;
}

/// @var _overlayLoadFailureReason
///
/// @brief Reason string passed to removeProcess() when a process's overlay
/// fails to load.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _overlayLoadFailureReason[] KEEP_IN_FLASH
  = "Overlay load failure";

/// @var _processCorruptionReason
///
/// @brief Reason string passed to removeProcess() when a process is found
/// to be corrupted.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _processCorruptionReason[] KEEP_IN_FLASH
  = "Process corruption detected";

/// @var _processRestartFailedReason
///
/// @brief Reason string passed to removeProcess() when a process's
/// restartFunction fails.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _processRestartFailedReason[] KEEP_IN_FLASH
  = "Process restart failed";

/// @fn int schedulerSendSignalCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Send a signal to a process.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerSendSignalCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  (void) schedulerState;

  int returnValue = 0;
  if (processMessage == NULL) {
    // This should be impossible, but there's nothing to do.  Print an error
    // and return good status.
    logError("NULL message provided\n");
    return returnValue; // 0
  }

  SchedulerSendSignalArgs *sendSignalArgs
    = (SchedulerSendSignalArgs*) processMessageData(processMessage);

  ProcessId pid = sendSignalArgs->pid;
  ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
  if ((pid < 2)
    || (pid > NANO_OS_NUM_PROCESSES)
    || (processRunning(processDescriptor) == false)
  ) {
    sendSignalArgs->returnValue = -1;
    sendSignalArgs->errorNumber = ESRCH;
    logError("Invalid process ID specified\n");
    processMessageSetDone(processMessage);
    goto exit; // return 0
  }

  if (sendSignalArgs->signal < 0) {
    sendSignalArgs->returnValue = -1;
    sendSignalArgs->errorNumber = EINVAL;
    logError("Invalid signal specified\n");
    processMessageSetDone(processMessage);
    goto exit; // return 0
  }

  if (sendSignalArgs->signal == 0) {
    // Per POSIX, no signal is sent, but checks are done.  We did the checks
    // above, so just return good status here.
    sendSignalArgs->returnValue = 0;
    sendSignalArgs->errorNumber = 0;
    goto exit; // return 0
  }

  SignalCallback signalCallback = {
    .signature = SIGNAL_SIGNATURE,
    .signum = sendSignalArgs->signal,
  };
  if (pid >= SCHEDULER_STATE->firstUserPid) {
    if (processRunning(processDescriptor) == true) {
      // This is a user process, which is in an overlay.  Make sure it's loaded.
      if (schedulerLoadOverlay(
        processDescriptor,
        processDescriptor->envp) != 0
      ) {
        // We can't deliver the signal to the process.
        sendSignalArgs->returnValue = -1;
        sendSignalArgs->errorNumber = ESRCH; // Closest POSIX-compliant value
        schedulerDumpMemoryAllocations();
        schedulerDumpOpenFiles();
        removeProcess(processDescriptor, _overlayLoadFailureReason);
        goto exit; // return 0
      }
    }
    
    // Configure the preemption timer to force the process to yield if it
    // doesn't voluntarily give up control within a reasonable amount of time.
    if (SCHEDULER_STATE->preemptionTimer > -1) {
      HAL->timer.configOneShot(
        SCHEDULER_STATE->preemptionTimer, 10000000, forceYield);
    }
  }
  processResume(processDescriptor, &signalCallback);

  sendSignalArgs->returnValue = 0;
  sendSignalArgs->errorNumber = 0;

exit:
  if (processMessageWaiting(processMessage) == false) {
    processMessageRelease(processMessage);
  }
  // else DO NOT release the message since that's done by the caller.

  return returnValue;
}

/// @fn int schedulerReplaceOverlayCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Replace the overlay of a process.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerReplaceOverlayCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  (void) schedulerState;

  int returnValue = 0;
  if (processMessage == NULL) {
    // This should be impossible, but there's nothing to do.  Print an error
    // and return good status.
    logError("NULL message provided\n");
    return returnValue; // 0
  }

  ProcessDescriptor *processDescriptor = processMessageFrom(processMessage);
  SchedulerReplaceOverlayArgs *schedulerReplaceOverlayArgs
    = (SchedulerReplaceOverlayArgs*) processMessageData(processMessage);
  FileBlockMetadata *overlay = schedulerReplaceOverlayArgs->overlay;
  if (overlay == NULL) {
    // Invalid argument
    schedulerReplaceOverlayArgs->returnValue = -EINVAL;
    processMessageSetDone(processMessage);
    return returnValue; // 0
  }
  if (schedulerReplaceOverlayArgs->overlayNamespace != OVERLAY_SAME_NAMESPACE) {
    processDescriptor->overlayNamespace
       = schedulerReplaceOverlayArgs->overlayNamespace;
  }
  processDescriptor->overlay.blockDevice = overlay->blockDevice;
  processDescriptor->overlay.startBlock  = overlay->startBlock;
  processDescriptor->overlay.numBlocks   = overlay->numBlocks;

  schedulerReplaceOverlayArgs->returnValue = returnValue;
  processMessageSetDone(processMessage);
  return returnValue;
}

/// @fn int schedulerShutdownCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Replace the overlay of a process.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerShutdownCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  (void) schedulerState;

  int returnValue = 0;
  if (processMessage == NULL) {
    // This should be impossible, but there's nothing to do.  Print an error
    // and return good status.
    logError("NULL message provided\n");
    return returnValue; // 0
  }

  SchedulerShutdownArgs *schedulerShutdownArgs
    = (SchedulerShutdownArgs*) processMessageData(processMessage);
  switch (schedulerShutdownArgs->shutdownType) {
    case NANO_OS_SHUTDOWN_OFF:
      {
        returnValue = HAL->power.enterMode(HAL_POWER_MODE_OFF);
      }
      break;
    
    case NANO_OS_SHUTDOWN_HYBERNATE:
      {
        // Store RAM on disk and then power off.
        // TODO: Store RAM on disk.
        returnValue = HAL->power.enterMode(HAL_POWER_MODE_OFF);
      }
      break;
    
    case NANO_OS_SHUTDOWN_SUSPEND:
      {
        returnValue = HAL->power.enterMode(HAL_POWER_MODE_SUSPEND);
      }
      break;
    
    case NANO_OS_SHUTDOWN_RESET:
      {
        returnValue = HAL->power.enterMode(HAL_POWER_MODE_RESET);
      }
      break;
    
    default:
      {
        returnValue = -EINVAL;
      }
      break;
  }

  schedulerShutdownArgs->returnValue = returnValue;
  returnValue = 0;
  processMessageSetDone(processMessage);
  return returnValue;
}

/// @typedef SchedulerCommandHandler
///
/// @brief Signature of command handler for a scheduler command.
typedef int (*SchedulerCommandHandler)(SchedulerState*, ProcessMessage*);

/// @var schedulerCommandHandlers
///
/// @brief Array of function pointers for commands that are understood by the
/// message handler for the main loop function.
KEEP_IN_FLASH
const SchedulerCommandHandler schedulerCommandHandlers[] = {
  schedulerKillProcessCommandHandler,       // SCHEDULER_KILL_PROCESS
  // SCHEDULER_GET_NUM_RUNNING_PROCESSES:
  schedulerGetNumProcessDescriptorsCommandHandler,
  schedulerGetProcessInfoCommandHandler,    // SCHEDULER_GET_PROCESS_INFO
  schedulerSetProcessUserCommandHandler,    // SCHEDULER_SET_PROCESS_USER
  schedulerGetHostnameCommandHandler,       // SCHEDULER_GET_HOSTNAME
  schedulerExecveCommandHandler,            // SCHEDULER_EXECVE
  schedulerSpawnCommandHandler,             // SCHEDULER_SPAWN
  schedulerSendSignalCommandHandler,        // SCHEDULER_SEND_SIGNAL
  schedulerReplaceOverlayCommandHandler,    // SCHEDULER_REPLACE_OVERLAY
  schedulerShutdownCommandHandler,          // SCHEDULER_SHUTDOWN
};

/// @fn void handleSchedulerMessage(SchedulerState *schedulerState)
///
/// @brief Handle one (and only one) message from our message queue.  If
/// handling the message is unsuccessful, the message will be returned to the
/// end of our message queue.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler process.
///
/// @return This function returns no value.
void handleSchedulerMessage(SchedulerState *schedulerState) {
  static int lastReturnValue = 0;
  ProcessMessage *message = processMessageQueuePop();
  if (message != NULL) {
    if ((processMessageType(message) & 0xffffffffffffff00)
      != SCHEDULER_COMMAND_SIGNATURE
    ) {
      logError("Received unknown signature 0x%lx from process %d\n",
        (unsigned long int)
          (processMessageType(message) & 0xffffffffffffff00),
        processPid(processMessageFrom(message)));
      // Don't attempt to process this message further and don't put it back on
      // our message queue.  Just return immediately.
      return;
    }

    SchedulerCommand messageType
      = (SchedulerCommand) (processMessageType(message) & 0xff);
    if (messageType >= NUM_SCHEDULER_COMMANDS) {
      // Invalid.  Purge the message.
      logError("Received invalid message 0x%lx of type %d "
        "from process %d\n",
        (unsigned long int) (uintptr_t) message,
        messageType, processPid(processMessageFrom(message)));
      return;
    }

    int returnValue = schedulerCommandHandlers[messageType](
      schedulerState, message);
    if (returnValue != 0) {
      // Processing the message failed.  We can't release it.  Put it on the
      // back of our own queue again and try again later.
      if (lastReturnValue == 0) {
        // Only print out a message if this is the first time we've failed.
        logError("Scheduler command handler failed for message %d\n"
          "Pushing message back onto our own queue\n", messageType);
      }
      processMessageQueuePush(getRunningProcess(), message);
    }
    lastReturnValue = returnValue;
  }

  return;
}

/// @fn void checkForTimeouts(SchedulerState *schedulerState)
///
/// @brief Check for anything that's timed out on the timedWaiting queue.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler process.
///
/// @return This function returns no value.
void checkForTimeouts(SchedulerState *schedulerState) {
  ProcessQueue *timedWaiting = &schedulerState->timedWaiting;
  uint8_t numElements = timedWaiting->numElements;
  int64_t now = processGetNanoseconds(NULL);

  for (uint8_t ii = 0; ii < numElements; ii++) {
    ProcessDescriptor *poppedDescriptor = processQueuePop(timedWaiting);
    Comutex *blockingComutex
      = poppedDescriptor->mainThread->blockingComutex;
    Cocondition *blockingCocondition
      = poppedDescriptor->mainThread->blockingCocondition;

    if ((blockingComutex != NULL) && (now >= blockingComutex->timeoutTime)) {
      processQueuePush(poppedDescriptor->readyQueue, poppedDescriptor);
      continue;
    } else if ((blockingCocondition != NULL)
      && (now >= blockingCocondition->timeoutTime)
    ) {
      processQueuePush(poppedDescriptor->readyQueue, poppedDescriptor);
      continue;
    }

    processQueuePush(timedWaiting, poppedDescriptor);
  }

  return;
}

/// @fn void forceYield(void)
///
/// @brief Callback that's invoked when the preemption timer fires.  Wrapper
///   for processYield.  Does nothing else.
///
/// @return This function returns no value.
void forceYield(void) {
  processYieldTo(&allProcesses[SCHEDULER_STATE->schedulerPid - 1]);
}

/// @fn int schedulerDumpMemoryAllocations(void)
///
/// @brief Make the memory manager dump metadata about all its outstanding
/// allocations.
///
/// @return Returns 0 on success, -1 on failure.
int schedulerDumpMemoryAllocations(void) {
  int returnValue = 0;
  
  if (schedulerInitSendMessageToPid(
    SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS,
    NULL, 0) != processSuccess
  ) { 
    logError("Could not send message "
      "MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS to memory manager\n");
  }
  
  return returnValue;
}

/// @fn int schedulerDumpOpenFiles(void)
///
/// @brief Make the filesystem process dump metadata about all its open files.
///
/// @return Returns 0 on success, -1 on failure.
int schedulerDumpOpenFiles(void) {
  if (SCHEDULER_STATE->rootFsPid == 0) {
    return -1;
  }

  FilesystemDumpOpenFilesArgs filesystemDumpOpenFilesArgs = {
    .returnValue = 0,
  };
  
  if (schedulerInitSendMessageToPid(
    SCHEDULER_STATE->rootFsPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_DUMP_OPEN_FILES,
    /* data= */ &filesystemDumpOpenFilesArgs,
    /* size= */ sizeof(filesystemDumpOpenFilesArgs)) != processSuccess
  ) { 
    logError("Could not send FILESYSTEM_DUMP_OPEN_FILES message "
      "to root FS process ID %d\n", SCHEDULER_STATE->rootFsPid);
  }
  
  return filesystemDumpOpenFilesArgs.returnValue;
}

/// @fn void removeProcess(
///   ProcessDescriptor *processDescriptor, const char *errorMessage)
///
/// @brief Clean up all of a process's resources so that it can be removed from
/// the scheduler's process queues.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to clean up.
/// @param errorMessage A string containing the message to display to the user
///   to indicate the reason this process is being remoevd.
///
/// @return This function returns no value.
void removeProcess(
  ProcessDescriptor *processDescriptor, const char *errorMessage
) {
  logError("%s\n       Removing process %d from process queues\n",
    errorMessage, processDescriptor->processId);

  processDescriptor->name = NULL;
  processDescriptor->userId = NO_USER_ID;
  processDescriptor->mainThread->state = PROCESS_STATE_NOT_RUNNING;

  ConsoleReleasePidPortArgs consoleReleasePidPortArgs = {
    .processId = processDescriptor->processId,
  };
  if (schedulerInitSendMessageToPid(
    SCHEDULER_STATE->consolePid,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_PID_PORT,
    /* data= */ &consoleReleasePidPortArgs,
    /* size= */ sizeof(consoleReleasePidPortArgs)) != processSuccess
  ) {
    logError("Could not send CONSOLE_RELEASE_PID_PORT message "
      "to console process\n");
  }

  MemoryManagerFreeProcessMemoryArgs memoryManagerFreeProcessMemoryArgs = {
    .pid = processDescriptor->processId,
    .returnValue = 0,
  };
  if (schedulerInitSendMessageToPid(
    SCHEDULER_STATE->memoryManagerPid,
    MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_FREE_PROCESS_MEMORY,
    &memoryManagerFreeProcessMemoryArgs,
    sizeof(memoryManagerFreeProcessMemoryArgs)) != processSuccess
  ) {
    logError("Could not free process memory. Memory leak.\n");
  }

  return;
}

/// @fn int schedulerLoadOverlay(
///   ProcessDescriptor *processDescriptor, char **envp)
///
/// @brief Load and configure an overlay into the overlayMap in memory.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that describes
///   the overlay to load.
/// @param envp The array of environment variables in "name=value" form.
///
/// @return Returns 0 on success, negative error code on failure.
int schedulerLoadOverlay(ProcessDescriptor *processDescriptor, char **envp) {
  if (processDescriptor == NULL) {
    // There's no overlay to load.  This isn't really an error, but there's
    // nothing to do.  Just return 0.
    return 0;
  }

  if (processDescriptor->privilegeLevel != PRIVILEGE_LEVEL_EXECUTIVE) {
    // This is the expected case, so list it first.
    nanoOsApi.executiveApi = NULL;
  } else {
    // Enable the executive API for the process.
    nanoOsApi.executiveApi = &nanoOsExecutiveApi;
  }
  
  if (processDescriptor->overlay.blockDevice == NULL) {
    // This process has no overlay metadata set yet (e.g. a dummy process slot
    // or a runBlockOverlay process before its first self-configuration yield).
    // Nothing to load.
    return 0;
  }

  NanoOsOverlayMap *overlayMap = HAL->memory.overlayMap;
  if ((overlayMap == NULL) || (HAL->memory.overlaySize == 0)) {
    logError("No overlay memory available for use.\n");
    return -ENOMEM;
  }

  NanoOsOverlayHeader *overlayHeader = &overlayMap->header;
  if ((overlayHeader->overlay.blockDevice
      == processDescriptor->overlay.blockDevice)
    && (overlayHeader->overlay.startBlock
      == processDescriptor->overlay.startBlock)
    && (overlayHeader->overlay.numBlocks
      == processDescriptor->overlay.numBlocks)
  ) {
    // Overlay is already loaded.  Do nothing.
    return 0;
  }

  if (processDescriptor->overlay.blockDevice->schedReadBlocks(
    processDescriptor->overlay.blockDevice->context,
    processDescriptor->overlay.startBlock,
    processDescriptor->overlay.numBlocks,
    processDescriptor->overlay.blockDevice->blockSize,
    (uint8_t*) overlayMap) != 0
  ) {
    logError("Could not read overlay\n");
    return -EIO;
  }

  if (overlayMap->header.magic != NANO_OS_OVERLAY_MAGIC) {
    logError("Overlay magic was not \"NanoOsOL\".\n");
    logDebug("Expected 0x%lx\n",
      (unsigned long int) NANO_OS_OVERLAY_MAGIC);
    logDebug("overlayMap->header.osApi = 0x%lx\n",
      (unsigned long int) (uintptr_t) overlayMap->header.osApi);
    logDebug("overlayMap->header.env = 0x%lx\n",
      (unsigned long int) (uintptr_t) overlayMap->header.env);
    logDebug("overlayMap->header.overlay.blockDevice = 0x%lx\n",
      (unsigned long int) (uintptr_t)
        overlayMap->header.overlay.blockDevice);
    logDebug("overlayMap->header.overlay.startBlock = %ld\n",
      (long int) overlayMap->header.overlay.startBlock);
    logDebug("overlayMap->header.overlay.numBlocks = %ld\n",
      (long int) overlayMap->header.overlay.numBlocks);
    logDebug("overlayMap->header.version = 0x%lx\n",
      (unsigned long int) (uintptr_t) overlayMap->header.version);
    logDebug("overlayMap->header.magic = 0x%lx\n",
      (unsigned long int) (uintptr_t) overlayMap->header.magic);
    logDebug("overlayMap->exports = 0x%lx\n",
      (unsigned long int) (uintptr_t) overlayMap->exports);
    logDebug("overlayMap->numExports = 0x%lx\n",
      (unsigned long int) (uintptr_t) overlayMap->numExports);

    return -ENOEXEC;
  }
  if (overlayMap->header.version != NANO_OS_OVERLAY_VERSION) {
    logError("Overlay version is 0x%lx\n",
      (unsigned long int) overlayMap->header.version);
    return -ENOEXEC;
  }

  // Set the pieces of the overlay header that the program needs to run.
  overlayHeader->osApi = NANO_OS_API;
  overlayHeader->osApi->callOverlayFunction
    = processDescriptor->callOverlayFunction;
  overlayHeader->env = envp;
  overlayHeader->overlay.blockDevice = processDescriptor->overlay.blockDevice;
  overlayHeader->overlay.startBlock = processDescriptor->overlay.startBlock;
  overlayHeader->overlay.numBlocks = processDescriptor->overlay.numBlocks;
  
  return 0;
}

/// @fn int schedulerRunOverlayCommand(ProcessDescriptor *processDescriptor,
///   const char *commandPath, int argc, const char **argv, const char **envp)
///
/// @brief Launch a command that's in overlay format on the filesystem.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that will be
///   populated with the overlay command.
/// @param commandPath The full path to the command overlay file on the
///   filesystem.
/// @param argc The number of arguments from the command line.
/// @param argv The of arguments from the command line as an array of C strings.
/// @param envp The array of environment variable strings where each element is
///   in "name=value" form.
///
/// @return Returns 0 on success, -errno on failure.
int schedulerRunOverlayCommand(ProcessDescriptor *processDescriptor,
  char *commandPath, char **argv, char **envp
) {
  int returnValue = 0;

  // Copy over the exec args.
  ExecArgs *execArgs = schedMalloc(sizeof(ExecArgs));
  if (execArgs == NULL) {
    returnValue = -ENOMEM;
    goto exit;
  }
  execArgs->callingPid = processDescriptor->processId;

  execArgs->pathname = (char*) schedMalloc(strlen(commandPath) + 1);
  if (execArgs->pathname == NULL) {
    returnValue = -ENOMEM;
    goto freeExecArgs;
  }
  strcpy(execArgs->pathname, commandPath);

  size_t argvLen = 0;
  for (; argv[argvLen] != NULL; argvLen++);
  argvLen++; // Account for the terminating NULL element
  execArgs->argv = (char**) schedCalloc(1, argvLen * sizeof(char*));
  if (execArgs->argv == NULL) {
    returnValue = -ENOMEM;
    goto freeExecArgs;
  }

  // argvLen is guaranteed to always be at least 1, so it's safe to run to
  // (argvLen - 1) here.
  size_t ii = 0;
  for (; ii < (argvLen - 1); ii++) {
    // We know that argv[ii] isn't NULL because of the calculation for argvLen
    // above, so it's safe to use strlen.
    execArgs->argv[ii] = (char*) schedMalloc(strlen(argv[ii]) + 1);
    if (execArgs->argv[ii] == NULL) {
      returnValue = -ENOMEM;
      goto freeExecArgs;
    }
    strcpy(execArgs->argv[ii], argv[ii]);
  }
  execArgs->argv[ii] = NULL; // NULL-terminate the array

  // There are two possibilities for how this function is called:  Either envp
  // is NULL or it's the envp that already existed for the processDescriptor.
  // So, either there is no envp or it is already the one we should be using for
  // the process.  We do *NOT* want to make a copy of it.  Just assign it
  // direclty here.  The logic below will take care of memory ownership.
  execArgs->envp = envp;

  execArgs->schedulerState = SCHEDULER_STATE;

  if (assignMemory(execArgs, processDescriptor->processId) != 0) {
    logWarn("Could not assign execArgs to exec process.\n"
      "Undefined behavior.\n");
  }

  if (assignMemory(execArgs->pathname, processDescriptor->processId) != 0) {
    logWarn("Could not assign execArgs->pathname to exec process.\n"
      "Undefined behavior.\n");
  }

  if (execArgs->argv != NULL) {
    if (assignMemory(execArgs->argv, processDescriptor->processId) != 0) {
      logWarn("Could not assign argv to exec process.\n"
        "Undefined behavior.\n");
    }

    for (int ii = 0; execArgs->argv[ii] != NULL; ii++) {
      if (assignMemory(execArgs->argv[ii], processDescriptor->processId) != 0) {
        logWarn("Could not assign execArgs->argv[%d] to exec process.\n"
          "Undefined behavior.\n", ii);
      }
    }
  }

  if (execArgs->envp != NULL) {
    if (assignMemory(execArgs->envp, processDescriptor->processId) != 0) {
      logWarn("Could not assign execArgs->envp to exec process.\n"
        "Undefined behavior.\n");
    }

    for (int ii = 0; execArgs->envp[ii] != NULL; ii++) {
      if (assignMemory(execArgs->envp[ii], processDescriptor->processId) != 0) {
        logWarn("Could not assign execArgs->envp[%d] to exec "
          "process\nUndefined behavior\n", ii);
      }
    }
  }

  processDescriptor->numFileDescriptors = NUM_STANDARD_FILE_DESCRIPTORS;
  // Use calloc for processDescriptor->fileDescriptors in case we fail to
  // allocate one of the FileDescriptor pointers later and have to free the
  // elements of the array.  It's safe to pass NULL to free().
  processDescriptor->fileDescriptors = (FileDescriptor**) schedCalloc(1,
    NUM_STANDARD_FILE_DESCRIPTORS * sizeof(FileDescriptor*));
  if (processDescriptor->fileDescriptors == NULL) {
    logError("Could not allocate file descriptor array for new command\n");
    returnValue = -ENOMEM;
    goto freeExecArgs;
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    processDescriptor->fileDescriptors[ii]
      = (FileDescriptor*) schedMalloc(sizeof(FileDescriptor));
    if (processDescriptor->fileDescriptors[ii] == NULL) {
      logError("Could not allocate memory for file descriptor %ld "
        "for new process\n",
        (long int) ii);
      returnValue = -ENOMEM;
      goto freeFileDescriptors;
    }
    memcpy(
      processDescriptor->fileDescriptors[ii],
      &standardUserFileDescriptors[ii],
      sizeof(FileDescriptor)
    );
    processDescriptor->fileDescriptors[ii]->lastOwner
      = processDescriptor->processId;
  }

  if (processCreate(processDescriptor, HAL->platform.execCommand, execArgs)
    == processError
  ) {
    logError("Could not configure process handle for new command\n");
    returnValue = -ENOEXEC;
    goto freeFileDescriptors;
  }

  if (HAL->platform.execCommand == execOverlayCommand) {
    processDescriptor->overlayNamespace = execArgs->pathname;
  }
  returnValue = loadProcessDescriptorOverlayMetadata(processDescriptor);
  if (returnValue == -EBUSY) {
    // We're in the middle of a filesystem operation already and can't access
    // a file right now.  Return error status and try again later.
    return returnValue; // -EBUSY
  } else if (returnValue != 0) {
    goto freeFileDescriptors;
  }
  processDescriptor->envp = execArgs->envp;
  processDescriptor->name = execArgs->argv[0];

  processResume(processDescriptor, NULL);

  return returnValue;

freeFileDescriptors:
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    schedFree(processDescriptor->fileDescriptors[ii]);
  }
  schedFree(processDescriptor->fileDescriptors);

freeExecArgs:
  schedFree(execArgs->pathname);

  if (execArgs->argv != NULL) {
    for (int ii = 0; execArgs->argv[ii] != NULL; ii++) {
      schedFree(execArgs->argv[ii]);
    }
    schedFree(execArgs->argv);
  }

  if (execArgs->envp != NULL) {
    logInfo("Freeing execArgs->envp = 0x%lx\n",
      (unsigned long int) (uintptr_t) processDescriptor->envp);
    for (int ii = 0; execArgs->envp[ii] != NULL; ii++) {
      schedFree(execArgs->envp[ii]);
    }
    schedFree(execArgs->envp);
  }

  // We don't need to and SHOULD NOT touch execArgs->schedulerState.

  schedFree(execArgs);

exit:
  return returnValue;
}

/// @var _gettyPath
///
/// @brief Path to the getty overlay command run for a login shell.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _gettyPath[] KEEP_IN_FLASH = "/usr/bin/getty";

/// @var _gettyName
///
/// @brief argv[0] used to launch the getty process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _gettyName[] KEEP_IN_FLASH = "getty";

/// @var _gettyArgs
///
/// @brief Command line arguments used to launch the getty process.  These have
/// to be declared global because they're referenced by the launched process on
/// its own stack.
static const char *_gettyArgs[] = {
  _gettyName,
  NULL,
};

/// @var _loggerPath
///
/// @brief Path to the logger overlay command on the filesystem.
static const char _loggerPath[] KEEP_IN_FLASH = "/usr/bin/logger";

/// @var _loggerName
///
/// @brief argv[0] used to launch the logger process.
static const char _loggerName[] KEEP_IN_FLASH = "logger";

/// @var _loggerData
///
/// @brief Path to the data file the logger needs to use.  This will be argv[1]
/// from within the logger process.
static const char _loggerData[] KEEP_IN_FLASH
  = "/usr/lib/nano-os-sim_rodata.bin";

/// @var _loggerArgs
///
/// @brief Command line arguments used to launch the logger process.
static const char *_loggerArgs[] = {
  _loggerName,
  _loggerData,
  NULL,
};

/// @var shellArgs
///
/// @brief Command line arguments used to launch the user's shell process.
/// These have to be declared global because they're referenced by the launched
/// process on its own stack.
static const char *shellArgs[] = {
  NULL, // argv[0], set by runScheduler
  NULL,
};

/// @var _consoleName
///
/// @brief Process name assigned to the console process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _consoleName[] KEEP_IN_FLASH = "console";

/// @var _memoryManagerName
///
/// @brief Process name assigned to the memory manager process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _memoryManagerName[] KEEP_IN_FLASH = "memory manager";

/// @var _shellName
///
/// @brief Process name assigned to a built-in shell process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _shellName[] KEEP_IN_FLASH = "shell";

/// @var _initName
///
/// @brief Process name assigned to the scheduler's own process slot.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _initName[] KEEP_IN_FLASH = "init";

/// @var _dummyName
///
/// @brief Process name assigned to an as-yet-unused process slot.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _dummyName[] KEEP_IN_FLASH = "dummy";

/// @fn int32_t restartConsole(ProcessDescriptor *processDescriptor)
///
/// @brief Implementation of restartFunction to re-launch the console process if
/// it dies.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that manages the
///   process's state.
///
/// @return Returns 0 on sucess, -errno onfailure.
int32_t restartConsole(ProcessDescriptor *processDescriptor) {
  logError("Console process not running; Restarting \n");
  uint64_t *consoleStackEnd = threadStackEnd(processDescriptor->mainThread);
  *consoleStackEnd = THREAD_STACK_END_VALUE;
  if (processCreate(processDescriptor, runConsole, NULL) != processSuccess) {
    logError("Could not restart console process.\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  threadSetStackEnd(processDescriptor->mainThread, consoleStackEnd);
  processDescriptor->name = _consoleName;
  processDescriptor->userId = ROOT_USER_ID;
  return 0;
}

/// @fn int32_t restartMemoryManager(ProcessDescriptor *processDescriptor)
///
/// @brief Implementation of restartFunction to re-launch the memory manager
/// process if it dies.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that manages the
///   process's state.
///
/// @return Returns 0 on sucess, -errno onfailure.
int32_t restartMemoryManager(ProcessDescriptor *processDescriptor) {
  logError("Memory manager process not running; Halting\n");
  while (1);
  if (processCreate(processDescriptor, runMemoryManager, NULL)
    != processSuccess
  ) {
    logError("Could not restart memory manager process.\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = _memoryManagerName;
  processDescriptor->userId = ROOT_USER_ID;
  return 0;
}

/// @fn int32_t restartBuiltinShell(ProcessDescriptor *processDescriptor)
///
/// @brief Implementation of restartFunction to re-launch a built-in shell if
/// one dies or a process that occupied its slot exits.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that manages the
///   process's state.
///
/// @return Returns 0 on sucess, -errno onfailure.
int32_t restartBuiltinShell(ProcessDescriptor *processDescriptor) {
  logDebug("In restartBuiltinShell\n");
  if ((SCHEDULER_STATE->hostname == NULL)
    || (*SCHEDULER_STATE->hostname == '\0')
  ) {
    logDebug(
      "restartBuiltinShell: scheduler not up.  Returning -EAGAIN\n");
    return -EAGAIN;
  }

  // Set the capabilities for the shell on the console.
  addProcessIpcCapability(
    &SCHEDULER_STATE->allProcesses[SCHEDULER_STATE->consolePid - 1],
    processDescriptor->processId, CONSOLE_COMMAND_SIGNATURE,
    CONSOLE_RETURNING_INPUT);

  int returnValue = 0;
  processDescriptor->numFileDescriptors = NUM_STANDARD_FILE_DESCRIPTORS;
  // Use calloc for processDescriptor->fileDescriptors in case we fail to
  // allocate one of the FileDescriptor pointers later and have to free the
  // elements of the array.  It's safe to pass NULL to free().
  processDescriptor->fileDescriptors = (FileDescriptor**) schedCalloc(1,
    NUM_STANDARD_FILE_DESCRIPTORS * sizeof(FileDescriptor*));
  if (processDescriptor->fileDescriptors == NULL) {
    logError("Could not allocate file descriptor array for new command\n");
    returnValue = -ENOMEM;
    goto exit;
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    processDescriptor->fileDescriptors[ii]
      = (FileDescriptor*) schedMalloc(sizeof(FileDescriptor));
    if (processDescriptor->fileDescriptors[ii] == NULL) {
      logError("Could not allocate memory for file descriptor %d "
        "for new process\n", ii);
      returnValue = -ENOMEM;
      goto freeFileDescriptors;
    }
    memcpy(
      processDescriptor->fileDescriptors[ii],
      &standardUserFileDescriptors[ii],
      sizeof(FileDescriptor)
    );
    processDescriptor->fileDescriptors[ii]->lastOwner
      = processDescriptor->processId;
  }

  // User process exited.  Re-launch the shell.
  logDebug("restartBuiltinShell: Restarting shell\n");
  if (processCreate(processDescriptor, runBuiltinShell, NULL)
    != processSuccess
  ) {
    logError("Could not restart memory manager process.\n");
    returnValue = -ENOMEM;
    goto freeFileDescriptors;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = _shellName;

  return returnValue;

freeFileDescriptors:
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    schedFree(processDescriptor->fileDescriptors[ii]);
  }
  schedFree(processDescriptor->fileDescriptors);

exit:
  return returnValue;
}

/// @fn int32_t restartOverlayShell(ProcessDescriptor *processDescriptor)
///
/// @brief Implementation of restartFunction to re-launch a shell if one dies or
/// a process that occupied its slot exits.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that manages the
///   process's state.
///
/// @return Returns 0 on sucess, -errno onfailure.
int32_t restartOverlayShell(ProcessDescriptor *processDescriptor) {
  logDebug("In restartOverlayShell\n");
  if ((SCHEDULER_STATE->hostname == NULL)
    || (*SCHEDULER_STATE->hostname == '\0')
  ) {
    logDebug("Scheduler not up.  Returning -EAGAIN\n");
    return -EAGAIN;
  }

  // Set the capabilities for the shell on the console.
  addProcessIpcCapability(&allProcesses[SCHEDULER_STATE->consolePid - 1],
    processDescriptor->processId, CONSOLE_COMMAND_SIGNATURE,
    CONSOLE_RETURNING_INPUT);

  if (processDescriptor->userId == NO_USER_ID) {
    if (processDescriptor->envp != NULL) {
      for (int ii = 0; processDescriptor->envp[ii] != NULL; ii++) {
        schedFree(processDescriptor->envp[ii]);
      }
      schedFree(processDescriptor->envp);
      processDescriptor->envp = NULL;
    }

    logDebug("Starting getty\n");
    int returnValue = schedulerRunOverlayCommand(processDescriptor,
      (char*) _gettyPath, (char**) _gettyArgs, NULL);
    if (returnValue == -EBUSY) {
      logDebug(
        "Starting getty failed.  Returning -EAGAIN\n");
      return -EAGAIN;
    }
    return returnValue;
  }

  // User process exited.  Re-launch the shell.
  logDebug("Restarting shell\n");
  int returnValue = 0;
  char *passwdStringBuffer
    = (char*) schedMalloc(NANO_OS_PASSWD_STRING_BUF_SIZE);
  if (passwdStringBuffer == NULL) {
    logError("Could not allocate space for passwdStringBuffer\n");
    return -ENOMEM;
  }

  struct passwd *pwd = (struct passwd*) schedMalloc(sizeof(struct passwd));
  if (pwd == NULL) {
    logError("Could not allocate space for pwd\n");
    schedFree(passwdStringBuffer);
    return -ENOMEM;
  }

  do {
    struct passwd *result = NULL;
    nanoOsGetpwuid_r(processDescriptor->userId, pwd,
      passwdStringBuffer, NANO_OS_PASSWD_STRING_BUF_SIZE, &result);
    if (result == NULL) {
      logError("Could not find passwd info for uid %ld\n",
        (long int) processDescriptor->userId);
      returnValue = -ENOENT;
      break;
    }

    shellArgs[0] = strrchr(pwd->pw_shell, '/') + 1;
    returnValue = schedulerRunOverlayCommand(processDescriptor,
      pwd->pw_shell, (char**) shellArgs, processDescriptor->envp);
    if (returnValue == -EBUSY) {
      returnValue = -EAGAIN;
    } else if (returnValue != 0) {
      if (processDescriptor->envp != NULL) {
        for (int ii = 0; processDescriptor->envp[ii] != NULL; ii++) {
          schedFree(processDescriptor->envp[ii]);
        }
        schedFree(processDescriptor->envp);
        processDescriptor->envp = NULL;
      }
    }
  } while (0);

  schedFree(pwd);
  schedFree(passwdStringBuffer);
  return returnValue;
}

/// @fn int32_t restartLogger(ProcessDescriptor *processDescriptor)
///
/// @brief Implementation of restartFunction to re-launch the logger if it dies.
///
/// @param processDescriptor A pointer to the ProcessDescriptor that manages the
///   process's state.
///
/// @return Returns 0 on sucess, -errno onfailure.
int32_t restartLogger(ProcessDescriptor *processDescriptor) {
  logDebug("In restartLogger\n");
  if ((SCHEDULER_STATE->hostname == NULL)
    || (*SCHEDULER_STATE->hostname == '\0')
  ) {
    logError("Scheduler not up.  Returning -EAGAIN\n");
    return -EAGAIN;
  }

  logDebug("Starting logger\n");
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  processDescriptor->halCapabilities = baseExecutiveHalCapabilities;
  processDescriptor->readyQueue
    = &SCHEDULER_STATE->ready[processDescriptor->privilegeLevel];
  int returnValue = schedulerRunOverlayCommand(processDescriptor,
    (char*) _loggerPath, (char**) _loggerArgs, NULL);
  if (returnValue == -EBUSY) {
    logError("Starting logger failed.  Returning -EAGAIN\n");
    return -EAGAIN;
  } else if (returnValue < 0) {
    logError("Starting logger returned status: %s\n", strerror(-returnValue));
    return -EAGAIN;
  }

  return 0;
}

/// @fn void runScheduler(void)
///
/// @brief Run one (1) iteration of the main scheduler loop.
///
/// @return This function returns no value.
void runScheduler(void) {
  if (processStackOverflowed(
    &allProcesses[SCHEDULER_STATE->schedulerPid - 1])
  ) {
    logError("Scheduler stack overflow detected");
    HAL->power.enterMode(HAL_POWER_MODE_OFF);
  }

  ProcessDescriptor *processDescriptor
    = processQueuePop(SCHEDULER_STATE->currentReady);
  if (processDescriptor == NULL) {
    // Nothing we can do.
    logError("No processes to pop in %s process queue\n",
      SCHEDULER_STATE->currentReady->name);
    goto exit;
  }

  if (processCorrupted(processDescriptor)) {
    removeProcess(processDescriptor, _processCorruptionReason);
    goto exit;
  }

  if (processDescriptor->privilegeLevel != PRIVILEGE_LEVEL_KERNEL) {
    if (processRunning(processDescriptor) == true) {
      // This is a non-kernel process running from an overlay.  Make sure it's
      // loaded.
      if (schedulerLoadOverlay(
        processDescriptor,
        processDescriptor->envp) != 0
      ) {
        schedulerDumpMemoryAllocations();
        schedulerDumpOpenFiles();
        removeProcess(processDescriptor, _overlayLoadFailureReason);
        goto exit;
      }
    }

    if (processDescriptor->privilegeLevel > PRIVILEGE_LEVEL_EXECUTIVE) {
      // Configure the preemption timer to force the process to yield if it
      // doesn't voluntarily give up control within a reasonable amount of time.
      if (SCHEDULER_STATE->preemptionTimer > -1) {
        HAL->timer.configOneShot(
          SCHEDULER_STATE->preemptionTimer, 10000000, forceYield);
      }
    }
  }
  processResume(processDescriptor, NULL);
  // No need to call HAL->timer.cancel since that's called by
  // yieldCallback if we're running preemptive multiprocessing.

  if (processStackOverflowed(processDescriptor)) {
    logError("Process %d's stack overflowed\n", processDescriptor->processId);
    processTerminate(processDescriptor, false);
    threadSetContext(processDescriptor->mainThread, processDescriptor);
    memset(&processDescriptor->message, 0, sizeof(ProcessMessage));
    processResetStack(processDescriptor);
  }

  if (processRunning(processDescriptor) == false) {
    if (processDescriptor->envp != NULL) {
      if (assignMemory(processDescriptor->envp, 0) != 0) {
        logWarn("Could not protect envp memory from process %d\n"
          "Undefined behavior\n",
          processDescriptor->processId);
      }

      for (int ii = 0; processDescriptor->envp[ii] != NULL; ii++) {
        if (assignMemory(processDescriptor->envp[ii], 0) != 0) {
          logWarn("Could not protect envp[%d] memory from process %d\n"
            "Undefined behavior\n", ii, processDescriptor->processId);
        }
      }
    }

    int returnValue = closeProcessFileDescriptors(processDescriptor);
    if (returnValue == -EBUSY) {
      processQueuePush(SCHEDULER_STATE->currentReady, processDescriptor);
      // DON'T goto exit.  We're in the middle of a loop inside
      // closeProcessFileDescriptors, so just return immediately.
      return;
    }

    MemoryManagerFreeProcessMemoryArgs memoryManagerFreeProcessMemoryArgs = {
      .pid = processDescriptor->processId,
      .returnValue = 0,
    };
    if (schedulerInitSendMessageToPid(
      SCHEDULER_STATE->memoryManagerPid,
      MEMORY_MANAGER_COMMAND_SIGNATURE | MEMORY_MANAGER_FREE_PROCESS_MEMORY,
      &memoryManagerFreeProcessMemoryArgs,
      sizeof(memoryManagerFreeProcessMemoryArgs)) != processSuccess
    ) {
      logError("Could not send MEMORY_MANAGER_FREE_PROCESS_MEMORY "
        "message to memory manager\n");
    }

    // Terminate the process so that any lingering messages in its message queue
    // get released.  Set the second parameter to false to make sure that
    // happens.
    processTerminate(processDescriptor, false);
    threadSetContext(processDescriptor->mainThread, processDescriptor);
    memset(&processDescriptor->message, 0, sizeof(ProcessMessage));

    if (processDescriptor->restartFunction != NULL) {
      logDebug("Process %ld has exited.  Restarting.\n",
        (long int) processDescriptor->processId);
      int returnValue = processDescriptor->restartFunction(processDescriptor);
      if (returnValue == -EAGAIN) {
        logDebug("processDescriptor->restartFunction returned -EAGAIN\n");
        processQueuePush(SCHEDULER_STATE->currentReady, processDescriptor);
        goto exit;
      } else if (returnValue != 0) {
        removeProcess(processDescriptor, _processRestartFailedReason);
        goto exit;
      }
    } else {
      if (processDescriptor->envp != NULL) {
        for (int ii = 0; processDescriptor->envp[ii] != NULL; ii++) {
          schedFree(processDescriptor->envp[ii]);
        }
        schedFree(processDescriptor->envp);
        processDescriptor->envp = NULL;
      }
    }
  }

  if (processState(processDescriptor) == PROCESS_STATE_WAIT) {
    processQueuePush(&SCHEDULER_STATE->waiting, processDescriptor);
  } else if (processState(processDescriptor) == PROCESS_STATE_TIMEDWAIT) {
    processQueuePush(&SCHEDULER_STATE->timedWaiting, processDescriptor);
  } else if (processFinished(processDescriptor)) {
    processQueuePush(&SCHEDULER_STATE->free, processDescriptor);
  } else { // Process is still running.
    processQueuePush(SCHEDULER_STATE->currentReady, processDescriptor);
  }

exit:
  checkForTimeouts(SCHEDULER_STATE);
  handleSchedulerMessage(SCHEDULER_STATE);

  return;
}

/// @var _kernelReadyName
///
/// @brief Name of the kernel-privilege-level ready queue.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _kernelReadyName[] KEEP_IN_FLASH = "kernel ready";

/// @var _executiveReadyName
///
/// @brief Name of the executive-privilege-level ready queue.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _executiveReadyName[] KEEP_IN_FLASH = "executive ready";

/// @var _supervisorReadyName
///
/// @brief Name of the supervisor-privilege-level ready queue.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _supervisorReadyName[] KEEP_IN_FLASH = "supervisor ready";

/// @var _userReadyName
///
/// @brief Name of the user-privilege-level ready queue.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _userReadyName[] KEEP_IN_FLASH = "user ready";

/// @var _waitingName
///
/// @brief Name of the queue holding processes blocked waiting indefinitely.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _waitingName[] KEEP_IN_FLASH = "waiting";

/// @var _timedWaitingName
///
/// @brief Name of the queue holding processes blocked with a timeout.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _timedWaitingName[] KEEP_IN_FLASH = "timed waiting";

/// @var _freeName
///
/// @brief Name of the queue holding free process slots.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _freeName[] KEEP_IN_FLASH = "free";

/// @var _hostnameFilePath
///
/// @brief Path to the file on the filesystem that stores the system
/// hostname.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _hostnameFilePath[] KEEP_IN_FLASH = "/etc/hostname";

/// @var _localhost
///
/// @brief Fallback hostname used when no hostname file is available.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _localhost[] KEEP_IN_FLASH = "localhost";

/// @fn int initializeSchedulerState(
///   SchedulerState *schedulerState, SchedulerState **threadStatePointer)
///
/// @brief Initialize the base SchedulerState member variables.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   startScheduler function.
/// @param threadStatePointer The double-pointer to a SchedulerState passed into
///   the startScheduler function.
/// @param messagesStorage The pointer to an array of ProcessMessages that will
///   hold the static pool of messages that can be used by processes.
///
/// @return Returns 0 on success, -errno on failure.
int initializeSchedulerState(
  SchedulerState *schedulerState, SchedulerState **threadStatePointer,
  ProcessMessage *messagesStorage
) {
  schedulerState->hostname = NULL;
  schedulerState->ready[PRIVILEGE_LEVEL_KERNEL].name = _kernelReadyName;
  schedulerState->ready[PRIVILEGE_LEVEL_EXECUTIVE].name = _executiveReadyName;
  schedulerState->ready[PRIVILEGE_LEVEL_SUPERVISOR].name = _supervisorReadyName;
  schedulerState->ready[PRIVILEGE_LEVEL_USER].name = _userReadyName;
  schedulerState->waiting.name = _waitingName;
  schedulerState->timedWaiting.name = _timedWaitingName;
  schedulerState->free.name = _freeName;
  schedulerState->currentReady
    = &schedulerState->ready[PRIVILEGE_LEVEL_KERNEL];
  schedulerState->preemptionTimer = -1;
  if (HAL->timer.numSupported > 0) {
    for (int32_t ii = 0; ii < ((int32_t) HAL->timer.numSupported); ii++) {
      if (online(HAL->timer, ii)) {
        schedulerState->preemptionTimer = ii;
#ifdef NANO_OS_DEBUG
        baseUserHalCapabilities[1].deviceIds = 1 << ii;
#else
        baseUserHalCapabilities[0].deviceIds = 1 << ii;
#endif // NANO_OS_DEBUG
        break;
      }
    }
  }
  schedulerState->schedulerPid = 1;
  schedulerState->consolePid = 2;
  schedulerState->memoryManagerPid = 3;
  schedulerState->firstUserPid = 4;
  schedulerState->firstShellPid = 4;
  schedulerState->rootFsPid = 0; // Invalid PID
  schedulerState->loggerPid = 0; // Invalid PID
  schedulerState->runSchedulerQueues = runSchedulerQueues;
  SCHEDULER_STATE = schedulerState;
  logDebug("Set scheduler state.\n");

  // Initialize the pointer that was used to configure threads.
  *threadStatePointer = schedulerState;

  // Initialize the static ProcessMessage storage.
  extern ProcessMessage *messages;
  messages = messagesStorage;
  logDebug("Allocated messages storage.\n");

  // Initialize the allProcesses pointer.  The processes are all zeroed because
  // we zeroed the entire schedulerState when we declared it.
  allProcesses = schedulerState->allProcesses;

  // Initialize the scheduler in the array of running commands.
  allProcesses[schedulerState->schedulerPid - 1].mainThread = schedulerThread;
  allProcesses[schedulerState->schedulerPid - 1].processId
    = schedulerState->schedulerPid;
  allProcesses[schedulerState->schedulerPid - 1].name = _initName;
  allProcesses[schedulerState->schedulerPid - 1].userId = ROOT_USER_ID;
  allProcesses[schedulerState->schedulerPid - 1].privilegeLevel
    = PRIVILEGE_LEVEL_KERNEL;
  threadSetContext(allProcesses[schedulerState->schedulerPid - 1].mainThread,
    &allProcesses[schedulerState->schedulerPid - 1]);
  logDebug("Configured scheduler process.\n");

  // Initialize the global file descriptors.
  // Kernel stdin file descriptor doesn't need an update because they don't
  // receive stdin.  Direct kernel process stdout and stderr to the console.
  standardKernelFileDescriptors[1].outputChannel.pid
    = schedulerState->consolePid;
  standardKernelFileDescriptors[1].outputChannel.messageType
    = CONSOLE_COMMAND_SIGNATURE | CONSOLE_WRITE_BUFFER;
  standardKernelFileDescriptors[2].outputChannel.pid
    = schedulerState->consolePid;
  standardKernelFileDescriptors[2].outputChannel.messageType
    = CONSOLE_COMMAND_SIGNATURE | CONSOLE_WRITE_BUFFER;

  // Direct the input pipe of user process stdin to the console.  Direcdt the
  // output pipes of user process stdout and stderr to the console as well.
  standardUserFileDescriptors[0].inputChannel.pid
    = schedulerState->consolePid;
  standardUserFileDescriptors[0].inputChannel.messageType
    = CONSOLE_COMMAND_SIGNATURE | CONSOLE_WAIT_FOR_INPUT;
  standardUserFileDescriptors[1].outputChannel.pid
    = schedulerState->consolePid;
  standardUserFileDescriptors[1].outputChannel.messageType
    = CONSOLE_COMMAND_SIGNATURE | CONSOLE_WRITE_BUFFER;
  standardUserFileDescriptors[2].outputChannel.pid
    = schedulerState->consolePid;
  standardUserFileDescriptors[2].outputChannel.messageType
    = CONSOLE_COMMAND_SIGNATURE | CONSOLE_WRITE_BUFFER;

  return 0;
}

/// @fn int setIpcCapabilities(SchedulerState *schedulerState)
///
/// @brief Fix all the PIDs in the built-in IPC capabilities once all the
/// processes are up.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   startScheduler function.
///
/// @return Returns 0 on success, -errno on failure.
int setIpcCapabilities(SchedulerState *schedulerState) {
  // Fix the IPC capabilities now that all the core processes are started.
  if (schedulerState->rootFsPid > 0) {
    baseSchedulerIpcCapabilities[0].destinationPid
      = schedulerState->rootFsPid - 1;
  }
  baseSchedulerIpcCapabilities[1].destinationPid
    = schedulerState->loggerPid;
  baseConsoleIpcCapabilities[0].destinationPid
    = schedulerState->schedulerPid;
  baseConsoleIpcCapabilities[1].destinationPid
    = schedulerState->memoryManagerPid;
  baseMemoryManagerIpcCapabilities[0].destinationPid
    = schedulerState->consolePid;
  baseFilesystemIpcCapabilities[0].destinationPid
    = schedulerState->schedulerPid;
  baseFilesystemIpcCapabilities[1].destinationPid
    = schedulerState->consolePid;
  baseFilesystemIpcCapabilities[2].destinationPid
    = schedulerState->memoryManagerPid;
  if (schedulerState->rootFsPid > 0) {
    baseFilesystemIpcCapabilities[3].destinationPid
      = schedulerState->rootFsPid - 1;
  }
  baseLoggerIpcCapabilities[0].destinationPid
    = schedulerState->schedulerPid;
  baseLoggerIpcCapabilities[1].destinationPid
    = schedulerState->memoryManagerPid;
  if (schedulerState->rootFsPid > 0) {
    baseLoggerIpcCapabilities[2].destinationPid
      = schedulerState->rootFsPid;
  }

  baseSupervisorIpcCapabilities[0].destinationPid
    = schedulerState->schedulerPid;
  baseSupervisorIpcCapabilities[1].destinationPid
    = schedulerState->consolePid;
  baseSupervisorIpcCapabilities[2].destinationPid
    = schedulerState->memoryManagerPid;
  if (schedulerState->rootFsPid > 0) {
    baseSupervisorIpcCapabilities[3].destinationPid
      = schedulerState->rootFsPid;
  }
  baseUserIpcCapabilities[0].destinationPid
    = schedulerState->schedulerPid;
  baseUserIpcCapabilities[1].destinationPid
    = schedulerState->consolePid;
  baseUserIpcCapabilities[2].destinationPid
    = schedulerState->memoryManagerPid;
  if (schedulerState->rootFsPid > 0) {
    baseUserIpcCapabilities[3].destinationPid
      = schedulerState->rootFsPid;
  }

  // Set the HAL capabilities for all of the processes.
  ProcessDescriptor *processDescriptor = NULL;
  for (ProcessId ii = 1; ii <= NANO_OS_NUM_PROCESSES; ii++) {
    processDescriptor = &allProcesses[ii - 1];
    if (processDescriptor->privilegeLevel == PRIVILEGE_LEVEL_KERNEL) {
      continue;
    } else if (processDescriptor->privilegeLevel == PRIVILEGE_LEVEL_EXECUTIVE) {
      processDescriptor->halCapabilities = baseExecutiveHalCapabilities;
      processDescriptor->numHalCapabilities
        = sizeof(baseExecutiveHalCapabilities)
        / sizeof(baseExecutiveHalCapabilities[0]);
    } else {
      processDescriptor->halCapabilities = baseUserHalCapabilities;
      processDescriptor->numHalCapabilities
        = sizeof(baseUserHalCapabilities)
        / sizeof(baseUserHalCapabilities[0]);
    }
  }

  allProcesses[schedulerState->schedulerPid - 1].ipcCapabilities
    = baseSchedulerIpcCapabilities;
  allProcesses[schedulerState->schedulerPid - 1].numIpcCapabilities
    = sizeof(baseSchedulerIpcCapabilities)
    / sizeof(baseSchedulerIpcCapabilities[0]);
  allProcesses[schedulerState->schedulerPid - 1].ipcCapabilitiesDynamic
    = false;
  allProcesses[schedulerState->consolePid - 1].ipcCapabilities
    = baseConsoleIpcCapabilities;
  allProcesses[schedulerState->consolePid - 1].numIpcCapabilities
    = sizeof(baseConsoleIpcCapabilities)
    / sizeof(baseConsoleIpcCapabilities[0]);
  allProcesses[schedulerState->consolePid - 1].ipcCapabilitiesDynamic
    = false;
  allProcesses[schedulerState->memoryManagerPid - 1].ipcCapabilities
    = baseMemoryManagerIpcCapabilities;
  allProcesses[schedulerState->memoryManagerPid - 1].numIpcCapabilities
    = sizeof(baseMemoryManagerIpcCapabilities)
    / sizeof(baseMemoryManagerIpcCapabilities[0]);
  allProcesses[schedulerState->memoryManagerPid - 1].ipcCapabilitiesDynamic
    = false;
  if (schedulerState->rootFsPid > 0) {
    allProcesses[schedulerState->rootFsPid - 1].ipcCapabilities
      = baseFilesystemIpcCapabilities;
    allProcesses[schedulerState->rootFsPid - 1].numIpcCapabilities
      = sizeof(baseFilesystemIpcCapabilities)
      / sizeof(baseFilesystemIpcCapabilities[0]);
    allProcesses[schedulerState->rootFsPid - 1].ipcCapabilitiesDynamic
      = false;
  }
  if (schedulerState->loggerPid > 0) {
    allProcesses[schedulerState->loggerPid - 1].ipcCapabilities
      = baseLoggerIpcCapabilities;
    allProcesses[schedulerState->loggerPid - 1].numIpcCapabilities
      = sizeof(baseLoggerIpcCapabilities)
      / sizeof(baseLoggerIpcCapabilities[0]);
    allProcesses[schedulerState->loggerPid - 1].ipcCapabilitiesDynamic
      = false;
  }
  for (ProcessId ii = schedulerState->firstUserPid;
    ii <= NANO_OS_NUM_PROCESSES;
    ii++
  ) {
    processDescriptor = &allProcesses[ii - 1];
    if (processDescriptor->privilegeLevel == PRIVILEGE_LEVEL_SUPERVISOR) {
      allProcesses[ii - 1].ipcCapabilities
        = baseSupervisorIpcCapabilities;
      allProcesses[ii - 1].numIpcCapabilities
        = sizeof(baseSupervisorIpcCapabilities)
        / sizeof(baseSupervisorIpcCapabilities[0]);
      allProcesses[ii - 1].ipcCapabilitiesDynamic
        = false;
    } else if (processDescriptor->privilegeLevel == PRIVILEGE_LEVEL_USER) {
      allProcesses[ii - 1].ipcCapabilities
        = baseUserIpcCapabilities;
      allProcesses[ii - 1].numIpcCapabilities
        = sizeof(baseUserIpcCapabilities)
        / sizeof(baseUserIpcCapabilities[0]);
      allProcesses[ii - 1].ipcCapabilitiesDynamic
        = false;
    }
  }

  return 0;
}

/// @fn int initializeProcesses(SchedulerState *schedulerState)
///
/// @brief Initialize all the processes and their state information.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   startScheduler function.
///
/// @return Returns 0 on success, -errno on failure.
int initializeProcesses(SchedulerState *schedulerState) {
  // Create the console process.  We used to have to double the size of the
  // console's stack, so we create this process before we create anything else.
  // Leaving it at this point of initialization in case we ever have to come
  // back to that flow again.
  logDebug("Creating console process.\n");
  ProcessDescriptor *processDescriptor
    = &allProcesses[schedulerState->consolePid - 1];
  if (processCreate(processDescriptor, runConsole, NULL) != processSuccess) {
    logError("Could not create console process.\n");
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = schedulerState->consolePid;
  processDescriptor->name = _consoleName;
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_KERNEL;
  processDescriptor->restartFunction = restartConsole;
  logDebug("Created console process.\n");

  uint8_t numExtraConsoleStacksVal = 0;
  HAL->memory.numExtraConsoleStacks(
    USE_HAL_MEMORY_DEBUG, &numExtraConsoleStacksVal);
  for (uint8_t ii = 0; ii < numExtraConsoleStacksVal; ii++) {
    Thread *thread = threadProvision(NULL, dummyProcess, NULL);
    if (thread == NULL) {
      logError("Could not increase console process's stack size.\n");
      break;
    }
    if (threadSetStackEnd(
      processDescriptor->mainThread, threadStackEnd(thread)) != processSuccess
    ) {
      logError("Could not set console process's stack size.\n");
    }
  }

  // Start the console by calling processResume.
  processResume(&allProcesses[schedulerState->consolePid - 1], NULL);
  logDebug("Started console process.\n");
  // Put the console process on the ready queue.
  allProcesses[schedulerState->consolePid - 1].readyQueue
    = &schedulerState->ready[PRIVILEGE_LEVEL_KERNEL];
  processQueuePush(allProcesses[schedulerState->consolePid - 1].readyQueue,
    &allProcesses[schedulerState->consolePid - 1]);

  // schedulerState->firstUserPid isn't populated until HAL->initRootStorage
  // completes, so we need to call that as soon as we can.
  int rv = 0;
  if (HAL->platform.initRootStorage != NULL) {
    rv = HAL->platform.initRootStorage();
    if (rv != 0) {
      logError("initRootStorage returned status %d\n", rv);
    }
  }
  logDebug("Initialized root storage\n");

  schedulerState->loggerPid = schedulerState->firstUserPid;
  processDescriptor = &allProcesses[schedulerState->loggerPid - 1];
  if (processCreate(processDescriptor, dummyProcess, NULL) != processSuccess) {
    logError("Could not create logger process\n");
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = schedulerState->loggerPid;
  processDescriptor->userId = NO_USER_ID;
  processDescriptor->name = _loggerName;
  processDescriptor->callOverlayFunction = HAL->platform.callFileOverlay;
  // The logger is an executive process, but we're going to start it in
  // supervisor mode until the system comes up far enough to launch it.
  // restartLogger will take care of fixing the level once it launches
  // successfully.
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
  processDescriptor->restartFunction = restartLogger;
  logDebug("Initialized logger process\n");

  schedulerState->firstUserPid++;
  schedulerState->firstShellPid = schedulerState->firstUserPid;

  // Get the number of shells we'll be managing.  This needs to be done before
  // assigning processes to their ready queues and before we create the user
  // processes.
  schedulerState->numShells = schedulerGetNumConsolePorts();
  if (schedulerState->numShells <= 0) {
    // This should be impossible since the HAL was successfully initialized,
    // but take no chances.
    logError("No console ports running.\nHalting.\n");
    while(1);
  }
  // Irrespective of how many ports the console may be running, we can't run
  // more shell processes than what we're configured for.  Make sure we set a
  // sensible limit.
  schedulerState->numShells
    = MIN(schedulerState->numShells, NANO_OS_MAX_NUM_SHELLS);
  logDebug("Managing %ld shells\n", (long int) schedulerState->numShells);

  // Set the shells for the ports.
  for (uint8_t ii = 0; ii < schedulerState->numShells; ii++) {
    if (schedulerSetPortShell(ii, schedulerState->firstShellPid + ii)
      != processSuccess
    ) {
      logWarn("Could not set port for shell %d\n"
        "Undefined behavior will result.\n", ii);
    }
  }
  logDebug("Set shells for ports.\n");

  // We need to do an initial population of all the processes because we need to
  // get to the end of memory to run the memory manager in whatever is left
  // over.  The scheduler will take care of cleaning up the dummy processes
  // after they exit.
  for (ProcessId ii = schedulerState->firstUserPid;
    ii <= NANO_OS_NUM_PROCESSES;
    ii++
  ) {
    processDescriptor = &allProcesses[ii - 1];
    if (processCreate(processDescriptor,
      dummyProcess, NULL) != processSuccess
    ) {
      logError("Could not create process %ld\n", (long int) ii);
    }
    threadSetContext(processDescriptor->mainThread, processDescriptor);
    processDescriptor->processId = ii;
    processDescriptor->userId = NO_USER_ID;
    processDescriptor->name = _dummyName;
    processDescriptor->callOverlayFunction = HAL->platform.callFileOverlay;
    if ((ii - schedulerState->firstShellPid) < schedulerState->numShells) {
      processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
      processDescriptor->restartFunction = HAL->platform.restartShell;
    } else {
      processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_USER;
      processDescriptor->restartFunction = NULL;
    }
  }

  // Create the memory manager process.  : THIS MUST BE THE LAST PROCESS
  // CREATED BECAUSE WE WANT TO USE THE ENTIRE REST OF MEMORY FOR IT :
  processDescriptor = &allProcesses[schedulerState->memoryManagerPid - 1];
  if (processCreate(processDescriptor,
    runMemoryManager, NULL) != processSuccess
  ) {
    logError("Could not create memory manager process.\n");
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->processId = schedulerState->memoryManagerPid;
  processDescriptor->name = _memoryManagerName;
  processDescriptor->userId = ROOT_USER_ID;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_KERNEL;
  processDescriptor->restartFunction = restartMemoryManager;
  logDebug("Created all processes.\n");

  // Now that we have all the processes setup, we can fix the IPC capabilities.
  setIpcCapabilities(schedulerState);

  // Assign the console ports to the memory manager.
  for (uint8_t ii = 0; ii < schedulerState->numShells; ii++) {
    if (schedulerAssignPortToPid(
      ii, schedulerState->memoryManagerPid) != processSuccess
    ) {
      logWarn("Could not assign console port to memory manager.\n");
    }
  }
  logDebug("Assigned console ports to memory manager.\n");

  // Initialize all the kernel process file descriptors.
  for (ProcessId ii = 1; ii < schedulerState->firstUserPid; ii++) {
    allProcesses[ii - 1].numFileDescriptors = NUM_STANDARD_FILE_DESCRIPTORS;
    allProcesses[ii - 1].fileDescriptors
      = standardKernelFileDescriptorsPointers;
  }
  logDebug("Initialized kernel process file descriptors.\n");

  // Mark all the processes as being part of the their ready queues.  Skip over
  // the scheduler (since by definition it can't be scheduled) and the console
  // (since it was added to its queue earlier).
  allProcesses[0].readyQueue = NULL;
  for (ProcessId ii = allProcesses[2].processId;
    ii <= NANO_OS_NUM_PROCESSES;
    ii++
  ) {
    allProcesses[ii - 1].readyQueue
      = &schedulerState->ready[allProcesses[ii - 1].privilegeLevel];
    processQueuePush(allProcesses[ii - 1].readyQueue, &allProcesses[ii - 1]);
  }
  logDebug("Populated ready queues.\n");

  // Get the memory manager and filesystem up and running.
  runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
  logDebug("Started memory manager and filesystem.\n");

  return 0;
}

/// @fn int logSchedulerDebugInfo(SchedulerState *schedulerState)
///
/// @brief Log all the "on-boot" debug information the scheduler emits.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   startScheduler function.
///
/// @return Returns 0 on success, -errno on failure.
int logSchedulerDebugInfo(SchedulerState *schedulerState) {
  // schedulerState is only used when we're logging debug messages, so mark it
  // unused so that the compiler doesn't complain.
  (void) schedulerState;

  logDebug("\n");
  logDebug("sizeof(int) = %ld\n", (long int) sizeof(int));
  logDebug("sizeof(void*) = %ld\n", (long int) sizeof(void*));
  logDebug("Main stack size = %ld bytes\n",
    (long int) ABS_DIFF(
      ((intptr_t) schedulerThread),
      ((intptr_t) threadStackEnd(schedulerThread))
    ));
  logDebug("schedulerState size = %ld bytes\n",
    (long int) sizeof(SchedulerState));
  logDebug("messagesStorage size = %ld bytes\n",
    (long int) (sizeof(ProcessMessage) * NANO_OS_NUM_MESSAGES));
  logDebug("ConsoleState size = %ld bytes\n",
    (long int) sizeof(ConsoleState));

  // allProcesses array is ordered console process, memory manager process, then
  // either the first block device or the first user process.  So, we want the
  // process after the memory manager, which would be the value of
  // schedulerState->memoryManagerPid since Pids are one-based instead of
  // zero-based.
  logDebug("Console stack size = %ld bytes\n",
    (long int) (ABS_DIFF(
      ((uintptr_t) allProcesses[schedulerState->memoryManagerPid].mainThread),
      ((uintptr_t) allProcesses[schedulerState->consolePid - 1].mainThread))
      - sizeof(Thread)));
  logDebug("Thread stack size = %ld bytes\n",
    (long int) (ABS_DIFF(
      ((uintptr_t) allProcesses[schedulerState->firstUserPid - 1].mainThread),
      ((uintptr_t) allProcesses[schedulerState->firstUserPid].mainThread))
      - sizeof(Thread)));
  logDebug("Thread size = %ld\n",
    (long int) sizeof(Thread));
  logDebug("standardKernelFileDescriptors size = %ld\n",
    (long int) sizeof(standardKernelFileDescriptors));

#ifdef NANO_OS_DEBUG
  // KEEP_IN_FLASH is required on these: .rodata is removed from the final
  // binary on some targets.  Declared here (rather than at file scope) so
  // they only exist in builds that actually define NANO_OS_DEBUG, avoiding
  // an unused-variable warning in the normal build.
  static const char _helloFilename[] KEEP_IN_FLASH = "hello";
  static const char _writeMode[] KEEP_IN_FLASH = "w";
  static const char _appendMode[] KEEP_IN_FLASH = "a";
  static const char _worldContent[] KEEP_IN_FLASH = "world";
  static const char _worldWorldContent[] KEEP_IN_FLASH = "worldworld";

  bool sanityTestFailed = false;
  do {
    FILE *helloFile = schedFopen(_helloFilename, _writeMode);
    if (helloFile == NULL) {
      logDebug("ERROR: Could not open hello file for writing!\n");
      sanityTestFailed = true;
      break;
    }
    logDebug("helloFile is non-NULL!\n");

    if (schedFputs(_worldContent, helloFile) == EOF) {
      logDebug("ERROR: Could not write to hello file!\n");
      schedFclose(helloFile);
      sanityTestFailed = true;
      break;
    }
    schedFclose(helloFile);

    helloFile = schedFopen(_helloFilename, _readMode);
    if (helloFile == NULL) {
      logDebug("ERROR: Could not open hello file for reading after write!\n");
      schedRemove(_helloFilename);
      sanityTestFailed = true;
      break;
    }
    logDebug("Opened helloFile for reading\n");

    char worldString[11] = {0};
    if (schedFgets(
      worldString, sizeof(worldString), helloFile) != worldString
    ) {
      logDebug("ERROR: Could not read worldString after write!\n");
      schedFclose(helloFile);
      schedRemove(_helloFilename);
      sanityTestFailed = true;
      break;
    }
    logDebug("Read data from helloFile into worldString\n");

    if (strcmp(worldString, _worldContent) != 0) {
      logDebug("ERROR: Expected \"world\", read \"%s\"!\n", worldString);
      schedFclose(helloFile);
      schedRemove(_helloFilename);
      sanityTestFailed = true;
      break;
    }
    logDebug("Successfully read \"world\" from \"hello\"!\n");
    schedFclose(helloFile);

    helloFile = schedFopen(_helloFilename, _appendMode);
    if (helloFile == NULL) {
      logDebug("ERROR: Could not open hello file for appending!\n");
      schedRemove(_helloFilename);
      sanityTestFailed = true;
      break;
    }

    if (schedFputs(_worldContent, helloFile) == EOF) {
      logDebug("ERROR: Could not append to hello file!\n");
      schedFclose(helloFile);
      schedRemove(_helloFilename);
      sanityTestFailed = true;
      break;
    }
    schedFclose(helloFile);

    helloFile = schedFopen(_helloFilename, _readMode);
    if (helloFile == NULL) {
      logDebug("ERROR: Could not open hello file for reading after append!\n");
      schedRemove(_helloFilename);
      sanityTestFailed = true;
      break;
    }

    if (schedFgets(
      worldString, sizeof(worldString), helloFile) != worldString
    ) {
      logDebug("ERROR: Could not read worldString after append!\n");
      schedFclose(helloFile);
      schedRemove(_helloFilename);
      sanityTestFailed = true;
      break;
    }

    if (strcmp(worldString, _worldWorldContent) == 0) {
      logDebug(
        "Successfully read \"worldworld\" from \"hello\"!\n");
    } else {
      logDebug("ERROR: Expected \"worldworld\", read \"%s\"!\n", worldString);
      sanityTestFailed = true;
    }

    schedFclose(helloFile);
    if (schedRemove(_helloFilename) != 0) {
      logDebug("ERROR: schedRemove failed to remove the \"hello\" file.\n");
      sanityTestFailed = true;
    }
  } while (0);
  logDebug("Filesystem sanity test complete\n");
  while (sanityTestFailed == true);
#endif // NANO_OS_DEBUG

  return 0;
}

/// @fn void startScheduler(SchedulerState **threadStatePointer)
///
/// @brief Initialize and run the round-robin scheduler.
///
/// @param threadStatePointer A double pointer to a SchedulerState that serves
///   as the state for the thread infrastructure setup outside of this call.
///
/// @return This function returns no value and, in fact, never returns at all.
__attribute__((noinline)) void startScheduler(
  SchedulerState **threadStatePointer
) {
  logDebug("Starting scheduler in debug mode...\n");

  SchedulerState schedulerState;
  memset(&schedulerState, 0, sizeof(schedulerState));
  ProcessMessage messagesStorage[NANO_OS_NUM_MESSAGES];
  memset(messagesStorage, 0, sizeof(messagesStorage));
  logDebug("Initializing scheduler state\n");
  initializeSchedulerState(&schedulerState, threadStatePointer,
    messagesStorage);

  logDebug("Initializing processes\n");
  initializeProcesses(&schedulerState);

  logDebug("Allocating memory for the hostname\n");
  schedulerState.hostname = (char*) schedCalloc(1, HOST_NAME_MAX + 1);
  if (schedulerState.hostname != NULL) {
    logDebug("Allocated memory for the hostname\n");
    FILE *hostnameFile = schedFopen(_hostnameFilePath, _readMode);
    if (hostnameFile != NULL) {
      logDebug("Opened hostname file.\n");
      if (schedFgets(
        schedulerState.hostname, HOST_NAME_MAX + 1, hostnameFile)
          != schedulerState.hostname
      ) {
        logError("fgets did not read hostname!\n");
      }
      if (strchr(schedulerState.hostname, '\r')) {
        *strchr(schedulerState.hostname, '\r') = '\0';
      } else if (strchr(schedulerState.hostname, '\n')) {
        *strchr(schedulerState.hostname, '\n') = '\0';
      } else if (*schedulerState.hostname == '\0') {
        strcpy(schedulerState.hostname, _localhost);
      }
      schedFclose(hostnameFile);
      logDebug("Closed hostname file.\nhostname = %s\n",
        schedulerState.hostname);
    } else {
      logError("schedFopen of hostname returned NULL!\n");
      strcpy(schedulerState.hostname, "localhost");
    }
    logDebug("Populated hostname\n");
  } else {
    logError("schedulerState.hostname is NULL!\n");
  }

  logSchedulerDebugInfo(&schedulerState);
  logDebug("Logged scheduler debug info\n");

  // Run our scheduler.
  while (1) {
    for (int ii = 0; ii < SCHEDULER_NUM_READY_QUEUES; ii++) {
      schedulerState.currentReady = &schedulerState.ready[ii];
      uint8_t queueSize = schedulerState.currentReady->numElements;
      for (uint8_t jj = 0; jj < queueSize; jj++) {
        runScheduler();
      }
    }
  }
}

