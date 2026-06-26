///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              12.02.2024
///
/// @file              Scheduler.h
///
/// @brief             Scheduler functionality for NanoOs.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
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
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @def SCHEDULER_COMMAND_SIGNATURE
///
/// @brief Signature to be used in scheduler commands that take argument
/// structures.  "\0SCHEDLR" expressed as a 64-bit, little-endian value.
#define SCHEDULER_COMMAND_SIGNATURE ((int64_t) 0x524C444548435300)

// Forward declarations and typedefs since we can't include NanoOsTypes.h here.
struct timespec;
typedef struct Cocondition Cocondition;
typedef struct Comutex Comutex;
typedef struct Coroutine Coroutine;
typedef struct FileDescriptor FileDescriptor;
typedef struct NanoOsFile NanoOsFile;
#define FILE NanoOsFile
typedef struct SchedulerState SchedulerState;
typedef struct ProcessDescriptor ProcessDescriptor;
typedef struct ProcessQueue ProcessQueue;
typedef Coroutine Thread;
typedef uint8_t ProcessId;
typedef struct ProcessInfo ProcessInfo;
typedef int16_t UserId;
typedef struct ExecArgs ExecArgs;
typedef struct SpawnArgs SpawnArgs;
typedef uint8_t NanoOsShutdownType;
typedef struct FileBlockMetadata FileBlockMetadata;

/// @struct SchedulerKillProcessArgs
///
/// @brief Arguments and return values for the SCHEDULER_KILL_PROCESS command.
///
/// @param pid The process ID of the process to kill.
/// @param returnValue The returnValue of the command handler.
/// @param errorNumber The errno value to set in the calling process.
typedef struct SchedulerKillProcessArgs {
  ProcessId pid;
  int returnValue;
  int errorNumber;
} SchedulerKillProcessArgs;

/// @struct SchedulerGetNumRunningProcessesArgs
///
/// @brief Arguments and return values for the
/// SCHEDULER_GET_NUM_RUNNING_PROCESSES command.
///
/// @param returnValue A ProcessId value containing the number of processes
///   currently running in the system.
/// @param errorNum The errno value set by the command in the scheduler, if any.
typedef struct SchedulerGetNumRunningProcessesArgs {
  ProcessId returnValue;
  int errorNumber;
} SchedulerGetNumRunningProcessesArgs;

/// @struct SchedulerGetProcessInfoArgs
///
/// @brief Arguments and return values for the SCHEDULER_GET_PROCESS_INFO
/// command.
///
/// @param processInfo A pointer to the process's ProcessInfo structure that
///   the command is to populate.
/// @param returnValue The returnValue of the command handler.
/// @param errorNumber The errno value to set in the calling process.
typedef struct SchedulerGetProcessInfoArgs {
  ProcessInfo *processInfo;
  int returnValue;
  int errorNumber;
} SchedulerGetProcessInfoArgs;

/// @struct SchedulerSetProcessUserArgs
///
/// @brief Arguments and return values for the SCHEDULER_SET_PROCESS_USER
/// command.
///
/// @param userId The UserId that should be associated with the process.
/// @param returnValue The returnValue of the command handler.
/// @param errorNumber The errno value to set in the calling process.
typedef struct SchedulerSetProcessUserArgs {
  UserId userId;
  int returnValue;
  int errorNumber;
} SchedulerSetProcessUserArgs;

/// @struct SchedulerGetHostnameArgs
///
/// @brief Arguments and return values for the SCHEDULER_GET_HOSTNAME command.
///
/// @param hostname The hostname string returned by the scheduler.
/// @param errorNumber The errno value to set in the calling process.
typedef struct SchedulerGetHostnameArgs {
  const char *hostname;
  int errorNumber;
} SchedulerGetHostnameArgs;

/// @struct SchedulerExecveArgs
///
/// Arguments and return values for the SCHEDULER_EXECVE command.
///
/// @param execArgs A pointer to the ExecArgs structure that contains the
///   arguments to execve.
/// @param errorNumber The errno value to set in the calling process.
typedef struct SchedulerExecveArgs {
  ExecArgs *execArgs;
  int errorNumber;
} SchedulerExecveArgs;

/// @struct SchedulerSpawnArgs
///
/// Arguments and return values for the SCHEDULER_SPAWN command.
///
/// @param spawnArgs A pointer to the SpawnArgs structure that contains the
///   arguments to posix_spawn.
/// @param errorNumber The errno value to return as the userspace return value.
typedef struct SchedulerSpawnArgs {
  SpawnArgs *spawnArgs;
  int errorNumber;
} SchedulerSpawnArgs;

/// @struct SchedulerSendSignalArgs
///
/// @brief Arguments and return values for the SCHEDULER_SEND_SIGNAL command.
///
/// @param pid The process ID of the process to send the signal to.
/// @param signal The integer signal to send.
/// @param returnValue The returnValue of the command handler.
/// @param errorNumber The errno value to set in the calling process.
typedef struct SchedulerSendSignalArgs {
  ProcessId pid;
  int signal;
  int returnValue;
  int errorNumber;
} SchedulerSendSignalArgs;

/// @struct SchedulerReplaceOverlayArgs
///
/// @brief Arguments and return values for the SCHEDULER_REPLACE_OVERLAY
/// command.
///
/// @param overlayNamespace The namespace (directory or block device ID) that
///   the overlay is in.
/// @param overlay A pointer to the FileBlockMetadata to use as the process's
///   new overlay.
/// @returnValue The returnValue of the command handler.
typedef struct SchedulerReplaceOverlayArgs {
  void *overlayNamespace;
  FileBlockMetadata *overlay;
  int returnValue;
} SchedulerReplaceOverlayArgs;

/// @struct SchedulerShutdownArgs
///
/// @brief Arguments and return values for the SCHEDULER_SHUTDOWN command.
///
/// @param shutdownType The NanoOsShutdownType to invoke.
/// @param returnValue The returnValue of the command handler.
typedef struct SchedulerShutdownArgs {
  NanoOsShutdownType shutdownType;
  int returnValue;
} SchedulerShutdownArgs;

/// @enum SchedulerCommandResponse
///
/// @brief Commands and responses understood by the scheduler inter-process
/// message handler.
typedef enum SchedulerCommandResponse {
  // Commands:
  SCHEDULER_KILL_PROCESS,
  SCHEDULER_GET_NUM_RUNNING_PROCESSES,
  SCHEDULER_GET_PROCESS_INFO,
  SCHEDULER_SET_PROCESS_USER,
  SCHEDULER_GET_HOSTNAME,
  SCHEDULER_EXECVE,
  SCHEDULER_SPAWN,
  SCHEDULER_SEND_SIGNAL,
  SCHEDULER_REPLACE_OVERLAY,
  SCHEDULER_SHUTDOWN,
  NUM_SCHEDULER_COMMANDS,
  // Responses:
  SCHEDULER_PROCESS_COMPLETE,
} SchedulerCommand;

extern SchedulerState *SCHEDULER_STATE;

// Exported functionality
void startScheduler(SchedulerState **coroutineStatePointer);
ProcessDescriptor* schedulerGetProcessById(unsigned int pid);
ProcessId schedulerGetNumRunningProcesses(struct timespec *timeout);
ProcessInfo* schedulerGetProcessInfo(void);
int schedulerKillProcess(ProcessId pid);
int schedulerSendSignal(ProcessId pid, int signal);
int schedulerSetProcessUser(UserId userId);
FileDescriptor* schedulerGetFileDescriptor(FILE *stream);
int schedulerCloseAllFileDescriptors(void);
const char* schedulerGetHostname(void);
int schedulerExecve(const char *pathname,
  char *const argv[], char *const envp[]);
int schedulerAssignMemory(void *ptr);
int processQueuePush(
  ProcessQueue *processQueue, ProcessDescriptor *processDescriptor);
ProcessDescriptor* processQueuePop(ProcessQueue *processQueue);
int processQueueRemove(
  ProcessQueue *processQueue, ProcessDescriptor *processDescriptor);
int schedulerReplaceOverlay(const void *overlayNamespace,
  FileBlockMetadata *overlay);

// Coroutine setup functions used in the loader.
void* dummyProcess(void *args);

// Process restart functions.
int32_t restartConsole(ProcessDescriptor *processDescriptor);
int32_t restartMemoryManager(ProcessDescriptor *processDescriptor);
int32_t restartShell(ProcessDescriptor *processDescriptor);

// Thread that will be used to represent the scheduler.
extern Thread *schedulerThread;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SCHEDULER_H
