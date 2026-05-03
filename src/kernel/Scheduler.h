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
typedef Coroutine Thread;
typedef uint8_t ProcessId;
typedef struct ProcessInfo ProcessInfo;
typedef int16_t UserId;

/// @enum SchedulerCommandResponse
///
/// @brief Commands and responses understood by the scheduler inter-process
/// message handler.
typedef enum SchedulerCommandResponse {
  // Commands:
  SCHEDULER_KILL_TASK,
  SCHEDULER_GET_NUM_RUNNING_TASKS,
  SCHEDULER_GET_TASK_INFO,
  SCHEDULER_GET_TASK_USER,
  SCHEDULER_SET_TASK_USER,
  SCHEDULER_CLOSE_ALL_FILE_DESCRIPTORS,
  SCHEDULER_GET_HOSTNAME,
  SCHEDULER_EXECVE,
  SCHEDULER_SPAWN,
  SCHEDULER_ASSIGN_MEMORY,
  NUM_SCHEDULER_COMMANDS,
  // Responses:
  SCHEDULER_TASK_COMPLETE,
} SchedulerCommand;

extern SchedulerState *SCHEDULER_STATE;

// Exported functionality
void startScheduler(SchedulerState **coroutineStatePointer);
ProcessDescriptor* schedulerGetProcessById(unsigned int pid);
ProcessId schedulerGetNumRunningProcesses(struct timespec *timeout);
ProcessInfo* schedulerGetProcessInfo(void);
int schedulerKillProcess(ProcessId pid);
UserId schedulerGetProcessUser(void);
int schedulerSetProcessUser(UserId userId);
FileDescriptor* schedulerGetFileDescriptor(FILE *stream);
int schedulerCloseAllFileDescriptors(void);
const char* schedulerGetHostname(void);
int schedulerExecve(const char *pathname,
  char *const argv[], char *const envp[]);
int schedulerAssignMemory(void *ptr);

// Coroutine setup functions used in the loader.
void yieldCallback(void *stateData, Coroutine *coroutine);
void unlockCallback(void *stateData, Comutex *comutex);
void signalCallback(void *stateData, Cocondition *cocondition);
void* dummyProcess(void *args);

// Thread that will be used to represent the scheduler.
extern Thread *schedulerThread;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SCHEDULER_H
