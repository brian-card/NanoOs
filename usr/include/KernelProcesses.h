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
  return overlayMap.header.osApi->getRunningCoroutineContext();
}

/// @fn static inline ProcessId getRunningPid(void)
///
/// @brief Get the process ID for the currently-running process.
///
/// @return Returns the ProcessId of the process currently executing.
static inline ProcessId getRunningPid(void) {
  return getRunningProcess()->processId;
}

/// @fn static inline UserId getRunningUid(void)
///
/// @brief Get the user ID for the currently-running process.
///
/// @return Returns the ID of the user running the process currently executing.
static inline UserId getRunningUid(void) {
  return getRunningProcess()->userId;
}

/// @fn static inline int processCreate(ProcessDescriptor *processDescriptor,
///   ThreadFunction *func, void *arg)
///
/// @brief Create a new process.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to populate with
///   the new process.
/// @param func The ThreadFunction to use as the main entrypoint for the main
///   thread of the process.
/// @param arg Any argument that is to be passed to the new process, cast to a
///   void*.
///
/// @return Returns processSuccess on success, error code on failure.
static inline int processCreate(ProcessDescriptor *processDescriptor,
  ThreadFunction *func, void *arg
) {
  return overlayMap.header.osApi->coroutineCreate(
    ((processDescriptor != NULL)
      ? &(((ProcessDescriptor*) processDescriptor))->mainThread
      : NULL
    ),
    func,
    arg);
}

/// @fn static inline Thread* threadProvision(Thread *thread,
///   ThreadFunction func, void *arg)
///
/// @brief Provision a Thread for execution.
///
/// @param thread A pointer to a Thread that has previously been provisioned.
///   This parameter may be NULL to provision a new one.
/// @param func The ThreadFunction to use as the main entrypoint for the thread.
/// @param arg Any argument that is to be passed to the new process, cast to a
///   void*.
///
/// @return Returns a pointer to the newly-provisioned Thread on success, NULL
///   on failure.
static inline Thread* threadProvision(Thread *thread,
  ThreadFunction func, void *arg
) {
  return overlayMap.header.osApi->coroutineInit(thread, func, arg);
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
  return overlayMap.header.osApi->coroutineSetContext(thread, context);
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
  return overlayMap.header.osApi->coroutineContext(thread);
}

/// @fn int static inline threadsConfig(Thread *first,
///   ThreadsConfigOptions *options)
///
/// @brief Configure the threads library.
///
/// @param first A pointer to an allocated Thread.
/// @param options A pointer to a ThreadsConfigOptions structure with the
///   optional parameters to use for configuring threads.
///
/// @return Returns processSuccess on success, error code on failure.
static inline int threadsConfig(Thread *first, ThreadsConfigOptions *options) {
  return overlayMap.header.osApi->coroutinesConfig(first, options);
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
  return overlayMap.header.osApi->coroutineStackEnd(thread);
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
  return overlayMap.header.osApi->coroutineSetStackEnd(thread, stackEnd);
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
/// @return This function always succeeds and always returns processSuccess.
static inline int processResetStack(ProcessDescriptor *processDescriptor) {
  *threadStackEnd(processDescriptor->mainThread) = THREAD_STACK_END_VALUE;
  return processSuccess;
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
  return overlayMap.header.osApi->coroutineStackOverflowed(
    processDescriptor->mainThread);
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
/// @brief Call to yield the processor to another process.
///
/// @return Returns the value that the parent process calls processResume with.
static inline void* processYield(void) {
  return overlayMap.header.osApi->coroutineYield(NULL, COROUTINE_STATE_BLOCKED);
}

/// @fn static inline void* processYieldTo(ProcessDescriptor *processDescriptor)
///
/// @brief Call to yield the processor to a specific process.
///
/// @param processDescriptor A pointer to the ProcessDescriptor to yield
///   execution to.  (i.e. The next process that should run.)
///
/// @return Returns the value that the parent process calls processResume with.
static inline void* processYieldTo(ProcessDescriptor *processDescriptor) {
  return overlayMap.header.osApi->coroutineYieldTo(
    processDescriptor->mainThread, NULL, COROUTINE_STATE_BLOCKED);
}

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

