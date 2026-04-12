///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              12.26.2024
///
/// @file              NanoOsTypes.h
///
/// @brief             Types used across NanoOs.
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

#ifndef NANO_OS_TYPES_H
#define NANO_OS_TYPES_H

// Custom includes
#include "BlockStorage.h"
#include "Coroutines.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Constants

/// @def NANO_OS_NUM_TASKS
///
/// @brief The total number of concurrent tasks that can be run by the OS,
/// including the scheduler.
///
/// @note If this value is increased beyond 15, the number of bits used to store
/// the owner in a MemNode in MemoryManager.cpp must be extended and the value
/// of TASK_ID_NOT_SET must be changed in Tasks.h.  If this value is
/// increased beyond 255, then the type defined by TaskId below m ust also
/// be extended.
#define NANO_OS_NUM_TASKS                             9

/// @def SCHEDULER_NUM_TASKS
///
/// @brief The number of tasks managed by the scheduler.  This is one fewer
/// than the total number of tasks managed by NanoOs since the scheduler is
/// a task.
#define SCHEDULER_NUM_TASKS (NANO_OS_NUM_TASKS - 1)

/// @def SCHEDULER_NUM_READY_QUEUES
///
/// @brief This is the total number of ready queues managed by the scheduler
#define SCHEDULER_NUM_READY_QUEUES 2

/// @def CONSOLE_BUFFER_SIZE
///
/// @brief The size, in bytes, of a single console buffer.  This is the number
/// of bytes that printf calls will have to work with.
#define CONSOLE_BUFFER_SIZE 96

/// @def CONSOLE_NUM_PORTS
///
/// @brief The number of console supports supported.
#define CONSOLE_NUM_PORTS 2

/// @def CONSOLE_NUM_BUFFERS
///
/// @brief The number of console buffers that will be allocated within the main
/// console task's stack.
#define CONSOLE_NUM_BUFFERS CONSOLE_NUM_PORTS

// Primitive types

/// @enum SchedulerReadyQueueType
///
/// @brief Designations for each of the ready queues.
typedef enum SchedulerReadyQueueType {
  SCHEDULER_READY_QUEUE_KERNEL,
  SCHEDULER_READY_QUEUE_USER,
  NUM_SCHEDULER_READY_QUEUE_TYPES,
} SchedulerReadyQueueType;

/// @typedef Task
///
/// @brief Definition of the Task object used by the OS.
typedef Coroutine* TaskHandle;

/// @typedef TaskId
///
/// @brief Definition of the type to use for a task ID.
typedef uint8_t TaskId;

/// @typedef TaskMessage
///
/// @brief Definition of the TaskMessage object that tasks will use for
/// inter-task communication.
typedef msg_t TaskMessage;

/// @typedef CommandFunction
///
/// @brief Type definition for the function signature that NanoOs commands must
/// have.
typedef int (*CommandFunction)(int argc, char **argv);

/// @typedef UserId
///
/// @brief The type to use to represent a numeric user ID.
typedef int16_t UserId;

/// @typedef NanoOsMessageData
///
/// @brief Data type used in a NanoOsMessage.
typedef unsigned long long int NanoOsMessageData;

/// @typedef ssize_t
///
/// @brief Signed, register-width integer.
typedef intptr_t ssize_t;

// Composite types

/// @struct NanoOsFile
///
/// @brief Definition of the FILE structure used internally to NanoOs.
///
/// @param file Pointer to the real file metadata.
/// @param next A pointer to the next open NanoOsFile.
/// @param prev A pointer to the previous open NanoOsFile.
/// @param currentPosition The current position within the file.
/// @param fd The numeric file descriptor for the file.
/// @param owner The TaskId of the process that owns the file.
typedef struct NanoOsFile {
  void              *file;
  struct NanoOsFile *next;
  struct NanoOsFile *prev;
  uint32_t           currentPosition;
  int                fd;
  TaskId             owner;
} NanoOsFile;

/// @struct IoChannel
///
/// @brief Information that can be used to direct the output of one task
/// into the input of another one.
///
/// @param taskId The task ID (PID) of the destination task.
/// @param messageType The type of message to send to the task.
typedef struct IoChannel {
  TaskId taskId;
  uint8_t messageType;
} IoChannel;

/// @struct FileDescriptor
///
/// @brief Definition of a file descriptor that a task can use for input
/// and/or output.
///
/// @param inputChannel An IoChannel object that describes where the file
///   descriptor gets its input, if any.
/// @param outputChannel An IoChannel object that describes where the file
///   descriptor sends its output, if any.
/// @param pipeEnd Pointer to the FileDescriptor at the other end of a pipe if
///   this FileDescriptor is part of a pipe.
typedef struct FileDescriptor {
  IoChannel inputChannel;
  IoChannel outputChannel;
  struct FileDescriptor *pipeEnd;
} FileDescriptor;

// Forward declaration.  Definition below.
typedef struct TaskQueue TaskQueue;

/// @struct TaskDescriptor
///
/// @brief Descriptor for a running task.
///
/// @param name The name of the command as stored in its CommandEntry or as
///   set by the scheduler at launch.
/// @param task A TaskHandle that manages the running command's execution
///   state.
/// @param 
/// @param userId The numerical ID of the user that is running the task.
/// @param numFileDescriptors The number of FileDescriptor objects contained by
///   the fileDescriptors array.
/// @param fileDescriptors Pointer to an array of FileDescriptor pointers that
///   are currently in use by the task.
/// @param overlayDir The base path to the overlays for the task, if any.
/// @param overlay The FileBlockMetadata of how to access the overlay from its
///   block device.
/// @param envp A pointer to the array of NULL-terminated environment variable
///   strings.
/// @param taskQueue The task queue that the descriptor is currently in.  This
///   will be NULL if the task is currently running (in no queue).
/// @param readyQueue The ready queue that the descriptor is to be assigned to
///   when the task transitions to ready.
typedef struct TaskDescriptor {
  const char         *name;
  TaskHandle          taskHandle;
  TaskId              taskId;
  UserId              userId;
  uint8_t             numFileDescriptors;
  FileDescriptor    **fileDescriptors;
  char               *overlayDir;
  FileBlockMetadata   overlay;
  char              **envp;
  TaskQueue          *taskQueue;
  TaskQueue          *readyQueue;
} TaskDescriptor;

/// @struct TaskInfoElement
///
/// @brief Information about a running task that is exportable to a user
/// task.
///
/// @param pid The numerical ID of the task.
/// @param name The name of the task.
/// @param userId The UserId of the user that owns the task.
typedef struct TaskInfoElement {
  int pid;
  const char *name;
  UserId userId;
} TaskInfoElement;

/// @struct TaskInfo
///
/// @brief The object that's populated and returned by a getTaskInfo call.
///
/// @param numTasks The number of elements in the tasks array.
/// @param tasks The array of TaskInfoElements that describe the
///   tasks.
typedef struct TaskInfo {
  uint8_t numTasks;
  TaskInfoElement tasks[1];
} TaskInfo;

/// @struct TaskQueue
///
/// @brief Structure to manage an individual task queue
///
/// @param name The string name of the queue for use in error messages.
/// @param tasks The array of pointers to TaskDescriptors from the
///   allTasks array.
/// @param head The index of the head of the queue.
/// @param tail The index of the tail of the queue.
/// @param numElements The number of elements currently in the queue.
typedef struct TaskQueue {
  const char *name;
  TaskDescriptor *tasks[SCHEDULER_NUM_TASKS];
  uint8_t head:4;
  uint8_t tail:4;
  uint8_t numElements:4;
} TaskQueue;

/// @struct SchedulerState
///
/// @brief State data used by the scheduler.
///
/// @param allTasks Array that will hold the metadata for every task,
///   including the scheduler.
/// @param ready Queue of tasks that are allocated and not waiting on
///   anything but not currently running.  This queue never includes the
///   scheduler task.
/// @param waiting Queue of tasks that are waiting on a mutex or condition
///   with an infinite timeout.  This queue never includes the scheduler
///   task.
/// @param timedWaiting Queue of tasks that are waiting on a mutex or
///   condition with a defined timeout.  This queue never includes the scheduler
///   task.
/// @param free Queue of tasks that are free within the allTasks
///   array.
/// @param hostname The contents of the /etc/hostname file read at startup.
/// @param numShells The number of shell tasks that the scheduler is
///   running.
/// @param preemptionTimer The index of the timer used for preemptive
///   multitasking.  If this is < 0 then the tasks run in cooperative mode.
/// @param schedulerTaskId The TaskId of the scheduler.
/// @param consoleTaskId The TaskId of the console.
/// @param memoryManagerTaskId The TaskId of the memory manager.
/// @param rootFsTaskId The TaskId of the root filesystem.
/// @param firstUserTaskId The TaskId of the first user task.
/// @param firstShellTaskId The TaskId of the first shell task.
/// @param runScheduler Function pointer to the runScheduler function in the
///   Scheduler library.
typedef struct SchedulerState {
  TaskDescriptor allTasks[NANO_OS_NUM_TASKS];
  TaskQueue ready[SCHEDULER_NUM_READY_QUEUES];
  TaskQueue *currentReady;
  TaskQueue waiting;
  TaskQueue timedWaiting;
  TaskQueue free;
  char *hostname;
  uint8_t numShells;
  int preemptionTimer;
  TaskId schedulerTaskId;
  TaskId consoleTaskId;
  TaskId memoryManagerTaskId;
  TaskId rootFsTaskId;
  TaskId firstUserTaskId;
  TaskId firstShellTaskId;
  void (*runScheduler)(void);
} SchedulerState;

/// @struct CommandDescriptor
///
/// @brief Container of information for launching a task.
///
/// @param consolePort The index of the ConsolePort the input came from.
/// @param consoleInput The input as provided by the console.
/// @param callingTask The task ID of the task that is launching the
///   command.
/// @param schedulerState A pointer to the SchedulerState structure maintained
///   by the scheduler.
typedef struct CommandDescriptor {
  int                consolePort;
  char              *consoleInput;
  TaskId             callingTask;
  SchedulerState    *schedulerState;
} CommandDescriptor;

/// @struct CommandEntry
///
/// @brief Descriptor for a command that can be looked up and run by the
/// handleCommand function.
///
/// @param name The textual name of the command.
/// @param func A function pointer to the task that will be spawned to
///   execute the command.
/// @param help A one-line summary of what this command does.
typedef struct CommandEntry {
  const char      *name;
  CommandFunction  func;
  const char      *help;
} CommandEntry;

/// @struct ConsoleBuffer
///
/// @brief Definition of a single console buffer that may be returned to a
/// sender of a CONSOLE_GET_BUFFER command via a CONSOLE_RETURNING_BUFFER
/// response.
///
/// @param inUse Whether or not this buffer is in use by a task.  Set by the
///   consoleGetBuffer function when getting a buffer for a caller and cleared
///   by the caller when no longer being used.
/// @param buffer The array of CONSOLE_BUFFER_SIZE characters that the calling
///   task can use.
typedef struct ConsoleBuffer {
  bool inUse;
  char buffer[CONSOLE_BUFFER_SIZE];
} ConsoleBuffer;

/// @struct ConsolePort
///
/// @brief Descriptor for a single console port that can be used for input from
/// a user.
///
/// @var portId The numerical ID for the port.
/// @param consoleBuffer A pointer to the ConsoleBuffer used to buffer input
///   from the user.
/// @param consoleBufferIndex An index into the buffer provided by consoleBuffer
///   of the next position to read a byte into.
/// @param outputOwner The ID of the task that currently has the ability to
///   write output to the port.
/// @param inputOwner The ID of the task that currently has the ability to
///   read input from the port.
/// @param shell The ID of the task that serves as the console port's shell.
/// @param waitingForInput Whether or not the owning task is currently
///   waiting for input from the user.
/// @param readByte A pointer to the non-blocking function that will attempt to
///   read a byte of input from the user.
/// @param echo Whether or not the data read from the port should be echoed back
///   to the port.
/// @param printString A pointer to the function that will print a string of
///   output to the console port.
typedef struct ConsolePort {
  unsigned char       portId;
  ConsoleBuffer      *consoleBuffer;
  unsigned char       consoleBufferIndex;
  TaskId              outputOwner;
  TaskId              inputOwner;
  TaskId              shell;
  bool                waitingForInput;
  int               (*readByte)(struct ConsolePort *consolePort);
  bool                echo;
  int               (*consolePrintString)(unsigned char port,
                        const char *string);
} ConsolePort;

/// @struct ConsoleState
///
/// @brief State maintained by the main console task and passed to the inter-
/// task command handlers.
///
/// @param consolePorts The array of ConsolePorts that will be polled for input
///   from the user.
/// @param consoleBuffers The array of ConsoleBuffers that can be used by
///   the console ports for input and by tasks for output.
/// @param numConsolePorts The number of active console ports.
typedef struct ConsoleState {
  ConsolePort consolePorts[CONSOLE_NUM_PORTS];
  // consoleBuffers needs to come at the end.
  ConsoleBuffer consoleBuffers[CONSOLE_NUM_BUFFERS];
  int numConsolePorts;
} ConsoleState;

/// @struct ConsolePortPidAssociation
///
/// @brief Structure to associate a console port with a task ID.  This
/// information is used in a CONSOLE_ASSIGN_PORT command.
///
/// @param consolePort The index into the consolePorts array of a ConsoleState
///   object.
/// @param taskId The task ID associated with the port.
typedef struct ConsolePortPidAssociation {
  uint8_t        consolePort;
  TaskId         taskId;
} ConsolePortPidAssociation;

/// @union ConsolePortPidUnion
///
/// @brief Union of a ConsolePortPidAssociation and a NanoOsMessageData to
/// allow for easy conversion between the two.
///
/// @param consolePortPidAssociation The ConsolePortPidAssociation part.
/// @param nanoOsMessageData The NanoOsMessageData part.
typedef union ConsolePortPidUnion {
  ConsolePortPidAssociation consolePortPidAssociation;
  NanoOsMessageData         nanoOsMessageData;
} ConsolePortPidUnion;

/// @struct ReallocMessage
///
/// @brief Structure that holds the data needed to make a request to reallocate
/// an existing pointer.
///
/// @param ptr The pointer to be reallocated.  If this value is NULL, new
///   memory will be allocated.
/// @param size The number of bytes to allocate.  If this value is 0, the memory
///   at ptr will be freed.
/// @param responseType The response type the caller is waiting for.
typedef struct ReallocMessage {
  void *ptr;
  size_t size;
  int responseType;
} ReallocMessage;

/// @struct MemNode
///
/// @brief Metadata for a single block of memory managed by the memory manager.
///
/// @param next Pointer to the next block of memory in the list.
/// @param prev Pointer to the previous block of memory in the list.
/// @param size The number of bytes allocated at this pointer.
/// @param owner The TaskId of the owner of this block of memory.
typedef struct MemNode {
  struct MemNode *next;
  struct MemNode *prev;
  size_t          size;
  TaskId          owner;
} MemNode;

/// @struct MemoryManagerState
///
/// @brief State metadata the memory manager task uses for allocations and
/// deallocations.
///
/// @param start Address of the first byte of memory managed by the memory
///   manager.
/// @param end Address of the last byte of memory managed by the memory
///   manager.
/// @param bytesFree The total number of bytes available to allocate.
/// @param firstFree Pointer to the MemNode that represents the first free
///   block of memory managed by the memory manager.
/// @param lastFree Pointer to the MemNode that represents the last free block
///   of memory managed by the memory manager.  This block will always hold all
///   of the remaining non-fragmented memory.
/// @param allocated Pointer to the MemNode that represents the first allocated
///   block of memory managed by the memory manager.
typedef struct MemoryManagerState {
  uintptr_t start;
  uintptr_t end;
  size_t    bytesFree;
  MemNode  *firstFree;
  MemNode  *lastFree;
  MemNode  *allocated;
} MemoryManagerState;

/// @struct User
///
/// @param userId The numeric ID for the user.
/// @param username The literal name of the user.
/// @param checksum The checksum of the username and password.
typedef struct User {
  UserId        userId;
  const char   *username;
  unsigned int  checksum;
} User;

/// @struct NanoOsMessage
///
/// @brief A generic message that can be exchanged between tasks.
///
/// @param func Information about the function to run, cast to an unsigned long
///   long int.
/// @param data Information about the data to use, cast to an unsigned long
///   long int.
typedef struct NanoOsMessage {
  NanoOsMessageData  func;
  NanoOsMessageData  data;
} NanoOsMessage;

/// @struct posix_spawn_file_actions_t
///
/// @brief Description of file-related operations that need to happen during a
/// posix_spawn call.
///
/// @param dup2 Array of file descriptors that need to be mapped from one to the
///   other before the process is started.
typedef struct posix_spawn_file_actions_t {
  int dup2[2][2];
} posix_spawn_file_actions_t;

// POSIX-mandated object required for posix_spawn
typedef struct posix_spawnattr_t posix_spawnattr_t;

/// @struct SpawnArgs
///
/// @brief Arguments for the standard POSIX posix_spawn call.
///
/// @param newPid A pointer to the pid_t that will hold the process ID of the
///   new process.
/// @param path The full path to the to the program to execute on disk.
/// @param fileActions A pointer to the posix_spawn_file_actions_t that
///   specifies the operations to do on the file descriptors of the new process.
///   Initialized and populated with posix_spawn_file_actions_init and
///   posix_spawn_file_actions_* functions before calling the spawn.
/// @param attrp A pointer to the posix_spawnattr_t that specifies various
///   attributes of the created child process.  Initialized and populated with
///   posix_spawnattr_init and posix_spawnattr_* functoions before callin the
///   spawn.
/// @param argv The NULL-terminated array of arguments for the command.  argv[0]
///   must be valid and should be the name of the program.
/// @param envp The NULL-terminated array of environment variables in
///   "name=value" format.  This array may be NULL.
typedef struct SpawnArgs {
  // Change this type if we change the size of pid_t or TaskId!!!
  uint8_t *newPid;
  char *path;
  posix_spawn_file_actions_t *fileActions;
  posix_spawnattr_t *attrp;
  char **argv;
  char **envp;
} SpawnArgs;

/// @struct ExecArgs
///
/// @brief Arguments for the standard POSIX execve call.
///
/// @param callingTaskId The task ID of the process that is execing.
/// @param pathname The full, absolute path on disk to the program to run.
/// @param argv The NULL-terminated array of arguments for the command.  argv[0]
///   must be valid and should be the name of the program.
/// @param envp The NULL-terminated array of environment variables in
///   "name=value" format.  This array may be NULL.
/// @param schedulerState A pointer to the SchedulerState managed by the
///   scheduler.  This is needed by the execCommand function.
/// @param fileActions A pointer to the posix_spawn_file_actions_t that
///   specifies the operations to do on the file descriptors of the new process.
///   This field may be NULL.
typedef struct ExecArgs {
  TaskId callingTaskId;
  char *pathname;
  char **argv;
  char **envp;
  SchedulerState *schedulerState;
  posix_spawn_file_actions_t *fileActions;
} ExecArgs;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NANO_OS_TYPES_H
