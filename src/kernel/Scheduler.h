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
/// structures.  "SCHEDCMD" expressed as a 64-bit, little-endian value.
#define SCHEDULER_COMMAND_SIGNATURE ((int64_t) 0x444D434445484353)

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
typedef uint8_t Pid;
typedef struct ProcessInfo ProcessInfo;
typedef int16_t UserId;

/// @struct SchedulerKillProcessArgs
///
/// @brief Arguments and return values for the SCHEDULER_KILL_PROCESS command.
///
/// @param signature The 64-bit signature for a scheduler command.  This should
///   always be SCHEDULER_COMMAND_SIGNATURE.
/// @param pid The process ID of the process to kill.
/// @param returnValue The returnValue of the command handler.
/// @param errorNumber The errno value to set in the calling process.
typedef struct SchedulerKillProcessArgs {
  int64_t signature;
  Pid pid;
  int returnValue;
  int errorNumber;
} SchedulerKillProcessArgs;

/// @struct SchedulerSendSignalArgs
///
/// @brief Arguments and return value for the SCHEDULER_SEND_SIGNAL command.
///
/// @param signature The 64-bit signature for a scheduler command.  This should
///   always be SCHEDULER_COMMAND_SIGNATURE.
/// @param pid The process ID of the process to send the signal to.
/// @param signal The integer signal to send.
/// @param returnValue The returnValue of the command handler.
/// @param errorNumber The errno value to set in the calling process.
typedef struct SchedulerSendSignalArgs {
  int64_t signature;
  Pid pid;
  int signal;
  int returnValue;
  int errorNumber;
} SchedulerSendSignalArgs;

/// @enum SchedulerCommandResponse
///
/// @brief Commands and responses understood by the scheduler inter-process
/// message handler.
typedef enum SchedulerCommandResponse {
  // Commands:
  SCHEDULER_KILL_PROCESS,
  SCHEDULER_GET_NUM_RUNNING_PROCESSES,
  SCHEDULER_GET_PROCESS_INFO,
  SCHEDULER_GET_PROCESS_USER,
  SCHEDULER_SET_PROCESS_USER,
  SCHEDULER_GET_HOSTNAME,
  SCHEDULER_EXECVE,
  SCHEDULER_SPAWN,
  SCHEDULER_SEND_SIGNAL,
  NUM_SCHEDULER_COMMANDS,
  // Responses:
  SCHEDULER_PROCESS_COMPLETE,
} SchedulerCommand;

extern SchedulerState *SCHEDULER_STATE;

// Exported functionality
void startScheduler(SchedulerState **coroutineStatePointer);
ProcessDescriptor* schedulerGetProcessById(unsigned int pid);
Pid schedulerGetNumRunningProcesses(struct timespec *timeout);
ProcessInfo* schedulerGetProcessInfo(void);
int schedulerKillProcess(Pid pid);
int schedulerSendSignal(Pid pid, int signal);
UserId schedulerGetProcessUser(void);
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

// Coroutine setup functions used in the loader.
void* dummyProcess(void *args);

// Thread that will be used to represent the scheduler.
extern Thread *schedulerThread;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SCHEDULER_H
