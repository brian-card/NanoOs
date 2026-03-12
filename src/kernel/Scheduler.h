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

// Unix includes
#include "sys/types.h"

// Custom includes
#include "NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct NanoOsFile NanoOsFile;
#define FILE NanoOsFile

/// @def PREEMPTION_TIMER
///
/// @brief The index of the timer used for preemption by the scheduler.
#define PREEMPTION_TIMER 0

/// @enum SchedulerCommandResponse
///
/// @brief Commands and responses understood by the scheduler inter-task
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

// Exported functionality
void startScheduler(SchedulerState **coroutineStatePointer);
TaskDescriptor* schedulerGetTaskById(unsigned int taskId);
int schedulerNotifyTaskComplete(TaskId taskId);
int schedulerWaitForTaskComplete(void);
TaskId schedulerGetNumRunningTasks(struct timespec *timeout);
TaskInfo* schedulerGetTaskInfo(void);
int schedulerKillTask(TaskId taskId);
UserId schedulerGetTaskUser(void);
int schedulerSetTaskUser(UserId userId);
FileDescriptor* schedulerGetFileDescriptor(FILE *stream);
int schedulerCloseAllFileDescriptors(void);
const char* schedulerGetHostname(void);
int schedulerExecve(const char *pathname,
  char *const argv[], char *const envp[]);
int schedulerSpawn(
  pid_t *pid, const char *path,
  const posix_spawn_file_actions_t *file_actions,
  const posix_spawnattr_t *attrp,
  char *const argv[], char *const envp[]);
int schedulerAssignMemory(void *ptr);

// Coroutine setup functions used in the loader.
void coroutineYieldCallback(void *stateData, Coroutine *coroutine);
void comutexUnlockCallback(void *stateData, Comutex *comutex);
void coconditionSignalCallback(void *stateData, Cocondition *cocondition);
void* dummyTask(void *args);

// TaskHandle that will be used to represent the scheduler.
extern TaskHandle schedulerTaskHandle;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SCHEDULER_H
