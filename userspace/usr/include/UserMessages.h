///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.22.2026
///
/// @file              UserMessages.h
///
/// @brief             The kernel's message passing, for any process.  IPC
///                    capabilities decide what a process may send to whom.
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

#ifndef USER_MESSAGES_H
#define USER_MESSAGES_H

#include "NanoOsUser.h"
#include "../../../src/kernel/Coroutines.h"
#include "../../../src/kernel/Messages.h"
#include "../../../src/kernel/MessageArgsConstants.h"
#include "../../../src/kernel/NanoOsTypes.h"

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
  ((uint8_t) 0xff)

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
  return overlayMap.header.osApi->userApi->processMessageInit(
    processMessage, MSG_CORO_SAFE, type, data, size, waiting);
}

/// @fn static inline void* messageArgsCreate(size_t size, uint8_t ownership)
///
/// @brief Allocate zeroed storage for a message's arguments whose lifetime the
/// caller manages.
///
/// @param size The number of bytes to allocate.
/// @param ownership The MESSAGE_ARGS_* ownership of the storage.
///
/// @return Returns a pointer to the storage on success, NULL on failure.
static inline void* messageArgsCreate(size_t size, uint8_t ownership) {
  return overlayMap.header.osApi->userApi->messageArgsCreate(
    size, ownership);
}

/// @fn static inline void messageArgsDestroy(void *ptr)
///
/// @brief Free storage from messageArgsCreate.
///
/// @param ptr The storage, or NULL.
///
/// @return This function returns no value.
static inline void messageArgsDestroy(void *ptr) {
  overlayMap.header.osApi->userApi->messageArgsDestroy(ptr);
}

/// @def MESSAGE_ARGS_CREATE
///
/// @brief Allocate zeroed storage for a `type` whose lifetime the caller
/// manages, or NULL if there was none.
#define MESSAGE_ARGS_CREATE(type, ownership) \
  ((type*) messageArgsCreate(sizeof(type), (ownership)))

/// @def MESSAGE_ARGS_CREATE_BYTES
///
/// @brief Allocate `size` zeroed bytes whose lifetime the caller manages, or
/// NULL if there were none.
#define MESSAGE_ARGS_CREATE_BYTES(size, ownership) \
  messageArgsCreate((size), (ownership))

/// @def MESSAGE_ARGS_DESTROY
///
/// @brief Free storage from MESSAGE_ARGS_CREATE or MESSAGE_ARGS_CREATE_BYTES.
#define MESSAGE_ARGS_DESTROY(ptr) messageArgsDestroy(ptr)

/// @fn static inline bool messageArgsChecked(void)
///
/// @brief Determine whether the kernel backs message argument storage with
/// the checker's arena, which the scoped and sharing macros below go through
/// only when it does.
///
/// @return Returns true in the checked configuration, false otherwise.
static inline bool messageArgsChecked(void) {
  return overlayMap.header.osApi->userApi->messageArgsChecked;
}

/// @fn static inline void* messageArgsAllocate(size_t size)
///
/// @brief Allocate the checker's storage for a scoped, blocking, round-trip
/// message.
///
/// @param size The number of bytes to allocate.
///
/// @return Returns a pointer to zeroed storage on success, NULL on failure.
static inline void* messageArgsAllocate(size_t size) {
  return overlayMap.header.osApi->userApi->messageArgsAllocate(size);
}

/// @fn static inline void messageArgsFree(void *ptr)
///
/// @brief Free storage from messageArgsAllocate.
///
/// @param ptr The storage.
///
/// @return This function returns no value.
static inline void messageArgsFree(void *ptr) {
  overlayMap.header.osApi->userApi->messageArgsFree(ptr);
}

/// @fn static inline void* messageBufferShare(void *ptr, size_t size,
///   uint8_t direction, ProcessId recipient)
///
/// @brief Share a buffer with the process a message carrying it is sent to.
///
/// @param ptr The buffer.
/// @param size The number of bytes in the buffer.
/// @param direction The MESSAGE_BUFFER_* direction of the buffer's data.
/// @param recipient The ProcessId of the process the buffer is shared with.
///
/// @return Returns the pointer to put in the argument structure, or NULL if
/// the buffer couldn't be shared.
static inline void* messageBufferShare(void *ptr, size_t size,
  uint8_t direction, ProcessId recipient
) {
  return overlayMap.header.osApi->userApi->messageBufferShare(
    ptr, size, direction, recipient);
}

/// @fn static inline void messageBufferUnshare(void *shared, void *original,
///   uint8_t direction)
///
/// @brief End the sharing of a buffer once the exchange is done.
///
/// @param shared What messageBufferShare returned.
/// @param original The buffer that was shared.
/// @param direction The MESSAGE_BUFFER_* direction it was shared with.
///
/// @return This function returns no value.
static inline void messageBufferUnshare(void *shared, void *original,
  uint8_t direction
) {
  overlayMap.header.osApi->userApi->messageBufferUnshare(
    shared, original, direction);
}

/// @def MESSAGE_ARGS_ALLOCATE
///
/// @brief Declare `type *name` pointing at zeroed storage for a scoped,
/// blocking, round-trip message.  `name` is NULL if there was no storage.
#define MESSAGE_ARGS_ALLOCATE(type, name) \
  type name##Storage = {0}; \
  type *name = messageArgsChecked() \
    ? (type*) messageArgsAllocate(sizeof(type)) : &name##Storage

/// @def MESSAGE_ARGS_FREE
///
/// @brief Free storage from MESSAGE_ARGS_ALLOCATE.
#define MESSAGE_ARGS_FREE(name) \
  do { \
    if (messageArgsChecked()) { \
      messageArgsFree(name); \
    } \
  } while (0)

/// @def MESSAGE_BUFFER_SHARE
///
/// @brief Yield the pointer to put in an argument structure for a buffer of
/// `size` bytes at `ptr` shared with process `recipient`, or NULL if it
/// couldn't be shared.  Outside the checked configuration, `size` and
/// `recipient` are not evaluated.
#define MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient) \
  (messageArgsChecked() \
    ? messageBufferShare((void*) (ptr), (size), (direction), (recipient)) \
    : (void*) (ptr))

/// @def MESSAGE_BUFFER_UNSHARE
///
/// @brief End the sharing of `original` through `shared` once the exchange is
/// done.
#define MESSAGE_BUFFER_UNSHARE(shared, original, direction) \
  do { \
    if (messageArgsChecked()) { \
      messageBufferUnshare((void*) (shared), (void*) (original), \
        (direction)); \
    } \
  } while (0)

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
  return overlayMap.header.osApi->userApi->processMessageSetDone(
    processMessage);
}

/// @fn static inline int processMessageRelease(ProcessMessage *processMessage)
///
/// @brief Release but do not deallocate a process message.
///
/// @param processMessage A pointer to the ProcessMessage to release.
///
/// @return Returns processSuccess on success, process error code on failure.
static inline int processMessageRelease(ProcessMessage *processMessage) {
  return overlayMap.header.osApi->userApi->processMessageRelease(
    processMessage);
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
  return overlayMap.header.osApi->userApi->processMessageWaitForDone(
    processMessage, ts);
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
  return overlayMap.header.osApi->userApi->comessageQueueWaitForType(
    type, ts);
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
  return overlayMap.header.osApi->userApi->comessageQueueWait(ts);
}

/// @fn static inline ProcessMessage* getAvailableMessage(void)
///
/// @brief Get a ProcessMessage that isn't in use and mark it in use.
///
/// @return Returns a pointer to the ProcessMessage on success, NULL if none is
/// available.
static inline ProcessMessage* getAvailableMessage(void) {
  return overlayMap.header.osApi->userApi->getAvailableMessage();
}

/// @fn static inline int sendProcessMessageToPid(
///   unsigned int pid, ProcessMessage *message)
///
/// @brief Send a process message to another process, if the running process
/// has the IPC capability to send it that message.
///
/// @param pid The ID of the process to send the message to.
/// @param message A pointer to the initialized ProcessMessage to send.
///
/// @return Returns processSuccess on success, processError on failure.
static inline int sendProcessMessageToPid(
  unsigned int pid, ProcessMessage *message
) {
  return overlayMap.header.osApi->userApi->sendProcessMessageToPid(
    pid, message);
}

/// @fn static inline int findSystemProcess(
///   const char *name, ProcessId *returnValue)
///
/// @brief Look up a system process's PID by the name it was registered under,
/// from a process at any level.  Executive code has findProcessByName.
///
/// @param name The name to search for.
/// @param returnValue A pointer to a ProcessId that will hold the result on
///   success.
///
/// @return Returns 0 on success, -errno on failure.
static inline int findSystemProcess(const char *name, ProcessId *returnValue) {
  return overlayMap.header.osApi->userApi->findProcessByName(
    name, returnValue);
}

/// @fn static inline ProcessMessage* processMessageQueuePop(void)
///
/// @brief Pop a process message from the running process's message queue.
///
/// @return Returns a pointer to a popped ProcessMessage on success, NULL on
/// failure.
static inline ProcessMessage* processMessageQueuePop(void) {
  return overlayMap.header.osApi->userApi->comessageQueuePop();
}

/// @fn static inline ProcessMessage* processMessageQueuePeek(void)
///
/// @brief Get the message at the head of the running process's message
/// queue without taking it off.
///
/// @return Returns a pointer to the ProcessMessage, or NULL if the queue is
/// empty.
static inline ProcessMessage* processMessageQueuePeek(void) {
  return overlayMap.header.osApi->userApi->comessageQueuePeek();
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
  return overlayMap.header.osApi->userApi->processMessageElement(
    processMessage, element);
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

#ifdef __cplusplus
}
#endif

#endif // USER_MESSAGES_H
