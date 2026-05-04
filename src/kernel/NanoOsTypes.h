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

/// @def NANO_OS_NUM_PROCESSES
///
/// @brief The total number of concurrent processes that can be run by the OS,
/// including the scheduler.
///
/// @note If this value is increased beyond 15, the number of bits used to store
/// the owner in a MemNode in MemoryManager.cpp must be extended and the value
/// of PROCESS_ID_NOT_SET must be changed in Processes.h.  If this value is
/// increased beyond 255, then the type defined by ProcessId below must also
/// be extended.
#define NANO_OS_NUM_PROCESSES                             9

/// @def SCHEDULER_NUM_PROCESSES
///
/// @brief The number of processes managed by the scheduler.  This is one fewer
/// than the total number of processes managed by NanoOs since the scheduler is
/// a process.
#define SCHEDULER_NUM_PROCESSES (NANO_OS_NUM_PROCESSES - 1)

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
/// console process's stack.
#define CONSOLE_NUM_BUFFERS 4

// Primitive types

/// @enum SchedulerReadyQueueType
///
/// @brief Designations for each of the ready queues.
typedef enum SchedulerReadyQueueType {
  SCHEDULER_READY_QUEUE_KERNEL,
  SCHEDULER_READY_QUEUE_USER,
  NUM_SCHEDULER_READY_QUEUE_TYPES,
} SchedulerReadyQueueType;

/// @typedef Process
///
/// @brief Definition of the Process object used by the OS.
typedef Coroutine Thread;

/// @typedef ProcessId
///
/// @brief Definition of the type to use for a process ID.
typedef uint8_t ProcessId;

/// @typedef ProcessMessage
///
/// @brief Definition of the ProcessMessage object that processes will use for
/// inter-process communication.
typedef msg_t ProcessMessage;

/// @typedef ProcessMessageQueue
///
/// @brief Type to use for inter-process message queues.
typedef msg_q_t ProcessMessageQueue;

/// @typedef CommandFunction
///
/// @brief Type definition for the function signature that NanoOs commands must
/// have.
typedef int (*CommandFunction)(int argc, char **argv);

/// @typedef UserId
///
/// @brief The type to use to represent a numeric user ID.
typedef int16_t UserId;

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
/// @param owner The ProcessId of the process that owns the file.
typedef struct NanoOsFile {
  void              *file;
  struct NanoOsFile *next;
  struct NanoOsFile *prev;
  uint32_t           currentPosition;
  int                fd;
  ProcessId             owner;
} NanoOsFile;

/// @struct IoChannel
///
/// @brief Information that can be used to direct the output of one process
/// into the input of another one.
///
/// @param pid The process ID (PID) of the destination process.
/// @param messageType The type of message to send to the process.
typedef struct IoChannel {
  ProcessId pid;
  uint8_t messageType;
} IoChannel;

/// @struct FileDescriptor
///
/// @brief Definition of a file descriptor that a process can use for input
/// and/or output.
///
/// @param inputChannel An IoChannel object that describes where the file
///   descriptor gets its input, if any.
/// @param outputChannel An IoChannel object that describes where the file
///   descriptor sends its output, if any.
/// @param pipeEnd Pointer to the FileDescriptor at the other end of a pipe if
///   this FileDescriptor is part of a pipe.
/// @param refCount The number of references to this FileDescriptor.
typedef struct FileDescriptor {
  IoChannel inputChannel;
  IoChannel outputChannel;
  struct FileDescriptor *pipeEnd;
  int refCount;
} FileDescriptor;

// Forward declaration.  Definition below.
typedef struct ProcessQueue ProcessQueue;

/// @struct ProcessDescriptor
///
/// @brief Descriptor for a running process.
///
/// @param name The name of the command as stored in its CommandEntry or as
///   set by the scheduler at launch.
/// @param mainThread A pointer to a Thread that manages the running command's
///   execution state.
/// @param pid The numerical ID of the process.
/// @param userId The numerical ID of the user that is running the process.
/// @param numFileDescriptors The number of FileDescriptor objects contained by
///   the fileDescriptors array.
/// @param fileDescriptors Pointer to an array of FileDescriptor pointers that
///   are currently in use by the process.
/// @param overlayDir The base path to the overlays for the process, if any.
/// @param overlay The FileBlockMetadata of how to access the overlay from its
///   block device.
/// @param envp A pointer to the array of NULL-terminated environment variable
///   strings.
/// @param processQueue The process queue that the descriptor is currently in.
///   This will be NULL if the process is currently running (in no queue).
/// @param readyQueue The ready queue that the descriptor is to be assigned to
///   when the process transitions to ready.
/// @param message The default, statically-allocated message for the process to use
///   to send to other processes.
typedef struct ProcessDescriptor {
  const char         *name;
  Thread             *mainThread;
  ProcessId           pid;
  UserId              userId;
  uint8_t             numFileDescriptors;
  FileDescriptor    **fileDescriptors;
  char               *overlayDir;
  FileBlockMetadata   overlay;
  char              **envp;
  ProcessQueue       *processQueue;
  ProcessQueue       *readyQueue;
  ProcessMessage      message;
} ProcessDescriptor;

/// @struct ProcessInfoElement
///
/// @brief Information about a running process that is exportable to a user
/// process.
///
/// @param pid The numerical ID of the process.
/// @param name The name of the process.
/// @param userId The UserId of the user that owns the process.
typedef struct ProcessInfoElement {
  int pid;
  const char *name;
  UserId userId;
} ProcessInfoElement;

/// @struct ProcessInfo
///
/// @brief The object that's populated and returned by a getProcessInfo call.
///
/// @param numProcesses The number of elements in the processes array.
/// @param processes The array of ProcessInfoElements that describe the
///   processes.
typedef struct ProcessInfo {
  uint8_t numProcesses;
  ProcessInfoElement processes[1];
} ProcessInfo;

/// @struct ProcessQueue
///
/// @brief Structure to manage an individual process queue
///
/// @param name The string name of the queue for use in error messages.
/// @param processes The array of pointers to ProcessDescriptors from the
///   allProcesses array.
/// @param head The index of the head of the queue.
/// @param tail The index of the tail of the queue.
/// @param numElements The number of elements currently in the queue.
typedef struct ProcessQueue {
  const char *name;
  ProcessDescriptor *processes[SCHEDULER_NUM_PROCESSES];
  uint8_t head:4;
  uint8_t tail:4;
  uint8_t numElements:4;
} ProcessQueue;

/// @struct SchedulerState
///
/// @brief State data used by the scheduler.
///
/// @param allProcesses Array that will hold the metadata for every process,
///   including the scheduler.
/// @param ready Queue of processes that are allocated and not waiting on
///   anything but not currently running.  This queue never includes the
///   scheduler process.
/// @param waiting Queue of processes that are waiting on a mutex or condition
///   with an infinite timeout.  This queue never includes the scheduler
///   process.
/// @param timedWaiting Queue of processes that are waiting on a mutex or
///   condition with a defined timeout.  This queue never includes the scheduler
///   process.
/// @param free Queue of processes that are free within the allProcesses
///   array.
/// @param hostname The contents of the /etc/hostname file read at startup.
/// @param numShells The number of shell processes that the scheduler is
///   running.
/// @param preemptionTimer The index of the timer used for preemptive
///   multiprocessing.  If this is < 0 then the processes run in cooperative mode.
/// @param schedulerProcessId The ProcessId of the scheduler.
/// @param consoleProcessId The ProcessId of the console.
/// @param memoryManagerProcessId The ProcessId of the memory manager.
/// @param rootFsProcessId The ProcessId of the root filesystem.
/// @param firstUserProcessId The ProcessId of the first user process.
/// @param firstShellProcessId The ProcessId of the first shell process.
/// @param runScheduler Function pointer to the runScheduler function in the
///   Scheduler library.
typedef struct SchedulerState {
  ProcessDescriptor allProcesses[NANO_OS_NUM_PROCESSES];
  ProcessQueue ready[SCHEDULER_NUM_READY_QUEUES];
  ProcessQueue *currentReady;
  ProcessQueue waiting;
  ProcessQueue timedWaiting;
  ProcessQueue free;
  char *hostname;
  uint8_t numShells;
  int preemptionTimer;
  ProcessId schedulerProcessId;
  ProcessId consoleProcessId;
  ProcessId memoryManagerProcessId;
  ProcessId rootFsProcessId;
  ProcessId firstUserProcessId;
  ProcessId firstShellProcessId;
  void (*runScheduler)(void);
} SchedulerState;

/// @struct CommandDescriptor
///
/// @brief Container of information for launching a process.
///
/// @param consolePort The index of the ConsolePort the input came from.
/// @param consoleInput The input as provided by the console.
/// @param callingProcess The process ID of the process that is launching the
///   command.
/// @param schedulerState A pointer to the SchedulerState structure maintained
///   by the scheduler.
typedef struct CommandDescriptor {
  int                consolePort;
  char              *consoleInput;
  ProcessId             callingProcess;
  SchedulerState    *schedulerState;
} CommandDescriptor;

/// @struct CommandEntry
///
/// @brief Descriptor for a command that can be looked up and run by the
/// handleCommand function.
///
/// @param name The textual name of the command.
/// @param func A function pointer to the process that will be spawned to
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
/// @param inUse Whether or not this buffer is in use by a process.  Set by the
///   consoleGetBuffer function when getting a buffer for a caller and cleared
///   by the caller when no longer being used.
/// @param buffer The array of CONSOLE_BUFFER_SIZE characters that the calling
///   process can use.
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
/// @param outputOwner The ID of the process that currently has the ability to
///   write output to the port.
/// @param inputOwner The ID of the process that currently has the ability to
///   read input from the port.
/// @param shell The ID of the process that serves as the console port's shell.
/// @param waitingForInput Whether or not the owning process is currently
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
  ProcessId           outputOwner;
  ProcessId           inputOwner;
  ProcessId           shell;
  bool                waitingForInput;
  int               (*readByte)(struct ConsolePort *consolePort);
  bool                echo;
  int               (*consolePrintString)(unsigned char port,
                      const char *string);
} ConsolePort;

/// @struct ConsoleState
///
/// @brief State maintained by the main console process and passed to the inter-
/// process command handlers.
///
/// @param consolePorts The array of ConsolePorts that will be polled for input
///   from the user.
/// @param consoleBuffers The array of ConsoleBuffers that can be used by
///   the console ports for input and by processes for output.
/// @param numConsolePorts The number of active console ports.
typedef struct ConsoleState {
  ConsolePort consolePorts[CONSOLE_NUM_PORTS];
  // consoleBuffers needs to come at the end.
  ConsoleBuffer consoleBuffers[CONSOLE_NUM_BUFFERS];
  int numConsolePorts;
} ConsoleState;

/// @struct ConsolePortPidAssociation
///
/// @brief Structure to associate a console port with a process ID.  This
/// information is used in a CONSOLE_ASSIGN_PORT command.
///
/// @param consolePort The index into the consolePorts array of a ConsoleState
///   object.
/// @param pid The process ID associated with the port.
typedef struct ConsolePortPidAssociation {
  uint8_t        consolePort;
  ProcessId         pid;
} ConsolePortPidAssociation;

/// @union ConsolePortPidUnion
///
/// @brief Union of a ConsolePortPidAssociation and a uintptr_t to
/// allow for easy conversion between the two.
///
/// @param consolePortPidAssociation The ConsolePortPidAssociation part.
/// @param nanoOsMessageData The uintptr_t part.
typedef union ConsolePortPidUnion {
  ConsolePortPidAssociation consolePortPidAssociation;
  uintptr_t                 nanoOsMessageData;
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
typedef struct ReallocMessage {
  void *ptr;
  size_t size;
} ReallocMessage;

/// @struct MemNode
///
/// @brief Metadata for a single block of memory managed by the memory manager.
///
/// @param next Pointer to the next block of memory in the list.
/// @param prev Pointer to the previous block of memory in the list.
/// @param size The number of bytes allocated at this pointer.
/// @param owner The ProcessId of the owner of this block of memory.
typedef struct MemNode {
  struct MemNode *next;
  struct MemNode *prev;
  size_t          size;
  ProcessId          owner;
} MemNode;

/// @struct MemoryManagerState
///
/// @brief State metadata the memory manager process uses for allocations and
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

// POSIX-mandated objects required for posix_spawn.
typedef struct posix_spawn_file_actions_t posix_spawn_file_actions_t;
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
  // Change this type if we change the size of pid_t or ProcessId!!!
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
/// @param callingProcessId The process ID of the process that is execing.
/// @param pathname The full, absolute path on disk to the program to run.
/// @param argv The NULL-terminated array of arguments for the command.  argv[0]
///   must be valid and should be the name of the program.
/// @param envp The NULL-terminated array of environment variables in
///   "name=value" format.  This array may be NULL.
/// @param schedulerState A pointer to the SchedulerState managed by the
///   scheduler.  This is needed by the execCommand function.
typedef struct ExecArgs {
  ProcessId callingProcessId;
  char *pathname;
  char **argv;
  char **envp;
  SchedulerState *schedulerState;
} ExecArgs;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NANO_OS_TYPES_H
