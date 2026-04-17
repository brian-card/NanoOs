////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file NanoOsUnistd.c
///
/// @brief Kernel-side implementation of Unix unistd functionality.

// Standard C includes
#define FILE C_FILE
#include "stdio.h"
#undef FILE
#include "string.h"

#include "NanoOsLibC.h"
#include "NanoOsUnistd.h"
#include "../kernel/Console.h"
#include "../kernel/NanoOs.h"
#include "../kernel/Scheduler.h"
#include "../kernel/Tasks.h"

// Must come last
#include "NanoOsStdio.h"

/// @fn int nanoOsClose(int fd)
///
/// @brief Implementation of the standard POSIX close function.
///
/// @param fd The file descriptor to close.
///
/// @return On success, 0 is returned.  On error, -1 is returned and the value
/// of errno is set.
int nanoOsClose(int fd) {
  int returnValue = 0;
  
  TaskDescriptor *taskDescriptor = getRunningTask();
  if (taskDescriptor == NULL) {
    // This should be impossible, but check anyway.
    errno = EOTHER;
    returnValue = -1;
    goto exit;
  }
  
  if ((fd > taskDescriptor->numFileDescriptors)
    || (taskDescriptor->fileDescriptors[fd] == NULL)
  ) {
    errno = EBADF;
    returnValue = -1;
    goto exit;
  }
  
  taskDescriptor->fileDescriptors[fd]->refCount--;
  if (taskDescriptor->fileDescriptors[fd]->refCount == 0) {
    if (taskDescriptor->fileDescriptors[fd]->pipeEnd != NULL) {
      taskDescriptor->fileDescriptors[fd]->pipeEnd->pipeEnd = NULL;
    }
    free(taskDescriptor->fileDescriptors[fd]);
    taskDescriptor->fileDescriptors[fd] = NULL;
  }
  
exit:
  return returnValue;
}

/// @fn int nanoOsDup2(int oldfd, int newfd)
///
/// @brief Implementation of the standard POSIX dup2 function.
///
/// @param oldfd The source file descriptor that is to be copied.
/// @param newfd The destination file descriptor that is to be replaced.
///
/// @return On success, the value of newfd is returned.  On failure, -1 is
/// returned and errno is set.
int nanoOsDup2(int oldfd, int newfd) {
  int returnValue = 0;
  
  TaskDescriptor *taskDescriptor = getRunningTask();
  if (taskDescriptor == NULL) {
    // This should be impossible, but check anyway.
    errno = EOTHER;
    returnValue = -1;
    goto exit;
  } else if (newfd >= taskDescriptor->numFileDescriptors) {
    errno = EBADF;
    returnValue = -1;
    goto exit;
  }
  
  FileDescriptor *oldFileDescriptor = taskDescriptor->fileDescriptors[oldfd];
  FileDescriptor *newFileDescriptor = taskDescriptor->fileDescriptors[newfd];
  if (newFileDescriptor->pipeEnd != NULL) {
    // We're about to free this FileDescriptor, so terminate the other end of
    // the pipe.
    newFileDescriptor->pipeEnd->pipeEnd = NULL;
  }
  free(newFileDescriptor); newFileDescriptor = NULL;
  
  // Move the old file descriptor into the new one's slot.
  taskDescriptor->fileDescriptors[newfd] = oldFileDescriptor;
  oldFileDescriptor->refCount++;
  
  if (oldFileDescriptor->pipeEnd != NULL) {
    // Check to see if we need to adjust the taskIds for the pipe.
    if (newfd == STDIN_FILENO) {
      // We need to set the taskId of the outputChannel of the other end of the
      // pipe to our ID and the taskId of the inputChannel of this end of the
      // pipe to the other end's ID.
      TaskId pipeEndTaskId = oldFileDescriptor->pipeEnd->inputChannel.taskId;
      oldFileDescriptor->pipeEnd->outputChannel.taskId = getRunningTaskId();
      if (pipeEndTaskId != ((TaskId) -1)) {
        // The scheduler has initialized the taskId in the file descriptor.
        // Use it.
        oldFileDescriptor->inputChannel.taskId = pipeEndTaskId;
      }
    } else if ((newfd == STDOUT_FILENO) || (newfd == STDERR_FILENO)) {
      // We need to set the taskId of the inputChannel of the other end of the
      // pipe to our ID and the taskId of the outputChannel of this end of the
      // pipe to the other end's ID.
      TaskId pipeEndTaskId = oldFileDescriptor->pipeEnd->outputChannel.taskId;
      oldFileDescriptor->pipeEnd->inputChannel.taskId = getRunningTaskId();
      if (pipeEndTaskId != ((TaskId) -1)) {
        // The scheduler has initialized the taskId in the file descriptor.
        // Use it.
        oldFileDescriptor->outputChannel.taskId = pipeEndTaskId;
      }
    }
  }
  
  returnValue = newfd;
  
exit:
  return returnValue;
}

/// @fn int nanoOsGethostname(char *name, size_t len)
///
/// @brief Implementation of the standard Unix nanoOsGethostname system call.
///
/// @param name Pointer to a character buffer to fill with the hostname.
/// @param len The number of bytes allocated to name.
///
/// @return Returns 0 on success, -1 on failure.  On failure, the value of
/// errno is also set.
int nanoOsGethostname(char *name, size_t len) {
  if (name == NULL) {
    errno = EFAULT;
    return -1;
  }
  
  const char *hostname = schedulerGetHostname();
  if (hostname == NULL) {
    hostname = "localhost";
  }
  size_t hostnameLen = strlen(hostname);
  
  int returnValue = 0;
  if (len < hostnameLen) {
    returnValue = -1;
    errno = ENAMETOOLONG;
  }
  strncpy(name, hostname, len);
  
  return returnValue;
}

/// @fn int nanoOsPipe(int pipefd[2])
///
/// @brief Implementation of the standard Unix pipe() system call.
///
/// @param pipefd A two-element array of integers that is to be populated with
///   the POSIX file descriptor values of the ends of the pipe.  Index 0 is the
///   read end, index 1 is the write end.
///
/// @return Returns 0 on success.  On failure, -1 is returned and errno is set.
int nanoOsPipe(int pipefd[2]) {
  int returnValue = 0;
  
  TaskDescriptor *taskDescriptor = getRunningTask();
  if (taskDescriptor == NULL) {
    // This should be impossible, but check anyway.
    errno = EOTHER;
    returnValue = -1;
    goto exit;
  }
  
  uint8_t numFileDescriptors = taskDescriptor->numFileDescriptors;
  void *check = realloc(taskDescriptor->fileDescriptors,
    (numFileDescriptors + 2) * sizeof(FileDescriptor*));
  if (check == NULL) {
    errno = ENOMEM;
    returnValue = -1;
    goto exit;
  }
  taskDescriptor->fileDescriptors = (FileDescriptor**) check;
  // We need to guarantee that the new slots added start out as NULL.  That way,
  // if one of the allocations fails, we can pass the pointer into free and not
  // worry about it.
  for (int ii = numFileDescriptors; ii < (numFileDescriptors + 2); ii++) {
    taskDescriptor->fileDescriptors[ii] = NULL;
  }
  
  // Now allocate each one
  for (int ii = numFileDescriptors; ii < (numFileDescriptors + 2); ii++) {
    taskDescriptor->fileDescriptors[ii]
      = (FileDescriptor*) malloc(sizeof(FileDescriptor));
    if (taskDescriptor->fileDescriptors[ii] == NULL) {
      errno = ENOMEM;
      goto freeFileDescriptors;
    }
    
    // Initialize the file descriptor's channels to point to ourself.
    taskDescriptor->fileDescriptors[ii]->inputChannel.taskId = -1;
    taskDescriptor->fileDescriptors[ii]->inputChannel.messageType = -1;
    taskDescriptor->fileDescriptors[ii]->outputChannel.taskId = -1;
    taskDescriptor->fileDescriptors[ii]->outputChannel.messageType = -1;
  }
  
  // Fix the messages for the relevant channels in the file descriptors.
  taskDescriptor->fileDescriptors[numFileDescriptors
    ]->inputChannel.messageType = CONSOLE_WAIT_FOR_INPUT;
  taskDescriptor->fileDescriptors[numFileDescriptors + 1
    ]->outputChannel.messageType = CONSOLE_WRITE_BUFFER;
  
  // Now point the pipe ends toward each other.
  taskDescriptor->fileDescriptors[numFileDescriptors]->pipeEnd
    = taskDescriptor->fileDescriptors[numFileDescriptors + 1];
  taskDescriptor->fileDescriptors[numFileDescriptors + 1]->pipeEnd
    = taskDescriptor->fileDescriptors[numFileDescriptors];
  
  // Set the values in the array that was provided.
  pipefd[0] = numFileDescriptors;
  pipefd[1] = numFileDescriptors + 1;
  
  // Lastly, increase the value of taskDescriptor->numFileDescriptors;
  taskDescriptor->numFileDescriptors += 2;
  
  return returnValue;
  
freeFileDescriptors:
  for (int ii = numFileDescriptors; ii < (numFileDescriptors + 2); ii++) {
    free(taskDescriptor->fileDescriptors[ii]);
  }
  // We need to shrink the fileDescriptors array back down to its original size.
  // Since we're reducing the amount of memory consumed, this is guaranteed to
  // be successful and not relocate the pointer, so no need to do anything with
  // the return value.
  realloc(taskDescriptor->fileDescriptors,
    numFileDescriptors * sizeof(FileDescriptor*));
  
exit:
  return returnValue;
}

/// @fn int nanoOsSethostname(const char *name, size_t len)
///
/// @brief Implementation of the standard Unix nanoOsSethostname system call.
///
/// @param name Pointer to a character buffer that contains the desired
///   hostname.
/// @param len The number of bytes allocated to name.
///
/// @return Returns 0 on success, -1 on failure.  On failure, the value of
/// errno is also set.
int nanoOsSethostname(const char *name, size_t len) {
  if (name == NULL) {
    errno = EFAULT;
    return -1;
  } else if (len > HOST_NAME_MAX) {
    errno = EINVAL;
    return -1;
  }
  
  FILE *hostnameFile = fopen("/etc/hostname", "r");
  if (hostnameFile == NULL) {
    printString("ERROR! fopen of hostname returned NULL!\n");
    return -1;
  }
  
  size_t bytesWritten = fwrite(name, 1, len, hostnameFile);
  if (bytesWritten != len) {
    printString("ERROR! Could not write hostname file.\n");
    fclose(hostnameFile);
    return -1;
  }
  
  fclose(hostnameFile);
  return 0;
}

/// @fd int nanoOsTtyname_r(int fd, char *buf, size_t buflen)
///
/// @brief Get the current tty name for a specified file descriptor in a
/// thread-safe way.
///
/// @param fd The integer file descriptor of the tty to get.
/// @param buf The character buffer to write the tty name into.
/// @param buflen The number of bytes available at the buf pointer.
///
/// @return Returns 0 on success, an error number on failure.
int nanoOsTtyname_r(int fd, char *buf, size_t buflen) {
  if (fd < 0) {
    return EBADF;
  } else if (fd > 2) {
    return ENOTTY;
  } else if (buflen < 10) {
    return ERANGE;
  }
  
  int consolePort = getOwnedConsolePort();
  if (consolePort < 0) {
    return ENOTTY;
  }
  
  snprintf(buf, buflen, "/dev/tty%d", consolePort);
  
  return 0;
}

