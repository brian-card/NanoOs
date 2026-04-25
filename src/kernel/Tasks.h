///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              12.02.2024
///
/// @file              Tasks.h
///
/// @brief             Task functionality for NanoOs.
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

#ifndef TASKS_H
#define TASKS_H

// Custom includes
#include "NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Task status values.
#define taskSuccess  coroutineSuccess
#define taskBusy     coroutineBusy
#define taskError    coroutineError
#define taskNomem    coroutineNomem
#define taskTimedout coroutineTimedout

/// @def TASK_ID_NOT_SET
///
/// @brief Value to be used to indicate that a task ID has not been set for
/// a TaskDescriptor object.
#define TASK_ID_NOT_SET \
  ((uint8_t) 0x0f)

/// @def getRunningTask
///
/// @brief Function macro to get the pointer to the currently running Task
/// object.
#define getRunningTask() \
  ((TaskDescriptor*) getRunningCoroutineContext())

/// @def taskCreate
///
/// @brief Function macro to create a new task.
#define taskCreate(taskDescriptor, func, arg) \
  coroutineCreate(&(taskDescriptor)->taskHandle, func, arg)

/// @def taskRunning
///
/// @brief Function macro to determine whether or not a given task is
/// currently running.
#define taskRunning(taskDescriptor) \
  coroutineRunning((taskDescriptor)->taskHandle)

/// @def taskFinished
///
/// @brief Function macro to determine whether or not a given task has
/// finished
#define taskFinished(taskDescriptor) \
  coroutineFinished((taskDescriptor)->taskHandle)

/// @def taskId
///
/// @brief Function macro to get the numeric TaskId given its descriptor.
#define taskId(taskDescriptor) \
  (taskDescriptor)->taskId

/// @def taskState
///
/// @brief Function macro to get the state of a task given its handle.
#define taskState(taskDescriptor) \
  coroutineState((taskDescriptor)->taskHandle)

/// @def taskSetContext
///
/// @brief Function macro to set the context of a task handle.
#define taskHandleSetContext(taskHandle, context) \
  coroutineSetContext(taskHandle, context)

/// @def taskYield
///
/// @brief Call to yield the processor to another task.
#define taskYield() \
  coroutineYield(NULL, COROUTINE_STATE_BLOCKED)

/// @def taskYieldValue
///
/// @brief Yield a value back to the scheduler.
#define taskYieldValue(value) \
  coroutineYield(value, COROUTINE_STATE_BLOCKED)

/// @def taskTerminate
///
/// @brief Function macro to terminate a running task.
#define taskTerminate(taskDescriptor) \
  coroutineTerminate((taskDescriptor)->taskHandle, NULL)

/// @def taskMessageInit
///
/// @brief Function macro to initialize a task message.
#define taskMessageInit(taskMessage, type, data, size, waiting) \
  msg_init(taskMessage, MSG_CORO_SAFE, type, data, size, waiting)

/// @def taskMessageSetDone
///
/// @brief Function macro to set a task message to the 'done' state.
#define taskMessageSetDone(taskMessage) \
  msg_set_done(taskMessage)

/// @def taskMessageRelease
///
/// @brief Function macro to release a task message.
#define taskMessageRelease(taskMessage) \
  msg_release(taskMessage)

/// @def taskMessageWaitForDone
///
/// @brief Function macro to wait for a task message to enter the 'done'
/// state.
#define taskMessageWaitForDone(taskMessage, ts) \
  msg_wait_for_done(taskMessage, ts)

/// @def taskMessageWaitForReplyWithType
///
/// @brief Function macro to wait on a reply to a message with a specified type.
#define taskMessageWaitForReplyWithType(sent, releaseAfterDone, type, ts) \
  msg_wait_for_reply_with_type(sent, releaseAfterDone, type, ts)

/// @def taskMessageQueueWaitForType
///
/// @brief Function macro to wait for a message of a specific type to be pushed
/// onto the running task's message queue.
#define taskMessageQueueWaitForType(type, ts) \
  comessageQueueWaitForType(type, ts)

/// @def taskMessageQueueWait
///
/// @brief Function macro to wait for a message to be pushed onto the running
/// task's message queue.
#define taskMessageQueueWait(ts) \
  comessageQueueWait(ts)

/// @def taskMessageQueuePush
///
/// @brief Function macro to push a task message on to a task's message
/// queue.
#define taskMessageQueuePush(taskDescriptor, message) \
  comessageQueuePush((taskDescriptor)->taskHandle, message)

/// @def taskMessageQueuePop
///
/// @brief Function macro to pop a task message from the running task's
/// message queue.
#define taskMessageQueuePop() \
  comessageQueuePop()

/// @def getRunningTaskId
///
/// @brief Get the task ID for the currently-running task.
#define getRunningTaskId() \
  (getRunningTask()->taskId)

/// @def taskResume
///
/// @brief Resume a task and update the currentTask state correctly.
#define taskResume(taskDescriptor, taskMessage) \
  coroutineResume((taskDescriptor)->taskHandle, taskMessage); \

// Task message accessors
#define taskMessageType(taskMessagePointer) \
  msg_type(taskMessagePointer)
#define taskMessageData(taskMessagePointer) \
  msg_data(taskMessagePointer)
#define taskMessageSize(taskMessagePointer) \
  msg_size(taskMessagePointer)
#define taskMessageWaiting(taskMessagePointer) \
  msg_waiting(taskMessagePointer)
#define taskMessageDone(taskMessagePointer) \
  msg_done(taskMessagePointer)
#define taskMessageInUse(taskMessagePointer) \
  msg_in_use(taskMessagePointer)
#define taskMessageFrom(taskMessagePointer) \
  ((TaskDescriptor*) coroutineContext(msg_from(taskMessagePointer).coro))
#define taskMessageTo(taskMessagePointer) \
  ((TaskDescriptor*) coroutineContext(msg_to(taskMessagePointer).coro))
#define taskMessageConfigured(taskMessagePointer) \
  msg_configured(taskMessagePointer)

// Exported functionality
void* execCommand(void *args);
int sendTaskMessageToTask(
  TaskDescriptor *taskDescriptor, TaskMessage *taskMessage);
int sendTaskMessageToTaskId(unsigned int taskId, TaskMessage *taskMessage);
TaskMessage* getAvailableMessage(void);
TaskMessage* initSendTaskMessageToTaskId(int taskId, int type,
  void *data, size_t size, bool waiting);
void* waitForDataMessage(TaskMessage *sent, int type, const struct timespec *ts);
ExecArgs* execArgsDestroy(ExecArgs *execArgs);
SpawnArgs* spawnArgsDestroy(SpawnArgs *spawnArgs);
char** parseArgs(char *command, int *argc);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TASKS_H
