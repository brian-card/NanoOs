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

// Process status values.
#define processSuccess  coroutineSuccess
#define processBusy     coroutineBusy
#define processError    coroutineError
#define processNomem    coroutineNomem
#define processTimedout coroutineTimedout

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

/// @def getRunningProcess
///
/// @brief Inline function to get the pointer to the currently running
/// ProcessDescriptor object.
///
/// @return Returns a pointer to the ProcessDescriptor for the process currently
/// executing.
static inline ProcessDescriptor* getRunningProcess(void) {
  return overlayMap.header.osApi->getRunningCoroutineContext();
}

/// @def getRunningPid
///
/// @brief Get the process ID for the currently-running process.
///
/// @return Returns the ProcessId of the process currently executing.
static inline ProcessId getRunningPid(void) {
  return getRunningProcess()->processId;
}

/// @def getRunningUid
///
/// @brief Get the user ID for the currently-running process.
#define getRunningUid() \
  (getRunningProcess()->userId)

/// @def processCreate
///
/// @brief Function macro to create a new process.
#define processCreate(processDescriptor, func, arg) \
  coroutineCreate( \
    ((processDescriptor != NULL) \
      ? &(((ProcessDescriptor*) processDescriptor))->mainThread \
      : NULL \
    ), \
    func, \
    arg)

/// @def threadProvision
///
/// @brief Provision a Thread.
#define threadProvision(handle, func, arg) \
  coroutineInit(handle, func, arg)

/// @def threadSetContext
///
/// @brief Function macro to set the context of a process handle.
#define threadSetContext(thread, context) \
  coroutineSetContext(thread, context)

/// @def threadContext
///
/// @brief Get the context previously set on a thread.
#define threadContext(thread) coroutineContext(thread)

/// @def threadsConfig
///
/// @brief Configure the threads library.
#define threadsConfig(first, options) coroutinesConfig(first, options)

/// @def threadStackEnd
///
/// @brief Get the address of the end of a thread's stack.
#define threadStackEnd(thread) coroutineStackEnd(thread)

/// @def threadSetStackEnd
///
/// @brief Set the end address of a thread's stack to the end address of an
/// adjoining thread's stack.
#define threadSetStackEnd(thread, stackEnd) \
  coroutineSetStackEnd(thread, stackEnd)

/// @def processResetStack
///
/// @brief Reset the stack of the main thread of a process back to a working
/// state.
#define processResetStack(processDescriptor) \
  *coroutineStackEnd((processDescriptor)->mainThread) \
    = COROUTINE_STACK_END_VALUE

/// @def processCorrupted
///
/// @brief Determine whether or not a process has become corrupted.
#define processCorrupted(processDescriptor) \
  coroutineCorrupted((processDescriptor)->mainThread)

/// @def processStackOverflowed
///
/// @brief Determine whether or not a process has overflowed its stack.
#define processStackOverflowed(processDescriptor) \
  coroutineStackOverflowed((processDescriptor)->mainThread)

/// @def processRunning
///
/// @brief Function macro to determine whether or not a given process is
/// currently running.
#define processRunning(processDescriptor) \
  coroutineRunning((processDescriptor)->mainThread)

/// @def processFinished
///
/// @brief Function macro to determine whether or not a given process has
/// finished
#define processFinished(processDescriptor) \
  coroutineFinished((processDescriptor)->mainThread)

/// @def pid
///
/// @brief Function macro to get the numeric Pid given its descriptor.
#define processPid(processDescriptor) \
  (processDescriptor)->processId

/// @def processState
///
/// @brief Function macro to get the state of a process given its handle.
#define processState(processDescriptor) \
  coroutineState((processDescriptor)->mainThread)

/// @def processYield
///
/// @brief Call to yield the processor to another process.
#define processYield() \
  coroutineYield(NULL, COROUTINE_STATE_BLOCKED)

/// @def processYieldTo
///
/// @brief Call to yield the processor to a specific process.
#define processYieldTo(processDescriptor) \
  coroutineYieldTo((processDescriptor)->mainThread, \
    NULL, COROUTINE_STATE_BLOCKED)

/// @def processYieldValue
///
/// @brief Yield a value back to the scheduler.
#define processYieldValue(value) \
  coroutineYield(value, COROUTINE_STATE_BLOCKED)

/// @def processTerminate
///
/// @brief Function macro to terminate a running process.
#define processTerminate(processDescriptor, keepMessageQueue) \
  coroutineTerminate((processDescriptor)->mainThread, NULL, keepMessageQueue)

/// @def processGetNanoseconds
///
/// @brief Process-specific call to get the nanoseconds from midnight, Jan 1, 1970
/// given a pointer to a struct timespec.
#define processGetNanoseconds(ts) coroutineGetNanoseconds((ts))

/// @def processMessageInit
///
/// @brief Function macro to initialize a process message.
#define processMessageInit(processMessage, type, data, size, waiting) \
  msg_init(processMessage, MSG_CORO_SAFE, type, data, size, waiting)

/// @def processMessageSetDone
///
/// @brief Function macro to set a process message to the 'done' state.
#define processMessageSetDone(processMessage) \
  msg_set_done(processMessage)

/// @def processMessageRelease
///
/// @brief Function macro to release a process message.
#define processMessageRelease(processMessage) \
  msg_release(processMessage)

/// @def processMessageWaitForDone
///
/// @brief Function macro to wait for a process message to enter the 'done'
/// state.
#define processMessageWaitForDone(processMessage, ts) \
  msg_wait_for_done(processMessage, ts)

/// @def processMessageWaitForReplyWithType
///
/// @brief Function macro to wait on a reply to a message with a specified type.
#define processMessageWaitForReplyWithType(sent, releaseAfterDone, type, ts) \
  msg_wait_for_reply_with_type(sent, releaseAfterDone, type, ts)

/// @def processMessageQueueWaitForType
///
/// @brief Function macro to wait for a message of a specific type to be pushed
/// onto the running process's message queue.
#define processMessageQueueWaitForType(type, ts) \
  comessageQueueWaitForType(type, ts)

/// @def processMessageQueueWait
///
/// @brief Function macro to wait for a message to be pushed onto the running
/// process's message queue.
#define processMessageQueueWait(ts) \
  comessageQueueWait(ts)

/// @def processMessageQueuePush
///
/// @brief Function macro to push a process message on to a process's message
/// queue.
#define processMessageQueuePush(processDescriptor, message) \
  comessageQueuePush((processDescriptor)->mainThread, message)

/// @def processMessageQueuePop
///
/// @brief Function macro to pop a process message from the running process's
/// message queue.
#define processMessageQueuePop() \
  comessageQueuePop()

/// @def processResume
///
/// @brief Resume a process and update the currentProcess state correctly.
#define processResume(processDescriptor, processMessage) \
  coroutineResume((processDescriptor)->mainThread, processMessage); \

// Process message accessors
#define processMessageType(processMessagePointer) \
  msg_type(processMessagePointer)
#define processMessageData(processMessagePointer) \
  msg_data(processMessagePointer)
#define processMessageSize(processMessagePointer) \
  msg_size(processMessagePointer)
#define processMessageWaiting(processMessagePointer) \
  msg_waiting(processMessagePointer)
#define processMessageDone(processMessagePointer) \
  msg_done(processMessagePointer)
#define processMessageInUse(processMessagePointer) \
  msg_in_use(processMessagePointer)
#define processMessageFrom(processMessagePointer) \
  ((ProcessDescriptor*) coroutineContext(msg_from(processMessagePointer).coro))
#define processMessageTo(processMessagePointer) \
  ((ProcessDescriptor*) coroutineContext(msg_to(processMessagePointer).coro))
#define processMessageConfigured(processMessagePointer) \
  msg_configured(processMessagePointer)

typedef Coroutine Thread;
typedef CoroutinesConfigOptions ThreadsConfigOptions;

// Exported functionality
void* execCommand(void *args);
int sendProcessMessageToProcess(
  ProcessDescriptor *processDescriptor, ProcessMessage *processMessage);
int sendProcessMessageToPid(unsigned int pid, ProcessMessage *processMessage);
ProcessMessage* getAvailableMessage(void);
ProcessMessage* initSendProcessMessageToPid(int pid, int64_t type,
  void *data, size_t size, bool waiting);
void* waitForDataMessage(ProcessMessage *sent, int type, const struct timespec *ts);
ExecArgs* execArgsDestroy(ExecArgs *execArgs);
SpawnArgs* spawnArgsDestroy(SpawnArgs *spawnArgs);

#ifdef __cplusplus
}
#endif

#endif // KERNEL_PROCESSES_H

