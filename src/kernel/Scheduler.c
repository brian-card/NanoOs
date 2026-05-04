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
#include "Hal.h"
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
    .inputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
  {
    // stdout
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .inputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
  {
    // stderr
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .inputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = TASK_ID_NOT_SET,
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
    .inputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
  {
    // stdout
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .inputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
  {
    // stderr
    // Uni-directional FileDescriptor, so clear the input pipe and direct the
    // output pipe to the console.
    .inputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .outputChannel = {
      .pid = TASK_ID_NOT_SET,
      .messageType = -1,
    },
    .pipeEnd = NULL,
    .refCount = 1,
  },
};

/// @var shellNames
///
/// @brief The names of the shells as they will appear in the process table.
static const char* const shellNames[NANO_OS_MAX_NUM_SHELLS] = {
  "shell 0",
  "shell 1",
};

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
    || (processQueue->numElements >= SCHEDULER_NUM_TASKS)
  ) {
    printString("ERROR: Could not push process ");
    printInt(processDescriptor->pid);
    printString(" onto ");
    printString(processQueue->name);
    printString(" queue:\n");
    return ENOMEM;
  }

  processQueue->processes[processQueue->tail] = processDescriptor;
  processQueue->tail++;
  processQueue->tail %= SCHEDULER_NUM_TASKS;
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
  processQueue->head %= SCHEDULER_NUM_TASKS;
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

// Thread callbacks.  ***DO NOT** do parameter validation.  These callbacks
// are set when threadConfig is called.  If these callbacks are called at
// all (which they should be), then we should assume that things are configured
// correctly.  This is in kernel space code, which we have full control over,
// so we should assume that things are setup correctly.  If they're not setup
// correctly, we should fix the configuration, not do parameter validation.
// These callbacks - especially yieldCallback - are in the critical
// path.  Single cycles matter.  Don't waste more time than we need to.

/// @fn void yieldCallback(void *stateData, Thread *thread)
///
/// @brief Function to be called right before a thread yields.
///
/// @param stateData The thread state pointer provided when threadConfig
///   was called.
/// @param thread A pointer to the Thread structure representing the
///   thread that's about to yield.  This parameter is unused by this
///   function.
///
/// @Return This function returns no value.
void yieldCallback(void *stateData, Thread *thread) {
  (void) thread;
  SchedulerState *schedulerState = *((SchedulerState**) stateData);
  if (schedulerState == NULL) {
    // We're being called before the scheduler has been started.  This is
    // sometimes done to fix the stack size of the scheduler itself before
    // starting it.  Just return.
    return;
  }

  // No need to check HAL->timer for NULL.  This function can't be configured
  // to be called unless it wasn't NULL at boot.
  HAL->timer->cancel(schedulerState->preemptionTimer);

  return;
}

/// @fn void unlockCallback(void *stateData, Comutex *comutex)
///
/// @brief Function to be called when a mutex (Comutex) is unlocked.
///
/// @param stateData The thread state pointer provided when threadConfig
///   was called.
/// @param comutex A pointer to the Comutex object that has been unlocked.  At
///   the time this callback is called, the mutex has been unlocked but its
///   thread pointer has not been cleared.
///
/// @return This function returns no value, but if the head of the Comutex's
/// lock queue is found in one of the waiting queues, it is removed from the
/// waiting queue and pushed onto the ready queue.
void unlockCallback(void *stateData, Comutex *comutex) {
  (void) stateData;
  ProcessDescriptor *processDescriptor = threadContext(comutex->head);
  if (processDescriptor == NULL) {
    // Nothing is waiting on this mutex.  Just return.
    return;
  }
  processQueueRemove(processDescriptor->processQueue, processDescriptor);
  processQueuePush(processDescriptor->readyQueue, processDescriptor);

  return;
}

/// @fn void signalCallback(
///   void *stateData, Cocondition *cocondition)
///
/// @brief Function to be called when a condition (Cocondition) is signalled.
///
/// @param stateData The thread state pointer provided when threadConfig
///   was called.
/// @param cocondition A pointer to the Cocondition object that has been
///   signalled.  At the time this callback is called, the number of signals has
///   been set to the number of waiters that will be signalled.
///
/// @return This function returns no value, but if the head of the Cocondition's
/// signal queue is found in one of the waiting queues, it is removed from the
/// waiting queue and pushed onto the ready queue.
void signalCallback(void *stateData, Cocondition *cocondition) {
  (void) stateData;
  Thread *cur = cocondition->head;

  for (int ii = 0; (ii < cocondition->numSignals) && (cur != NULL); ii++) {
    ProcessDescriptor *processDescriptor = threadContext(cur);
    // It's not possible for processDescriptor to be NULL.  We only enter this
    // loop if cocondition->numSignals > 0, so there MUST be something waiting
    // on this condition.
    processQueueRemove(processDescriptor->processQueue, processDescriptor);
    processQueuePush(processDescriptor->readyQueue, processDescriptor);
    cur = cur->nextToSignal;
  }

  return;
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
  if ((pid > 0) && (pid <= NANO_OS_NUM_TASKS)) {
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
    printString(
      "ERROR: Attempt to send scheduler processMessage to NULL process.\n");
    returnValue = processError;
    return returnValue;
  } else if (processMessage == NULL) {
    printString(
      "ERROR: Attempt to send NULL scheduler processMessage to process.\n");
    returnValue = processError;
    return returnValue;
  }
  // processMessage->from would normally be set when we do a
  // processMessageQueuePush. We're not using that mechanism here, so we have
  // to do it manually.  If we don't do this, then commands that validate that
  // the message came from the scheduler will fail.
  msg_from(processMessage).coro = schedulerThread;

  // Have to set the endpoint type manually since we're not using
  // comessageQueuePush.
  processMessage->msg_sync = &msg_sync_array[MSG_CORO_SAFE];

  if (processCorrupted(processDescriptor)) {
    printString("ERROR: Called process is corrupted:\n");
    returnValue = processError;
    return returnValue;
  }
  processResume(processDescriptor, processMessage);

  if (processMessageDone(processMessage) != true) {
    // This is our only indication from the called process that something went
    // wrong.  Return an error status here.
    printString("ERROR: Process ");
    printInt(processDescriptor->pid);
    printString(" did not mark sent message done.\n");
    returnValue = processError;
  }

  return returnValue;
}

/// @fn int schedulerSendProcessMessageToProcessId(SchedulerState *schedulerState,
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
int schedulerSendProcessMessageToProcessId(SchedulerState *schedulerState,
  unsigned int pid, ProcessMessage *processMessage
) {
  int returnValue = processError;
  if ((pid <= 0) || (pid > NANO_OS_NUM_TASKS)) {
    // Not a valid PID.  Fail.
    printString("ERROR: ");
    printInt(pid);
    printString(" is not a valid PID.\n");
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
///   ProcessDescriptor *processDescriptor, int type,
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
  int type, void *data, size_t size
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

/// @fn int schedulerInitSendMessageToProcessId(
///   int pid, int type, void *data, size_t size)
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
int schedulerInitSendMessageToProcessId(
  int pid, int type, void *data, size_t size
) {
  int returnValue = processError;
  if ((pid <= 0) || (pid > NANO_OS_NUM_TASKS)) {
    // Not a valid PID.  Fail.
    printString("ERROR: ");
    printInt(pid);
    printString(" is not a valid PID.\n");
    return returnValue; // processError
  }

  ProcessDescriptor *processDescriptor = &SCHEDULER_STATE->allProcesses[pid - 1];
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
  
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    // Nothing we can do.  The scheduler can't yield.  Bail.
    return returnValue;
  }

  processMessageInit(sent, MEMORY_MANAGER_REALLOC,
    &reallocMessage, sizeof(reallocMessage), true);
  // sent->from would normally be set during processMessageQueuePush.  We're
  // not using that mechanism here, so we have to do it manually.  Things will
  // get messed up if we don't.
  msg_from(sent).coro = schedulerThread;

  processResume(&allProcesses[SCHEDULER_STATE->memoryManagerProcessId - 1], sent);
  if (processMessageDone(sent) == true) {
    // The handler set the pointer back in the structure we sent it, so grab it
    // out of the structure we already have.
    returnValue = reallocMessage.ptr;
  } else {
    printString(
      "Warning:  Memory manager did not mark realloc message done.\n");
  }
  // The handler pushes the message back onto our queue, which is not what we
  // want.  Pop it off again.
  processMessageQueuePop();
  processMessageRelease(sent);

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
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    // Nothing we can do.  The scheduler can't yield.  Bail.
    return;
  }

  processMessageInit(sent, MEMORY_MANAGER_FREE, ptr, 0, true);
  // sent->from would normally be set during processMessageQueuePush.  We're
  // not using that mechanism here, so we have to do it manually.  Things will
  // get messed up if we don't.
  msg_from(sent).coro = schedulerThread;

  processResume(&allProcesses[SCHEDULER_STATE->memoryManagerProcessId - 1], sent);
  if (processMessageDone(sent) == false) {
    printString(
      "Warning:  Memory manager did not mark free message done.\n");
  }
  processMessageRelease(sent);

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
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    // Nothing we can do.  The scheduler can't yield.  Bail.
    return -ENOMEM;
  }
  
  AssignMemoryParams assignMemoryParams = {
    .ptr = ptr,
    .pid = pid,
  };
  processMessageInit(sent, MEMORY_MANAGER_ASSIGN_MEMORY,
    &assignMemoryParams, sizeof(assignMemoryParams), true);

  // sent->from would normally be set during processMessageQueuePush.  We're
  // not using that mechanism here, so we have to do it manually.  Things will
  // get messed up if we don't.
  msg_from(sent).coro = schedulerThread;

  int returnValue = 0;
  processResume(&allProcesses[SCHEDULER_STATE->memoryManagerProcessId - 1],
    sent);
  if (processMessageDone(sent) == true) {
    // The usual case.
    returnValue = (int) ((intptr_t) processMessageData(sent));
  } else {
    printString(
      "Warning:  Memory manager did not mark assignMemory message done.\n");
    returnValue = -ETIMEDOUT;
  }
  processMessageRelease(sent);

  return returnValue;
}

/// @fn int schedulerAssignPortToProcessId(uint8_t consolePort, ProcessId owner)
///
/// @brief Assign a console port to a process ID.
///
/// @param consolePort The ID of the consolePort to assign.
/// @param owner The ID of the process to assign the port to.
///
/// @return Returns processSuccess on success, processError on failure.
int schedulerAssignPortToProcessId(uint8_t consolePort, ProcessId owner) {
  ConsolePortPidUnion consolePortPidUnion;
  consolePortPidUnion.consolePortPidAssociation.consolePort
    = consolePort;
  consolePortPidUnion.consolePortPidAssociation.pid = owner;

  int returnValue = schedulerInitSendMessageToProcessId(
    SCHEDULER_STATE->consoleProcessId, CONSOLE_ASSIGN_PORT,
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

  if (shell >= NANO_OS_NUM_TASKS) {
    printString("ERROR: schedulerSetPortShell called with invalid shell PID ");
    printInt(shell);
    printString("\n");
    return returnValue; // processError
  }

  ConsolePortPidUnion consolePortPidUnion;
  consolePortPidUnion.consolePortPidAssociation.consolePort
    = consolePort;
  consolePortPidUnion.consolePortPidAssociation.pid = shell;

  returnValue = schedulerInitSendMessageToProcessId(
    SCHEDULER_STATE->consoleProcessId, CONSOLE_SET_PORT_SHELL,
    (void*) ((uintptr_t) consolePortPidUnion.nanoOsMessageData), /* size= */ 0);

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
  ProcessQueue *currentReady = schedulerState->currentReady;
  schedulerState->currentReady
    = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

  int returnValue = -1;
  ProcessMessage *messageToSend = getAvailableMessage();
  for (int ii = 0;
    (ii < MAX_GET_MESSAGE_RETRIES) && (messageToSend == NULL);
    ii++
  ) {
    runScheduler();
    messageToSend = getAvailableMessage();
  }
  if (messageToSend == NULL) {
    printInt(getRunningProcessId());
    printString(": ");
    printString(__func__);
    printString(": ERROR: Out of messages\n");
    return returnValue; // -1
  }
  schedulerState->currentReady = currentReady;

  processMessageInit(messageToSend, CONSOLE_GET_NUM_PORTS,
    /*data= */ 0, /* size= */ 0, /* waiting= */ true);
  if (schedulerSendProcessMessageToProcessId(schedulerState,
    SCHEDULER_STATE->consoleProcessId, messageToSend) != processSuccess
  ) {
    printString("ERROR: Could not send CONSOLE_GET_NUM_PORTS to console\n");
    return returnValue; // -1
  }

  returnValue = (int) ((intptr_t) processMessageData(messageToSend));
  processMessageRelease(messageToSend);

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

  processMessage = initSendProcessMessageToProcessId(
    SCHEDULER_STATE->schedulerProcessId, SCHEDULER_GET_NUM_RUNNING_TASKS,
    /* data= */  0, /* size= */ 0, true);
  if (processMessage == NULL) {
    printf("ERROR: Could not communicate with scheduler.\n");
    goto exit;
  }

  waitStatus = processMessageWaitForDone(processMessage, timeout);
  if (waitStatus != processSuccess) {
    if (waitStatus == processTimedout) {
      printf("Command to get the number of running processes timed out.\n");
    } else {
      printf("Command to get the number of running processes failed.\n");
    }

    // Without knowing how many processes there are, we can't continue.  Bail.
    goto releaseMessage;
  }

  numProcessDescriptors = (ProcessId) ((uintptr_t) processMessageData(processMessage));
  if (numProcessDescriptors == 0) {
    printf("ERROR: Number of running processes returned from the "
      "scheduler is 0.\n");
    goto releaseMessage;
  }

releaseMessage:
  if (processMessageRelease(processMessage) != processSuccess) {
    printf("ERROR: Could not release message sent to scheduler for "
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
    printf(
      "ERROR: Could not allocate memory for processInfo in getProcessInfo.\n");
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

  processMessage
    = initSendProcessMessageToProcessId(SCHEDULER_STATE->schedulerProcessId,
    SCHEDULER_GET_TASK_INFO, processInfo, sizeof(*processInfo), true);

  if (processMessage == NULL) {
    printf("ERROR: Could not send scheduler message to get process info.\n");
    goto freeMemory;
  }

  waitStatus = processMessageWaitForDone(processMessage, &timeout);
  if (waitStatus != processSuccess) {
    if (waitStatus == processTimedout) {
      printf("Command to get process information timed out.\n");
    } else {
      printf("Command to get process information failed.\n");
    }

    // Without knowing the data for the processes, we can't display them.  Bail.
    goto releaseMessage;
  }

  if (processMessageRelease(processMessage) != processSuccess) {
    printf("ERROR: Could not release message sent to scheduler for "
      "getting the number of running processes.\n");
  }

  return processInfo;

releaseMessage:
  if (processMessageRelease(processMessage) != processSuccess) {
    printf("ERROR: Could not release message sent to scheduler for "
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
  ProcessMessage *processMessage = initSendProcessMessageToProcessId(
    SCHEDULER_STATE->schedulerProcessId, SCHEDULER_KILL_TASK,
    (void*) ((uintptr_t) pid), /* size= */ 0, true);
  if (processMessage == NULL) {
    printf("ERROR: Could not communicate with scheduler.\n");
    return 1;
  }

  // We don't know where our message to the scheduler will be in its queue, so
  // we can't assume it will be processed immediately, but we can't wait forever
  // either.  Set a 100 ms timeout.
  struct timespec ts = { 0, 0 };
  timespec_get(&ts, TIME_UTC);
  ts.tv_nsec += 100000000;

  int waitStatus = processMessageWaitForDone(processMessage, &ts);
  int returnValue = 0;
  if (waitStatus == processSuccess) {
    returnValue = (int) ((intptr_t) processMessageData(processMessage));
    if (returnValue == 0) {
      printf("Termination successful.\n");
    } else {
      printf("Process termination returned status \"%s\".\n",
        strerror(returnValue));
    }
  } else {
    returnValue = 1;
    if (waitStatus == processTimedout) {
      printf("Command to kill PID %d timed out.\n", pid);
    } else {
      printf("Command to kill PID %d failed.\n", pid);
    }
  }

  if (processMessageRelease(processMessage) != processSuccess) {
    returnValue = 1;
    printf("ERROR: "
      "Could not release message sent to scheduler for kill command.\n");
  }

  return returnValue;
}

/// @fn UserId schedulerGetProcessUser(void)
///
/// @brief Get the ID of the user running the current process.
///
/// @return Returns the ID of the user running the current process on success,
/// -1 on failure.
UserId schedulerGetProcessUser(void) {
  UserId userId = -1;
  ProcessMessage *processMessage
    = initSendProcessMessageToProcessId(
    SCHEDULER_STATE->schedulerProcessId, SCHEDULER_GET_TASK_USER,
    /* data= */ 0, /* size= */ 0, true);
  if (processMessage == NULL) {
    printString("ERROR: Could not communicate with scheduler.\n");
    return userId; // -1
  }

  processMessageWaitForDone(processMessage, NULL);
  userId = (UserId) ((intptr_t) processMessageData(processMessage));
  processMessageRelease(processMessage);

  return userId;
}

/// @fn int schedulerSetProcessUser(UserId userId)
///
/// @brief Set the user ID of the current process to the specified user ID.
///
/// @return Returns 0 on success, -1 on failure.
int schedulerSetProcessUser(UserId userId) {
  int returnValue = -1;
  ProcessMessage *processMessage
    = initSendProcessMessageToProcessId(
    SCHEDULER_STATE->schedulerProcessId, SCHEDULER_SET_TASK_USER,
    /* data= */ (void*) ((intptr_t) userId), /* size= */ 0, true);
  if (processMessage == NULL) {
    printString("ERROR: Could not communicate with scheduler.\n");
    return returnValue; // -1
  }

  processMessageWaitForDone(processMessage, NULL);
  returnValue = (int) ((intptr_t) processMessageData(processMessage));
  processMessageRelease(processMessage);

  if (returnValue != 0) {
    printf("Scheduler returned \"%s\" for setProcessUser.\n",
      strerror(returnValue));
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
  ProcessId runningProcessIndex = getRunningProcessId() - 1;

  if (fdIndex <= allProcesses[runningProcessIndex].numFileDescriptors) {
    returnValue = allProcesses[runningProcessIndex].fileDescriptors[fdIndex - 1];
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
/// process.
///
/// @return Returns 0 on success, -1 on failure.
int schedulerCloseAllFileDescriptors(void) {
  ProcessMessage *processMessage = initSendProcessMessageToProcessId(
    SCHEDULER_STATE->schedulerProcessId, SCHEDULER_CLOSE_ALL_FILE_DESCRIPTORS,
    /* data= */ 0, /* size= */ 0, true);
  if (processMessage == NULL) {
    printString("ERROR: Could not send SCHEDULER_CLOSE_ALL_FILE_DESCRIPTORS ");
    printString("message to scheduler process\n");
    return -1;
  }

  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

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
  ProcessMessage *processMessage
    = initSendProcessMessageToProcessId(
    SCHEDULER_STATE->schedulerProcessId, SCHEDULER_GET_HOSTNAME,
    /* data= */ 0, /* size= */ 0, true);
  if (processMessage == NULL) {
    printString("ERROR: Could not communicate with scheduler.\n");
    return hostname; // NULL
  }

  processMessageWaitForDone(processMessage, NULL);
  hostname = (const char*) processMessageData(processMessage);
  processMessageRelease(processMessage);

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

  ExecArgs *execArgs = (ExecArgs*) calloc(1, sizeof(ExecArgs));
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
  execArgs->argv = (char**) calloc(1, argvLen * sizeof(char*));
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
    execArgs->envp = (char**) calloc(1, envpLen * sizeof(char*));
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

  ProcessMessage *processMessage
    = initSendProcessMessageToProcessId(
    SCHEDULER_STATE->schedulerProcessId, SCHEDULER_EXECVE,
    /* data= */ execArgs, /* size= */ sizeof(*execArgs), true);
  if (processMessage == NULL) {
    // The only way this should be possible is if all available messages are
    // in use, so use ENOMEM as the errno.
    errno = ENOMEM;
    goto freeExecArgs;
  }

  processMessageWaitForDone(processMessage, NULL);

  // If we got this far then the exec failed for some reason.  The error will
  // be in the data portion of the message we sent to the scheduler.
  errno = (int) ((intptr_t) processMessageData(processMessage));
  processMessageRelease(processMessage);

freeExecArgs:
  execArgs = execArgsDestroy(execArgs);

  return -1;
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
  ProcessMessage *processMessage = getAvailableMessage();
  for (int ii = 0;
    (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
    ii++
  ) {
    processYield();
    processMessage = getAvailableMessage();
  }
  if (processMessage == NULL) {
    printInt(getRunningProcessId());
    printString(": ");
    printString(__func__);
    printString(": ERROR: Out of process messages\n");
    return -ENOMEM;
  }

  processMessageInit(processMessage, SCHEDULER_ASSIGN_MEMORY, ptr, 0, true);

  if (sendProcessMessageToProcessId(SCHEDULER_STATE->schedulerProcessId,
    processMessage) != processSuccess
  ) {
    processMessageRelease(processMessage);
    fprintf(stderr,
      "ERROR: Could not send SCHEDULER_ASSIGN_MEMORY message to scheduler\n");
    return -EIO;
  }

  processMessageWaitForDone(processMessage, NULL);

  int returnValue = (int) ((intptr_t) processMessageData(processMessage));
  processMessageRelease(processMessage);

  return returnValue;
}

////////////////////////////////////////////////////////////////////////////////
// Scheduler command handlers and support functions
////////////////////////////////////////////////////////////////////////////////

/// @fn int closeProcessFileDescriptors(
///   SchedulerState *schedulerState, ProcessDescriptor *processDescriptor)
///
/// @brief Helper function to close out the file descriptors owned by a process
/// when it exits or is killed.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processDescriptor A pointer to the ProcessDescriptor that holds the
///   fileDescriptors array to close.
///
/// @return Returns 0 on success, -1 on failure.
int closeProcessFileDescriptors(
  SchedulerState *schedulerState, ProcessDescriptor *processDescriptor
) {
  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    ProcessQueue *currentReady = schedulerState->currentReady;
    schedulerState->currentReady
      = &schedulerState->ready[SCHEDULER_READY_QUEUE_KERNEL];

    FileDescriptor **fileDescriptors = processDescriptor->fileDescriptors;
    if (fileDescriptors == NULL) {
      // Nothing to do.
      return 0;
    }
    ProcessMessage *messageToSend = getAvailableMessage();
    for (int ii = 0;
      (ii < MAX_GET_MESSAGE_RETRIES) && (messageToSend == NULL);
      ii++
    ) {
      runScheduler();
      messageToSend = getAvailableMessage();
    }
    if (messageToSend == NULL) {
      printInt(getRunningProcessId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ERROR: Out of process messages\n");
      return -1;
    }
    uint8_t numFileDescriptors = processDescriptor->numFileDescriptors;
    for (uint8_t ii = 0; ii < numFileDescriptors; ii++) {
      FileDescriptor *fileDescriptor = fileDescriptors[ii];
      if (fileDescriptor == NULL) {
        // This file descriptor was previously closed.  Move on.
        continue;
      }

      ProcessId waitingOutputProcessId
        = fileDescriptor->outputChannel.pid;
      
      if ((waitingOutputProcessId != TASK_ID_NOT_SET)
        && (waitingOutputProcessId != SCHEDULER_STATE->consoleProcessId)
      ) {
        ProcessDescriptor *waitingProcessDescriptor
          = &schedulerState->allProcesses[waitingOutputProcessId - 1];

        if ((waitingProcessDescriptor->fileDescriptors != NULL)
          && (waitingProcessDescriptor->fileDescriptors[
            STDIN_FILE_DESCRIPTOR_INDEX] != NULL)
          && (waitingProcessDescriptor->numFileDescriptors
            > STDIN_FILE_DESCRIPTOR_INDEX)
        ) {
          // Clear the pid of the waiting process's stdin file descriptor.
          waitingProcessDescriptor->fileDescriptors[
            STDIN_FILE_DESCRIPTOR_INDEX]->pipeEnd = NULL;
          waitingProcessDescriptor->fileDescriptors[
            STDIN_FILE_DESCRIPTOR_INDEX]->inputChannel.pid = TASK_ID_NOT_SET;

          if (processRunning(waitingProcessDescriptor)) {
            // Send an empty message to the waiting process so that it will become
            // unblocked.
            processMessageInit(messageToSend,
              fileDescriptor->outputChannel.messageType,
              /*data= */ NULL, /* size= */ 0, /* waiting= */ false);
            processMessageQueuePush(waitingProcessDescriptor, messageToSend);

            // The function that was waiting should have released the message we
            // sent it.  Get another one.
            messageToSend = getAvailableMessage();
            for (int jj = 0;
              (jj < MAX_GET_MESSAGE_RETRIES) && (messageToSend == NULL);
              jj++
            ) {
              runScheduler();
              messageToSend = getAvailableMessage();
            }
            if (messageToSend == NULL) {
              printInt(getRunningProcessId());
              printString(": ");
              printString(__func__);
              printString(": ");
              printInt(__LINE__);
              printString(": ERROR: Out of process messages\n");
              return -1;
            }
          }
        }
      }

      ProcessId waitingInputProcessId = fileDescriptor->inputChannel.pid;
      if ((waitingInputProcessId != TASK_ID_NOT_SET)
        && (waitingInputProcessId != SCHEDULER_STATE->consoleProcessId)
      ) {
        ProcessDescriptor *waitingProcessDescriptor
          = &schedulerState->allProcesses[waitingInputProcessId - 1];

        if ((waitingProcessDescriptor->fileDescriptors != NULL)
          && (waitingProcessDescriptor->fileDescriptors[
            STDOUT_FILE_DESCRIPTOR_INDEX] != NULL)
          && (waitingProcessDescriptor->numFileDescriptors
            > STDOUT_FILE_DESCRIPTOR_INDEX)
        ) {
          // Clear the pid of the waiting process's stdout file descriptor.
          waitingProcessDescriptor->fileDescriptors[
            STDOUT_FILE_DESCRIPTOR_INDEX]->pipeEnd = NULL;
          waitingProcessDescriptor->fileDescriptors[
            STDOUT_FILE_DESCRIPTOR_INDEX]->outputChannel.pid
            = TASK_ID_NOT_SET;

          if (processRunning(waitingProcessDescriptor)) {
            // Send an empty message to the waiting process so that it will become
            // unblocked.
            processMessageInit(messageToSend,
              fileDescriptor->outputChannel.messageType,
              /*data= */ NULL, /* size= */ 0, /* waiting= */ false);
            processMessageQueuePush(waitingProcessDescriptor, messageToSend);

            // The function that was waiting should have released the message we
            // sent it.  Get another one.
            messageToSend = getAvailableMessage();
            for (int jj = 0;
              (jj < MAX_GET_MESSAGE_RETRIES) && (messageToSend == NULL);
              jj++
            ) {
              runScheduler();
              messageToSend = getAvailableMessage();
            }
            if (messageToSend == NULL) {
              printInt(getRunningProcessId());
              printString(": ");
              printString(__func__);
              printString(": ");
              printInt(__LINE__);
              printString(": ERROR: Out of process messages\n");
              return -1;
            }
          }
        }
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
    processMessageRelease(messageToSend);
    schedFree(fileDescriptors); processDescriptor->fileDescriptors = NULL;
    processDescriptor->numFileDescriptors = 0;

    schedulerState->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    if (HAL->power != NULL) {
      HAL->power->shutdown(HAL_SHUTDOWN_OFF);
    } else {
      while (1);
    }
  }

  return 0;
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

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;
    SCHEDULER_STATE->currentReady
      = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

    printDebugString("schedFopen: Getting message\n");
    ProcessMessage *processMessage = getAvailableMessage();
    for (int ii = 0;
      (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
      ii++
    ) {
      runScheduler();
      processMessage = getAvailableMessage();
    }
    if (processMessage == NULL) {
      printInt(getRunningProcessId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ERROR: Out of process messages\n");
      return NULL;
    }
    printDebugString("schedFopen: Message retrieved\n");
    FilesystemFopenParameters fopenParameters = {
      .pathname = pathname,
      .mode = mode,
    };
    printDebugString("schedFopen: Initializing message\n");
    processMessageInit(processMessage, FILESYSTEM_OPEN_FILE,
      &fopenParameters, sizeof(fopenParameters), true);
    printDebugString("schedFopen: Pushing message\n");
    processMessageQueuePush(
      &SCHEDULER_STATE->allProcesses[SCHEDULER_STATE->rootFsProcessId - 1],
      processMessage);

    printDebugString("schedFopen: Resuming filesystem\n");
    while (processMessageDone(processMessage) == false) {
      runScheduler();
    }
    printDebugString("schedFopen: Filesystem message is done\n");

    returnValue = (FILE*) processMessageData(processMessage);

    processMessageRelease(processMessage);
    SCHEDULER_STATE->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    if (HAL->power != NULL) {
      HAL->power->shutdown(HAL_SHUTDOWN_OFF);
    } else {
      while (1);
    }
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

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;
    SCHEDULER_STATE->currentReady
      = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

    ProcessMessage *processMessage = getAvailableMessage();
    for (int ii = 0;
      (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
      ii++
    ) {
      runScheduler();
      processMessage = getAvailableMessage();
    }
    if (processMessage == NULL) {
      printInt(getRunningProcessId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ERROR: Out of process messages\n");
      errno = ENOMEM;
      return EOF;
    }
    FilesystemFcloseParameters fcloseParameters;
    fcloseParameters.stream = stream;
    fcloseParameters.returnValue = 0;
    processMessageInit(processMessage, FILESYSTEM_CLOSE_FILE,
      &fcloseParameters, sizeof(fcloseParameters), true);
    processMessageQueuePush(
      &SCHEDULER_STATE->allProcesses[SCHEDULER_STATE->rootFsProcessId - 1],
      processMessage);

    while (processMessageDone(processMessage) == false) {
      runScheduler();
    }

    if (fcloseParameters.returnValue != 0) {
      errno = -fcloseParameters.returnValue;
      returnValue = EOF;
    }

    processMessageRelease(processMessage);
    SCHEDULER_STATE->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    if (HAL->power != NULL) {
      HAL->power->shutdown(HAL_SHUTDOWN_OFF);
    } else {
      while (1);
    }
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

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;
    SCHEDULER_STATE->currentReady
      = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

    ProcessMessage *processMessage = getAvailableMessage();
    for (int ii = 0;
      (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
      ii++
    ) {
      runScheduler();
      processMessage = getAvailableMessage();
    }
    if (processMessage == NULL) {
      printInt(getRunningProcessId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ERROR: Out of process messages\n");
      errno = ENOMEM;
      return -1;
    }
    processMessageInit(processMessage, FILESYSTEM_REMOVE_FILE,
      (void*) pathname, strlen(pathname) + 1, true);
    processMessageQueuePush(
      &SCHEDULER_STATE->allProcesses[SCHEDULER_STATE->rootFsProcessId - 1],
      processMessage);

    while (processMessageDone(processMessage) == false) {
      runScheduler();
    }

    returnValue = (int) ((intptr_t) processMessageData(processMessage));
    if (returnValue != 0) {
      // returnValue holds a negative errno.  Set errno for the current process
      // and return -1 like we're supposed to.
      errno = -returnValue;
      returnValue = -1;
    }

    processMessageRelease(processMessage);
    SCHEDULER_STATE->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    if (HAL->power != NULL) {
      HAL->power->shutdown(HAL_SHUTDOWN_OFF);
    } else {
      while (1);
    }
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
  FilesystemIoCommandParameters filesystemIoCommandParameters = {
    .file = stream,
    .buffer = ptr,
    .length = size * nmemb
  };

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;
    SCHEDULER_STATE->currentReady
      = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

    ProcessMessage *processMessage = getAvailableMessage();
    for (int ii = 0;
      (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
      ii++
    ) {
      runScheduler();
      processMessage = getAvailableMessage();
    }
    if (processMessage == NULL) {
      printInt(getRunningProcessId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ERROR: Out of process messages\n");
      errno = ENOMEM;
      return 0;
    }
    processMessageInit(processMessage, FILESYSTEM_READ_FILE,
      &filesystemIoCommandParameters, sizeof(filesystemIoCommandParameters),
      true);
    processMessageQueuePush(
      &SCHEDULER_STATE->allProcesses[SCHEDULER_STATE->rootFsProcessId - 1],
      processMessage);

    while (processMessageDone(processMessage) == false) {
      runScheduler();
    }

    processMessageRelease(processMessage);
    SCHEDULER_STATE->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    if (HAL->power != NULL) {
      HAL->power->shutdown(HAL_SHUTDOWN_OFF);
    } else {
      while (1);
    }
  }

  return filesystemIoCommandParameters.length / size;
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
  FilesystemIoCommandParameters filesystemIoCommandParameters = {
    .file = stream,
    .buffer = ptr,
    .length = size * nmemb
  };

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;
    SCHEDULER_STATE->currentReady
      = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

    ProcessMessage *processMessage = getAvailableMessage();
    for (int ii = 0;
      (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
      ii++
    ) {
      runScheduler();
      processMessage = getAvailableMessage();
    }
    if (processMessage == NULL) {
      printInt(getRunningProcessId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ERROR: Out of process messages\n");
      errno = ENOMEM;
      return 0;
    }
    processMessageInit(processMessage, FILESYSTEM_WRITE_FILE,
      &filesystemIoCommandParameters, sizeof(filesystemIoCommandParameters),
      true);
    processMessageQueuePush(
      &SCHEDULER_STATE->allProcesses[SCHEDULER_STATE->rootFsProcessId - 1],
      processMessage);

    while (processMessageDone(processMessage) == false) {
      runScheduler();
    }

    processMessageRelease(processMessage);
    SCHEDULER_STATE->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    if (HAL->power != NULL) {
      HAL->power->shutdown(HAL_SHUTDOWN_OFF);
    } else {
      while (1);
    }
  }

  return filesystemIoCommandParameters.length / size;
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
  char *returnValue = NULL;

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;
    SCHEDULER_STATE->currentReady
      = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

    FilesystemIoCommandParameters filesystemIoCommandParameters = {
      .file = stream,
      .buffer = buffer,
      .length = (uint32_t) size - 1
    };

    ProcessMessage *processMessage = getAvailableMessage();
    for (int ii = 0;
      (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
      ii++
    ) {
      runScheduler();
      processMessage = getAvailableMessage();
    }
    if (processMessage == NULL) {
      printInt(getRunningProcessId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ERROR: Out of process messages\n");
      errno = ENOMEM;
      return NULL;
    }
    processMessageInit(processMessage, FILESYSTEM_READ_FILE,
      &filesystemIoCommandParameters, sizeof(filesystemIoCommandParameters),
      true);
    processMessageQueuePush(
      &SCHEDULER_STATE->allProcesses[SCHEDULER_STATE->rootFsProcessId - 1],
      processMessage);

    while (processMessageDone(processMessage) == false) {
      runScheduler();
    }
    if (filesystemIoCommandParameters.length > 0) {
      buffer[filesystemIoCommandParameters.length] = '\0';
      returnValue = buffer;
    }

    processMessageRelease(processMessage);
    SCHEDULER_STATE->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    if (HAL->power != NULL) {
      HAL->power->shutdown(HAL_SHUTDOWN_OFF);
    } else {
      while (1);
    }
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
  int returnValue = 0;

  if (_functionInProgress == NULL) {
    _functionInProgress = __func__;

    ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;
    SCHEDULER_STATE->currentReady
      = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

    FilesystemIoCommandParameters filesystemIoCommandParameters = {
      .file = stream,
      .buffer = (void*) s,
      .length = (uint32_t) strlen(s)
    };

    ProcessMessage *processMessage = getAvailableMessage();
    for (int ii = 0;
      (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
      ii++
    ) {
      runScheduler();
      processMessage = getAvailableMessage();
    }
    if (processMessage == NULL) {
      printInt(getRunningProcessId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ERROR: Out of process messages\n");
      errno = ENOMEM;
      return EOF;
    }
    processMessageInit(processMessage, FILESYSTEM_WRITE_FILE,
      &filesystemIoCommandParameters, sizeof(filesystemIoCommandParameters),
      true);
    processMessageQueuePush(
      &SCHEDULER_STATE->allProcesses[SCHEDULER_STATE->rootFsProcessId - 1],
      processMessage);

    while (processMessageDone(processMessage) == false) {
      runScheduler();
    }
    if (filesystemIoCommandParameters.length == 0) {
      returnValue = EOF;
    }

    processMessageRelease(processMessage);
    SCHEDULER_STATE->currentReady = currentReady;
    _functionInProgress = NULL;
  } else {
    printString("ERROR: Cannot execute ");
    printString(__func__);
    printString(" because ");
    printString(_functionInProgress);
    printString(" is already in progress\n");
    if (HAL->power != NULL) {
      HAL->power->shutdown(HAL_SHUTDOWN_OFF);
    } else {
      while (1);
    }
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
  if ((stream == NULL) || (metadata == NULL)) {
    return -EINVAL;
  }

  ProcessQueue *currentReady = SCHEDULER_STATE->currentReady;
  SCHEDULER_STATE->currentReady
    = &SCHEDULER_STATE->ready[SCHEDULER_READY_QUEUE_KERNEL];

  GetFileBlockMetadataArgs args = {
    .stream = stream,
    .metadata = metadata,
  };

  ProcessMessage *processMessage = getAvailableMessage();
  for (int ii = 0;
    (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
    ii++
  ) {
    SCHEDULER_STATE->runScheduler();
    processMessage = getAvailableMessage();
  }
  if (processMessage == NULL) {
    printInt(getRunningProcessId());
    printString(": ");
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": ERROR: Out of process messages\n");
    return -ENOMEM;
  }

  processMessageInit(processMessage, FILESYSTEM_GET_FILE_BLOCK_METADATA,
    &args, sizeof(args), true);
  if (sendProcessMessageToProcessId(SCHEDULER_STATE->rootFsProcessId, processMessage)
    != processSuccess
  ) {
    printString("ERROR! Failed to send message to filesystem to get file "
      "block metadata\n");
    processMessageRelease(processMessage);
    return -EIO;
  }
  while (processMessageDone(processMessage) == false) {
    SCHEDULER_STATE->runScheduler();
  }
  processMessageRelease(processMessage);

  SCHEDULER_STATE->currentReady = currentReady;
  return 0;
}

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

  FILE *stream = schedFopen(path, "r");
  if (stream == NULL) {
    printInt(getRunningProcessId());
    printString(": ");
    printString(__func__);
    printString(": ERROR! Could not open file \"");
    printString(path);
    printString("\"\n");
    return -EIO;
  }
  int returnValue = schedGetFileBlockMetadataFromFile(stream, metadata);
  schedFclose(stream); stream = NULL;

  return returnValue;
}

/// @fn int loadProcessDescriptorOverlayMetadata(ProcessDescriptor *processDescriptor)
///
/// @brief Load the FileBlockMetadata for a ProcessDescriptor's overlay.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to load the
///   FileBlockMetadata for.
///
/// @return Returns 0 on success, -errno on failure.
int loadProcessDescriptorOverlayMetadata(ProcessDescriptor *processDescriptor) {
  char *overlayPath = (char*) schedMalloc(
    strlen(processDescriptor->overlayDir) + OVERLAY_EXT_LEN + 6);
  if (overlayPath == NULL) {
    // Fail.
    printString("ERROR: malloc failure for overlayPath.\n");
    return -ENOMEM;
  }
  strcpy(overlayPath, processDescriptor->overlayDir);
  strcat(overlayPath, "/main");
  strcat(overlayPath, OVERLAY_EXT);

  int returnValue
    = schedGetFileBlockMetadataFromPath(overlayPath, &processDescriptor->overlay);

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

  UserId callingUserId
    = allProcesses[pid(processMessageFrom(processMessage)) - 1].userId;
  ProcessId pid = (ProcessId) ((uintptr_t) processMessageData(processMessage));
  int processIndex = pid - 1;

  if ((pid >= schedulerState->firstUserProcessId)
    && (pid <= NANO_OS_NUM_TASKS)
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
        if (processQueueRemove(processDescriptor->readyQueue, processDescriptor) != 0
        ) {
          processQueueRemove(&schedulerState->timedWaiting, processDescriptor);
        }
      }

      // Tell the console to release the port for us.  We will forward it
      // the message we acquired above, which it will use to send to the
      // correct shell to unblock it.  We need to do this before terminating
      // the process because, in the event the process we're terminating is one
      // of the shell process slots, the message won't get released because
      // there's no shell blocking waiting for the message.
      if (schedulerInitSendMessageToProcessId(
        SCHEDULER_STATE->consoleProcessId,
        CONSOLE_RELEASE_PID_PORT,
        /* data= */ (void*) ((intptr_t) pid),
        /* size= */ 0) != processSuccess
      ) {
        printString("ERROR: Could not send CONSOLE_RELEASE_PID_PORT message ");
        printString("to console process\n");
      }

      // Forward the message on to the memory manager to have it clean up the
      // process's memory.  *DO NOT* mark the message as done.  The memory
      // manager will do that.
      processMessageInit(processMessage, MEMORY_MANAGER_FREE_TASK_MEMORY,
        (void*) ((uintptr_t) pid), /* size= */ 0, /* waiting= */ true);
      if (sendProcessMessageToProcess(
        &schedulerState->allProcesses[SCHEDULER_STATE->memoryManagerProcessId - 1],
        processMessage) != processSuccess
      ) {
        printString("ERROR: Could not send MEMORY_MANAGER_FREE_TASK_MEMORY ");
        printString("message to memory manager\n");
        processMessageData(processMessage) = (void*) ((intptr_t) 1);
        if (processMessageSetDone(processMessage) != processSuccess) {
          printString("ERROR: Could not mark message done in "
            "schedulerKillProcessCommandHandler.\n");
        }
      }

      // Close the file descriptors before we terminate the process so that
      // anything that gets sent to the process's queue gets cleaned up when
      // we terminate it.
      closeProcessFileDescriptors(schedulerState, processDescriptor);

      if (processTerminate(processDescriptor) == processSuccess) {
        threadSetContext(processDescriptor->mainThread,
          processDescriptor);
        processDescriptor->name = NULL;
        processDescriptor->userId = NO_USER_ID;

        // It's likely (i.e. almost certain) that the killed process was a user
        // process that was killed by a user process.  That would mean that we were
        // in the middle of processing a user process queue, the number of items
        // in which was captured before the runScheduler loop was started.
        // (See the logic at the end of startScheduler.)  Rather than pushing
        // the killed process onto the free queue, push it back onto its ready
        // queue so that we don't try to pop a process from an empty queue.
        // runScheduler will do the cleanup and put the process onto the free
        // queue again once it picks back up again.
        processQueuePush(processDescriptor->readyQueue, processDescriptor);
      } else {
        // Tell the caller that we've failed.
        printString("Failed to terminate process; marking message 0x");
        printHex((uintptr_t) processMessage);
        printString(" done\n");
        processMessageData(processMessage) = (void*) ((intptr_t) 1);
        if (processMessageSetDone(processMessage) != processSuccess) {
          printString("ERROR: Could not mark message done in "
            "schedulerKillProcessCommandHandler.\n");
        }

        // Do *NOT* push the process back onto the free queue in this case.
        // If we couldn't terminate it, it's not valid to try and reuse it for
        // another process.
      }
    } else {
      // Tell the caller that we've failed.
      processMessageData(processMessage) = (void*) ((intptr_t) EACCES);
      if (processMessageSetDone(processMessage) != processSuccess) {
        printString("ERROR: Could not mark message done in "
          "schedulerKillProcessCommandHandler.\n");
      }
    }
  } else {
    // Tell the caller that we've failed.
    processMessageData(processMessage) = (void*) ((intptr_t) EINVAL);
    if (processMessageSetDone(processMessage) != processSuccess) {
      printString("ERROR: "
        "Could not mark message done in schedulerKillProcessCommandHandler.\n");
    }
  }

  // DO NOT release the message since that's done by the caller.

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

  uint8_t numProcessDescriptors = 0;
  for (int ii = 1; ii <= NANO_OS_NUM_TASKS; ii++) {
    if (processRunning(&schedulerState->allProcesses[ii - 1])) {
      numProcessDescriptors++;
    }
  }
  processMessageData(processMessage) = (void*) ((uintptr_t) numProcessDescriptors);

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

  ProcessInfo *processInfo = (ProcessInfo*) processMessageData(processMessage);
  int maxProcesses = processInfo->numProcesses;
  ProcessInfoElement *processes = processInfo->processes;
  int idx = 0;
  for (int ii = 1; (ii <= NANO_OS_NUM_TASKS) && (idx < maxProcesses); ii++) {
    if (processRunning(&schedulerState->allProcesses[ii - 1])) {
      processes[idx].pid = (int) schedulerState->allProcesses[ii - 1].pid;
      processes[idx].name = schedulerState->allProcesses[ii - 1].name;
      processes[idx].userId = schedulerState->allProcesses[ii - 1].userId;
      idx++;
    }
  }

  // It's possible that a process completed between the time that processInfo
  // was allocated and now, so set the value of numProcesses to the value of
  // idx.
  processInfo->numProcesses = idx;

  processMessageSetDone(processMessage);

  // DO NOT release the message since the caller is waiting on the response.

  return returnValue;
}

/// @fn int schedulerGetProcessUserCommandHandler(
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
int schedulerGetProcessUserCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  int returnValue = 0;
  ProcessId callingProcessId = pid(processMessageFrom(processMessage));
  if ((callingProcessId > 0) && (callingProcessId <= NANO_OS_NUM_TASKS)) {
    processMessageData(processMessage)
      = (void*) ((intptr_t) schedulerState->allProcesses[callingProcessId - 1].userId);
  } else {
    processMessageData(processMessage) = (void*) ((intptr_t) -1);
  }

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
  ProcessId callingProcessId = pid(processMessageFrom(processMessage));
  UserId userId = (UserId) ((intptr_t) processMessageData(processMessage));
  processMessageData(processMessage) = (void*) ((intptr_t) -1);

  if ((callingProcessId > 0) && (callingProcessId <= NANO_OS_NUM_TASKS)) {
    if ((schedulerState->allProcesses[callingProcessId - 1].userId == -1)
      || (userId == -1)
    ) {
      schedulerState->allProcesses[callingProcessId - 1].userId = userId;
      processMessageData(processMessage) = (void*) ((intptr_t) 0);
    } else {
      processMessageData(processMessage) = (void*) ((intptr_t) EACCES);
    }
  }

  processMessageSetDone(processMessage);

  // DO NOT release the message since the caller is waiting on the response.

  return returnValue;
}

/// @fn int schedulerCloseAllFileDescriptorsCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Get the number of processes that are currently running in the system.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerCloseAllFileDescriptorsCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  int returnValue = 0;
  ProcessId callingProcessId = pid(processMessageFrom(processMessage));
  ProcessDescriptor *processDescriptor
    = &schedulerState->allProcesses[callingProcessId - 1];
  closeProcessFileDescriptors(schedulerState, processDescriptor);

  processMessageSetDone(processMessage);

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

  processMessageData(processMessage) = schedulerState->hostname;
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

  ProcessDescriptor *processDescriptor = &schedulerState->allProcesses[
    pid(processMessageFrom(processMessage)) - 1];

  ExecArgs *execArgs = (ExecArgs*) processMessageData(processMessage);
  if (execArgs == NULL) {
    printString("ERROR! execArgs provided was NULL.\n");
    processMessageData(processMessage) = (void*) ((intptr_t) EINVAL);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  processMessageData(processMessage) = (void*) ((intptr_t) 0);
  execArgs->callingProcessId = pid(processMessageFrom(processMessage));

  char *pathname = execArgs->pathname;
  if (pathname == NULL) {
    // Invalid
    printString("ERROR! pathname provided was NULL.\n");
    processMessageData(processMessage) = (void*) ((intptr_t) EINVAL);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **argv = execArgs->argv;
  if (argv == NULL) {
    // Invalid
    printString("ERROR! argv provided was NULL.\n");
    processMessageData(processMessage) = (void*) ((intptr_t) EINVAL);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  } else if (argv[0] == NULL) {
    // Invalid
    printString("ERROR! argv[0] provided was NULL.\n");
    processMessageData(processMessage) = (void*) ((intptr_t) EINVAL);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **envp = execArgs->envp;

  // The arguments provided to this command are going to replace the ones that
  // spawned the original.  We need to free the original envp if there was one.
  if (processDescriptor->envp != NULL) {
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": Freeing processDescriptor->envp = 0x");
    printHex((uintptr_t) processDescriptor->envp);
    printString("\n");
    for (int ii = 0; processDescriptor->envp[ii] != NULL; ii++)  {
      schedFree(processDescriptor->envp[ii]);
    }
    schedFree(processDescriptor->envp);
    processDescriptor->envp = NULL;
  }

  if (assignMemory(execArgs, SCHEDULER_STATE->schedulerProcessId) != 0) {
    printString("WARNING: Could not assign execArgs to scheduler.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(pathname, SCHEDULER_STATE->schedulerProcessId) != 0) {
    printString("WARNING: Could not assign pathname to scheduler.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(argv, SCHEDULER_STATE->schedulerProcessId) != 0) {
    printString("WARNING: Could not assign argv to scheduler.\n");
    printString("Undefined behavior.\n");
  }
  for (int ii = 0; argv[ii] != NULL; ii++) {
    if (assignMemory(argv[ii], SCHEDULER_STATE->schedulerProcessId) != 0) {
      printString("WARNING: Could not assign argv[");
      printInt(ii);
      printString("] to scheduler.\n");
      printString("Undefined behavior.\n");
    }
  }

  if (envp != NULL) {
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": Assigning envp = 0x");
    printHex((uintptr_t) envp);
    printString(" to scheduler\n");
    if (assignMemory(envp, SCHEDULER_STATE->schedulerProcessId) != 0) {
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ");
      printString("WARNING: Could not assign envp to scheduler.\n");
      printString("Undefined behavior.\n");
    }
    for (int ii = 0; envp[ii] != NULL; ii++) {
      if (assignMemory(envp[ii], SCHEDULER_STATE->schedulerProcessId) != 0) {
        printString(__func__);
        printString(": ");
        printInt(__LINE__);
        printString(": ");
        printString("WARNING: Could not assign envp[");
        printInt(ii);
        printString("] to scheduler.\n");
        printString("Undefined behavior.\n");
      }
    }
  }

  if (assignMemory(processDescriptor->fileDescriptors,
    SCHEDULER_STATE->schedulerProcessId) != 0
  ) {
    printString("WARNING: Could not assign fileDescriptors to scheduler.\n");
    printString("Undefined behavior.\n");
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    if (assignMemory(processDescriptor->fileDescriptors[ii],
      SCHEDULER_STATE->schedulerProcessId) != 0
    ) {
      printString("WARNING: Could not assign fileDescriptors[");
      printInt(ii);
      printString("] to scheduler.\n");
      printString("Undefined behavior.\n");
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

  // Kill and clear out the calling process.
  processTerminate(processDescriptor);
  threadSetContext(processDescriptor->mainThread, processDescriptor);

  // We don't want to wait for the memory manager to release the memory.  Make
  // it do it immediately.
  if (schedulerInitSendMessageToProcessId(
    SCHEDULER_STATE->memoryManagerProcessId,
    MEMORY_MANAGER_FREE_TASK_MEMORY,
    (void*) ((uintptr_t) processDescriptor->pid), /* size= */ 0)
  ) {
    printString("WARNING: Could not release memory for process ");
    printInt(processDescriptor->pid);
    printString("\n");
    printString("Memory leak.\n");
  }

  execArgs->schedulerState = schedulerState;
  if (processCreate(processDescriptor, execCommand, execArgs) == processError) {
    printString(
      "ERROR: Could not configure process handle for new command.\n");
  }

  if (assignMemory(execArgs, processDescriptor->pid) != 0) {
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": ");
    printString("WARNING: Could not assign execArgs to exec process.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(pathname, processDescriptor->pid) != 0) {
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": ");
    printString("WARNING: Could not assign pathname to exec process.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(argv, processDescriptor->pid) != 0) {
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": ");
    printString("WARNING: Could not assign argv to exec process.\n");
    printString("Undefined behavior.\n");
  }
  for (int ii = 0; argv[ii] != NULL; ii++) {
    if (assignMemory(argv[ii], processDescriptor->pid) != 0) {
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ");
      printString("WARNING: Could not assign argv[");
      printInt(ii);
      printString("] to exec process.\n");
      printString("Undefined behavior.\n");
    }
  }

  if (envp != NULL) {
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": Assigning envp = 0x");
    printHex((uintptr_t) envp);
    printString(" to process ");
    printInt(processDescriptor->pid);
    printString("\n");
    if (assignMemory(envp, processDescriptor->pid) != 0) {
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ");
      printString("WARNING: Could not assign envp to exec process.\n");
      printString("Undefined behavior.\n");
    }
    for (int ii = 0; envp[ii] != NULL; ii++) {
      if (assignMemory(envp[ii], processDescriptor->pid) != 0) {
        printString(__func__);
        printString(": ");
        printInt(__LINE__);
        printString(": ");
        printString("WARNING: Could not assign envp[");
        printInt(ii);
        printString("] to exec process.\n");
        printString("Undefined behavior.\n");
      }
    }
  }

  if (assignMemory(processDescriptor->fileDescriptors,
    processDescriptor->pid) != 0
  ) {
    printString("WARNING: Could not assign fileDescriptors to scheduler.\n");
    printString("Undefined behavior.\n");
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    if (assignMemory(processDescriptor->fileDescriptors[ii],
      processDescriptor->pid) != 0
    ) {
      printString("WARNING: Could not assign fileDescriptors[");
      printInt(ii);
      printString("] to scheduler.\n");
      printString("Undefined behavior.\n");
    }
  }

  processDescriptor->overlayDir = pathname;
  returnValue = loadProcessDescriptorOverlayMetadata(processDescriptor);
  if (returnValue != 0) {
    processMessageData(processMessage) = (void*) ((intptr_t) returnValue);
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
    if (schedulerAssignPortToProcessId(
      /*commandDescriptor->consolePort*/ 255, processDescriptor->pid)
      != processSuccess
    ) {
      printString("WARNING: Could not assign console port to process.\n");
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

  SpawnArgs *spawnArgs = (SpawnArgs*) processMessageData(processMessage);
  processMessageData(processMessage) = (void*) ((uintptr_t) 0);
  if (spawnArgs == NULL) {
    printString("ERROR! spawnArgs provided was NULL.\n");
    processMessageData(processMessage) = (void*) ((uintptr_t) EINVAL);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }

  char *pathname = spawnArgs->path;
  if (pathname == NULL) {
    // Invalid
    printString("ERROR! pathname provided was NULL.\n");
    processMessageData(processMessage) = (void*) ((uintptr_t) EINVAL);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **argv = spawnArgs->argv;
  if (argv == NULL) {
    // Invalid
    printString("ERROR! argv provided was NULL.\n");
    processMessageData(processMessage) = (void*) ((uintptr_t) EINVAL);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  } else if (argv[0] == NULL) {
    // Invalid
    printString("ERROR! argv[0] provided was NULL.\n");
    processMessageData(processMessage) = (void*) ((uintptr_t) EINVAL);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  char **envp = spawnArgs->envp;

  ProcessDescriptor *processDescriptor = processQueuePop(&schedulerState->free);
  if (processDescriptor == NULL) {
    printString("Out of process slots to launch process.\n");
    processMessageData(processMessage) = (void*) ((uintptr_t) EINVAL);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  *spawnArgs->newPid = processDescriptor->pid;

  // Initialize the new process.
  threadSetContext(processDescriptor->mainThread, processDescriptor);

  ExecArgs *execArgs = (ExecArgs*) schedMalloc(sizeof(ExecArgs));
  if (execArgs == NULL) {
    printString("Out of memory for ExecArgs.\n");
    processMessageData(processMessage) = (void*) ((uintptr_t) ENOMEM);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  execArgs->callingProcessId = pid(processMessageFrom(processMessage));
  execArgs->pathname = spawnArgs->path;
  execArgs->argv = spawnArgs->argv;
  execArgs->envp = spawnArgs->envp;
  execArgs->schedulerState = schedulerState;

  processDescriptor->userId
    = allProcesses[pid(processMessageFrom(processMessage)) - 1].userId;

  processDescriptor->numFileDescriptors = NUM_STANDARD_FILE_DESCRIPTORS;
  // Use calloc for processDescriptor->fileDescriptors in case we fail to
  // allocate one of the FileDescriptor pointers later and have to free the
  // elements of the array.  It's safe to pass NULL to free().
  processDescriptor->fileDescriptors = (FileDescriptor**) schedCalloc(1,
    NUM_STANDARD_FILE_DESCRIPTORS * sizeof(FileDescriptor*));
  if (processDescriptor->fileDescriptors == NULL) {
    printString(
      "ERROR: Could not allocate file descriptor array for new command\n");
    processMessageData(processMessage) = (void*) ((uintptr_t) ENOMEM);
    schedFree(execArgs);
    processMessageSetDone(processMessage);
    return returnValue; // 0; Don't retry this command
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    processDescriptor->fileDescriptors[ii]
      = (FileDescriptor*) schedMalloc(sizeof(FileDescriptor));
    if (processDescriptor->fileDescriptors[ii] == NULL) {
      printString("ERROR: Could not allocate memory for file descriptor ");
      printInt(ii);
      printString(" for new process\n");
      processMessageData(processMessage) = (void*) ((uintptr_t) ENOMEM);
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

      // The dup2->dup FileDescriptor almost certainly has a non-NULL pipeEnd
      // pointer since we're handling dup2 logic, but guard anyway.
      if (dup2->dup->pipeEnd != NULL) {
        if (dup2->fd == STDIN_FILENO) {
          // We need to set the pid of the outputChannel of the other end of
          // the pipe to our ID.
          dup2->dup->pipeEnd->outputChannel.pid = processDescriptor->pid;
        } else if ((dup2->fd == STDOUT_FILENO) || (dup2->fd == STDERR_FILENO)) {
          // We need to set the pid of the inputChannel of the other end of
          // the pipe to our ID.
          dup2->dup->pipeEnd->inputChannel.pid = processDescriptor->pid;
        }
      }
    }

    schedFree(spawnArgs->fileActions); spawnArgs->fileActions = NULL;
  }

  schedFree(spawnArgs); spawnArgs = NULL;

  if (processCreate(processDescriptor, execCommand, execArgs) == processError) {
    printString(
      "ERROR: Could not configure process handle for new command.\n");
  }

  if (assignMemory(execArgs, processDescriptor->pid) != 0) {
    printString("WARNING: Could not assign execArgs to spawn process.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(pathname, processDescriptor->pid) != 0) {
    printString("WARNING: Could not assign pathname to spawn process.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(argv, processDescriptor->pid) != 0) {
    printString("WARNING: Could not assign argv to spawn process.\n");
    printString("Undefined behavior.\n");
  }
  for (int ii = 0; argv[ii] != NULL; ii++) {
    if (assignMemory(argv[ii], processDescriptor->pid) != 0) {
      printString("WARNING: Could not assign argv[");
      printInt(ii);
      printString("] to spawn process.\n");
      printString("Undefined behavior.\n");
    }
  }

  if (envp != NULL) {
    if (assignMemory(envp, processDescriptor->pid) != 0) {
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ");
      printString("WARNING: Could not assign envp to spawn process.\n");
      printString("Undefined behavior.\n");
    }
    for (int ii = 0; envp[ii] != NULL; ii++) {
      if (assignMemory(envp[ii], processDescriptor->pid) != 0) {
        printString(__func__);
        printString(": ");
        printInt(__LINE__);
        printString(": ");
        printString("WARNING: Could not assign envp[");
        printInt(ii);
        printString("] to spawn process.\n");
        printString("Undefined behavior.\n");
      }
    }
  }

  if (assignMemory(processDescriptor->fileDescriptors,
    processDescriptor->pid) != 0
  ) {
    printString("WARNING: Could not assign fileDescriptors to spawn process.\n");
    printString("Undefined behavior.\n");
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    if (assignMemory(processDescriptor->fileDescriptors[ii],
      processDescriptor->pid) != 0
    ) {
      printString("WARNING: Could not assign fileDescriptors[");
      printInt(ii);
      printString("] to spawn process.\n");
      printString("Undefined behavior.\n");
    }
  }

  processDescriptor->overlayDir = pathname;
  returnValue = loadProcessDescriptorOverlayMetadata(processDescriptor);
  if (returnValue != 0) {
    processMessageData(processMessage) = (void*) ((uintptr_t) returnValue);
    returnValue = 0; // Don't retry this command
    processMessageSetDone(processMessage);

    // We have to terminate the process because something may have pushed a message
    // onto its message queue.
    processTerminate(processDescriptor);
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

  processMessageSetDone(processMessage);

  return returnValue;
}

/// @fn int schedulerAssignMemoryCommandHandler(
///   SchedulerState *schedulerState, ProcessMessage *processMessage)
///
/// @brief Assign a piece of memory to the scheduler for ownership.
///
/// @param schedulerState A pointer to the SchedulerState maintained by the
///   scheduler process.
/// @param processMessage A pointer to the ProcessMessage that was received.
///
/// @return Returns 0 on success, non-zero error code on failure.
int schedulerAssignMemoryCommandHandler(
  SchedulerState *schedulerState, ProcessMessage *processMessage
) {
  (void) schedulerState;

  if (processMessage == NULL) {
    // This should be impossible, but there's nothing to do.  Return good
    // status.
    return 0;
  }

  void *ptr = processMessageData(processMessage);
  int returnValue = assignMemory(ptr, SCHEDULER_STATE->schedulerProcessId);
  if (returnValue == 0) {
    processMessageInit(processMessage, 0, (void*) ((intptr_t) 0), 0, true);
  } else {
    printString("WARNING: Could not assign memory from process ");
    printInt(pid(processMessageFrom(processMessage)));
    printString(" to the scheduler\n");
    printString("Undefined behavior.\n");
    processMessageInit(processMessage, 0,
      (void*) ((intptr_t) returnValue), 0, true);
  }

  processMessageSetDone(processMessage);

  return 0;
}

/// @typedef SchedulerCommandHandler
///
/// @brief Signature of command handler for a scheduler command.
typedef int (*SchedulerCommandHandler)(SchedulerState*, ProcessMessage*);

/// @var schedulerCommandHandlers
///
/// @brief Array of function pointers for commands that are understood by the
/// message handler for the main loop function.
const SchedulerCommandHandler schedulerCommandHandlers[] = {
  schedulerKillProcessCommandHandler,          // SCHEDULER_KILL_TASK
  // SCHEDULER_GET_NUM_RUNNING_TASKS:
  schedulerGetNumProcessDescriptorsCommandHandler,
  schedulerGetProcessInfoCommandHandler,       // SCHEDULER_GET_TASK_INFO
  schedulerGetProcessUserCommandHandler,       // SCHEDULER_GET_TASK_USER
  schedulerSetProcessUserCommandHandler,       // SCHEDULER_SET_TASK_USER
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
///   the scheduler process.
///
/// @return This function returns no value.
void handleSchedulerMessage(SchedulerState *schedulerState) {
  static int lastReturnValue = 0;
  ProcessMessage *message = processMessageQueuePop();
  if (message != NULL) {
    SchedulerCommand messageType
      = (SchedulerCommand) processMessageType(message);
    if (messageType >= NUM_SCHEDULER_COMMANDS) {
      // Invalid.  Purge the message.
      printInt(getRunningProcessId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": Received invalid message 0x");
      printHex((uintptr_t) message);
      printString(" of type ");
      printInt(messageType);
      printString(" from process ");
      printInt(pid(processMessageFrom(message)));
      printString("\n");
      return;
    }

    int returnValue = schedulerCommandHandlers[messageType](
      schedulerState, message);
    if (returnValue != 0) {
      // Processing the message failed.  We can't release it.  Put it on the
      // back of our own queue again and try again later.
      if (lastReturnValue == 0) {
        // Only print out a message if this is the first time we've failed.
        printString("Scheduler command handler failed for message ");
        printInt(messageType);
        printString("\n");
        printString("Pushing message back onto our own queue\n");
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
  processYieldTo(&allProcesses[SCHEDULER_STATE->schedulerProcessId - 1]);
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
  
  ProcessMessage *dumpMemoryAllocationsMessage = getAvailableMessage();
  if (dumpMemoryAllocationsMessage != NULL) {
    processMessageInit(dumpMemoryAllocationsMessage,
      MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS, NULL, 0, true);
    if (schedulerSendProcessMessageToProcess(
      &schedulerState->allProcesses[SCHEDULER_STATE->memoryManagerProcessId - 1],
      dumpMemoryAllocationsMessage) != processSuccess
    ) {
      printString("ERROR: Could not send message ");
      printString("MEMORY_MANAGER_DUMP_MEMORY_ALLOCATIONS to memory manager\n");
      processMessageRelease(dumpMemoryAllocationsMessage);
    }
    if (processMessageDone(dumpMemoryAllocationsMessage) == false) {
      printString("ERROR: dumpMemoryAllocationsMessage is not done!\n");
    }
    processMessageRelease(dumpMemoryAllocationsMessage);
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
  
  ProcessMessage *dumpOpenFilesMessage = getAvailableMessage();
  if (dumpOpenFilesMessage != NULL) {
    processMessageInit(dumpOpenFilesMessage,
      FILESYSTEM_DUMP_OPEN_FILES, NULL, 0, true);
    if (schedulerSendProcessMessageToProcess(
      &schedulerState->allProcesses[schedulerState->rootFsProcessId - 1],
      dumpOpenFilesMessage) != processSuccess
    ) {
      printString("ERROR: Could not send FILESYSTEM_DUMP_OPEN_FILES message ");
      printString("to root FS process ID ");
      printInt(schedulerState->rootFsProcessId);
      printString("\n");
    }
    if (processMessageDone(dumpOpenFilesMessage) == false) {
      printString("ERROR: dumpOpenFilesMessage is not done!\n");
    }
    processMessageRelease(dumpOpenFilesMessage);
  } else {
    printString("WARNING: Could not allocate dumpOpenFilesMessage.\n");
    returnValue = -1;
  }
  
  return returnValue;
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
void removeProcess(ProcessDescriptor *processDescriptor, const char *errorMessage) {
  printString("ERROR: ");
  printString(errorMessage);
  printString("\n");
  printString("       Removing process ");
  printInt(processDescriptor->pid);
  printString(" from process queues\n");

  processDescriptor->name = NULL;
  processDescriptor->userId = NO_USER_ID;
  processDescriptor->mainThread->state = TASK_STATE_NOT_RUNNING;

  if (schedulerInitSendMessageToProcessId(
    SCHEDULER_STATE->consoleProcessId,
    CONSOLE_RELEASE_PID_PORT,
    /* data= */ (void*) ((intptr_t) processDescriptor->pid),
    /* size= */ 0) != processSuccess
  ) {
    printString("ERROR: Could not send CONSOLE_RELEASE_PID_PORT message ");
    printString("to console process\n");
  }

  if (schedulerInitSendMessageToProcessId(
    SCHEDULER_STATE->memoryManagerProcessId,
    MEMORY_MANAGER_FREE_TASK_MEMORY,
    (void*) ((uintptr_t) processDescriptor->pid), /* size= */ 0) != processSuccess
  ) {
    printString("ERROR: Could not free process memory. Memory leak.\n");
  }

  return;
}

/// @fn int schedulerLoadOverlay(FileBlockMetadata *overlay, char **envp)
///
/// @brief Load and configure an overlay into the overlayMap in memory.
///
/// @param overlay A pointer to the FileBlockMetadata that describes the overlay
///   to load.
/// @param envp The array of environment variables in "name=value" form.
///
/// @return Returns 0 on success, negative error code on failure.
int schedulerLoadOverlay(FileBlockMetadata *overlay, char **envp) {
  if (overlay == NULL) {
    // There's no overlay to load.  This isn't really an error, but there's
    // nothing to do.  Just return 0.
    return 0;
  }

  NanoOsOverlayMap *overlayMap = HAL->memory->overlayMap;
  if ((overlayMap == NULL) || (HAL->memory->overlaySize == 0)) {
    printString("No overlay memory available for use.\n");
    return -ENOMEM;
  }

  NanoOsOverlayHeader *overlayHeader = &overlayMap->header;
  if ((overlayHeader->overlay.blockDevice == overlay->blockDevice)
    && (overlayHeader->overlay.startBlock == overlay->startBlock)
    && (overlayHeader->overlay.numBlocks == overlay->numBlocks)
  ) {
    // Overlay is already loaded.  Do nothing.
    return 0;
  }

  if (overlay->blockDevice->schedReadBlocks(
    overlay->blockDevice->context,
    overlay->startBlock,
    overlay->numBlocks,
    overlay->blockDevice->blockSize,
    (uint8_t*) overlayMap) != 0
  ) {
    printString("Could not read overlay\n");
    return -EIO;
  }

  if (overlayMap->header.magic != NANO_OS_OVERLAY_MAGIC) {
    printString("Overlay magic was not \"NanoOsOL\".\n");
    printDebugString("Expected 0x");
    printDebugHex((uintptr_t) NANO_OS_OVERLAY_MAGIC);
    printDebugString("\n");

    printDebugString("overlayMap->header.osApi = 0x");
    printDebugHex((uintptr_t) overlayMap->header.osApi);
    printDebugString("\noverlayMap->header.env = 0x");
    printDebugHex((uintptr_t) overlayMap->header.env);
    printDebugString("\noverlayMap->header.overlay.blockDevice = 0x");
    printDebugHex((uintptr_t) overlayMap->header.overlay.blockDevice);
    printDebugString("\noverlayMap->header.overlay.startBlock = ");
    printDebugInt(overlayMap->header.overlay.startBlock);
    printDebugString("\noverlayMap->header.overlay.numBlocks = ");
    printDebugInt(overlayMap->header.overlay.numBlocks);
    printDebugString("\noverlayMap->header.version = 0x");
    printDebugHex((uintptr_t) overlayMap->header.version);
    printDebugString("\noverlayMap->header.magic = 0x");
    printDebugHex((uintptr_t) overlayMap->header.magic);
    printDebugString("\noverlayMap->exports = 0x");
    printDebugHex((uintptr_t) overlayMap->exports);
    printDebugString("\noverlayMap->numExports = 0x");
    printDebugHex((uintptr_t) overlayMap->numExports);
    printDebugString("\n");

    return -ENOEXEC;
  }
  if (overlayMap->header.version != NANO_OS_OVERLAY_VERSION) {
    printString("Overlay version is 0x");
    printHex(overlayMap->header.version);
    printString("\n");
    return -ENOEXEC;
  }

  // Set the pieces of the overlay header that the program needs to run.
  overlayHeader->osApi = &nanoOsApi;
  overlayHeader->env = envp;
  overlayHeader->overlay.blockDevice = overlay->blockDevice;
  overlayHeader->overlay.startBlock = overlay->startBlock;
  overlayHeader->overlay.numBlocks = overlay->numBlocks;
  
  return 0;
}

/// @fn int schedulerRunOverlayCommand(
///   SchedulerState *schedulerState, ProcessDescriptor *processDescriptor,
///   const char *commandPath, int argc, const char **argv, const char **envp)
///
/// @brief Launch a command that's in overlay format on the filesystem.
///
/// @param schedulerState A pointer to the SchedulerState object maintained by
///   the scheduler process.
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
int schedulerRunOverlayCommand(
  SchedulerState *schedulerState, ProcessDescriptor *processDescriptor,
  char *commandPath, char **argv, char **envp
) {
  int returnValue = 0;

  // Copy over the exec args.
  ExecArgs *execArgs = schedMalloc(sizeof(ExecArgs));
  if (execArgs == NULL) {
    returnValue = -ENOMEM;
    goto exit;
  }
  execArgs->callingProcessId = processDescriptor->pid;

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

  execArgs->schedulerState = schedulerState;

  if (assignMemory(execArgs, processDescriptor->pid) != 0) {
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": ");
    printString("WARNING: Could not assign execArgs to exec process.\n");
    printString("Undefined behavior.\n");
  }

  if (assignMemory(execArgs->pathname, processDescriptor->pid) != 0) {
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": ");
    printString(
      "WARNING: Could not assign execArgs->pathname to exec process.\n");
    printString("Undefined behavior.\n");
  }

  if (execArgs->argv != NULL) {
    if (assignMemory(execArgs->argv, processDescriptor->pid) != 0) {
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ");
      printString("WARNING: Could not assign argv to exec process.\n");
      printString("Undefined behavior.\n");
    }

    for (int ii = 0; execArgs->argv[ii] != NULL; ii++) {
      if (assignMemory(execArgs->argv[ii], processDescriptor->pid) != 0) {
        printString(__func__);
        printString(": ");
        printInt(__LINE__);
        printString(": ");
        printString("WARNING: Could not assign execArgs->argv[");
        printInt(ii);
        printString("] to exec process.\n");
        printString("Undefined behavior.\n");
      }
    }
  }

  if (execArgs->envp != NULL) {
    if (assignMemory(execArgs->envp, processDescriptor->pid) != 0) {
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ");
      printString(
        "WARNING: Could not assign execArgs->envp to exec process.\n");
      printString("Undefined behavior.\n");
    }

    for (int ii = 0; execArgs->envp[ii] != NULL; ii++) {
      if (assignMemory(execArgs->envp[ii], processDescriptor->pid) != 0) {
        printString(__func__);
        printString(": ");
        printInt(__LINE__);
        printString(": ");
        printString("WARNING: Could not assign execArgs->envp[");
        printInt(ii);
        printString("] to exec process\n");
        printString("Undefined behavior\n");
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
    printString(
      "ERROR: Could not allocate file descriptor array for new command\n");
    returnValue = -ENOMEM;
    goto freeExecArgs;
  }
  for (int ii = 0; ii < processDescriptor->numFileDescriptors; ii++) {
    processDescriptor->fileDescriptors[ii]
      = (FileDescriptor*) schedMalloc(sizeof(FileDescriptor));
    if (processDescriptor->fileDescriptors[ii] == NULL) {
      printString("ERROR: Could not allocate memory for file descriptor ");
      printInt(ii);
      printString(" for new process\n");
      returnValue = -ENOMEM;
      goto freeFileDescriptors;
    }
    memcpy(
      processDescriptor->fileDescriptors[ii],
      &standardUserFileDescriptors[ii],
      sizeof(FileDescriptor)
    );
  }

  if (processCreate(processDescriptor, execCommand, execArgs) == processError) {
    printString(
      "ERROR: Could not configure process handle for new command\n");
    returnValue = -ENOEXEC;
    goto freeFileDescriptors;
  }

  processDescriptor->overlayDir = execArgs->pathname;
  returnValue = loadProcessDescriptorOverlayMetadata(processDescriptor);
  if (returnValue != 0) {
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
    printString(__func__);
    printString(": ");
    printInt(__LINE__);
    printString(": Freeing execArgs->envp = 0x");
    printHex((uintptr_t) processDescriptor->envp);
    printString("\n");
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

/// @fn void runScheduler(void)
///
/// @brief Run one (1) iteration of the main scheduler loop.
///
/// @return This function returns no value.
void runScheduler(void) {
  ProcessDescriptor *processDescriptor
    = processQueuePop(SCHEDULER_STATE->currentReady);
  if (processDescriptor == NULL) {
    // Nothing we can do.
    printString("ERROR: No processes to pop in ");
    printString(SCHEDULER_STATE->currentReady->name);
    printString(" process queue\n");
    return;
  }

  if (processCorrupted(processDescriptor)) {
    removeProcess(processDescriptor, "Process corruption detected");
    return;
  }

  if (processDescriptor->pid >= SCHEDULER_STATE->firstUserProcessId) {
    if (processRunning(processDescriptor) == true) {
      // This is a user process, which is in an overlay.  Make sure it's loaded.
      if (schedulerLoadOverlay(
        &processDescriptor->overlay,
        processDescriptor->envp) != 0
      ) {
        schedulerDumpMemoryAllocations(SCHEDULER_STATE);
        schedulerDumpOpenFiles(SCHEDULER_STATE);
        removeProcess(processDescriptor, "Overlay load failure");
        return;
      }
    }
    
    // Configure the preemption timer to force the process to yield if it
    // doesn't voluntarily give up control within a reasonable amount of time.
    if (SCHEDULER_STATE->preemptionTimer > -1) {
      // No need to check HAL->timer for NULL since it can't be NULL in this
      // case.
      HAL->timer->configOneShot(
        SCHEDULER_STATE->preemptionTimer, 10000000, forceYield);
    }
  }
  processResume(processDescriptor, NULL);
  // No need to call HAL->timer->cancel since that's called by
  // yieldCallback if we're running preemptive multiprocessing.

  if (processRunning(processDescriptor) == false) {
    if (schedulerInitSendMessageToProcessId(
      SCHEDULER_STATE->memoryManagerProcessId, MEMORY_MANAGER_FREE_TASK_MEMORY,
      (void*) ((uintptr_t) processDescriptor->pid), 0) != processSuccess
    ) {
      printString("ERROR: Could not send MEMORY_MANAGER_FREE_TASK_MEMORY ");
      printString("message to memory manager\n");
    }

    // Terminate the process so that any lingering messages in its message queue
    // get released.
    processTerminate(processDescriptor);
    threadSetContext(processDescriptor->mainThread, processDescriptor);
    memset(&processDescriptor->message, 0, sizeof(ProcessMessage));
  }

  // Check the shells and restart them if needed.
  if ((processDescriptor->pid >= SCHEDULER_STATE->firstShellProcessId)
    && (processDescriptor->pid
      < (SCHEDULER_STATE->firstShellProcessId + SCHEDULER_STATE->numShells))
    && (processRunning(processDescriptor) == false)
  ) {
    if ((SCHEDULER_STATE->hostname == NULL)
      || (*SCHEDULER_STATE->hostname == '\0')
    ) {
      // We're not done initializing yet.  Put the process back on the ready
      // queue and try again later.
      processQueuePush(SCHEDULER_STATE->currentReady, processDescriptor);
      return;
    }

    if (processDescriptor->userId == NO_USER_ID) {
      // Login failed.  Re-launch getty.
      if (schedulerRunOverlayCommand(SCHEDULER_STATE, processDescriptor,
        "/usr/bin/getty", (char**) gettyArgs, NULL) != 0
      ) {
        removeProcess(processDescriptor, "Failed to load getty");
        return;
      }
    } else {
      // User process exited.  Re-launch the shell.
      char *passwdStringBuffer = NULL;
      struct passwd *pwd = NULL;
      do {
        passwdStringBuffer
          = (char*) schedMalloc(NANO_OS_PASSWD_STRING_BUF_SIZE);
        if (passwdStringBuffer == NULL) {
          printString(
            "ERROR! Could not allocate space for passwdStringBuffer in "
            "runScheduler\n");
          break;
        }
        
        pwd = (struct passwd*) schedMalloc(sizeof(struct passwd));
        if (pwd == NULL) {
          printString("ERROR! Could not allocate space for pwd in "
            "runScheduler\n");
          break;
        }
        
        struct passwd *result = NULL;
        nanoOsGetpwuid_r(processDescriptor->userId, pwd,
          passwdStringBuffer, NANO_OS_PASSWD_STRING_BUF_SIZE, &result);
        if (result == NULL) {
          printString("Could not find passwd info for uid ");
          printInt(processDescriptor->userId);
          printString("\n");
          break;
        }
        
        // strrchr(pwd->pw_shell, '/') must be non-NULL in order for pw_shell
        // to be valid.
        shellArgs[0] = strrchr(pwd->pw_shell, '/') + 1;
        if (schedulerRunOverlayCommand(SCHEDULER_STATE, processDescriptor,
          pwd->pw_shell, (char**) shellArgs, processDescriptor->envp) != 0
        ) {
          removeProcess(processDescriptor, "Failed to load shell");
          schedFree(pwd);
          schedFree(passwdStringBuffer);
          if (processDescriptor->envp != NULL) {
            printString(__func__);
            printString(": ");
            printInt(__LINE__);
            printString(": Freeing processDescriptor->envp = 0x");
            printHex((uintptr_t) processDescriptor->envp);
            printString("\n");
            for (int ii = 0; processDescriptor->envp[ii] != NULL; ii++) {
              schedFree(processDescriptor->envp[ii]);
            }
            schedFree(processDescriptor->envp);
            processDescriptor->envp = NULL;
          }
          return;
        }
      } while (0);
      schedFree(pwd);
      schedFree(passwdStringBuffer);
    }
  } else if (processRunning(processDescriptor) == false) {
    // The process is no longer running, but it wasn't a shell process.  That
    // means its envp isn't going to be reused.  Free it.
    if (processDescriptor->envp != NULL) {
      for (int ii = 0; processDescriptor->envp[ii] != NULL; ii++) {
        schedFree(processDescriptor->envp[ii]);
      }
      schedFree(processDescriptor->envp);
      processDescriptor->envp = NULL;
    }
  }

  if (processState(processDescriptor) == TASK_STATE_WAIT) {
    processQueuePush(&SCHEDULER_STATE->waiting, processDescriptor);
  } else if (processState(processDescriptor) == TASK_STATE_TIMEDWAIT) {
    processQueuePush(&SCHEDULER_STATE->timedWaiting, processDescriptor);
  } else if (processFinished(processDescriptor)) {
    processQueuePush(&SCHEDULER_STATE->free, processDescriptor);
  } else { // Process is still running.
    processQueuePush(SCHEDULER_STATE->currentReady, processDescriptor);
  }

  checkForTimeouts(SCHEDULER_STATE);
  handleSchedulerMessage(SCHEDULER_STATE);

  return;
}

/// @fn void startScheduler(SchedulerState **threadStatePointer)
///
/// @brief Initialize and run the round-robin scheduler.
///
/// @return This function returns no value and, in fact, never returns at all.
__attribute__((noinline)) void startScheduler(
  SchedulerState **threadStatePointer
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
  schedulerState.preemptionTimer = -1;
  if ((HAL->timer != NULL) && (HAL->timer->getNum() > 0)) {
    schedulerState.preemptionTimer = 0;
  }
  schedulerState.schedulerProcessId = 1;
  schedulerState.consoleProcessId = 2;
  schedulerState.memoryManagerProcessId = 3;
  schedulerState.firstUserProcessId = 4;
  schedulerState.firstShellProcessId = 4;
  schedulerState.runScheduler = runScheduler;
  SCHEDULER_STATE = &schedulerState;
  printDebugString("Set scheduler state.\n");

  // Initialize the pointer that was used to configure threads.
  *threadStatePointer = &schedulerState;

  // Initialize the static ProcessMessage storage.
  ProcessMessage messagesStorage[NANO_OS_NUM_MESSAGES] = {0};
  extern ProcessMessage *messages;
  messages = messagesStorage;
  printDebugString("Allocated messages storage.\n");

  // Initialize the allProcesses pointer.  The processes are all zeroed because
  // we zeroed the entire schedulerState when we declared it.
  allProcesses = schedulerState.allProcesses;

  // Initialize the scheduler in the array of running commands.
  allProcesses[schedulerState.schedulerProcessId - 1].mainThread = schedulerThread;
  allProcesses[schedulerState.schedulerProcessId - 1].pid
    = schedulerState.schedulerProcessId;
  allProcesses[schedulerState.schedulerProcessId - 1].name = "init";
  allProcesses[schedulerState.schedulerProcessId - 1].userId = ROOT_USER_ID;
  threadSetContext(allProcesses[schedulerState.schedulerProcessId - 1].mainThread,
    &allProcesses[schedulerState.schedulerProcessId - 1]);
  printDebugString("Configured scheduler process.\n");

  // Initialize the global file descriptors.
  // Kernel stdin file descriptor doesn't need an update because they don't
  // receive stdin.  Direct kernel process stdout and stderr to the console.
  standardKernelFileDescriptors[1].outputChannel.pid
    = schedulerState.consoleProcessId;
  standardKernelFileDescriptors[1].outputChannel.messageType
    = CONSOLE_WRITE_BUFFER;
  standardKernelFileDescriptors[2].outputChannel.pid
    = schedulerState.consoleProcessId;
  standardKernelFileDescriptors[2].outputChannel.messageType
    = CONSOLE_WRITE_BUFFER;

  // Direct the input pipe of user process stdin to the console.  Direcdt the
  // output pipes of user process stdout and stderr to the console as well.
  standardUserFileDescriptors[0].inputChannel.pid
    = schedulerState.consoleProcessId;
  standardUserFileDescriptors[0].inputChannel.messageType
    = CONSOLE_WAIT_FOR_INPUT;
  standardUserFileDescriptors[1].outputChannel.pid
    = schedulerState.consoleProcessId;
  standardUserFileDescriptors[1].outputChannel.messageType
    = CONSOLE_WRITE_BUFFER;
  standardUserFileDescriptors[2].outputChannel.pid
    = schedulerState.consoleProcessId;
  standardUserFileDescriptors[2].outputChannel.messageType
    = CONSOLE_WRITE_BUFFER;

  for (uint8_t ii = 0;
    ii < HAL->memory->numExtraSchedulerStacks(USE_HAL_MEMORY_DEBUG);
    ii++
  ) {
    if (threadProvision(NULL, dummyProcess, NULL) == NULL) {
      printString("Could not increase scheduler process's stack size.\n");
    }
  }

  // Create the console process.  We used to have to double the size of the
  // console's stack, so we create this process before we create anything else.
  // Leaving it at this point of initialization in case we ever have to come
  // back to that flow again.
  ProcessDescriptor *processDescriptor
    = &allProcesses[schedulerState.consoleProcessId - 1];
  if (processCreate(processDescriptor, runConsole, NULL) != processSuccess) {
    printString("Could not create console process.\n");
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->pid = schedulerState.consoleProcessId;
  processDescriptor->name = "console";
  processDescriptor->userId = ROOT_USER_ID;
  printDebugString("Created console process.\n");

  for (uint8_t ii = 0;
    ii < HAL->memory->numExtraConsoleStacks(USE_HAL_MEMORY_DEBUG);
    ii++
  ) {
    if (threadProvision(NULL, dummyProcess, NULL) == NULL) {
      printString("Could not increase console process's stack size.\n");
    }
  }

  printDebugString("\n");
  printDebugString("sizeof(int) = ");
  printDebugInt(sizeof(int));
  printDebugString("\n");
  printDebugString("sizeof(void*) = ");
  printDebugInt(sizeof(void*));
  printDebugString("\n");
  printDebugString("Main stack size = ");
  printDebugInt(ABS_DIFF(
    ((intptr_t) schedulerThread),
    ((intptr_t) allProcesses[schedulerState.consoleProcessId - 1].mainThread)
  ));
  printDebugString(" bytes\n");
  printDebugString("schedulerState size = ");
  printDebugInt(sizeof(SchedulerState));
  printDebugString(" bytes\n");
  printDebugString("messagesStorage size = ");
  printDebugInt(sizeof(ProcessMessage) * NANO_OS_NUM_MESSAGES);
  printDebugString(" bytes\n");
  printDebugString("ConsoleState size = ");
  printDebugInt(sizeof(ConsoleState));
  printDebugString(" bytes\n");

  // schedulerState.firstUserProcessId isn't populated until HAL->initRootStorage
  // completes, so we need to call that as soon as we can.
  int rv = HAL->initRootStorage(&schedulerState);
  if (rv != 0) {
    printString("ERROR: initRootStorage returned status ");
    printInt(rv);
    printString("\n");
  }
  printDebugString("Initialized root storage\n");

  // Initialize all the kernel process file descriptors.
  for (ProcessId ii = 1; ii <= schedulerState.firstUserProcessId; ii++) {
    allProcesses[ii - 1].numFileDescriptors = NUM_STANDARD_FILE_DESCRIPTORS;
    allProcesses[ii - 1].fileDescriptors = standardKernelFileDescriptorsPointers;
  }
  printDebugString("Initialized kernel process file descriptors.\n");

  // Start the console by calling processResume.
  processResume(&allProcesses[schedulerState.consoleProcessId - 1], NULL);
  printDebugString("Started console process.\n");

  schedulerState.numShells = schedulerGetNumConsolePorts(&schedulerState);
  if (schedulerState.numShells <= 0) {
    // This should be impossible since the HAL was successfully initialized,
    // but take no chances.
    printString("ERROR! No console ports running.\nHalting.\n");
    while(1);
  }
  // Irrespective of how many ports the console may be running, we can't run
  // more shell processes than what we're configured for.  Make sure we set a
  // sensible limit.
  schedulerState.numShells
    = MIN(schedulerState.numShells, NANO_OS_MAX_NUM_SHELLS);
  printDebugString("Managing ");
  printDebugInt(schedulerState.numShells);
  printDebugString(" shells\n");

  // We need to do an initial population of all the processes because we need to
  // get to the end of memory to run the memory manager in whatever is left
  // over.
  for (ProcessId ii = schedulerState.firstUserProcessId;
    ii <= NANO_OS_NUM_TASKS;
    ii++
  ) {
    processDescriptor = &allProcesses[ii - 1];
    if (processCreate(processDescriptor,
      dummyProcess, NULL) != processSuccess
    ) {
      printString("Could not create process ");
      printInt(ii);
      printString(".\n");
    }
    threadSetContext(
      processDescriptor->mainThread, processDescriptor);
    processDescriptor->pid = ii;
    processDescriptor->userId = NO_USER_ID;
    processDescriptor->name = "dummy";
  }
  printDebugString("Created all processes.\n");

  // allProcesses array is ordered console process, memory manager process, then either
  // the first block device or the first user process.  So, we want the process
  // after the memory manager, which would be the value of
  // schedulerState.memoryManagerProcessId since ProcessIds are one-based instead of
  // zero-based.
  printDebugString("Console stack size = ");
  printDebugInt(ABS_DIFF(
    ((uintptr_t) allProcesses[schedulerState.memoryManagerProcessId].mainThread),
    ((uintptr_t) allProcesses[schedulerState.consoleProcessId - 1].mainThread))
    - sizeof(Thread)
  );
  printDebugString(" bytes\n");

  printDebugString("Thread stack size = ");
  printDebugInt(ABS_DIFF(
    ((uintptr_t) allProcesses[schedulerState.firstUserProcessId - 1].mainThread),
    ((uintptr_t) allProcesses[schedulerState.firstUserProcessId].mainThread))
    - sizeof(Thread)
  );
  printDebugString(" bytes\n");

  printDebugString("Thread size = ");
  printDebugInt(sizeof(Thread));
  printDebugString("\n");

  printDebugString("standardKernelFileDescriptors size = ");
  printDebugInt(sizeof(standardKernelFileDescriptors));
  printDebugString("\n");

  // Create the memory manager process.  : THIS MUST BE THE LAST TASK
  // CREATED BECAUSE WE WANT TO USE THE ENTIRE REST OF MEMORY FOR IT :
  processDescriptor = &allProcesses[schedulerState.memoryManagerProcessId - 1];
  if (processCreate(processDescriptor,
    runMemoryManager, NULL) != processSuccess
  ) {
    printString("Could not create memory manager process.\n");
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->pid = schedulerState.memoryManagerProcessId;
  processDescriptor->name = "memory manager";
  processDescriptor->userId = ROOT_USER_ID;
  printDebugString("Created memory manager.\n");

  // Start the memory manager by calling processResume.
  processResume(&allProcesses[schedulerState.memoryManagerProcessId - 1], NULL);
  printDebugString("Started memory manager.\n");

  // Assign the console ports to it.
  for (uint8_t ii = 0; ii < schedulerState.numShells; ii++) {
    if (schedulerAssignPortToProcessId(
      ii, schedulerState.memoryManagerProcessId) != processSuccess
    ) {
      printString(
        "WARNING: Could not assign console port to memory manager.\n");
    }
  }
  printDebugString("Assigned console ports to memory manager.\n");

  // Set the shells for the ports.
  for (uint8_t ii = 0; ii < schedulerState.numShells; ii++) {
    if (schedulerSetPortShell(ii, schedulerState.firstShellProcessId + ii)
      != processSuccess
    ) {
      printString("WARNING: Could not set shell for ");
      printString(shellNames[ii]);
      printString(".\n");
      printString("         Undefined behavior will result.\n");
    }
  }
  printDebugString("Set shells for ports.\n");

  // Mark all the kernel processes as being part of the kernel ready queue.
  // Skip over the scheduler (process 0).
  allProcesses[0].readyQueue = NULL;
  for (ProcessId ii = allProcesses[1].pid;
    ii < schedulerState.firstUserProcessId;
    ii++
  ) {
    allProcesses[ii - 1].readyQueue
      = &schedulerState.ready[SCHEDULER_READY_QUEUE_KERNEL];
    processQueuePush(allProcesses[ii - 1].readyQueue, &allProcesses[ii - 1]);
  }
  printDebugString("Populated kernel ready queue.\n");

  // The scheduler will take care of cleaning up the dummy processes in the
  // ready queue.
  for (ProcessId ii = schedulerState.firstUserProcessId;
    ii <= NANO_OS_NUM_TASKS;
    ii++
  ) {
    allProcesses[ii - 1].readyQueue
      = &schedulerState.ready[SCHEDULER_READY_QUEUE_USER];
    processQueuePush(allProcesses[ii - 1].readyQueue, &allProcesses[ii - 1]);
  }
  printDebugString("Populated user ready queue.\n");

  if (HAL->memory->overlayMap != NULL) {
    // Make sure the overlay map is zeroed out for first use.
    memset(HAL->memory->overlayMap, 0, sizeof(NanoOsOverlayMap));
  }

  // Get the memory manager and filesystem up and running.
  processResume(&allProcesses[schedulerState.memoryManagerProcessId - 1], NULL);
  processResume(&allProcesses[schedulerState.rootFsProcessId - 1], NULL);
  printDebugString("Started memory manager and filesystem.\n");

  // Allocate memory for the hostname.
  schedulerState.hostname = (char*) schedCalloc(1, HOST_NAME_MAX + 1);
  printDebugString("Allocated memory for the hostname.\n");
  if (schedulerState.hostname != NULL) {
    FILE *hostnameFile = schedFopen("/etc/hostname", "r");
    if (hostnameFile != NULL) {
      printDebugString("Opened hostname file.\n");
      if (schedFgets(
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
      schedFclose(hostnameFile);
      printDebugString("Closed hostname file.\n");
      printDebugString("hostname = ");
      printDebugString(schedulerState.hostname);
      printDebugString("\n");
    } else {
      printString("ERROR! schedFopen of hostname returned NULL!\n");
      strcpy(schedulerState.hostname, "localhost");
    }
  } else {
    printString("ERROR! schedulerState.hostname is NULL!\n");
  }

#ifdef NANO_OS_DEBUG
  bool sanityTestFailed = false;
  do {
    FILE *helloFile = schedFopen("hello", "w");
    if (helloFile == NULL) {
      printDebugString("ERROR: Could not open hello file for writing!\n");
      sanityTestFailed = true;
      break;
    }
    printDebugString("helloFile is non-NULL!\n");

    if (schedFputs("world", helloFile) == EOF) {
      printDebugString("ERROR: Could not write to hello file!\n");
      schedFclose(helloFile);
      sanityTestFailed = true;
      break;
    }
    schedFclose(helloFile);

    helloFile = schedFopen("hello", "r");
    if (helloFile == NULL) {
      printDebugString("ERROR: Could not open hello file for reading after write!\n");
      schedRemove("hello");
      sanityTestFailed = true;
      break;
    }
    printDebugString("Opened helloFile for reading\n");

    char worldString[11] = {0};
    if (schedFgets(
      worldString, sizeof(worldString), helloFile) != worldString
    ) {
      printDebugString("ERROR: Could not read worldString after write!\n");
      schedFclose(helloFile);
      schedRemove("hello");
      sanityTestFailed = true;
      break;
    }
    printDebugString("Read data from helloFile into worldString\n");

    if (strcmp(worldString, "world") != 0) {
      printDebugString("ERROR: Expected \"world\", read \"");
      printDebugString(worldString);
      printDebugString("\"!\n");
      schedFclose(helloFile);
      schedRemove("hello");
      sanityTestFailed = true;
      break;
    }
    printDebugString("Successfully read \"world\" from \"hello\"!\n");
    schedFclose(helloFile);

    helloFile = schedFopen("hello", "a");
    if (helloFile == NULL) {
      printDebugString("ERROR: Could not open hello file for appending!\n");
      schedRemove("hello");
      sanityTestFailed = true;
      break;
    }

    if (schedFputs("world", helloFile) == EOF) {
      printDebugString("ERROR: Could not append to hello file!\n");
      schedFclose(helloFile);
      schedRemove("hello");
      sanityTestFailed = true;
      break;
    }
    schedFclose(helloFile);

    helloFile = schedFopen("hello", "r");
    if (helloFile == NULL) {
      printDebugString(
        "ERROR: Could not open hello file for reading after append!\n");
      schedRemove("hello");
      sanityTestFailed = true;
      break;
    }

    if (schedFgets(
      worldString, sizeof(worldString), helloFile) != worldString
    ) {
      printDebugString("ERROR: Could not read worldString after append!\n");
      schedFclose(helloFile);
      schedRemove("hello");
      sanityTestFailed = true;
      break;
    }

    if (strcmp(worldString, "worldworld") == 0) {
      printDebugString(
        "Successfully read \"worldworld\" from \"hello\"!\n");
    } else {
      printDebugString("ERROR: Expected \"worldworld\", read \"");
      printDebugString(worldString);
      printDebugString("\"!\n");
      sanityTestFailed = true;
    }

    schedFclose(helloFile);
    if (schedRemove("hello") != 0) {
      printDebugString(
        "ERROR: schedRemove failed to remove the \"hello\" file.\n");
      sanityTestFailed = true;
    }
  } while (0);
  printDebugString("Filesystem sanity test complete\n");
  while (sanityTestFailed == true);
#endif // NANO_OS_DEBUG

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

