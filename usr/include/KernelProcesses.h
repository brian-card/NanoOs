///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              06.07.2026
///
/// @file              KernelProcesses.h
///
/// @brief             Exposed NanoOs kernel functionality related to kernel
///                    processes.
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

#ifndef KERNEL_PROCESSES_H
#define KERNEL_PROCESSES_H

#include "NanoOsUser.h"
#include "../../src/kernel/Coroutines.h"
#include "../../src/kernel/Messages.h"
#include "../../src/kernel/NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def blockOverlayId
///
/// Format a block overlay's ID into a format that can be used to load an
/// overlay.
#define blockOverlayId(id) ((void*) id)

// Process status values.
#define processSuccess  coroutineSuccess
#define processBusy     coroutineBusy
#define processError    coroutineError
#define processNomem    coroutineNomem
#define processTimedout coroutineTimedout

/// @typedef ThreadFunction
///
/// @brief Function signature that can be used as a process thread.
typedef CoroutineFunction ThreadFunction;

/// @typedef Thread
///
/// @brief Base type to hold the metadata for a thread.
typedef Coroutine Thread;

/// @typedef ThreadsConfigOptions
///
/// @brief Type that holds the options to be used to initially configure threads
/// for the system.
typedef CoroutinesConfigOptions ThreadsConfigOptions;

/// @def PROCESS_ID_NOT_SET
///
/// @brief Value to be used to indicate that a process ID has not been set for
/// a ProcessDescriptor object.
#define PROCESS_ID_NOT_SET \
  ((uint8_t) 0x0f)

/// @def PROCESS_STATE_WAIT
///
/// @brief State a process is in when waiting indefinitely.
#define PROCESS_STATE_WAIT COROUTINE_STATE_WAIT

/// @def PROCESS_STATE_TIMEDWAIT
///
/// @brief State a process is in when waiing for a limited amount of time.
#define PROCESS_STATE_TIMEDWAIT COROUTINE_STATE_TIMEDWAIT

/// @def PROCESS_STATE_NOT_RUNNING
///
/// @brief State a process is in when it is not running.
#define PROCESS_STATE_NOT_RUNNING COROUTINE_STATE_NOT_RUNNING

/// @def THREAD_STACK_END_VALUE
///
/// @brief The value used to mark the end of a thread's stack.
#define THREAD_STACK_END_VALUE COROUTINE_STACK_END_VALUE

/// @fn static inline ProcessDescriptor* getRunningProcess(void)
///
/// @brief Inline function to get the pointer to the currently running
/// ProcessDescriptor object.
///
/// @return Returns a pointer to the ProcessDescriptor for the process currently
/// executing.
static inline ProcessDescriptor* getRunningProcess(void) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return (ProcessDescriptor*) overlayMap.header.osApi->executiveApi->
      coroutineContext(
        overlayMap.header.osApi->executiveApi->getRunningCoroutine());
  }
  return NULL;
}

/// @fn static inline ProcessId getRunningPid(void)
///
/// @brief Get the process ID for the currently-running process.
///
/// @return Returns the ProcessId of the process currently executing.
static inline ProcessId getRunningPid(void) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return getRunningProcess()->processId;
  }
  return 0;
}

/// @fn static inline UserId getRunningUid(void)
///
/// @brief Get the user ID for the currently-running process.
///
/// @return Returns the ID of the user running the process currently executing.
static inline UserId getRunningUid(void) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return getRunningProcess()->userId;
  }
  return (UserId) -1;
}

/// @fn static inline int threadSetContext(Thread *thread, void *context)
///
/// @brief Set the context of a thread.
///
/// @param thread A pointer to the Thread object to set the context of.
/// @param context A pointer to whatever context should be set for the Thread.
///
/// @return Returns processSuccess on success, error status on failure.
static inline int threadSetContext(Thread *thread, void *context) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->coroutineSetContext(
      thread, context);
  }
  return processError;
}

/// @fn static inline void* threadContext(Thread *thread)
///
/// @brief Get the context previously set on a thread.
///
/// @param thread A pointer to the Thread object to get the context of.
///
/// @return Returns a pointer to the thread's context on success, NULL on
/// failure.
static inline void* threadContext(Thread *thread) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->coroutineContext(thread);
  }
  return NULL;
}

/// @fn static inline uint64_t* threadStackEnd(Thread *thread)
///
/// @brief Get the address of the end of a thread's stack.
///
/// @param thread A pointer to the Thread to get the stack end of.
///
/// @return Returns a pointer to the uint64_t at the end of the thread's stack
///   on success, NULL on failure.
static inline uint64_t* threadStackEnd(Thread *thread) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->coroutineStackEnd(thread);
  }
  return NULL;
}

/// @fn static inline int threadSetStackEnd(Thread *thread, uint64_t *stackEnd)
///
/// @brief Set the end address of a thread's stack.
///
/// @param thread A pointer to the Thread object to set the stack end of.
/// @param stackEnd A pointer to the uint64_t to use as the thread's stack end.
///   The value of the variable pointed to must be THREAD_STACK_END_VALUE.
///
/// @return Returns processSuccess on success, process error code on failure.
static inline int threadSetStackEnd(Thread *thread, uint64_t *stackEnd) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->coroutineSetStackEnd(
      thread, stackEnd);
  }
  return processError;
}

/// @fn static inline int processResetStack(
///   ProcessDescriptor *processDescriptor)
///
/// @brief Reset the stack of the main thread of a process back to a working
/// state.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to reset the
///   stack of.
///
/// @return Returns processSuccess on success, processError on failure.
static inline int processResetStack(ProcessDescriptor *processDescriptor) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    *threadStackEnd(processDescriptor->mainThread) = THREAD_STACK_END_VALUE;
    return processSuccess;
  }
  return processError;
}

/// @def processCorrupted(processDescriptor)
///
/// @brief Determine whether or not a process has become corrupted.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to investigate.
///
/// @return Returns true if the process's state was detected to be corrupt,
/// false otherwise.
#define processCorrupted(processDescriptor) \
  coroutineCorrupted(processDescriptor->mainThread)

/// @fn static inline bool processStackOverflowed(
///   ProcessDescriptor *processDescriptor)
///
/// @brief Determine whether or not a process has overflowed its stack.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to investigate.
///
/// @return Returns true if the process's stack has overflowed, false otherwise.
static inline bool processStackOverflowed(
  ProcessDescriptor *processDescriptor
) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->coroutineStackOverflowed(
      processDescriptor->mainThread);
  }
  return false;
}

/// @def processRunning(processDescriptor)
///
/// @brief Determine whether or not a given process is currently running.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to investigate.
/// 
/// @return Returns true if the process has an active function associated with
/// it, false otherwise.
#define processRunning(processDescriptor) \
  coroutineRunning((processDescriptor)->mainThread)

/// @fn processFinished(processDescriptor)
///
/// @brief Determine whether or not a given process has finished
///
/// @param processDescriptor A pointer to the ProcessDescriptor to investigate.
/// 
/// @return Returns true if the process does *NOT* have an active function
/// associated with it, false otherwise.
#define processFinished(processDescriptor) \
  coroutineFinished(processDescriptor->mainThread)

/// @def processPid(processDescriptor)
///
/// @brief Get the numeric ProcessId given a process's descriptor.
#define processPid(processDescriptor) \
  (processDescriptor)->processId

/// @def processState(processDescriptor)
///
/// @brief Get the state of a process given a pointer to its ProcessDescriptor.
#define processState(processDescriptor) \
  coroutineState((processDescriptor)->mainThread)

/// @fn static inline void* processYield(void)
///
/// @brief Yield the processor back to the scheduler.
///
/// @return Returns the value that the scheduler calls processResume with.
static inline void* processYield(void) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->coroutineYield(
      NULL, COROUTINE_STATE_BLOCKED);
  }
  return NULL;
}

/// @fn static inline void* processYieldValue(void *value)
///
/// @brief Yield the processor back to the scheduler and pass it a value.
///
/// @param value The value to pass back to the scheduler.  This will be the
///   return value of processResume() in the scheduler.
///
/// @return Returns the value that the scheduler calls processResume with.
static inline void* processYieldValue(void *value) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->coroutineYield(
      value, COROUTINE_STATE_BLOCKED);
  }
  return NULL;
}

/// @fn static inline int processMessageInit(ProcessMessage *processMessage,
///   int64_t type, void *data, size_t size, bool waiting)
///
/// @brief Initialize a process message.
///
/// @param processMessage A pointer to the ProcessMessage to initialize.
/// @param type The 64-bit value to use as the message's type.
/// @param data A pointer to the data payload of the message.
/// @param size The size, in bytes, of the data payload.
/// @param waiting Whether or not the initializating process will be waiting on
///   the message to be marked "done".
///
/// @return Returns processSuccess on success, process error code on failure.
static inline int processMessageInit(ProcessMessage *processMessage,
  int64_t type, void *data, size_t size, bool waiting
) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->processMessageInit(
      processMessage, MSG_CORO_SAFE, type, data, size, waiting);
  }
  return processError;
}

/// @fn static inline int processMessageSetDone(ProcessMessage *processMessage)
///
/// @brief Set a process message to the 'done' state.  If a process is waiting
/// for the message to be marked done, it will be signaled as a result of this
/// call.
///
/// @param processMessage A pointer to the ProcessMessage to mark done.
///
/// @return Returns processSuccess on success, process error code on failure.
static inline int processMessageSetDone(ProcessMessage *processMessage) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->processMessageSetDone(
      processMessage);
  }
  return processError;
}

/// @fn static inline int processMessageRelease(ProcessMessage *processMessage)
///
/// @brief Release but do not deallocate a process message.
///
/// @param processMessage A pointer to the ProcessMessage to release.
///
/// @return Returns processSuccess on success, process error code on failure.
static inline int processMessageRelease(ProcessMessage *processMessage) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->processMessageRelease(
      processMessage);
  }
  return processError;
}

/// @fn static inline int processMessageWaitForDone(
///   ProcessMessage *processMessage, struct timespec *ts)
///
/// @brief Wait for a process message to enter the 'done' state.
///
/// @param processMessage A pointer to the ProcessMessage to wait for.
/// @param ts A pointer to a struct timespec containing a future time to timeout
///   if the message is not marked as done by then.
///
/// @return Returns processSuccess on success, process error code on failure.
static inline int processMessageWaitForDone(
  ProcessMessage *processMessage, struct timespec *ts
) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->processMessageWaitForDone(
      processMessage, ts);
  }
  return processError;
}

/// @fn static inline ProcessMessage* processMessageQueueWaitForType(
///   int64_t type, struct timespec *ts)
///
/// @brief Wait for a message of a specific type to be pushed onto the running
/// process's message queue.
///
/// @param type An int64_t type of message to wait for.
/// @param ts A pointer to a struct timespec containing a future time to timeout
///   if a message of the specified type is not received by then.
///
/// @return Returns a pointer to a popped message of the specified type on
/// success, NULL on failure.
static inline ProcessMessage* processMessageQueueWaitForType(
  int64_t type, struct timespec *ts
) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->comessageQueueWaitForType(
      type, ts);
  }
  return NULL;
}

/// @fn static inline ProcessMessage* processMessageQueueWait(
///   struct timespec *ts)
///
/// @brief Wait for a message of any type to be pushed onto the running
/// process's message queue.
///
/// @param ts A pointer to a struct timespec containing a future time to timeout
///   if a message is not received by then.
///
/// @return Returns a pointer to a popped message of the specified type on
/// success, NULL on failure.
static inline ProcessMessage* processMessageQueueWait(struct timespec *ts) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->comessageQueueWait(ts);
  }
  return NULL;
}

/// @fn static inline int processMessageQueuePush(
///   ProcessDescriptor *processDescriptor, ProcessMessage *message)
///
/// @brief Push a process message on to a process's message queue.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to push the
///   message onto.
/// @param message A pointer to the ProcessMessage to push.
///
/// @return Returns processSuccess on success, process error code on failure.
static inline int processMessageQueuePush(
  ProcessDescriptor *processDescriptor, ProcessMessage *message
) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->comessageQueuePush(
      processDescriptor->mainThread, message);
  }
  return processError;
}

/// @fn static inline ProcessMessage* processMessageQueuePop(void)
///
/// @brief Pop a process message from the running process's message queue.
///
/// @return Returns a pointer to a popped ProcessMessage on success, NULL on
/// failure.
static inline ProcessMessage* processMessageQueuePop(void) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->comessageQueuePop();
  }
  return NULL;
}

/// @typedef ProcessMessageElement
///
/// @brief Enum mapping the elements of a ProcessMessage.
typedef msg_element_t ProcessMessageElement;

/// @fn static inline void* processMessageElement(
///   ProcessMessage *message, ProcessMessageElement element)
///
/// @brief Get a pointer to a member element of a ProcessMessage.
///
/// @param processMessage A pointer to the ProcessMessage to get the element of.
/// @param element A ProcessMessageElement value specifying which element to
///   get.
///
/// @return Returns a pointer to the specified element of the provided message
/// on success, NULL on failure.
static inline void* processMessageElement(
  ProcessMessage *processMessage, ProcessMessageElement element
) {
  if (overlayMap.header.osApi->executiveApi != NULL) {
    return overlayMap.header.osApi->executiveApi->processMessageElement(
      processMessage, element);
  }
  return NULL;
}

// Process message accessors
#define processMessageType(message) \
  (*((int64_t*) processMessageElement((message), MSG_ELEMENT_TYPE)))
#define processMessageData(message) \
  (*((void**) processMessageElement((message), MSG_ELEMENT_DATA)))
#define processMessageSize(message) \
  (*((size_t*) processMessageElement((message), MSG_ELEMENT_SIZE)))
#define processMessageWaiting(message) \
  (*((bool*) processMessageElement((message), MSG_ELEMENT_WAITING)))
#define processMessageDone(message) \
  (*((bool*) processMessageElement((message), MSG_ELEMENT_DONE)))
#define processMessageInUse(message) \
  (*((bool*) processMessageElement((message), MSG_ELEMENT_IN_USE)))
#define processMessageFrom(message) \
  ((ProcessDescriptor*) overlayMap.header.osApi->executiveApi-> \
    coroutineContext((*( \
    (msg_endpoint_t*) processMessageElement((message), \
    MSG_ELEMENT_FROM))).coro))
#define processMessageTo(message) \
  ((ProcessDescriptor*) overlayMap.header.osApi->executiveApi-> \
    coroutineContext((*( \
    (msg_endpoint_t*) processMessageElement((message), \
    MSG_ELEMENT_TO))).coro))

#ifdef __cplusplus
}
#endif

#endif // KERNEL_PROCESSES_H

