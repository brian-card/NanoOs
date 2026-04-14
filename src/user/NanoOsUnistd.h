///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              10.22.2025
///
/// @file              NanoOsUnistd.h
///
/// @brief             Functionality from unistd.h to be exported to user
///                    programs.
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

#ifndef NANO_OS_USER_UNISTD_H
#define NANO_OS_USER_UNISTD_H

#ifdef __cplusplus
extern "C"
{
#endif

#define _POSIX_HOST_NAME_MAX 255

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif // HOST_NAME_MAX

#if HOST_NAME_MAX > _POSIX_HOST_NAME_MAX
#error "HOST_NAME_MAX cannot exceed _POSIX_HOST_NAME_MAX"
#endif // HOST_NAME_MAX > _POSIX_HOST_NAME_MAX

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Declared in NanoOsTypes.h.
typedef struct FileDescriptor FileDescriptor;

/// @struct Dup2
///
/// @brief Information about how to dup a FileDescriptor into the ones managed
/// by a task.
///
/// @param fd The destination index into the task's fileDescriptors array.
/// @param dup A pointer to the FileDescriptor that is to be used.
typedef struct Dup2 {
  int fd;
  FileDescriptor *dup;
} Dup2;

/// @struct posix_spawn_file_actions_t
///
/// @brief Description of file-related operations that need to happen during a
/// posix_spawn call.
///
/// @param numDup2 The number of dup2 operations that need to happen before the
///   process begins.
/// @param dup2 Array of Dup2 objects that specify the FileDescriptors to dup
///   onto the file descriptors used by the spawned process.
typedef struct posix_spawn_file_actions_t {
  uint8_t numDup2;
  Dup2 dup2[2];
} posix_spawn_file_actions_t;

int nanoOsDup2(int oldfd, int newfd);
int nanoOsGethostname(char *name, size_t len);
int nanoOsPipe(int pipefd[2]);
int nanoOsSethostname(const char *name, size_t len);
int nanoOsTtyname_r(int fd, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_UNISTD_H

