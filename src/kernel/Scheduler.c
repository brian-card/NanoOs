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

// Doxygen marker
/// @file

// Unix includes
#include "sys/types.h"

// Custom includes
#include "Console.h"
#include "ExFatFilesystem.h"
#include "ExFatTask.h"
#include "Hal.h"
#include "NanoOs.h"
#include "NanoOsOverlayFunctions.h"
#include "Tasks.h"
#include "Scheduler.h"
#include "SdCard.h"

// User space includes
#include "../user/NanoOsLibC.h"
#include "../user/NanoOsUnistd.h"

// Must come last
#include "../user/NanoOsStdio.h"

// Support prototypes.
void runScheduler(SchedulerState *schedulerState);

/// @def NUM_STANDARD_FILE_DESCRIPTORS
///
/// @brief The number of file descriptors a task usually starts out with.
#define NUM_STANDARD_FILE_DESCRIPTORS 3

/// @def STDIN_FILE_DESCRIPTOR_INDEX
///
/// @brief Index into a TaskDescriptor's fileDescriptors array that holds the
/// FileDescriptor object that maps to the task's stdin FILE stream.
#define STDIN_FILE_DESCRIPTOR_INDEX 0

/// @def STDOUT_FILE_DESCRIPTOR_INDEX
///
/// @brief Index into a TaskDescriptor's fileDescriptors array that holds the
/// FileDescriptor object that maps to the task's stdout FILE stream.
#define STDOUT_FILE_DESCRIPTOR_INDEX 1

/// @def STDERR_FILE_DESCRIPTOR_INDEX
///
/// @brief Index into a TaskDescriptor's fileDescriptors array that holds the
/// FileDescriptor object that maps to the task's stderr FILE stream.
#define STDERR_FILE_DESCRIPTOR_INDEX 2

/// @var _functionInProgress
///
/// @brief Function that's already in progress that keeps another function from
/// running.
const char *_functionInProgress = NULL;

/// @var schedulerTaskHandle
///
/// @brief Pointer to the main task handle that's allocated before the
/// scheduler is started.
TaskHandle schedulerTaskHandle = NULL;

/// @var schedulerTask
///
/// @brief Pointer to the scheduler task.
static TaskDescriptor *schedulerTask = NULL;

/// @var currentTask
///
/// @brief Pointer to the task that is currently executing.
static TaskDescriptor *currentTask = NULL;

/// @var allTasks
///
/// @brief Pointer to the allTasks array that is part of the
/// SchedulerState object maintained by the scheduler task.  This is needed
/// in order to do lookups from task IDs to task object pointers.
static TaskDescriptor *allTasks = NULL;

/// @var standardKernelFileDescriptors
///
/// @brief The array of file descriptors that all kernel tasks use.
static const FileDescriptor standardKernelFileDescriptors[
  NUM_STANDARD_FILE_DESCRIPTORS
] = {
  {
    // stdin
    // Kernel tasks do not read from stdin, so clear out both pipes.
    .inputPipe = {
      .taskId = TASK_ID_NOT_SET,
      .messageType = 0,
    },
    .outputPipe = {
      .taskId = TASK_ID_NOT_SET,
      .messageType = 0,
    },
  },
  {
    // stdout
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .inputPipe = {
      .taskId = TASK_ID_NOT_SET,
      .messageType = 0,
    },
    .outputPipe = {
      .taskId = NANO_OS_CONSOLE_TASK_ID,
      .messageType = CONSOLE_WRITE_BUFFER,
    },
  },
  {
    // stderr
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .inputPipe = {
      .taskId = TASK_ID_NOT_SET,
      .messageType = 0,
    },
    .outputPipe = {
      .taskId = NANO_OS_CONSOLE_TASK_ID,
      .messageType = CONSOLE_WRITE_BUFFER,
    },
  },
};

/// @var standardUserFileDescriptors
///
/// @brief Pointer to the array of FileDescriptor objects (declared in the
/// startScheduler function on the scheduler's stack) that all tasks start
/// out with.
static const FileDescriptor standardUserFileDescriptors[
  NUM_STANDARD_FILE_DESCRIPTORS
] = {
  {
    // stdin
    // Uni-directional FileDescriptor, so clear the output pipe and direct the
    // input pipe to the console.
    .inputPipe = {
      .taskId = NANO_OS_CONSOLE_TASK_ID,
      .messageType = CONSOLE_WAIT_FOR_INPUT,
    },
    .outputPipe = {
      .taskId = TASK_ID_NOT_SET,
      .messageType = 0,
    },
  },
  {
    // stdout
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .inputPipe = {
      .taskId = TASK_ID_NOT_SET,
      .messageType = 0,
    },
    .outputPipe = {
      .taskId = NANO_OS_CONSOLE_TASK_ID,
      .messageType = CONSOLE_WRITE_BUFFER,
    },
  },
  {
    // stderr
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .inputPipe = {
      .taskId = TASK_ID_NOT_SET,
      .messageType = 0,
    },
    .outputPipe = {
      .taskId = NANO_OS_CONSOLE_TASK_ID,
      .messageType = CONSOLE_WRITE_BUFFER,
    },
  },
};

/// @var shellNames
///
/// @brief The names of the shells as they will appear in the task table.
static const char* const shellNames[NANO_OS_MAX_NUM_SHELLS] = {
  "shell 0",
  "shell 1",
};

/// @fn int taskQueuePush(
///   TaskQueue *taskQueue, TaskDescriptor *taskDescriptor)
///
/// @brief Push a pointer to a TaskDescriptor onto a TaskQueue.
///
/// @param taskQueue A pointer to a TaskQueue to push the pointer to.
/// @param taskDescriptor A pointer to a TaskDescriptor to push onto the
///   queue.
///
/// @return Returns 0 on success, ENOMEM on failure.
int taskQueuePush(
  TaskQueue *taskQueue, TaskDescriptor *taskDescriptor
) {
  if ((taskQueue == NULL)
    || (taskQueue->numElements >= SCHEDULER_NUM_TASKS)
  ) {
    printString("ERROR: Could not push task ");
    printInt(taskDescriptor->taskId);
    printString(" onto ");
    printString(taskQueue->name);
    printString(" queue:\n");
    return ENOMEM;
  }

  taskQueue->tasks[taskQueue->tail] = taskDescriptor;
  taskQueue->tail++;
  taskQueue->tail %= SCHEDULER_NUM_TASKS;
  taskQueue->numElements++;
  taskDescriptor->taskQueue = taskQueue;

  return 0;
}

/// @fn TaskDescriptor* taskQueuePop(TaskQueue *taskQueue)
///
/// @brief Pop a pointer to a TaskDescriptor from a TaskQueue.
///
/// @param taskQueue A pointer to a TaskQueue to pop the pointer from.
///
/// @return Returns a pointer to a TaskDescriptor on success, NULL on
/// failure.
TaskDescriptor* taskQueuePop(TaskQueue *taskQueue) {
  TaskDescriptor *taskDescriptor = NULL;
  if ((taskQueue == NULL) || (taskQueue->numElements == 0)) {
    return taskDescriptor; // NULL
  }

  taskDescriptor = taskQueue->tasks[taskQueue->head];
  taskQueue->head++;
  taskQueue->head %= SCHEDULER_NUM_TASKS;
  taskQueue->numElements--;
  taskDescriptor->taskQueue = NULL;

  return taskDescriptor;
}

/// @fn int taskQueueRemove(
///   TaskQueue *taskQueue, TaskDescriptor *taskDescriptor)
///
/// @brief Remove a pointer to a TaskDescriptor from a TaskQueue.
///
/// @param taskQueue A pointer to a TaskQueue to remove the pointer from.
/// @param taskDescriptor A pointer to a TaskDescriptor to remove from the
///   queue.
///
/// @return Returns 0 on success, ENOMEM on failure.
int taskQueueRemove(
  TaskQueue *taskQueue, TaskDescriptor *taskDescriptor
) {
  int returnValue = EINVAL;
  if ((taskQueue == NULL) || (taskQueue->numElements == 0)) {
    // Nothing to do.
    return returnValue; // EINVAL
  }

  TaskDescriptor *poppedDescriptor = NULL;
  for (uint8_t ii = 0; ii < taskQueue->numElements; ii++) {
    poppedDescriptor = taskQueuePop(taskQueue);
    if (poppedDescriptor == taskDescriptor) {
      returnValue = ENOERR;
      taskDescriptor->taskQueue = NULL;
      break;
    }
    // This is not what we're looking for.  Put it back.
    taskQueuePush(taskQueue, poppedDescriptor);
  }

  return returnValue;
}

// Coroutine callbacks.  ***DO NOT** do parameter validation.  These callbacks
// are set when coroutineConfig is called.  If these callbacks are called at
// all (which they should be), then we should assume that things are configured
// correctly.  This is in kernel space code, which we have full control over,
// so we should assume that things are setup correctly.  If they're not setup
// correctly, we should fix the configuration, not do parameter validation.
// These callbacks - especially coroutineYieldCallback - are in the critical
// path.  Single cycles matter.  Don't waste more time than we need to.

/// @fn void coroutineYieldCallback(void *stateData, Coroutine *coroutine)
///
/// @brief Function to be called right before a coroutine yields.
///
/// @param stateData The coroutine state pointer provided when coroutineConfig
///   was called.
/// @param coroutine A pointer to the Coroutine structure representing the
///   coroutine that's about to yield.  This parameter is unused by this
///   function.
///
/// @Return This function returns no value.
void coroutineYieldCallback(void *stateData, Coroutine *coroutine) {
  (void) coroutine;
  SchedulerState *schedulerState = *((SchedulerState**) stateData);
  if (schedulerState == NULL) {
    // We're being called before the scheduler has been started.  This is
    // sometimes done to fix the stack size of the scheduler itself before
    // starting it.  Just return.
    return;
  }

  HAL->cancelTimer(schedulerState->preemptionTimer);

  return;
}

/// @fn void comutexUnlockCallback(void *stateData, Comutex *comutex)
///
/// @brief Function to be called when a mutex (Comutex) is unlocked.
///
/// @param stateData The coroutine state pointer provided when coroutineConfig
///   was called.
/// @param comutex A pointer to the Comutex object that has been unlocked.  At
///   the time this callback is called, the mutex has been unlocked but its
///   coroutine pointer has not been cleared.
///
/// @return This function returns no value, but if the head of the Comutex's
/// lock queue is found in one of the waiting queues, it is removed from the
/// waiting queue and pushed onto the ready queue.
void comutexUnlockCallback(void *stateData, Comutex *comutex) {
  (void) stateData;
  TaskDescriptor *taskDescriptor = coroutineContext(comutex->head);
  if (taskDescriptor == NULL) {
    // Nothing is waiting on this mutex.  Just return.
    return;
  }
  taskQueueRemove(taskDescriptor->taskQueue, taskDescriptor);
  taskQueuePush(taskDescriptor->readyQueue, taskDescriptor);

  return;
}

/// @fn void coconditionSignalCallback(
///   void *stateData, Cocondition *cocondition)
///
/// @brief Function to be called when a condition (Cocondition) is signalled.
///
/// @param stateData The coroutine state pointer provided when coroutineConfig
///   was called.
/// @param cocondition A pointer to the Cocondition object that has been
///   signalled.  At the time this callback is called, the number of signals has
///   been set to the number of waiters that will be signalled.
///
/// @return This function returns no value, but if the head of the Cocondition's
/// signal queue is found in one of the waiting queues, it is removed from the
/// waiting queue and pushed onto the ready queue.
void coconditionSignalCallback(void *stateData, Cocondition *cocondition) {
  (void) stateData;
  TaskHandle cur = cocondition->head;

  for (int ii = 0; (ii < cocondition->numSignals) && (cur != NULL); ii++) {
    TaskDescriptor *taskDescriptor = coroutineContext(cur);
    // It's not possible for taskDescriptor to be NULL.  We only enter this
    // loop if cocondition->numSignals > 0, so there MUST be something waiting
    // on this condition.
    taskQueueRemove(taskDescriptor->taskQueue, taskDescriptor);
    taskQueuePush(taskDescriptor->readyQueue, taskDescriptor);
    cur = cur->nextToSignal;
  }

  return;
}

/// @fn TaskDescriptor* schedulerGetTaskById(unsigned int taskId)
///
/// @brief Look up a task for a running command given its task ID.
///
/// @note This function is meant to be called from outside of the scheduler's
/// running state.  That's why there's no SchedulerState pointer in the
/// parameters.
///
/// @param taskId The integer ID for the task.
///
/// @return Returns the found task descriptor on success, NULL on failure.
TaskDescriptor* schedulerGetTaskById(unsigned int taskId) {
  TaskDescriptor *taskDescriptor = NULL;
  if ((taskId > 0) && (taskId <= NANO_OS_NUM_TASKS)) {
    taskDescriptor = &allTasks[taskId - 1];
  }

  return taskDescriptor;
}

/// @fn void* dummyTask(void *args)
///
/// @brief Dummy task that's loaded at startup to prepopulate the task
/// array with tasks.
///
/// @param args Any arguments passed to this function.  Ignored.
///
/// @return This function always returns NULL.
void* dummyTask(void *args) {
  (void) args;
  return NULL;
}

/// @fn int schedulerSendTaskMessageToTask(
///   TaskDescriptor *taskDescriptor, TaskMessage *taskMessage)
///
/// @brief Get an available TaskMessage, populate it with the specified data,
/// and push it onto a destination task's queue.
///
/// @param taskDescriptor A pointer to the TaskDescriptor that manages
///   the task to send a message to.
/// @param taskMessage A pointer to the message to send to the destination
///   task.
///
/// @return Returns taskSuccess on success, taskError on failure.
int schedulerSendTaskMessageToTask(
  TaskDescriptor *taskDescriptor, TaskMessage *taskMessage
) {
  int returnValue = taskSuccess;
  if ((taskDescriptor == NULL)
    || (taskDescriptor->taskHandle == NULL)
  ) {
    printString(
      "ERROR: Attempt to send scheduler taskMessage to NULL task.\n");
    returnValue = taskError;
    return returnValue;
  } else if (taskMessage == NULL) {
    printString(
      "ERROR: Attempt to send NULL scheduler taskMessage to task.\n");
    returnValue = taskError;
    return returnValue;
  }
  // taskMessage->from would normally be set when we do a
  // taskMessageQueuePush. We're not using that mechanism here, so we have
  // to do it manually.  If we don't do this, then commands that validate that
  // the message came from the scheduler will fail.
  msg_from(taskMessage).coro = schedulerTaskHandle;

  // Have to set the endpoint type manually since we're not using
  // comessageQueuePush.
  taskMessage->msg_sync = &msg_sync_array[MSG_CORO_SAFE];

  if (coroutineCorrupted(taskDescriptor->taskHandle)) {
    printString("ERROR: Called task is corrupted:\n");
    returnValue = taskError;
    return returnValue;
  }
  taskResume(taskDescriptor, taskMessage);

  if (taskMessageDone(taskMessage) != true) {
    // This is our only indication from the called task that something went
    // wrong.  Return an error status here.
    printString("ERROR: Task ");
    printInt(taskDescriptor->taskId);
    printString(" did not mark sent message done.\n");
    returnValue = taskError;
  }

  return returnValue;
}

/// @fn int schedulerSendTaskMessageToTaskId(SchedulerState *schedulerState,
///   unsigned int pid, TaskMessage *taskMessage)
///
/// @brief Look up a task by its PID and send a message to it.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param pid The ID of the task to send the message to.
/// @param taskMessage A pointer to the message to send to the destination
///   task.
///
/// @return Returns taskSuccess on success, taskError on failure.
int schedulerSendTaskMessageToTaskId(SchedulerState *schedulerState,
  unsigned int pid, TaskMessage *taskMessage
) {
  int returnValue = taskError;
  if ((pid <= 0) || (pid > NANO_OS_NUM_TASKS)) {
    // Not a valid PID.  Fail.
    printString("ERROR: ");
    printInt(pid);
    printString(" is not a valid PID.\n");
    return returnValue; // taskError
  }

  TaskDescriptor *taskDescriptor = &schedulerState->allTasks[pid - 1];
  // If taskDescriptor is NULL, it will be detected as not running by
  // schedulerSendTaskMessageToTask, so there's no real point in
  //  checking for NULL here.
  return schedulerSendTaskMessageToTask(
    taskDescriptor, taskMessage);
}

/// @fn int schedulerSendNanoOsMessageToTask(
///   TaskDescriptor *taskDescriptor, int type,
///   NanoOsMessageData func, NanoOsMessageData data)
///
/// @brief Send a NanoOsMessage to another task identified by its Coroutine.
///
/// @param taskDescriptor A pointer to the ProcesDescriptor that holds the
///   metadata for the task.
/// @param type The type of the message to send to the destination task.
/// @param func The function information to send to the destination task,
///   cast to a NanoOsMessageData.
/// @param data The data to send to the destination task, cast to a
///   NanoOsMessageData.
/// @param waiting Whether or not the sender is waiting on a response from the
///   destination task.
///
/// @return Returns taskSuccess on success, a different task status
/// on failure.
int schedulerSendNanoOsMessageToTask(TaskDescriptor *taskDescriptor,
  int type, NanoOsMessageData func, NanoOsMessageData data
) {
  TaskMessage taskMessage;
  memset(&taskMessage, 0, sizeof(taskMessage));
  NanoOsMessage nanoOsMessage;

  nanoOsMessage.func = func;
  nanoOsMessage.data = data;

  // These messages are always waiting for done from the caller, so hardcode
  // the waiting parameter to true here.
  taskMessageInit(
    &taskMessage, type, &nanoOsMessage, sizeof(nanoOsMessage), true);

  int returnValue = schedulerSendTaskMessageToTask(
    taskDescriptor, &taskMessage);

  return returnValue;
}

/// @fn int schedulerSendNanoOsMessageToTaskId(
///   SchedulerState *schedulerState,
///   int pid, int type,
///   NanoOsMessageData func, NanoOsMessageData data)
///
/// @brief Send a NanoOsMessage to another task identified by its PID. Looks
/// up the task's Coroutine by its PID and then calls
/// schedulerSendNanoOsMessageToTask.
///
/// @param schedulerState A pointer to the SchedulerState object maintainted by
///   the scheduler.
/// @param pid The task ID of the destination task.
/// @param type The type of the message to send to the destination task.
/// @param func The function information to send to the destination task,
///   cast to a NanoOsMessageData.
/// @param data The data to send to the destination task, cast to a
///   NanoOsMessageData.
///
/// @return Returns taskSuccess on success, a different task status
/// on failure.
int schedulerSendNanoOsMessageToTaskId(
  SchedulerState *schedulerState,
  int pid, int type,
  NanoOsMessageData func, NanoOsMessageData data
) {
  int returnValue = taskError;
  if ((pid <= 0) || (pid > NANO_OS_NUM_TASKS)) {
    // Not a valid PID.  Fail.
    printString("ERROR: ");
    printInt(pid);
    printString(" is not a valid PID.\n");
    return returnValue; // taskError
  }

  TaskDescriptor *taskDescriptor = &schedulerState->allTasks[pid - 1];
  returnValue = schedulerSendNanoOsMessageToTask(
    taskDescriptor, type, func, data);
  return returnValue;
}

/// @fn void* schedulerResumeReallocMessage(void *ptr, size_t size)
///
/// @brief Send a MEMORY_MANAGER_REALLOC command to the memory manager task
/// by resuming it with the message and get a reply.
///
/// @param ptr The pointer to send to the task.
/// @param size The size to send to the task.
///
/// @return Returns the data pointer returned in the reply.
void* schedulerResumeReallocMessage(void *ptr, size_t size) {
  void *returnValue = NULL;
  
  ReallocMessage reallocMessage;
  reallocMessage.ptr = ptr;
  reallocMessage.size = size;
  reallocMessage.responseType = MEMORY_MANAGER_RETURNING_POINTER;
  
  TaskMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    // Nothing we can do.  The scheduler can't yield.  Bail.
    return returnValue;
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(sent);
  nanoOsMessage->data = (NanoOsMessageData) ((uintptr_t) &reallocMessage);
  taskMessageInit(sent, MEMORY_MANAGER_REALLOC,
    nanoOsMessage, sizeof(*nanoOsMessage), true);
  // sent->from would normally be set during taskMessageQueuePush.  We're
  // not using that mechanism here, so we have to do it manually.  Things will
  // get messed up if we don't.
  msg_from(sent).coro = schedulerTaskHandle;

  taskResume(&allTasks[NANO_OS_MEMORY_MANAGER_TASK_ID - 1], sent);
  if (taskMessageDone(sent) == true) {
    // The handler set the pointer back in the structure we sent it, so grab it
    // out of the structure we already have.
    returnValue = reallocMessage.ptr;
  } else {
    printString(
      "Warning:  Memory manager did not mark realloc message done.\n");
  }
  // The handler pushes the message back onto our queue, which is not what we
  // want.  Pop it off again.
  taskMessageQueuePop();
  taskMessageRelease(sent);

  // The message that was sent to us is the one that we allocated on the stack,
  // so, there's no reason to call taskMessageRelease here.
  
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
  printDebugString("Calling schedulerResumeReallocMessage\n");
  void *returnValue = schedulerResumeReallocMessage(NULL, totalSize);
  printDebugString("Returned from schedulerResumeReallocMessage\n");
  
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
  TaskMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    // Nothing we can do.  The scheduler can't yield.  Bail.
    return;
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(sent);
  nanoOsMessage->data = (NanoOsMessageData) ((intptr_t) ptr);
  taskMessageInit(sent, MEMORY_MANAGER_FREE,
    nanoOsMessage, sizeof(*nanoOsMessage), true);
  // sent->from would normally be set during taskMessageQueuePush.  We're
  // not using that mechanism here, so we have to do it manually.  Things will
  // get messed up if we don't.
  msg_from(sent).coro = schedulerTaskHandle;

  taskResume(&allTasks[NANO_OS_MEMORY_MANAGER_TASK_ID - 1], sent);
  if (taskMessageDone(sent) == false) {
    printString(
      "Warning:  Memory manager did not mark free message done.\n");
  }
  taskMessageRelease(sent);

  return;
}

/// @fn int assignMemory(void *ptr, TaskId taskId) {
///
/// @brief Assign a piece of memory to a specific task.
///
/// @param ptr The pointer to the memory to assign.
/// @param taskId The ID of the task to assign the memory to.
///
/// @return Returns 0 on success, -errno on failure.
int assignMemory(void *ptr, TaskId taskId) {
  TaskMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    // Nothing we can do.  The scheduler can't yield.  Bail.
    return -ENOMEM;
  }
  
  AssignMemoryParams assignMemoryParams = {
    .ptr = ptr,
    .taskId = taskId,
  };
  taskMessageInit(sent, MEMORY_MANAGER_ASSIGN_MEMORY,
    &assignMemoryParams, sizeof(assignMemoryParams), true);

  // sent->from would normally be set during taskMessageQueuePush.  We're
  // not using that mechanism here, so we have to do it manually.  Things will
  // get messed up if we don't.
  msg_from(sent).coro = schedulerTaskHandle;

  int returnValue = 0;
  taskResume(&allTasks[NANO_OS_MEMORY_MANAGER_TASK_ID - 1], sent);
  if (taskMessageDone(sent) == false) {
    printString(
      "Warning:  Memory manager did not mark assignMemory message done.\n");
    returnValue = -ETIMEDOUT;
  }
  taskMessageRelease(sent);

  return returnValue;
}

/// @fn int schedulerAssignPortToTaskId(
///   SchedulerState *schedulerState,
///   uint8_t consolePort, TaskId owner)
///
/// @brief Assign a console port to a task ID.
///
/// @param schedulerState A pointer to the SchedulerState object maintainted by
///   the scheduler.
/// @param consolePort The ID of the consolePort to assign.
/// @param owner The ID of the task to assign the port to.
///
/// @return Returns taskSuccess on success, taskError on failure.
int schedulerAssignPortToTaskId(
  SchedulerState *schedulerState,
  uint8_t consolePort, TaskId owner
) {
  ConsolePortPidUnion consolePortPidUnion;
  consolePortPidUnion.consolePortPidAssociation.consolePort
    = consolePort;
  consolePortPidUnion.consolePortPidAssociation.taskId = owner;

  int returnValue = schedulerSendNanoOsMessageToTaskId(schedulerState,
    NANO_OS_CONSOLE_TASK_ID, CONSOLE_ASSIGN_PORT,
    /* func= */ 0, consolePortPidUnion.nanoOsMessageData);

  return returnValue;
}

/// @fn int schedulerAssignPortInputToTaskId(
///   SchedulerState *schedulerState,
///   uint8_t consolePort, TaskId owner)
///
/// @brief Assign a console port to a task ID.
///
/// @param schedulerState A pointer to the SchedulerState object maintainted by
///   the scheduler.
/// @param consolePort The ID of the consolePort to assign.
/// @param owner The ID of the task to assign the port to.
///
/// @return Returns taskSuccess on success, taskError on failure.
int schedulerAssignPortInputToTaskId(
  SchedulerState *schedulerState,
  uint8_t consolePort, TaskId owner
) {
  ConsolePortPidUnion consolePortPidUnion;
  consolePortPidUnion.consolePortPidAssociation.consolePort
    = consolePort;
  consolePortPidUnion.consolePortPidAssociation.taskId = owner;

  int returnValue = schedulerSendNanoOsMessageToTaskId(schedulerState,
    NANO_OS_CONSOLE_TASK_ID, CONSOLE_ASSIGN_PORT_INPUT,
    /* func= */ 0, consolePortPidUnion.nanoOsMessageData);

  return returnValue;
}

/// @fn int schedulerSetPortShell(
///   SchedulerState *schedulerState,
///   uint8_t consolePort, TaskId shell)
///
/// @brief Assign a console port to a task ID.
///
/// @param schedulerState A pointer to the SchedulerState object maintainted by
///   the scheduler.
/// @param consolePort The ID of the consolePort to set the shell for.
/// @param shell The ID of the shell task for the port.
///
/// @return Returns taskSuccess on success, taskError on failure.
int schedulerSetPortShell(
  SchedulerState *schedulerState,
  uint8_t consolePort, TaskId shell
) {
  int returnValue = taskError;

  if (shell >= NANO_OS_NUM_TASKS) {
    printString("ERROR: schedulerSetPortShell called with invalid shell PID ");
    printInt(shell);
    printString("\n");
    return returnValue; // taskError
  }

  ConsolePortPidUnion consolePortPidUnion;
  consolePortPidUnion.consolePortPidAssociation.consolePort
    = consolePort;
  consolePortPidUnion.consolePortPidAssociation.taskId = shell;

  returnValue = schedulerSendNanoOsMessageToTaskId(schedulerState,
    NANO_OS_CONSOLE_TASK_ID, CONSOLE_SET_PORT_SHELL,
    /* func= */ 0, consolePortPidUnion.nanoOsMessageData);

  return returnValue;
}

/// @fn int schedulerGetNumConsolePorts(SchedulerState *schedulerState)
///
/// @brief Get the number of ports the console is running.
///
/// @param schedulerState A pointer to the SchedulerState object maintainted by
///   the scheduler.
///
/// @return Returns the number of ports the console is running on success, -1
/// on failure.
int schedulerGetNumConsolePorts(SchedulerState *schedulerState) {
  TaskQueue *currentReady = schedulerState->currentReady;
  schedulerState->currentReady
    = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

  TaskMessage *messageToSend = getAvailableMessage();
  while (messageToSend == NULL) {
    runScheduler(schedulerState);
    messageToSend = getAvailableMessage();
  }
  int returnValue = -1;
  schedulerState->currentReady = currentReady;

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(messageToSend);
  taskMessageInit(messageToSend, CONSOLE_GET_NUM_PORTS,
    /*data= */ nanoOsMessage, /* size= */ sizeof(NanoOsMessage),
    /* waiting= */ true);
  if (schedulerSendTaskMessageToTaskId(schedulerState,
    NANO_OS_CONSOLE_TASK_ID, messageToSend) != taskSuccess
  ) {
    printString("ERROR: Could not send CONSOLE_GET_NUM_PORTS to console\n");
    return returnValue; // -1
  }

  returnValue = nanoOsMessageDataValue(messageToSend, int);
  taskMessageRelease(messageToSend);

  return returnValue;
}

/// @fn int schedulerNotifyTaskComplete(TaskId taskId)
///
/// @brief Notify a waiting task that a running task has completed.
///
/// @param taskId The ID of the task to notify.
///
/// @return Returns taskSuccess on success, taskError on failure.
int schedulerNotifyTaskComplete(TaskId taskId) {
  if (sendNanoOsMessageToTaskId(taskId,
    SCHEDULER_TASK_COMPLETE, 0, 0, false) == NULL
  ) {
    return taskError;
  }

  return taskSuccess;
}

/// @fn int schedulerWaitForTaskComplete(void)
///
/// @brief Wait for another task to send us a message indicating that a
/// task is complete.
///
/// @return Returns taskSuccess on success, taskError on failure.
int schedulerWaitForTaskComplete(void) {
  TaskMessage *doneMessage
    = taskMessageQueueWaitForType(SCHEDULER_TASK_COMPLETE, NULL);
  if (doneMessage == NULL) {
    return taskError;
  }

  // We don't need any data from the message.  Just release it.
  taskMessageRelease(doneMessage);

  return taskSuccess;
}

/// @fn TaskId schedulerGetNumRunningTasks(struct timespec *timeout)
///
/// @brief Get the number of running tasks from the scheduler.
///
/// @param timeout A pointer to a struct timespec with the end time for the
///   timeout.
///
/// @return Returns the number of running tasks on success, 0 on failure.
/// There is no way for the number of running tasks to exceed the maximum
/// value of a TaskId type, so it's used here as the return type.
TaskId schedulerGetNumRunningTasks(struct timespec *timeout) {
  TaskMessage *taskMessage = NULL;
  int waitStatus = taskSuccess;
  TaskId numTaskDescriptors = 0;

  taskMessage = sendNanoOsMessageToTaskId(
    NANO_OS_SCHEDULER_TASK_ID, SCHEDULER_GET_NUM_RUNNING_TASKS,
    (NanoOsMessageData) 0, (NanoOsMessageData) 0, true);
  if (taskMessage == NULL) {
    printf("ERROR: Could not communicate with scheduler.\n");
    goto exit;
  }

  waitStatus = taskMessageWaitForDone(taskMessage, timeout);
  if (waitStatus != taskSuccess) {
    if (waitStatus == taskTimedout) {
      printf("Command to get the number of running tasks timed out.\n");
    } else {
      printf("Command to get the number of running tasks failed.\n");
    }

    // Without knowing how many tasks there are, we can't continue.  Bail.
    goto releaseMessage;
  }

  numTaskDescriptors = nanoOsMessageDataValue(taskMessage, TaskId);
  if (numTaskDescriptors == 0) {
    printf("ERROR: Number of running tasks returned from the "
      "scheduler is 0.\n");
    goto releaseMessage;
  }

releaseMessage:
  if (taskMessageRelease(taskMessage) != taskSuccess) {
    printf("ERROR: Could not release message sent to scheduler for "
      "getting the number of running tasks.\n");
  }

exit:
  return numTaskDescriptors;
}

/// @fn TaskInfo* schedulerGetTaskInfo(void)
///
/// @brief Get information about all tasks running in the system from the
/// scheduler.
///
/// @return Returns a populated, dynamically-allocated TaskInfo object on
/// success, NULL on failure.
TaskInfo* schedulerGetTaskInfo(void) {
  TaskMessage *taskMessage = NULL;
  int waitStatus = taskSuccess;

  // We don't know where our messages to the scheduler will be in its queue, so
  // we can't assume they will be processed immediately, but we can't wait
  // forever either.  Set a 100 ms timeout.
  struct timespec timeout = {0};
  timespec_get(&timeout, TIME_UTC);
  timeout.tv_nsec += 100000000;

  // Because the scheduler runs on the main coroutine, it doesn't have the
  // ability to yield.  That means it can't do anything that requires a
  // synchronus message exchange, i.e. allocating memory.  So, we need to
  // allocate memory from the current task and then pass that back to the
  // scheduler to populate.  That means we first need to know how many tasks
  // are running so that we know how much space to allocate.  So, get that
  // first.
  TaskId numTaskDescriptors = schedulerGetNumRunningTasks(&timeout);

  // We need numTaskDescriptors rows.
  TaskInfo *taskInfo = (TaskInfo*) malloc(sizeof(TaskInfo)
    + ((numTaskDescriptors - 1) * sizeof(TaskInfoElement)));
  if (taskInfo == NULL) {
    printf(
      "ERROR: Could not allocate memory for taskInfo in getTaskInfo.\n");
    goto exit;
  }

  // It is possible, although unlikely, that an additional task is started
  // between the time we made the call above and the time that our message gets
  // handled below.  We allocated our return value based upon the size that was
  // returned above and, if we're not careful, it will be possible to overflow
  // the array.  Initialize taskInfo->numTasks so that
  // schedulerGetTaskInfoCommandHandler knows the maximum number of
  // TaskInfoElements it can populated.
  taskInfo->numTasks = numTaskDescriptors;

  taskMessage
    = sendNanoOsMessageToTaskId(NANO_OS_SCHEDULER_TASK_ID,
    SCHEDULER_GET_TASK_INFO, /* func= */ 0, (intptr_t) taskInfo, true);

  if (taskMessage == NULL) {
    printf("ERROR: Could not send scheduler message to get task info.\n");
    goto freeMemory;
  }

  waitStatus = taskMessageWaitForDone(taskMessage, &timeout);
  if (waitStatus != taskSuccess) {
    if (waitStatus == taskTimedout) {
      printf("Command to get task information timed out.\n");
    } else {
      printf("Command to get task information failed.\n");
    }

    // Without knowing the data for the tasks, we can't display them.  Bail.
    goto releaseMessage;
  }

  if (taskMessageRelease(taskMessage) != taskSuccess) {
    printf("ERROR: Could not release message sent to scheduler for "
      "getting the number of running tasks.\n");
  }

  return taskInfo;

releaseMessage:
  if (taskMessageRelease(taskMessage) != taskSuccess) {
    printf("ERROR: Could not release message sent to scheduler for "
      "getting the number of running tasks.\n");
  }

freeMemory:
  free(taskInfo); taskInfo = NULL;

exit:
  return taskInfo;
}

/// @fn int schedulerKillTask(TaskId taskId)
///
/// @brief Do all the inter-task communication with the scheduler required
/// to kill a running task.
///
/// @param taskId The ID of the task to kill.
///
/// @return Returns 0 on success, 1 on failure.
int schedulerKillTask(TaskId taskId) {
  TaskMessage *taskMessage = sendNanoOsMessageToTaskId(
    NANO_OS_SCHEDULER_TASK_ID, SCHEDULER_KILL_TASK,
    (NanoOsMessageData) 0, (NanoOsMessageData) taskId, true);
  if (taskMessage == NULL) {
    printf("ERROR: Could not communicate with scheduler.\n");
    return 1;
  }

  // We don't know where our message to the scheduler will be in its queue, so
  // we can't assume it will be processed immediately, but we can't wait forever
  // either.  Set a 100 ms timeout.
  struct timespec ts = { 0, 0 };
  timespec_get(&ts, TIME_UTC);
  ts.tv_nsec += 100000000;

  int waitStatus = taskMessageWaitForDone(taskMessage, &ts);
  int returnValue = 0;
  if (waitStatus == taskSuccess) {
    NanoOsMessage *nanoOsMessage
      = (NanoOsMessage*) taskMessageData(taskMessage);
    returnValue = nanoOsMessage->data;
    if (returnValue == 0) {
      printf("Termination successful.\n");
    } else {
      printf("Task termination returned status \"%s\".\n",
        strerror(returnValue));
    }
  } else {
    returnValue = 1;
    if (waitStatus == taskTimedout) {
      printf("Command to kill PID %d timed out.\n", taskId);
    } else {
      printf("Command to kill PID %d failed.\n", taskId);
    }
  }

  if (taskMessageRelease(taskMessage) != taskSuccess) {
    returnValue = 1;
    printf("ERROR: "
      "Could not release message sent to scheduler for kill command.\n");
  }

  return returnValue;
}

/// @fn UserId schedulerGetTaskUser(void)
///
/// @brief Get the ID of the user running the current task.
///
/// @return Returns the ID of the user running the current task on success,
/// -1 on failure.
UserId schedulerGetTaskUser(void) {
  UserId userId = -1;
  TaskMessage *taskMessage
    = sendNanoOsMessageToTaskId(
    NANO_OS_SCHEDULER_TASK_ID, SCHEDULER_GET_TASK_USER,
    /* func= */ 0, /* data= */ 0, true);
  if (taskMessage == NULL) {
    printString("ERROR: Could not communicate with scheduler.\n");
    return userId; // -1
  }

  taskMessageWaitForDone(taskMessage, NULL);
  userId = nanoOsMessageDataValue(taskMessage, UserId);
  taskMessageRelease(taskMessage);

  return userId;
}

/// @fn int schedulerSetTaskUser(UserId userId)
///
/// @brief Set the user ID of the current task to the specified user ID.
///
/// @return Returns 0 on success, -1 on failure.
int schedulerSetTaskUser(UserId userId) {
  int returnValue = -1;
  TaskMessage *taskMessage
    = sendNanoOsMessageToTaskId(
    NANO_OS_SCHEDULER_TASK_ID, SCHEDULER_SET_TASK_USER,
    /* func= */ 0, /* data= */ (UserId) userId, true);
  if (taskMessage == NULL) {
    printString("ERROR: Could not communicate with scheduler.\n");
    return returnValue; // -1
  }

  taskMessageWaitForDone(taskMessage, NULL);
  returnValue = nanoOsMessageDataValue(taskMessage, int);
  taskMessageRelease(taskMessage);

  if (returnValue != 0) {
    printf("Scheduler returned \"%s\" for setTaskUser.\n",
      strerror(returnValue));
  }

  return returnValue;
}

/// @fn FileDescriptor* schedulerGetFileDescriptor(FILE *stream)
///
/// @brief Get the IoPipe object for a task given a pointer to the FILE
///   stream to write to.
///
/// @param stream A pointer to the desired FILE output stream (stdout or
///   stderr).
///
/// @return Returns the appropriate FileDescriptor object for the current
/// task on success, NULL on failure.
FileDescriptor* schedulerGetFileDescriptor(FILE *stream) {
  FileDescriptor *returnValue = NULL;
  uintptr_t fdIndex = (uintptr_t) stream;
  TaskId runningTaskIndex = getRunningTaskId() - 1;

  if (fdIndex <= allTasks[runningTaskIndex].numFileDescriptors) {
    returnValue = &allTasks[runningTaskIndex].fileDescriptors[fdIndex - 1];
  } else {
    printString("ERROR: Received request for unknown stream ");
    printInt((intptr_t) stream);
    printString(".\n");
  }

  return returnValue;
}

/// @fn int schedulerCloseAllFileDescriptors(void)
///
/// @brief Close all the open file descriptors for the currently-running
/// task.
///
/// @return Returns 0 on success, -1 on failure.
int schedulerCloseAllFileDescriptors(void) {
  TaskMessage *taskMessage = sendNanoOsMessageToTaskId(
    NANO_OS_SCHEDULER_TASK_ID, SCHEDULER_CLOSE_ALL_FILE_DESCRIPTORS,
    /* func= */ 0, /* data= */ 0, true);
  taskMessageWaitForDone(taskMessage, NULL);
  taskMessageRelease(taskMessage);

  return 0;
}

/// @fn char* schedulerGetHostname(void)
///
/// @brief Get the hostname that's read during startup.
///
/// @return Returns the hostname that's read during startup on success, NULL on
/// failure.
const char* schedulerGetHostname(void) {
  const char *hostname = NULL;
  TaskMessage *taskMessage
    = sendNanoOsMessageToTaskId(
    NANO_OS_SCHEDULER_TASK_ID, SCHEDULER_GET_HOSTNAME,
    /* func= */ 0, /* data= */ 0, true);
  if (taskMessage == NULL) {
    printString("ERROR: Could not communicate with scheduler.\n");
    return hostname; // NULL
  }

  taskMessageWaitForDone(taskMessage, NULL);
  hostname = nanoOsMessageDataValue(taskMessage, char*);
  taskMessageRelease(taskMessage);

  return hostname;
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

  ExecArgs *execArgs = (ExecArgs*) malloc(sizeof(ExecArgs));
  if (execArgs == NULL) {
    errno = ENOMEM;
    return -1;
  }

  execArgs->pathname = (char*) malloc(strlen(pathname) + 1);
  if (execArgs->pathname == NULL) {
    errno = ENOMEM;
    goto freeExecArgs;
  }
  strcpy(execArgs->pathname, pathname);

  size_t argvLen = 0;
  for (; argv[argvLen] != NULL; argvLen++);
  argvLen++; // Account for the terminating NULL element
  execArgs->argv = (char**) malloc(argvLen * sizeof(char*));
  if (execArgs->argv == NULL) {
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
    execArgs->envp = (char**) malloc(envpLen * sizeof(char*));
    if (execArgs->envp == NULL) {
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

  TaskMessage *taskMessage
    = sendNanoOsMessageToTaskId(
    NANO_OS_SCHEDULER_TASK_ID, SCHEDULER_EXECVE,
    /* func= */ 0, /* data= */ (uintptr_t) execArgs, true);
  if (taskMessage == NULL) {
    // The only way this should be possible is if all available messages are
    // in use, so use ENOMEM as the errno.
    errno = ENOMEM;
    goto freeExecArgs;
  }

  taskMessageWaitForDone(taskMessage, NULL);

  // If we got this far then the exec failed for some reason.  The error will
  // be in the data portion of the message we sent to the scheduler.
  errno = nanoOsMessageDataValue(taskMessage, int);
  taskMessageRelease(taskMessage);
freeExecArgs:
  execArgs = execArgsDestroy(execArgs);

  return -1;
}

/// @fn int schedulerSpawn(
///   pid_t *pid, const char *path,
///   const posix_spawn_file_actions_t *file_actions,
///   const posix_spawnattr_t *attrp,
///   char *const argv[], char *const envp[])
///
/// @brief NanoOs implementation of Unix posix_spawn function.
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
int schedulerSpawn(
  pid_t *pid, const char *path,
  const posix_spawn_file_actions_t *file_actions,
  const posix_spawnattr_t *attrp,
  char *const argv[], char *const envp[]
) {
  int returnValue = 0;
  if ((path == NULL) || (argv == NULL) || (argv[0] == NULL)) {
    return EFAULT;
  }

  SpawnArgs *spawnArgs = (SpawnArgs*) malloc(sizeof(SpawnArgs));
  if (spawnArgs == NULL) {
    return ENOMEM;
  }

  spawnArgs->newPid = pid;

  spawnArgs->path = (char*) malloc(strlen(path) + 1);
  if (spawnArgs->path == NULL) {
    returnValue = ENOMEM;
    goto freeSpawnArgs;
  }
  strcpy(spawnArgs->path, path);

  // Not doing anything intelligent with these two args yet.  We need to make
  // copies of these rather than casting away the `const` qualifier here if we
  // ever do use them.
  spawnArgs->fileActions = (posix_spawn_file_actions_t*) file_actions;
  spawnArgs->attrp = (posix_spawnattr_t*) attrp;

  size_t argvLen = 0;
  for (; argv[argvLen] != NULL; argvLen++);
  argvLen++; // Account for the terminating NULL element
  spawnArgs->argv = (char**) malloc(argvLen * sizeof(char*));
  if (spawnArgs->argv == NULL) {
    returnValue = ENOMEM;
    goto freeSpawnArgs;
  }

  // argvLen is guaranteed to always be at least 1, so it's safe to run to
  // (argvLen - 1) here.
  size_t ii = 0;
  for (; ii < (argvLen - 1); ii++) {
    // We know that argv[ii] isn't NULL because of the calculation for argvLen
    // above, so it's safe to use strlen.
    spawnArgs->argv[ii] = (char*) malloc(strlen(argv[ii]) + 1);
    if (spawnArgs->argv[ii] == NULL) {
      returnValue = ENOMEM;
      goto freeSpawnArgs;
    }
    strcpy(spawnArgs->argv[ii], argv[ii]);
  }
  spawnArgs->argv[ii] = NULL; // NULL-terminate the array

  if (envp != NULL) {
    size_t envpLen = 0;
    for (; envp[envpLen] != NULL; envpLen++);
    envpLen++; // Account for the terminating NULL element
    spawnArgs->envp = (char**) malloc(envpLen * sizeof(char*));
    if (spawnArgs->envp == NULL) {
      returnValue = ENOMEM;
      goto freeSpawnArgs;
    }

    // envpLen is guaranteed to always be at least 1, so it's safe to run to
    // (envpLen - 1) here.
    for (ii = 0; ii < (envpLen - 1); ii++) {
      // We know that envp[ii] isn't NULL because of the calculation for envpLen
      // above, so it's safe to use strlen.
      spawnArgs->envp[ii] = (char*) malloc(strlen(envp[ii]) + 1);
      if (spawnArgs->envp[ii] == NULL) {
        returnValue = ENOMEM;
        goto freeSpawnArgs;
      }
      strcpy(spawnArgs->envp[ii], envp[ii]);
    }
    spawnArgs->envp[ii] = NULL; // NULL-terminate the array
  } else {
    spawnArgs->envp = NULL;
  }

  TaskMessage *taskMessage
    = sendNanoOsMessageToTaskId(
    NANO_OS_SCHEDULER_TASK_ID, SCHEDULER_SPAWN,
    /* func= */ 0, /* data= */ (uintptr_t) spawnArgs, true);
  if (taskMessage == NULL) {
    // The only way this should be possible is if all available messages are
    // in use, so use ENOMEM as the errno.
    errno = ENOMEM;
    goto freeSpawnArgs;
  }

  taskMessageWaitForDone(taskMessage, NULL);
  returnValue = nanoOsMessageDataValue(taskMessage, int);
  taskMessageRelease(taskMessage);

  return returnValue;

freeSpawnArgs:
  spawnArgs = spawnArgsDestroy(spawnArgs);

  return returnValue;
}

/// @fn int schedulerAssignMemory(void *ptr)
///
/// @brief Assign a piece of memory to be owned by the scheduler.  This
/// protects the memory from being automatically deleted when the process
/// exits.
///
/// @param ptr A pointer to the block of memory to assign to the scheduler.
///
/// @return Returns 0 on success, -errno on failure.
int schedulerAssignMemory(void *ptr) {
  TaskMessage *taskMessage = getAvailableMessage();
  while (taskMessage == NULL) {
    taskYield();
    taskMessage = getAvailableMessage();
  }

  taskMessageInit(taskMessage, SCHEDULER_ASSIGN_MEMORY, ptr, 0, true);

  if (sendTaskMessageToTaskId(NANO_OS_SCHEDULER_TASK_ID, taskMessage)
    != taskSuccess
  ) {
    taskMessageRelease(taskMessage);
    fprintf(stderr,
      "ERROR: Could not send SCHEDULER_ASSIGN_MEMORY message to scheduler\n");
    return -EIO;
  }

  taskMessageWaitForDone(taskMessage, NULL);

  int returnValue = (int) ((intptr_t) taskMessageData(taskMessage));
  taskMessageRelease(taskMessage);

  return returnValue;
}

////////////////////////////////////////////////////////////////////////////////
// Scheduler command handlers and support functions
////////////////////////////////////////////////////////////////////////////////

/// @fn int closeTaskFileDescriptors(
///   SchedulerState *schedulerState, TaskDescriptor *taskDescriptor)
///
/// @brief Helper function to close out the file descriptors owned by a task
/// when it exits or is killed.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskDescriptor A pointer to the TaskDescriptor that holds the
///   fileDescriptors array to close.
///
/// @return Returns 0 on success, -1 on failure.
int closeTaskFileDescriptors(
  SchedulerState *schedulerState, TaskDescriptor *taskDescriptor
) {
  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    TaskQueue *currentReady = schedulerState->currentReady;
    schedulerState->currentReady
      = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

    FileDescriptor *fileDescriptors = taskDescriptor->fileDescriptors;
    if (fileDescriptors != standardUserFileDescriptors) {
      TaskMessage *messageToSend = getAvailableMessage();
      while (messageToSend == NULL) {
        runScheduler(schedulerState);
        messageToSend = getAvailableMessage();
      }

      uint8_t numFileDescriptors = taskDescriptor->numFileDescriptors;
      for (uint8_t ii = 0; ii < numFileDescriptors; ii++) {
        TaskId waitingOutputTaskId
          = fileDescriptors[ii].outputPipe.taskId;
        if ((waitingOutputTaskId != TASK_ID_NOT_SET)
          && (waitingOutputTaskId != NANO_OS_CONSOLE_TASK_ID)
        ) {
          TaskDescriptor *waitingTaskDescriptor
            = &schedulerState->allTasks[waitingOutputTaskId - 1];

          // Clear the taskId of the waiting task's stdin file descriptor.
          waitingTaskDescriptor->fileDescriptors[
            STDIN_FILE_DESCRIPTOR_INDEX].
            inputPipe.taskId = TASK_ID_NOT_SET;

          // Send an empty message to the waiting task so that it will become
          // unblocked.
          taskMessageInit(messageToSend,
              fileDescriptors[ii].outputPipe.messageType,
              /*data= */ NULL, /* size= */ 0, /* waiting= */ false);
          taskMessageQueuePush(waitingTaskDescriptor, messageToSend);
          // Give the task a chance to unblock.
          taskResume(waitingTaskDescriptor, NULL);

          // The function that was waiting should have released the message we
          // sent it.  Get another one.
          messageToSend = getAvailableMessage();
          while (messageToSend == NULL) {
            runScheduler(schedulerState);
            messageToSend = getAvailableMessage();
          }
        }

        TaskId waitingInputTaskId
          = fileDescriptors[ii].inputPipe.taskId;
        if ((waitingInputTaskId != TASK_ID_NOT_SET)
          && (waitingInputTaskId != NANO_OS_CONSOLE_TASK_ID)
        ) {
          TaskDescriptor *waitingTaskDescriptor
            = &schedulerState->allTasks[waitingInputTaskId - 1];

          // Clear the taskId of the waiting task's stdin file descriptor.
          waitingTaskDescriptor->fileDescriptors[
            STDOUT_FILE_DESCRIPTOR_INDEX].
            outputPipe.taskId = TASK_ID_NOT_SET;

          // Send an empty message to the waiting task so that it will become
          // unblocked.
          taskMessageInit(messageToSend,
              fileDescriptors[ii].outputPipe.messageType,
              /*data= */ NULL, /* size= */ 0, /* waiting= */ false);
          taskMessageQueuePush(waitingTaskDescriptor, messageToSend);
          // Give the task a chance to unblock.
          taskResume(waitingTaskDescriptor, NULL);

          // The function that was waiting should have released the message we
          // sent it.  Get another one.
          messageToSend = getAvailableMessage();
          while (messageToSend == NULL) {
            runScheduler(schedulerState);
            messageToSend = getAvailableMessage();
          }
        }
      }

      // schedFree will pull an available message.  Release the one we've been
      // using so that we're guaranteed it will be successful.
      taskMessageRelease(messageToSend);
      schedFree(fileDescriptors); taskDescriptor->fileDescriptors = NULL;
    }

    schedulerState->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    HAL->shutdown(HAL_SHUTDOWN_OFF);
  }
  return 0;
}

/// @fn FILE* schedFopen(SchedulerState *schedulerState,
///   const char *pathname, const char *mode)
///
/// @brief Version of fopen for the scheduler.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
/// @param pathname A pointer to the C string with the full path to the file to
///   open.
/// @param mode A pointer to the C string that defines the way to open the file.
///
/// @return Returns a pointer to the opened file on success, NULL on failure.
FILE* schedFopen(SchedulerState *schedulerState,
  const char *pathname, const char *mode
) {
  FILE *returnValue = NULL;

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    TaskQueue *currentReady = schedulerState->currentReady;
    schedulerState->currentReady
      = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

    printDebugString("schedFopen: Getting message\n");
    TaskMessage *taskMessage = getAvailableMessage();
    while (taskMessage == NULL) {
      runScheduler(schedulerState);
      taskMessage = getAvailableMessage();
    }
    printDebugString("schedFopen: Message retrieved\n");
    NanoOsMessage *nanoOsMessage
      = (NanoOsMessage*) taskMessageData(taskMessage);
    nanoOsMessage->func = (intptr_t) mode;
    nanoOsMessage->data = (intptr_t) pathname;
    printDebugString("schedFopen: Initializing message\n");
    taskMessageInit(taskMessage, FILESYSTEM_OPEN_FILE,
      nanoOsMessage, sizeof(*nanoOsMessage), true);
    printDebugString("schedFopen: Pushing message\n");
    taskMessageQueuePush(
      &schedulerState->allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1],
      taskMessage);

    printDebugString("schedFopen: Resuming filesystem\n");
    while (taskMessageDone(taskMessage) == false) {
      runScheduler(schedulerState);
    }
    printDebugString("schedFopen: Filesystem message is done\n");

    returnValue = nanoOsMessageDataPointer(taskMessage, FILE*);

    taskMessageRelease(taskMessage);
    schedulerState->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    HAL->shutdown(HAL_SHUTDOWN_OFF);
  }

  return returnValue;
}

/// @fn int schedFclose(SchedulerState *schedulerState, FILE *stream)
///
/// @brief Version of fclose for the scheduler.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
/// @param stream A pointer to the FILE object that was previously opened.
///
/// @return Returns 0 on success, EOF on failure.  On failure, the value of
/// errno is also set to the appropriate error.
int schedFclose(SchedulerState *schedulerState, FILE *stream) {
  int returnValue = 0;

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    TaskQueue *currentReady = schedulerState->currentReady;
    schedulerState->currentReady
      = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

    TaskMessage *taskMessage = getAvailableMessage();
    while (taskMessage == NULL) {
      runScheduler(schedulerState);
      taskMessage = getAvailableMessage();
    }
    NanoOsMessage *nanoOsMessage
      = (NanoOsMessage*) taskMessageData(taskMessage);
    FilesystemFcloseParameters fcloseParameters;
    fcloseParameters.stream = stream;
    fcloseParameters.returnValue = 0;
    nanoOsMessage->data = (intptr_t) &fcloseParameters;
    taskMessageInit(taskMessage, FILESYSTEM_CLOSE_FILE,
      nanoOsMessage, sizeof(*nanoOsMessage), true);
    taskMessageQueuePush(
      &schedulerState->allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1],
      taskMessage);

    while (taskMessageDone(taskMessage) == false) {
      runScheduler(schedulerState);
    }

    if (fcloseParameters.returnValue != 0) {
      errno = -fcloseParameters.returnValue;
      returnValue = EOF;
    }

    taskMessageRelease(taskMessage);
    schedulerState->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    HAL->shutdown(HAL_SHUTDOWN_OFF);
  }

  return returnValue;
}

/// @fn int schedRemove(SchedulerState *schedulerState, const char *pathname)
///
/// @brief Version of remove for the scheduler.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
/// @param pathname A pointer to the C string with the full path to the file to
///   remove.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int schedRemove(SchedulerState *schedulerState, const char *pathname) {
  int returnValue = 0;

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    TaskQueue *currentReady = schedulerState->currentReady;
    schedulerState->currentReady
      = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

    TaskMessage *taskMessage = getAvailableMessage();
    while (taskMessage == NULL) {
      runScheduler(schedulerState);
      taskMessage = getAvailableMessage();
    }
    NanoOsMessage *nanoOsMessage
      = (NanoOsMessage*) taskMessageData(taskMessage);
    nanoOsMessage->data = (intptr_t) pathname;
    taskMessageInit(taskMessage, FILESYSTEM_REMOVE_FILE,
      nanoOsMessage, sizeof(*nanoOsMessage), true);
    taskMessageQueuePush(
      &schedulerState->allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1],
      taskMessage);

    while (taskMessageDone(taskMessage) == false) {
      runScheduler(schedulerState);
    }

    returnValue = nanoOsMessageDataValue(taskMessage, int);
    if (returnValue != 0) {
      // returnValue holds a negative errno.  Set errno for the current task
      // and return -1 like we're supposed to.
      errno = -returnValue;
      returnValue = -1;
    }

    taskMessageRelease(taskMessage);
    schedulerState->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    HAL->shutdown(HAL_SHUTDOWN_OFF);
  }

  return returnValue;
}

/// @fn size_t schedFread(SchedulerState *schedulerState,
///   void *ptr, size_t size, size_t nmemb, FILE *stream)
///
/// @brief Version of fread for the scheduler.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
/// @param ptr A pointer to the buffer to read data into.
/// @param size The size, in bytes, of each item that is to be read in.
/// @param nmemb The number of items to read from the file.
/// @param stream A pointer to the open FILE to read data in from.
///
/// @return Returns the number of items successfully read in.
size_t schedFread(SchedulerState *schedulerState,
  void *ptr, size_t size, size_t nmemb, FILE *stream
) {
  FilesystemIoCommandParameters filesystemIoCommandParameters = {
    .file = stream,
    .buffer = ptr,
    .length = size * nmemb
  };

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    TaskQueue *currentReady = schedulerState->currentReady;
    schedulerState->currentReady
      = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

    TaskMessage *taskMessage = getAvailableMessage();
    while (taskMessage == NULL) {
      runScheduler(schedulerState);
      taskMessage = getAvailableMessage();
    }
    NanoOsMessage *nanoOsMessage
      = (NanoOsMessage*) taskMessageData(taskMessage);
    nanoOsMessage->data = (intptr_t) &filesystemIoCommandParameters;
    taskMessageInit(taskMessage, FILESYSTEM_READ_FILE,
      nanoOsMessage, sizeof(*nanoOsMessage), true);
    taskMessageQueuePush(
      &schedulerState->allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1],
      taskMessage);

    while (taskMessageDone(taskMessage) == false) {
      runScheduler(schedulerState);
    }

    taskMessageRelease(taskMessage);
    schedulerState->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    HAL->shutdown(HAL_SHUTDOWN_OFF);
  }

  return filesystemIoCommandParameters.length / size;
}

/// @fn size_t schedFwrite(SchedulerState *schedulerState,
///   void *ptr, size_t size, size_t nmemb, FILE *stream)
///
/// @brief Version of fwrite for the scheduler.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
/// @param ptr A pointer to the buffer to write data from.
/// @param size The size, in bytes, of each item that is to be written out.
/// @param nmemb The number of items to written to the file.
/// @param stream A pointer to the open FILE to write data out to.
///
/// @return Returns the number of items successfully written out.
size_t schedFwrite(SchedulerState *schedulerState,
  void *ptr, size_t size, size_t nmemb, FILE *stream
) {
  FilesystemIoCommandParameters filesystemIoCommandParameters = {
    .file = stream,
    .buffer = ptr,
    .length = size * nmemb
  };

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    TaskQueue *currentReady = schedulerState->currentReady;
    schedulerState->currentReady
      = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

    TaskMessage *taskMessage = getAvailableMessage();
    while (taskMessage == NULL) {
      runScheduler(schedulerState);
      taskMessage = getAvailableMessage();
    }
    NanoOsMessage *nanoOsMessage
      = (NanoOsMessage*) taskMessageData(taskMessage);
    nanoOsMessage->data = (intptr_t) &filesystemIoCommandParameters;
    taskMessageInit(taskMessage, FILESYSTEM_WRITE_FILE,
      nanoOsMessage, sizeof(*nanoOsMessage), true);
    taskMessageQueuePush(
      &schedulerState->allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1],
      taskMessage);

    while (taskMessageDone(taskMessage) == false) {
      runScheduler(schedulerState);
    }

    taskMessageRelease(taskMessage);
    schedulerState->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    HAL->shutdown(HAL_SHUTDOWN_OFF);
  }

  return filesystemIoCommandParameters.length / size;
}

/// @fn int schedFgets(SchedulerState *schedulerState,
///   char *buffer, int size, FILE *stream)
///
/// @brief Version of fgets for the scheduler.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
/// @param buffer The character buffer to read the file data into.
/// @param size The size of the buffer provided, in bytes.
/// @param stream A pointer to the FILE object that was previously opened.
///
/// @return Returns a pointer to the provided buffer on success, NULL on
/// failure.
char* schedFgets(SchedulerState *schedulerState,
  char *buffer, int size, FILE *stream
) {
  char *returnValue = NULL;

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    TaskQueue *currentReady = schedulerState->currentReady;
    schedulerState->currentReady
      = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

    FilesystemIoCommandParameters filesystemIoCommandParameters = {
      .file = stream,
      .buffer = buffer,
      .length = (uint32_t) size - 1
    };

    TaskMessage *taskMessage = getAvailableMessage();
    while (taskMessage == NULL) {
      runScheduler(schedulerState);
      taskMessage = getAvailableMessage();
    }
    NanoOsMessage *nanoOsMessage
      = (NanoOsMessage*) taskMessageData(taskMessage);
    nanoOsMessage->data = (intptr_t) &filesystemIoCommandParameters;
    taskMessageInit(taskMessage, FILESYSTEM_READ_FILE,
      nanoOsMessage, sizeof(*nanoOsMessage), true);
    taskMessageQueuePush(
      &schedulerState->allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1],
      taskMessage);

    while (taskMessageDone(taskMessage) == false) {
      runScheduler(schedulerState);
    }
    if (filesystemIoCommandParameters.length > 0) {
      buffer[filesystemIoCommandParameters.length] = '\0';
      returnValue = buffer;
    }

    taskMessageRelease(taskMessage);
    schedulerState->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    HAL->shutdown(HAL_SHUTDOWN_OFF);
  }

  return returnValue;
}

/// @fn int schedFputs(SchedulerState *schedulerState,
///   const char *s, FILE *stream)
///
/// @brief Version of fputs for the scheduler.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
/// @param s A pointer to the C string to write to the file.
/// @param stream A pointer to the FILE object that was previously opened.
///
/// @return Returns 0 on success, EOF on failure.  On failure, the value of
/// errno is also set to the appropriate error.
int schedFputs(SchedulerState *schedulerState,
  const char *s, FILE *stream
) {
  int returnValue = 0;

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    TaskQueue *currentReady = schedulerState->currentReady;
    schedulerState->currentReady
      = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

    FilesystemIoCommandParameters filesystemIoCommandParameters = {
      .file = stream,
      .buffer = (void*) s,
      .length = (uint32_t) strlen(s)
    };

    TaskMessage *taskMessage = getAvailableMessage();
    while (taskMessage == NULL) {
      runScheduler(schedulerState);
      taskMessage = getAvailableMessage();
    }
    NanoOsMessage *nanoOsMessage
      = (NanoOsMessage*) taskMessageData(taskMessage);
    nanoOsMessage->data = (intptr_t) &filesystemIoCommandParameters;
    taskMessageInit(taskMessage, FILESYSTEM_WRITE_FILE,
      nanoOsMessage, sizeof(*nanoOsMessage), true);
    taskMessageQueuePush(
      &schedulerState->allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1],
      taskMessage);

    while (taskMessageDone(taskMessage) == false) {
      runScheduler(schedulerState);
    }
    if (filesystemIoCommandParameters.length == 0) {
      returnValue = EOF;
    }

    taskMessageRelease(taskMessage);
    schedulerState->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    HAL->shutdown(HAL_SHUTDOWN_OFF);
  }

  return returnValue;
}

/// @fn int schedulerKillTaskCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Kill a task identified by its task ID.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received that contains
///   the information about the task to kill.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerKillTaskCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  int returnValue = 0;

  TaskMessage *schedulerTaskCompleteMessage = getAvailableMessage();
  if (schedulerTaskCompleteMessage == NULL) {
    // We have to have a message to send to unblock the console.  Fail and try
    // again later.
    return EBUSY;
  }
  taskMessageInit(schedulerTaskCompleteMessage,
    SCHEDULER_TASK_COMPLETE, 0, 0, false);

  UserId callingUserId
    = allTasks[taskId(taskMessageFrom(taskMessage)) - 1].userId;
  TaskId taskId
    = nanoOsMessageDataValue(taskMessage, TaskId);
  int taskIndex = taskId - 1;
  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);

  if ((taskId >= NANO_OS_FIRST_USER_TASK_ID)
    && (taskId <= NANO_OS_NUM_TASKS)
    && (taskRunning(&allTasks[taskIndex]))
  ) {
    if ((allTasks[taskIndex].userId == callingUserId)
      || (callingUserId == ROOT_USER_ID)
    ) {
      TaskDescriptor *taskDescriptor = &allTasks[taskIndex];
      // Regardless of whether or not we succeed at terminating it, we have
      // to remove it from its queue.  We don't know which queue it's on,
      // though.  The fact that we're killing it makes it likely that it's hung.
      // The most likely reason is that it's waiting on something with an
      // infinite timeout, so it's most likely to be on the waiting queue.  The
      // second most likely reason is that it's in an infinite loop, so the
      // ready queue is the second-most-likely place it could be.  The least-
      // likely place for it to be would be the timed waiting queue with a very
      // long timeout.  So, attempt to remove from the queues in that order.
      if (taskQueueRemove(&schedulerState->waiting, taskDescriptor) != 0
      ) {
        if (taskQueueRemove(taskDescriptor->readyQueue, taskDescriptor) != 0
        ) {
          taskQueueRemove(&schedulerState->timedWaiting, taskDescriptor);
        }
      }

      // Tell the console to release the port for us.  We will forward it
      // the message we acquired above, which it will use to send to the
      // correct shell to unblock it.  We need to do this before terminating
      // the task because, in the event the task we're terminating is one
      // of the shell task slots, the message won't get released because
      // there's no shell blocking waiting for the message.
      schedulerSendNanoOsMessageToTaskId(
        schedulerState,
        NANO_OS_CONSOLE_TASK_ID,
        CONSOLE_RELEASE_PID_PORT,
        (intptr_t) schedulerTaskCompleteMessage,
        taskId);

      // Forward the message on to the memory manager to have it clean up the
      // task's memory.  *DO NOT* mark the message as done.  The memory
      // manager will do that.
      taskMessageInit(taskMessage, MEMORY_MANAGER_FREE_TASK_MEMORY,
        nanoOsMessage, sizeof(*nanoOsMessage), /* waiting= */ true);
      sendTaskMessageToTask(
        &schedulerState->allTasks[NANO_OS_MEMORY_MANAGER_TASK_ID - 1],
        taskMessage);

      // Close the file descriptors before we terminate the task so that
      // anything that gets sent to the task's queue gets cleaned up when
      // we terminate it.
      closeTaskFileDescriptors(schedulerState, taskDescriptor);

      if (taskTerminate(taskDescriptor) == taskSuccess) {
        taskHandleSetContext(taskDescriptor->taskHandle,
          taskDescriptor);
        taskDescriptor->name = NULL;
        taskDescriptor->userId = NO_USER_ID;

        // It's likely (i.e. almost certain) that the killed task was a user
        // task that was killed by a user task.  That would mean that we were
        // in the middle of processing a user task queue, the number of items
        // in which was captured before the runScheduler loop was started.
        // (See the logic at the end of startScheduler.)  Rather than pushing
        // the killed task onto the free queue, push it back onto its ready
        // queue so that we don't try to pop a task from an empty queue.
        // runScheduler will do the cleanup and put the task onto the free
        // queue again once it picks back up again.
        taskQueuePush(taskDescriptor->readyQueue, taskDescriptor);
      } else {
        // Tell the caller that we've failed.
        nanoOsMessage->data = 1;
        if (taskMessageSetDone(taskMessage) != taskSuccess) {
          printString("ERROR: Could not mark message done in "
            "schedulerKillTaskCommandHandler.\n");
        }

        // Do *NOT* push the task back onto the free queue in this case.
        // If we couldn't terminate it, it's not valid to try and reuse it for
        // another task.
      }
    } else {
      // Tell the caller that we've failed.
      nanoOsMessage->data = EACCES; // Permission denied
      if (taskMessageSetDone(taskMessage) != taskSuccess) {
        printString("ERROR: Could not mark message done in "
          "schedulerKillTaskCommandHandler.\n");
      }
      if (taskMessageRelease(schedulerTaskCompleteMessage)
        != taskSuccess
      ) {
        printString("ERROR: "
          "Could not release schedulerTaskCompleteMessage.\n");
      }
    }
  } else {
    // Tell the caller that we've failed.
    nanoOsMessage->data = EINVAL; // Invalid argument
    if (taskMessageSetDone(taskMessage) != taskSuccess) {
      printString("ERROR: "
        "Could not mark message done in schedulerKillTaskCommandHandler.\n");
    }
    if (taskMessageRelease(schedulerTaskCompleteMessage)
      != taskSuccess
    ) {
      printString("ERROR: "
        "Could not release schedulerTaskCompleteMessage.\n");
    }
  }

  // DO NOT release the message since that's done by the caller.

  return returnValue;
}

/// @fn int schedulerGetNumTaskDescriptorsCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Get the number of tasks that are currently running in the system.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received.  This will be
///   reused for the reply.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerGetNumTaskDescriptorsCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  int returnValue = 0;

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);

  uint8_t numTaskDescriptors = 0;
  for (int ii = 1; ii <= NANO_OS_NUM_TASKS; ii++) {
    if (taskRunning(&schedulerState->allTasks[ii - 1])) {
      numTaskDescriptors++;
    }
  }
  nanoOsMessage->data = numTaskDescriptors;

  taskMessageSetDone(taskMessage);

  // DO NOT release the message since the caller is waiting on the response.

  return returnValue;
}

/// @fn int schedulerGetTaskInfoCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Fill in a provided array with information about the currently-running
/// tasks.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received.  This will be
///   reused for the reply.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerGetTaskInfoCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  int returnValue = 0;

  TaskInfo *taskInfo
    = nanoOsMessageDataPointer(taskMessage, TaskInfo*);
  int maxTasks = taskInfo->numTasks;
  TaskInfoElement *tasks = taskInfo->tasks;
  int idx = 0;
  for (int ii = 1; (ii <= NANO_OS_NUM_TASKS) && (idx < maxTasks); ii++) {
    if (taskRunning(&schedulerState->allTasks[ii - 1])) {
      tasks[idx].pid = (int) schedulerState->allTasks[ii - 1].taskId;
      tasks[idx].name = schedulerState->allTasks[ii - 1].name;
      tasks[idx].userId = schedulerState->allTasks[ii - 1].userId;
      idx++;
    }
  }

  // It's possible that a task completed between the time that taskInfo
  // was allocated and now, so set the value of numTasks to the value of
  // idx.
  taskInfo->numTasks = idx;

  taskMessageSetDone(taskMessage);

  // DO NOT release the message since the caller is waiting on the response.

  return returnValue;
}

/// @fn int schedulerGetTaskUserCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Get the number of tasks that are currently running in the system.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received.  This will be
///   reused for the reply.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerGetTaskUserCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  int returnValue = 0;
  TaskId callingTaskId = taskId(taskMessageFrom(taskMessage));
  NanoOsMessage *nanoOsMessage = (NanoOsMessage*) taskMessageData(taskMessage);
  if ((callingTaskId > 0) && (callingTaskId <= NANO_OS_NUM_TASKS)) {
    nanoOsMessage->data = schedulerState->allTasks[callingTaskId - 1].userId;
  } else {
    nanoOsMessage->data = -1;
  }

  taskMessageSetDone(taskMessage);

  // DO NOT release the message since the caller is waiting on the response.

  return returnValue;
}

/// @fn int schedulerSetTaskUserCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Get the number of tasks that are currently running in the system.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received.
///   This will be reused for the reply.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerSetTaskUserCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  int returnValue = 0;
  TaskId callingTaskId = taskId(taskMessageFrom(taskMessage));
  UserId userId = nanoOsMessageDataValue(taskMessage, UserId);
  NanoOsMessage *nanoOsMessage = (NanoOsMessage*) taskMessageData(taskMessage);
  nanoOsMessage->data = -1;

  if ((callingTaskId > 0) && (callingTaskId <= NANO_OS_NUM_TASKS)) {
    if ((schedulerState->allTasks[callingTaskId - 1].userId == -1)
      || (userId == -1)
    ) {
      schedulerState->allTasks[callingTaskId - 1].userId = userId;
      nanoOsMessage->data = 0;
    } else {
      nanoOsMessage->data = EACCES;
    }
  }

  taskMessageSetDone(taskMessage);

  // DO NOT release the message since the caller is waiting on the response.

  return returnValue;
}

/// @fn int schedulerCloseAllFileDescriptorsCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Get the number of tasks that are currently running in the system.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerCloseAllFileDescriptorsCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  int returnValue = 0;
  TaskId callingTaskId = taskId(taskMessageFrom(taskMessage));
  TaskDescriptor *taskDescriptor
    = &schedulerState->allTasks[callingTaskId - 1];
  closeTaskFileDescriptors(schedulerState, taskDescriptor);

  taskMessageSetDone(taskMessage);

  return returnValue;
}

/// @fn int schedulerGetHostnameCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Get the hostname that's read when the scheduler starts.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerGetHostnameCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  int returnValue = 0;

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  nanoOsMessage->data = (uintptr_t) schedulerState->hostname;
  taskMessageSetDone(taskMessage);

  return returnValue;
}

/// @fn int schedulerExecveCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Exec a new program in place of a running program.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerExecveCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  int returnValue = 0;
  if (taskMessage == NULL) {
    // This should be impossible, but there's nothing to do.  Return good
    // status.
    return returnValue; // 0
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  ExecArgs *execArgs = nanoOsMessageDataValue(taskMessage, ExecArgs*);
  if (execArgs == NULL) {
    printString("ERROR! execArgs provided was NULL.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  }
  nanoOsMessage->data = 0; // Until proven otherwise
  execArgs->callingTaskId = taskId(taskMessageFrom(taskMessage));

  char *pathname = execArgs->pathname;
  if (pathname == NULL) {
    // Invalid
    printString("ERROR! pathname provided was NULL.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **argv = execArgs->argv;
  if (argv == NULL) {
    // Invalid
    printString("ERROR! argv provided was NULL.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  } else if (argv[0] == NULL) {
    // Invalid
    printString("ERROR! argv[0] provided was NULL.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **envp = execArgs->envp;

  if (assignMemory(execArgs, NANO_OS_SCHEDULER_TASK_ID) != 0) {
    printString("WARNING: Could not assign execArgs to scheduler.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(pathname, NANO_OS_SCHEDULER_TASK_ID) != 0) {
    printString("WARNING: Could not assign pathname to scheduler.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(argv, NANO_OS_SCHEDULER_TASK_ID) != 0) {
    printString("WARNING: Could not assign argv to scheduler.\n");
    printString("Undefined behavior.\n");
  }
  for (int ii = 0; argv[ii] != NULL; ii++) {
    if (assignMemory(argv[ii], NANO_OS_SCHEDULER_TASK_ID) != 0) {
      printString("WARNING: Could not assign argv[");
      printInt(ii);
      printString("] to scheduler.\n");
      printString("Undefined behavior.\n");
    }
  }

  if (envp != NULL) {
    if (assignMemory(envp, NANO_OS_SCHEDULER_TASK_ID) != 0) {
      printString("WARNING: Could not assign envp to scheduler.\n");
      printString("Undefined behavior.\n");
    }
    for (int ii = 0; envp[ii] != NULL; ii++) {
      if (assignMemory(envp[ii], NANO_OS_SCHEDULER_TASK_ID) != 0) {
        printString("WARNING: Could not assign envp[");
        printInt(ii);
        printString("] to scheduler.\n");
        printString("Undefined behavior.\n");
      }
    }
  }

  TaskDescriptor *taskDescriptor = &schedulerState->allTasks[
    taskId(taskMessageFrom(taskMessage)) - 1];
  // The task should be blocked in taskMessageQueueWaitForType waiting
  // on a condition with an infinite timeout.  So, it *SHOULD* be on the
  // waiting queue.  Take no chances, though.
  if (taskQueueRemove(&schedulerState->waiting, taskDescriptor) != 0) {
    if (taskQueueRemove(&schedulerState->timedWaiting, taskDescriptor)
      != 0
    ) {
      taskQueueRemove(taskDescriptor->readyQueue, taskDescriptor);
    }
  }

  // Kill and clear out the calling task.
  taskTerminate(taskDescriptor);
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);

  // We don't want to wait for the memory manager to release the memory.  Make
  // it do it immediately.
  if (schedulerSendNanoOsMessageToTaskId(
    schedulerState, NANO_OS_MEMORY_MANAGER_TASK_ID,
    MEMORY_MANAGER_FREE_TASK_MEMORY,
    /* func= */ 0, taskDescriptor->taskId)
  ) {
    printString("WARNING: Could not release memory for task ");
    printInt(taskDescriptor->taskId);
    printString("\n");
    printString("Memory leak.\n");
  }

  execArgs->schedulerState = schedulerState;
  if (taskCreate(taskDescriptor, execCommand, execArgs) == taskError) {
    printString(
      "ERROR: Could not configure task handle for new command.\n");
  }

  if (assignMemory(execArgs, taskDescriptor->taskId) != 0) {
    printString("WARNING: Could not assign execArgs to exec task.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(pathname, taskDescriptor->taskId) != 0) {
    printString("WARNING: Could not assign pathname to exec task.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(argv, taskDescriptor->taskId) != 0) {
    printString("WARNING: Could not assign argv to exec task.\n");
    printString("Undefined behavior.\n");
  }
  for (int ii = 0; argv[ii] != NULL; ii++) {
    if (assignMemory(argv[ii], taskDescriptor->taskId) != 0) {
      printString("WARNING: Could not assign argv[");
      printInt(ii);
      printString("] to exec task.\n");
      printString("Undefined behavior.\n");
    }
  }

  if (envp != NULL) {
    if (assignMemory(envp, taskDescriptor->taskId) != 0) {
      printString("WARNING: Could not assign envp to exec task.\n");
      printString("Undefined behavior.\n");
    }
    for (int ii = 0; envp[ii] != NULL; ii++) {
      if (assignMemory(envp[ii], taskDescriptor->taskId) != 0) {
        printString("WARNING: Could not assign envp[");
        printInt(ii);
        printString("] to exec task.\n");
        printString("Undefined behavior.\n");
      }
    }
  }

  taskDescriptor->overlayDir = pathname;
  taskDescriptor->overlay = "main";
  taskDescriptor->envp = envp;
  taskDescriptor->name = argv[0];

  /*
   * This shouldn't be necessary.  In hindsight, perhaps I shouldn't be
   * assigning a port to a task at all.  That's not the way Unix works.  I
   * should probably remove the ability to exclusively assign a port to a
   * task at some point in the future.  Delete this if I haven't found a
   * good reason to continue granting exclusive access to a task by then.
   * Leaving it uncommented in an if (false) so that compilation will fail
   * if/when I delete the functionality.
   *
   * JBC 14-Nov-2025
   */
  if (false) {
    if (schedulerAssignPortToTaskId(schedulerState,
      /*commandDescriptor->consolePort*/ 255, taskDescriptor->taskId)
      != taskSuccess
    ) {
      printString("WARNING: Could not assign console port to task.\n");
    }
  }

  // Resume the coroutine so that it picks up all the pointers it needs before
  // we release the message we were sent.
  taskResume(taskDescriptor, NULL);

  // Put the task on the ready queue.
  taskQueuePush(taskDescriptor->readyQueue, taskDescriptor);

  taskMessageRelease(taskMessage);

  return returnValue;
}

/// @fn int schedulerSpawnCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Spawn a program in a new process.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerSpawnCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  int returnValue = 0;
  if (taskMessage == NULL) {
    // This should be impossible, but there's nothing to do.  Return good
    // status.
    return returnValue; // 0
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  SpawnArgs *spawnArgs = nanoOsMessageDataValue(taskMessage, SpawnArgs*);
  if (spawnArgs == NULL) {
    printString("ERROR! spawnArgs provided was NULL.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  }
  nanoOsMessage->data = 0; // Until proven otherwise

  char *pathname = spawnArgs->path;
  if (pathname == NULL) {
    // Invalid
    printString("ERROR! pathname provided was NULL.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **argv = spawnArgs->argv;
  if (argv == NULL) {
    // Invalid
    printString("ERROR! argv provided was NULL.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  } else if (argv[0] == NULL) {
    // Invalid
    printString("ERROR! argv[0] provided was NULL.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **envp = spawnArgs->envp;

  TaskDescriptor *taskDescriptor = taskQueuePop(&schedulerState->free);
  if (taskDescriptor == NULL) {
    printString("Out of task slots to launch task.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  }

  // Initialize the new task.
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);

  ExecArgs *execArgs = (ExecArgs*) schedMalloc(sizeof(ExecArgs));
  if (execArgs == NULL) {
    printString("Out of memory for ExecArgs.\n");
    nanoOsMessage->data = EINVAL;
    taskMessageSetDone(taskMessage);
    return returnValue; // 0; Don't retry this command
  }
  execArgs->callingTaskId = taskId(taskMessageFrom(taskMessage));
  execArgs->pathname = spawnArgs->path;
  execArgs->argv = spawnArgs->argv;
  execArgs->envp = spawnArgs->envp;
  execArgs->schedulerState = schedulerState;

  schedFree(spawnArgs); spawnArgs = NULL;

  if (taskCreate(taskDescriptor, execCommand, execArgs) == taskError) {
    printString(
      "ERROR: Could not configure task handle for new command.\n");
  }

  if (assignMemory(execArgs, taskDescriptor->taskId) != 0) {
    printString("WARNING: Could not assign execArgs to exec task.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(pathname, taskDescriptor->taskId) != 0) {
    printString("WARNING: Could not assign pathname to exec task.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(argv, taskDescriptor->taskId) != 0) {
    printString("WARNING: Could not assign argv to exec task.\n");
    printString("Undefined behavior.\n");
  }
  for (int ii = 0; argv[ii] != NULL; ii++) {
    if (assignMemory(argv[ii], taskDescriptor->taskId) != 0) {
      printString("WARNING: Could not assign argv[");
      printInt(ii);
      printString("] to exec task.\n");
      printString("Undefined behavior.\n");
    }
  }

  if (envp != NULL) {
    if (assignMemory(envp, taskDescriptor->taskId) != 0) {
      printString("WARNING: Could not assign envp to exec task.\n");
      printString("Undefined behavior.\n");
    }
    for (int ii = 0; envp[ii] != NULL; ii++) {
      if (assignMemory(envp[ii], taskDescriptor->taskId) != 0) {
        printString("WARNING: Could not assign envp[");
        printInt(ii);
        printString("] to exec task.\n");
        printString("Undefined behavior.\n");
      }
    }
  }

  taskDescriptor->overlayDir = pathname;
  taskDescriptor->overlay = "main";
  taskDescriptor->envp = envp;
  taskDescriptor->name = argv[0];

  // Resume the coroutine so that it picks up all the pointers it needs before
  // we release the message we were sent.
  taskResume(taskDescriptor, NULL);

  // Put the task on the ready queue.
  taskQueuePush(taskDescriptor->readyQueue, taskDescriptor);

  taskMessageSetDone(taskMessage);

  return returnValue;
}

/// @fn int schedulerAssignMemoryCommandHandler(
///   SchedulerState *schedulerState, TaskMessage *taskMessage)
///
/// @brief Assign a piece of memory to the scheduler for ownership.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler task.
/// @param taskMessage A pointer to the TaskMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerAssignMemoryCommandHandler(
  SchedulerState *schedulerState, TaskMessage *taskMessage
) {
  (void) schedulerState;

  if (taskMessage == NULL) {
    // This should be impossible, but there's nothing to do.  Return good
    // status.
    return 0;
  }

  void *ptr = taskMessageData(taskMessage);
  int returnValue = assignMemory(ptr, NANO_OS_SCHEDULER_TASK_ID);
  if (returnValue == 0) {
    taskMessageInit(taskMessage, 0, (void*) ((intptr_t) 0), 0, true);
  } else {
    printString("WARNING: Could not assign memory from task ");
    printInt(taskId(taskMessageFrom(taskMessage)));
    printString(" to the scheduler\n");
    printString("Undefined behavior.\n");
    taskMessageInit(taskMessage, 0, (void*) ((intptr_t) returnValue), 0, true);
  }

  taskMessageSetDone(taskMessage);

  return 0;
}

/// @typedef SchedulerCommandHandler
///
/// @brief Signature of command handler for a scheduler command.
typedef int (*SchedulerCommandHandler)(SchedulerState*, TaskMessage*);

/// @var schedulerCommandHandlers
///
/// @brief Array of function pointers for commands that are understood by the
/// message handler for the main loop function.
const SchedulerCommandHandler schedulerCommandHandlers[] = {
  schedulerKillTaskCommandHandler,          // SCHEDULER_KILL_TASK
  // SCHEDULER_GET_NUM_RUNNING_TASKS:
  schedulerGetNumTaskDescriptorsCommandHandler,
  schedulerGetTaskInfoCommandHandler,       // SCHEDULER_GET_TASK_INFO
  schedulerGetTaskUserCommandHandler,       // SCHEDULER_GET_TASK_USER
  schedulerSetTaskUserCommandHandler,       // SCHEDULER_SET_TASK_USER
  // SCHEDULER_CLOSE_ALL_FILE_DESCRIPTORS:
  schedulerCloseAllFileDescriptorsCommandHandler,
  schedulerGetHostnameCommandHandler,       // SCHEDULER_GET_HOSTNAME
  schedulerExecveCommandHandler,            // SCHEDULER_EXECVE
  schedulerSpawnCommandHandler,             // SCHEDULER_SPAWN
  schedulerAssignMemoryCommandHandler,      // SCHEDULER_ASSIGN_MEMORY,
};

/// @fn void handleSchedulerMessage(SchedulerState *schedulerState)
///
/// @brief Handle one (and only one) message from our message queue.  If
/// handling the message is unsuccessful, the message will be returned to the
/// end of our message queue.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
///
/// @return This function returns no value.
void handleSchedulerMessage(SchedulerState *schedulerState) {
  static int lastReturnValue = 0;
  TaskMessage *message = taskMessageQueuePop();
  if (message != NULL) {
    SchedulerCommand messageType
      = (SchedulerCommand) taskMessageType(message);
    if (messageType >= NUM_SCHEDULER_COMMANDS) {
      // Invalid.  Purge the message.
      if (taskMessageRelease(message) != taskSuccess) {
        printString("ERROR: "
          "Could not release message from handleSchedulerMessage "
          "for invalid message type.\n");
      }
      return;
    }

    int returnValue = schedulerCommandHandlers[messageType](
      schedulerState, message);
    if (returnValue != 0) {
      // Tasking the message failed.  We can't release it.  Put it on the
      // back of our own queue again and try again later.
      if (lastReturnValue == 0) {
        // Only print out a message if this is the first time we've failed.
        printString("Scheduler command handler failed.\n");
        printString("Pushing message back onto our own queue.\n");
      }
      taskMessageQueuePush(getRunningTask(), message);
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
///   the scheduler task.
///
/// @return This function returns no value.
void checkForTimeouts(SchedulerState *schedulerState) {
  TaskQueue *timedWaiting = &schedulerState->timedWaiting;
  uint8_t numElements = timedWaiting->numElements;
  int64_t now = coroutineGetNanoseconds(NULL);

  for (uint8_t ii = 0; ii < numElements; ii++) {
    TaskDescriptor *poppedDescriptor = taskQueuePop(timedWaiting);
    Comutex *blockingComutex
      = poppedDescriptor->taskHandle->blockingComutex;
    Cocondition *blockingCocondition
      = poppedDescriptor->taskHandle->blockingCocondition;

    if ((blockingComutex != NULL) && (now >= blockingComutex->timeoutTime)) {
      taskQueuePush(poppedDescriptor->readyQueue, poppedDescriptor);
      continue;
    } else if ((blockingCocondition != NULL)
      && (now >= blockingCocondition->timeoutTime)
    ) {
      taskQueuePush(poppedDescriptor->readyQueue, poppedDescriptor);
      continue;
    }

    taskQueuePush(timedWaiting, poppedDescriptor);
  }

  return;
}

/// @fn void forceYield(void)
///
/// @brief Callback that's invoked when the preemption timer fires.  Wrapper
///   for taskYield.  Does nothing else.
///
/// @return This function returns no value.
void forceYield(void) {
  taskYield();
}

/// @fn int schedulerDumpMemoryAllocations(SchedulerState *schedulerState)
///
/// @brief Make the memory manager dump metadata about all its outstanding
/// allocations.
///
/// @param schedulerState A pointer to the SchedulerState managed by the
///   scheduler.
///
/// @return Returns 0 on success, -1 on failure.
int schedulerDumpMemoryAllocations(SchedulerState *schedulerState) {
  int returnValue = 0;
  
  TaskMessage *dumpMemoryAllocationsMessage = getAvailableMessage();
  if (dumpMemoryAllocationsMessage != NULL) {
    taskMessageInit(dumpMemoryAllocationsMessage,
      MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS, NULL, 0, true);
    schedulerSendTaskMessageToTask(
      &schedulerState->allTasks[NANO_OS_MEMORY_MANAGER_TASK_ID - 1],
      dumpMemoryAllocationsMessage);
    if (taskMessageDone(dumpMemoryAllocationsMessage) == false) {
      printString("ERROR: dumpMemoryAllocationsMessage is not done!\n");
    }
    taskMessageRelease(dumpMemoryAllocationsMessage);
  } else {
    printString("WARNING: Could not allocate dumpMemoryAllocationsMessage.\n");
    returnValue = -1;
  }
  
  return returnValue;
}

/// @fn int schedulerDumpOpenFiles(SchedulerState *schedulerState)
///
/// @brief Make the memory manager dump metadata about all its outstanding
/// allocations.
///
/// @param schedulerState A pointer to the SchedulerState managed by the
///   scheduler.
///
/// @return Returns 0 on success, -1 on failure.
int schedulerDumpOpenFiles(SchedulerState *schedulerState) {
  int returnValue = 0;
  
  TaskMessage *dumpOpenFilesMessage = getAvailableMessage();
  if (dumpOpenFilesMessage != NULL) {
    taskMessageInit(dumpOpenFilesMessage,
      FILESYSTEM_DUMP_OPEN_FILES, NULL, 0, true);
    schedulerSendTaskMessageToTask(
      &schedulerState->allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1],
      dumpOpenFilesMessage);
    if (taskMessageDone(dumpOpenFilesMessage) == false) {
      printString("ERROR: dumpOpenFilesMessage is not done!\n");
    }
    taskMessageRelease(dumpOpenFilesMessage);
  } else {
    printString("WARNING: Could not allocate dumpOpenFilesMessage.\n");
    returnValue = -1;
  }
  
  return returnValue;
}

/// @fn void removeTask(SchedulerState *schedulerState,
///   TaskDescriptor *taskDescriptor, const char *errorMessage)
///
/// @brief Clean up all of a task's resources so that it can be removed from
/// the scheduler's task queues.
///
/// @param schedulerState A pointer to the SchedulerState managed by the
///   scheduler.
/// @param taskDescriptor A pointer to the TaskDescriptor to clean up.
/// @param errorMessage A string containing the message to display to the user
///   to indicate the reason this task is being remoevd.
///
/// @return This function returns no value.
void removeTask(SchedulerState *schedulerState, TaskDescriptor *taskDescriptor,
  const char *errorMessage
) {
  printString("ERROR: ");
  printString(errorMessage);
  printString("\n");
  printString("       Removing task ");
  printInt(taskDescriptor->taskId);
  printString(" from task queues\n");

  taskDescriptor->name = NULL;
  taskDescriptor->userId = NO_USER_ID;
  taskDescriptor->taskHandle->state = COROUTINE_STATE_NOT_RUNNING;

  TaskMessage *consoleReleasePidPortMessage = getAvailableMessage();
  if (consoleReleasePidPortMessage != NULL) {
    schedulerSendNanoOsMessageToTaskId(
      schedulerState,
      NANO_OS_CONSOLE_TASK_ID,
      CONSOLE_RELEASE_PID_PORT,
      (intptr_t) consoleReleasePidPortMessage,
      taskDescriptor->taskId);
    taskMessageRelease(consoleReleasePidPortMessage);
  } else {
    printString("WARNING: Could not allocate "
      "consoleReleasePidPortMessage.  Console leak.\n");
    // If we can't allocate the first message, we can't allocate the second
    // one either, so bail.
    return;
  }

  if (schedulerSendNanoOsMessageToTaskId(
    schedulerState, NANO_OS_MEMORY_MANAGER_TASK_ID,
    MEMORY_MANAGER_FREE_TASK_MEMORY,
    /* func= */ 0, taskDescriptor->taskId)
  ) {
    printString("ERROR: Could not free task memory. Memory leak.\n");
  }

  return;
}

/// @fn int schedulerLoadOverlay(SchedulerState *schedulerState,
///   const char *overlayDir, const char *overlay, char **envp)
///
/// @brief Load and configure an overlay into the overlayMap in memory.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
/// @param overlayDir The full path to the directory of the overlay on the
///   filesystem.
/// @param overlay The name of the overlay within the overlayDir to load (minus
///   the ".overlay" file extension).
/// @param envp The array of environment variables in "name=value" form.
///
/// @return Returns 0 on success, negative error code on failure.
int schedulerLoadOverlay(SchedulerState *schedulerState,
  const char *overlayDir, const char *overlay, char **envp
) {
  if ((overlayDir == NULL) || (overlay == NULL)) {
    // There's no overlay to load.  This isn't really an error, but there's
    // nothing to do.  Just return 0.
    return 0;
  }

  NanoOsOverlayMap *overlayMap = HAL->overlayMap;
  if ((overlayMap == NULL) || (HAL->overlaySize == 0)) {
    printString("No overlay memory available for use.\n");
    return -ENOMEM;
  }

  NanoOsOverlayHeader *overlayHeader = &overlayMap->header;
  if ((overlayHeader->overlayDir != NULL) && (overlayHeader->overlay != NULL)) {
    if ((strcmp(overlayHeader->overlayDir, overlayDir) == 0)
      && (strcmp(overlayHeader->overlay, overlay) == 0)
    ) {
      // Overlay is already loaded.  Do nothing.
      return 0;
    }
  }

  // We need two extra characters:  One for the '/' that separates the directory
  // and the file name and one for the terminating NULL byte.
  char *fullPath = (char*) schedMalloc(
    strlen(overlayDir) + strlen(overlay) + OVERLAY_EXT_LEN + 2);
  if (fullPath == NULL) {
    return -ENOMEM;
  }
  strcpy(fullPath, overlayDir);
  strcat(fullPath, "/");
  strcat(fullPath, overlay);
  strcat(fullPath, OVERLAY_EXT);
  FILE *overlayFile = schedFopen(schedulerState, fullPath, "r");
  if (overlayFile == NULL) {
    printString("Could not open file \"");
    printString(fullPath);
    printString("\" from the filesystem.\n");
    schedFree(fullPath); fullPath = NULL;
    return -ENOENT;
  }

  printDebugString(__func__);
  printDebugString(": Reading from overlayFile 0x");
  printDebugHex((uintptr_t) overlayFile);
  printDebugString("\n");
  if (schedFread(schedulerState,
    overlayMap, 1, HAL->overlaySize, overlayFile) == 0
  ) {
    printString("Could not read overlay from \"");
    printString(fullPath);
    printString("\" file\n");
    schedFclose(schedulerState, overlayFile); overlayFile = NULL;
    schedFree(fullPath); fullPath = NULL;
    return -EIO;
  }
  printDebugString(__func__);
  printDebugString(": Closing overlayFile 0x");
  printDebugHex((uintptr_t) overlayFile);
  printDebugString("\n");
  schedFclose(schedulerState, overlayFile); overlayFile = NULL;

  printDebugString("Verifying overlay magic\n");
  if (overlayMap->header.magic != NANO_OS_OVERLAY_MAGIC) {
    printString("Overlay magic for \"");
    printString(fullPath);
    printString("\" was not \"NanoOsOL\".\n");
    schedFree(fullPath); fullPath = NULL;
    return -ENOEXEC;
  }
  printDebugString("Verifying overlay version\n");
  if (overlayMap->header.version != NANO_OS_OVERLAY_VERSION) {
    printString("Overlay version is 0x");
    printHex(overlayMap->header.version);
    printString(" for \"");
    printString(fullPath);
    printString("\"\n");
    schedFree(fullPath); fullPath = NULL;
    return -ENOEXEC;
  }
  schedFree(fullPath); fullPath = NULL;

  // Set the pieces of the overlay header that the program needs to run.
  printDebugString("Configuring overlay environment\n");
  overlayHeader->osApi = &nanoOsApi;
  overlayHeader->env = envp;
  overlayHeader->overlayDir = overlayDir;
  overlayHeader->overlay = overlay;
  
  return 0;
}

/// @fn int schedulerRunOverlayCommand(
///   SchedulerState *schedulerState, TaskDescriptor *taskDescriptor,
///   const char *commandPath, int argc, const char **argv, const char **envp)
///
/// @brief Launch a command that's in overlay format on the filesystem.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
/// @param taskDescriptor A pointer to the TaskDescriptor that will be
///   populated with the overlay command.
/// @param commandPath The full path to the command overlay file on the
///   filesystem.
/// @param argc The number of arguments from the command line.
/// @param argv The of arguments from the command line as an array of C strings.
/// @param envp The array of environment variable strings where each element is
///   in "name=value" form.
///
/// @return Returns 0 on success, -errno on failure.
int schedulerRunOverlayCommand(
  SchedulerState *schedulerState, TaskDescriptor *taskDescriptor,
  char *commandPath, char **argv, char **envp
) {
  int returnValue = 0;

  // Copy over the exec args.
  ExecArgs *execArgs = schedMalloc(sizeof(ExecArgs));
  if (execArgs == NULL) {
    returnValue = -ENOMEM;
    goto exit;
  }
  execArgs->callingTaskId = taskDescriptor->taskId;

  execArgs->pathname = (char*) schedMalloc(strlen(commandPath) + 1);
  if (execArgs->pathname == NULL) {
    returnValue = -ENOMEM;
    goto freeExecArgs;
  }
  strcpy(execArgs->pathname, commandPath);

  size_t argvLen = 0;
  for (; argv[argvLen] != NULL; argvLen++);
  argvLen++; // Account for the terminating NULL element
  execArgs->argv = (char**) schedMalloc(argvLen * sizeof(char*));
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
  // is NULL or it's the envp that already existed for the taskDescriptor.  So,
  // either there is no envp or it is already the one we should be using for
  // the task.  We do *NOT* want to make a copy of it.  Just assign it direclty
  // here.  The logic below will take care of memory ownership.
  execArgs->envp = envp;

  execArgs->schedulerState = schedulerState;

  if (assignMemory(execArgs, taskDescriptor->taskId) != 0) {
    printString("WARNING: Could not assign execArgs to exec task.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(execArgs->pathname, taskDescriptor->taskId) != 0) {
    printString("WARNING: Could not assign execArgs->pathname to exec task.\n");
    printString("Undefined behavior.\n");
  }

  if (execArgs->argv != NULL) {
    if (assignMemory(execArgs->argv, taskDescriptor->taskId) != 0) {
      printString("WARNING: Could not assign argv to exec task.\n");
      printString("Undefined behavior.\n");
    }

    for (int ii = 0; execArgs->argv[ii] != NULL; ii++) {
      if (assignMemory(execArgs->argv[ii], taskDescriptor->taskId) != 0) {
        printString("WARNING: Could not assign execArgs->argv[");
        printInt(ii);
        printString("] to exec task.\n");
        printString("Undefined behavior.\n");
      }
    }
  }

  if (execArgs->envp != NULL) {
    if (assignMemory(execArgs->envp, taskDescriptor->taskId) != 0) {
      printString("WARNING: Could not assign execArgs->envp to exec task.\n");
      printString("Undefined behavior.\n");
    }

    for (int ii = 0; execArgs->envp[ii] != NULL; ii++) {
      if (assignMemory(execArgs->envp[ii], taskDescriptor->taskId) != 0) {
        printString("WARNING: Could not assign execArgs->envp[");
        printInt(ii);
        printString("] to exec task.\n");
        printString("Undefined behavior.\n");
      }
    }
  }

  if (taskCreate(taskDescriptor, execCommand, execArgs) == taskError) {
    printString(
      "ERROR: Could not configure task handle for new command.\n");
    schedFree(execArgs);
    return -ENOEXEC;
  }

  taskDescriptor->overlayDir = execArgs->pathname;
  taskDescriptor->overlay = "main";
  taskDescriptor->envp = execArgs->envp;
  taskDescriptor->name = execArgs->argv[0];

  taskDescriptor->numFileDescriptors = NUM_STANDARD_FILE_DESCRIPTORS;
  taskDescriptor->fileDescriptors
    = (FileDescriptor*) standardUserFileDescriptors;

  taskResume(taskDescriptor, NULL);

  return returnValue;

freeExecArgs:
  schedFree(execArgs->pathname);

  if (execArgs->argv != NULL) {
    for (int ii = 0; execArgs->argv[ii] != NULL; ii++) {
      schedFree(execArgs->argv[ii]);
    }
    schedFree(execArgs->argv);
  }

  if (execArgs->envp != NULL) {
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

/// @var gettyArgs
///
/// @brief Command line arguments used to launch the getty process.  These have
/// to be declared global because they're referenced by the launched process on
/// its own stack.
static const char *gettyArgs[] = {
  "getty",
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

/// @fn void runScheduler(SchedulerState *schedulerState)
///
/// @brief Run one (1) iteration of the main scheduler loop.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler task.
///
/// @return This function returns no value.
void runScheduler(SchedulerState *schedulerState) {
  TaskDescriptor *taskDescriptor
    = taskQueuePop(schedulerState->currentReady);
  if (taskDescriptor == NULL) {
    // Nothing we can do.
    printString("ERROR: No tasks to pop in ");
    printString(schedulerState->currentReady->name);
    printString(" task queue\n");
    return;
  }

  if (coroutineCorrupted(taskDescriptor->taskHandle)) {
    removeTask(schedulerState, taskDescriptor, "Task corruption detected");
    return;
  }

  if (taskDescriptor->taskId >= NANO_OS_FIRST_USER_TASK_ID) {
    if (taskRunning(taskDescriptor) == true) {
      // This is a user task, which is in an overlay.  Make sure it's loaded.
      if (schedulerLoadOverlay(schedulerState,
        taskDescriptor->overlayDir, taskDescriptor->overlay,
        taskDescriptor->envp) != 0
      ) {
        schedulerDumpMemoryAllocations(schedulerState);
        schedulerDumpOpenFiles(schedulerState);
        removeTask(schedulerState, taskDescriptor, "Overlay load failure");
        return;
      }
    }
    
    // Configure the preemption timer to force the task to yield if it doesn't
    // voluntarily give up control within a reasonable amount of time.
    HAL->configOneShotTimer(
      schedulerState->preemptionTimer, 10000000, forceYield);
  }
  taskResume(taskDescriptor, NULL);
  // No need to call HAL->cancelTimer since that's called by
  // coroutineYieldCallback if we're running preemptive multitasking.

  if (taskRunning(taskDescriptor) == false) {
    schedulerSendNanoOsMessageToTaskId(schedulerState,
      NANO_OS_MEMORY_MANAGER_TASK_ID, MEMORY_MANAGER_FREE_TASK_MEMORY,
      /* func= */ 0, /* data= */ taskDescriptor->taskId);
  }

  // Check the shells and restart them if needed.
  if ((taskDescriptor->taskId >= NANO_OS_FIRST_SHELL_PID)
    && (taskDescriptor->taskId
      < (NANO_OS_FIRST_SHELL_PID + schedulerState->numShells))
    && (taskRunning(taskDescriptor) == false)
  ) {
    if ((schedulerState->hostname == NULL)
      || (*schedulerState->hostname == '\0')
    ) {
      // We're not done initializing yet.  Put the task back on the ready
      // queue and try again later.
      taskQueuePush(schedulerState->currentReady, taskDescriptor);
      return;
    }

    if (taskDescriptor->userId == NO_USER_ID) {
      // Login failed.  Re-launch getty.
      if (schedulerRunOverlayCommand(schedulerState, taskDescriptor,
        "/usr/bin/getty", (char**) gettyArgs, NULL) != 0
      ) {
        removeTask(schedulerState, taskDescriptor, "Failed to load getty");
        return;
      }
    } else {
      // User task exited.  Re-launch the shell.
      char *passwdStringBuffer = NULL;
      struct passwd *pwd = NULL;
      do {
        passwdStringBuffer
          = (char*) schedMalloc(NANO_OS_PASSWD_STRING_BUF_SIZE);
        if (passwdStringBuffer == NULL) {
          fprintf(stderr,
            "ERROR! Could not allocate space for passwdStringBuffer in %s.\n",
            "runScheduler");
          break;
        }
        
        pwd = (struct passwd*) schedMalloc(sizeof(struct passwd));
        if (pwd == NULL) {
          fprintf(stderr,
            "ERROR! Could not allocate space for pwd in %s.\n", "runScheduler");
          break;
        }
        
        struct passwd *result = NULL;
        nanoOsGetpwuid_r(taskDescriptor->userId, pwd,
          passwdStringBuffer, NANO_OS_PASSWD_STRING_BUF_SIZE, &result);
        if (result == NULL) {
          fprintf(stderr,
            "Could not find passwd info for uid %d\n", taskDescriptor->userId);
          break;
        }
        
        // strrchr(pwd->pw_shell, '/') must be non-NULL in order for pw_shell
        // to be valid.
        shellArgs[0] = strrchr(pwd->pw_shell, '/') + 1;
        if (schedulerRunOverlayCommand(schedulerState, taskDescriptor,
          pwd->pw_shell, (char**) shellArgs, taskDescriptor->envp) != 0
        ) {
          removeTask(schedulerState, taskDescriptor, "Failed to load mush");
          schedFree(pwd);
          schedFree(passwdStringBuffer);
          return;
        }
      } while (0);
      schedFree(pwd);
      schedFree(passwdStringBuffer);
    }
  }

  if (coroutineState(taskDescriptor->taskHandle)
    == COROUTINE_STATE_WAIT
  ) {
    taskQueuePush(&schedulerState->waiting, taskDescriptor);
  } else if (coroutineState(taskDescriptor->taskHandle)
    == COROUTINE_STATE_TIMEDWAIT
  ) {
    taskQueuePush(&schedulerState->timedWaiting, taskDescriptor);
  } else if (taskFinished(taskDescriptor)) {
    taskQueuePush(&schedulerState->free, taskDescriptor);
  } else { // Task is still running.
    taskQueuePush(schedulerState->currentReady, taskDescriptor);
  }

  checkForTimeouts(schedulerState);
  handleSchedulerMessage(schedulerState);

  return;
}

/// @fn void startScheduler(SchedulerState **coroutineStatePointer)
///
/// @brief Initialize and run the round-robin scheduler.
///
/// @return This function returns no value and, in fact, never returns at all.
__attribute__((noinline)) void startScheduler(
  SchedulerState **coroutineStatePointer
) {
  printDebugString("Starting scheduler in debug mode...\n");

  // Initialize the scheduler's state.
  SchedulerState schedulerState = {0};
  schedulerState.hostname = NULL;
  schedulerState.ready[SCHEDULER_READY_QUEUE_KERNEL].name = "kernel ready";
  schedulerState.ready[SCHEDULER_READY_QUEUE_USER].name = "user ready";
  schedulerState.waiting.name = "waiting";
  schedulerState.timedWaiting.name = "timed waiting";
  schedulerState.free.name = "free";
  schedulerState.currentReady
    = &schedulerState.ready[SCHEDULER_READY_QUEUE_KERNEL];
  schedulerState.preemptionTimer
    = (HAL->getNumTimers() > PREEMPTION_TIMER) ? PREEMPTION_TIMER : -1;
  printDebugString("Set scheduler state.\n");

  // Initialize the pointer that was used to configure coroutines.
  *coroutineStatePointer = &schedulerState;

  // Initialize the static TaskMessage storage.
  TaskMessage messagesStorage[NANO_OS_NUM_MESSAGES] = {0};
  extern TaskMessage *messages;
  messages = messagesStorage;

  // Initialize the static NanoOsMessage storage.
  NanoOsMessage nanoOsMessagesStorage[NANO_OS_NUM_MESSAGES] = {0};
  extern NanoOsMessage *nanoOsMessages;
  nanoOsMessages = nanoOsMessagesStorage;
  printDebugString("Allocated messages storage.\n");

  // Initialize the allTasks pointer.  The tasks are all zeroed because
  // we zeroed the entire schedulerState when we declared it.
  allTasks = schedulerState.allTasks;

  // Initialize the scheduler in the array of running commands.
  schedulerTask = &allTasks[NANO_OS_SCHEDULER_TASK_ID - 1];
  schedulerTask->taskHandle = schedulerTaskHandle;
  schedulerTask->taskId
    = NANO_OS_SCHEDULER_TASK_ID;
  schedulerTask->name = "init";
  schedulerTask->userId = ROOT_USER_ID;
  taskHandleSetContext(schedulerTask->taskHandle, schedulerTask);

  // We are not officially running the first task, so make it current.
  currentTask = schedulerTask;
  printDebugString("Configured scheduler task.\n");

  // Initialize all the kernel task file descriptors.
  for (TaskId ii = 1; ii <= NANO_OS_FIRST_USER_TASK_ID; ii++) {
    allTasks[ii - 1].numFileDescriptors = NUM_STANDARD_FILE_DESCRIPTORS;
    allTasks[ii - 1].fileDescriptors
      = (FileDescriptor*) standardKernelFileDescriptors;
  }
  printDebugString("Initialized kernel task file descriptors.\n");

  // Create the console task.
  TaskDescriptor *taskDescriptor
    = &allTasks[NANO_OS_CONSOLE_TASK_ID - 1];
  if (taskCreate(taskDescriptor, runConsole, NULL) != taskSuccess) {
    printString("Could not create console task.\n");
  }
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);
  taskDescriptor->taskId = NANO_OS_CONSOLE_TASK_ID;
  taskDescriptor->name = "console";
  taskDescriptor->userId = ROOT_USER_ID;
  printDebugString("Created console task.\n");

  // Start the console by calling taskResume.
  taskResume(&allTasks[NANO_OS_CONSOLE_TASK_ID - 1], NULL);
  printDebugString("Started console task.\n");

  printDebugString("\n");
  printDebugString("sizeof(int) = ");
  printDebugInt(sizeof(int));
  printDebugString("\n");
  printDebugString("sizeof(void*) = ");
  printDebugInt(sizeof(void*));
  printDebugString("\n");
  printDebugString("Main stack size = ");
  printDebugInt(ABS_DIFF(
    ((intptr_t) schedulerTaskHandle),
    ((intptr_t) allTasks[NANO_OS_CONSOLE_TASK_ID - 1].taskHandle)
  ));
  printDebugString(" bytes\n");
  printDebugString("schedulerState size = ");
  printDebugInt(sizeof(SchedulerState));
  printDebugString(" bytes\n");
  printDebugString("messagesStorage size = ");
  printDebugInt(sizeof(TaskMessage) * NANO_OS_NUM_MESSAGES);
  printDebugString(" bytes\n");
  printDebugString("nanoOsMessagesStorage size = ");
  printDebugInt(sizeof(NanoOsMessage) * NANO_OS_NUM_MESSAGES);
  printDebugString(" bytes\n");
  printDebugString("ConsoleState size = ");
  printDebugInt(sizeof(ConsoleState));
  printDebugString(" bytes\n");

  schedulerState.numShells = schedulerGetNumConsolePorts(&schedulerState);
  if (schedulerState.numShells <= 0) {
    // This should be impossible since the HAL was successfully initialized,
    // but take no chances.
    printString("ERROR! No console ports running.\nHalting.\n");
    while(1);
  }
  // Irrespective of how many ports the console may be running, we can't run
  // more shell tasks than what we're configured for.  Make sure we set a
  // sensible limit.
  schedulerState.numShells
    = MIN(schedulerState.numShells, NANO_OS_MAX_NUM_SHELLS);
  printDebugString("Managing ");
  printDebugInt(schedulerState.numShells);
  printDebugString(" shells\n");

  int rv = HAL->initRootStorage(&schedulerState);
  if (rv != 0) {
    printString("ERROR: initRootStorage returned status ");
    printInt(rv);
    printString("\n");
  }

  // We need to do an initial population of all the tasks because we need to
  // get to the end of memory to run the memory manager in whatever is left
  // over.
  for (TaskId ii = NANO_OS_FIRST_USER_TASK_ID;
    ii <= NANO_OS_NUM_TASKS;
    ii++
  ) {
    taskDescriptor = &allTasks[ii - 1];
    if (taskCreate(taskDescriptor,
      dummyTask, NULL) != taskSuccess
    ) {
      printString("Could not create task ");
      printInt(ii);
      printString(".\n");
    }
    taskHandleSetContext(
      taskDescriptor->taskHandle, taskDescriptor);
    taskDescriptor->taskId = ii;
    taskDescriptor->userId = NO_USER_ID;
  }
  printDebugString("Created all tasks.\n");

  printDebugString("Console stack size = ");
  printDebugInt(ABS_DIFF(
    ((uintptr_t) allTasks[NANO_OS_SD_CARD_TASK_ID - 1].taskHandle),
    ((uintptr_t) allTasks[NANO_OS_CONSOLE_TASK_ID - 1].taskHandle))
    - sizeof(Coroutine)
  );
  printDebugString(" bytes\n");

  printDebugString("Coroutine stack size = ");
  printDebugInt(ABS_DIFF(
    ((uintptr_t) allTasks[NANO_OS_FIRST_USER_TASK_ID - 1].taskHandle),
    ((uintptr_t) allTasks[NANO_OS_FIRST_USER_TASK_ID].taskHandle))
    - sizeof(Coroutine)
  );
  printDebugString(" bytes\n");

  printDebugString("Coroutine size = ");
  printDebugInt(sizeof(Coroutine));
  printDebugString("\n");

  printDebugString("standardKernelFileDescriptors size = ");
  printDebugInt(sizeof(standardKernelFileDescriptors));
  printDebugString("\n");

  // Create the memory manager task.  : THIS MUST BE THE LAST TASK
  // CREATED BECAUSE WE WANT TO USE THE ENTIRE REST OF MEMORY FOR IT :
  taskDescriptor = &allTasks[NANO_OS_MEMORY_MANAGER_TASK_ID - 1];
  if (taskCreate(taskDescriptor,
    runMemoryManager, NULL) != taskSuccess
  ) {
    printString("Could not create memory manager task.\n");
  }
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);
  taskDescriptor->taskId = NANO_OS_MEMORY_MANAGER_TASK_ID;
  taskDescriptor->name = "memory manager";
  taskDescriptor->userId = ROOT_USER_ID;
  printDebugString("Created memory manager.\n");

  // Start the memory manager by calling taskResume.
  taskResume(&allTasks[NANO_OS_MEMORY_MANAGER_TASK_ID - 1], NULL);
  printDebugString("Started memory manager.\n");

  // Assign the console ports to it.
  for (uint8_t ii = 0; ii < schedulerState.numShells; ii++) {
    if (schedulerAssignPortToTaskId(&schedulerState,
      ii, NANO_OS_MEMORY_MANAGER_TASK_ID) != taskSuccess
    ) {
      printString(
        "WARNING: Could not assign console port to memory manager.\n");
    }
  }
  printDebugString("Assigned console ports to memory manager.\n");

  // Set the shells for the ports.
  for (uint8_t ii = 0; ii < schedulerState.numShells; ii++) {
    if (schedulerSetPortShell(&schedulerState,
      ii, NANO_OS_FIRST_SHELL_PID + ii) != taskSuccess
    ) {
      printString("WARNING: Could not set shell for ");
      printString(shellNames[ii]);
      printString(".\n");
      printString("         Undefined behavior will result.\n");
    }
  }
  printDebugString("Set shells for ports.\n");

  taskQueuePush(&schedulerState.ready[SCHEDULER_READY_QUEUE_KERNEL],
    &allTasks[NANO_OS_MEMORY_MANAGER_TASK_ID - 1]);
  taskQueuePush(&schedulerState.ready[SCHEDULER_READY_QUEUE_KERNEL],
    &allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1]);
  taskQueuePush(&schedulerState.ready[SCHEDULER_READY_QUEUE_KERNEL],
    &allTasks[NANO_OS_SD_CARD_TASK_ID - 1]);
  taskQueuePush(&schedulerState.ready[SCHEDULER_READY_QUEUE_KERNEL],
    &allTasks[NANO_OS_CONSOLE_TASK_ID - 1]);
  // Mark all the kernel processes as being part of the kernel ready queue.
  // Skip over the scheduler (task 0).
  allTasks[0].readyQueue = NULL;
  for (TaskId ii = 1; ii < NANO_OS_FIRST_USER_TASK_ID; ii++) {
    allTasks[ii - 1].readyQueue
      = &schedulerState.ready[SCHEDULER_READY_QUEUE_KERNEL];
  }

  // The scheduler will take care of cleaning up the dummy tasks in the
  // ready queue.
  for (TaskId ii = NANO_OS_FIRST_USER_TASK_ID;
    ii <= NANO_OS_NUM_TASKS;
    ii++
  ) {
    taskQueuePush(&schedulerState.ready[SCHEDULER_READY_QUEUE_USER],
      &allTasks[ii - 1]);
    allTasks[ii - 1].readyQueue
      = &schedulerState.ready[SCHEDULER_READY_QUEUE_USER];
  }
  printDebugString("Populated ready queue.\n");

  // Get the memory manager and filesystem up and running.
  taskResume(&allTasks[NANO_OS_MEMORY_MANAGER_TASK_ID - 1], NULL);
  taskResume(&allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1], NULL);
  printDebugString("Started memory manager and filesystem.\n");

  // Allocate memory for the hostname.
  schedulerState.hostname = (char*) schedCalloc(1, HOST_NAME_MAX + 1);
  printDebugString("Allocated memory for the hostname.\n");
  if (schedulerState.hostname != NULL) {
    FILE *hostnameFile = schedFopen(&schedulerState, "/etc/hostname", "r");
    if (hostnameFile != NULL) {
      printDebugString("Opened hostname file.\n");
      if (schedFgets(&schedulerState,
        schedulerState.hostname, HOST_NAME_MAX + 1, hostnameFile)
          != schedulerState.hostname
      ) {
        printString("ERROR! fgets did not read hostname!\n");
      }
      if (strchr(schedulerState.hostname, '\r')) {
        *strchr(schedulerState.hostname, '\r') = '\0';
      } else if (strchr(schedulerState.hostname, '\n')) {
        *strchr(schedulerState.hostname, '\n') = '\0';
      } else if (*schedulerState.hostname == '\0') {
        strcpy(schedulerState.hostname, "localhost");
      }
      schedFclose(&schedulerState, hostnameFile);
      printDebugString("Closed hostname file.\n");
    } else {
      printString("ERROR! schedFopen of hostname returned NULL!\n");
      strcpy(schedulerState.hostname, "localhost");
    }
  } else {
    printString("ERROR! schedulerState.hostname is NULL!\n");
  }

#ifdef NANO_OS_DEBUG
  do {
    FILE *helloFile = schedFopen(&schedulerState, "hello", "w");
    if (helloFile == NULL) {
      printDebugString("ERROR: Could not open hello file for writing!\n");
      break;
    }
    printDebugString("helloFile is non-NULL!\n");

    if (schedFputs(&schedulerState, "world", helloFile) == EOF) {
      printDebugString("ERROR: Could not write to hello file!\n");
      schedFclose(&schedulerState, helloFile);
      break;
    }
    schedFclose(&schedulerState, helloFile);

    helloFile = schedFopen(&schedulerState, "hello", "r");
    if (helloFile == NULL) {
      printDebugString("ERROR: Could not open hello file for reading after write!\n");
      schedRemove(&schedulerState, "hello");
      break;
    }
    printDebugString("Opened helloFile for reading\n");

    char worldString[11] = {0};
    if (schedFgets(&schedulerState,
      worldString, sizeof(worldString), helloFile) != worldString
    ) {
      printDebugString("ERROR: Could not read worldString after write!\n");
      schedFclose(&schedulerState, helloFile);
      schedRemove(&schedulerState, "hello");
      break;
    }
    printDebugString("Read data from helloFile into worldString\n");

    if (strcmp(worldString, "world") != 0) {
      printDebugString("ERROR: Expected \"world\", read \"");
      printDebugString(worldString);
      printDebugString("\"!\n");
      schedFclose(&schedulerState, helloFile);
      schedRemove(&schedulerState, "hello");
      break;
    }
    printDebugString("Successfully read \"world\" from \"hello\"!\n");
    schedFclose(&schedulerState, helloFile);

    helloFile = schedFopen(&schedulerState, "hello", "a");
    if (helloFile == NULL) {
      printDebugString("ERROR: Could not open hello file for appending!\n");
      schedRemove(&schedulerState, "hello");
      break;
    }

    if (schedFputs(&schedulerState, "world", helloFile) == EOF) {
      printDebugString("ERROR: Could not append to hello file!\n");
      schedFclose(&schedulerState, helloFile);
      schedRemove(&schedulerState, "hello");
      break;
    }
    schedFclose(&schedulerState, helloFile);

    helloFile = schedFopen(&schedulerState, "hello", "r");
    if (helloFile == NULL) {
      printDebugString(
        "ERROR: Could not open hello file for reading after append!\n");
      schedRemove(&schedulerState, "hello");
      break;
    }

    if (schedFgets(&schedulerState,
      worldString, sizeof(worldString), helloFile) != worldString
    ) {
      printDebugString("ERROR: Could not read worldString after append!\n");
      schedFclose(&schedulerState, helloFile);
      schedRemove(&schedulerState, "hello");
      break;
    }

    if (strcmp(worldString, "worldworld") == 0) {
      printDebugString(
        "Successfully read \"worldworld\" from \"hello\"!\n");
    } else {
      printDebugString("ERROR: Expected \"worldworld\", read \"");
      printDebugString(worldString);
      printDebugString("\"!\n");
    }

    schedFclose(&schedulerState, helloFile);
    if (schedRemove(&schedulerState, "hello") != 0) {
      printDebugString("ERROR: schedRemove failed to remove the \"hello\" file.\n");
    }
  } while (0);
  printDebugString("Filesystem sanity test complete\n");
#endif // NANO_OS_DEBUG

  // Run our scheduler.
  while (1) {
    for (int ii = 0; ii < SCHEDULER_NUM_READY_QUEUES; ii++) {
      schedulerState.currentReady = &schedulerState.ready[ii];
      uint8_t queueSize = schedulerState.currentReady->numElements;
      for (uint8_t jj = 0; jj < queueSize; jj++) {
        runScheduler(&schedulerState);
      }
    }
  }
}

