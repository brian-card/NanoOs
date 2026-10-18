///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              10.18.2026
///
/// @file              FilesystemPaths.h
///
/// @brief             Path resolution helpers for the filesystem command
///                    handlers.
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

#ifndef FILESYSTEMS_FILESYSTEM_PATHS_H
#define FILESYSTEMS_FILESYSTEM_PATHS_H

#include <stdlib.h>
#include <string.h>

// The kernel-linked build gets ProcessMessage, processMessageFrom and
// pathCollapse from the kernel headers FilesystemCommandHandlers.c includes
// before this one.
#ifndef NANO_OS_KERNEL_BUILD
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"
#endif // NANO_OS_KERNEL_BUILD
#include "FilesystemUtils.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def FILESYSTEM_PATH_ENOMEM
///
/// @brief errno value used when an absolute path can't be allocated.
/// ***WARNING*** This value has to match ENOMEM in src/user/NanoOsErrno.h.
#define FILESYSTEM_PATH_ENOMEM 3 // ENOMEM

/// @fn static inline char* filesystemAbsolutePath(
///   FilesystemState *filesystemState, ProcessMessage *processMessage,
///   char *path)
///
/// @brief Resolve a path sent by another process into an absolute path by
/// prefixing a relative path with the sender's PWD (or "/" if it has none) and
/// collapsing the result.
///
/// @param filesystemState A pointer to the filesystem process's
///   FilesystemState.
/// @param processMessage The ProcessMessage that carried the path.
/// @param path The path as provided by the sending process.
///
/// @return Returns path itself if it is NULL or already absolute, a newly
/// allocated absolute path that the caller must free otherwise, or NULL if
/// the new path could not be allocated.
static inline char* filesystemAbsolutePath(
  FilesystemState *filesystemState, ProcessMessage *processMessage,
  char *path
) {
  if ((path == NULL) || (*path == '/')) {
    return path;
  }

  const char *pwd = NULL;
  const ProcessDescriptor *from = processMessageFrom(processMessage);
  if ((from >= filesystemState->allProcesses)
    && (from < filesystemState->allProcesses + filesystemState->numProcesses)
    && (from->envp != NULL)
  ) {
    for (char **envp = from->envp; *envp != NULL; envp++) {
      const char *entry = *envp;
      if ((entry[0] == 'P') && (entry[1] == 'W') && (entry[2] == 'D')
        && (entry[3] == '=') && (entry[4] == '/')
      ) {
        pwd = &entry[4];
        break;
      }
    }
  }

  size_t pwdLength = (pwd != NULL) ? strlen(pwd) : 0;
  size_t pathLength = strlen(path);
  char *absolutePath = (char*) malloc(pwdLength + pathLength + 2);
  if (absolutePath == NULL) {
    return NULL;
  }
  if (pwd != NULL) {
    memcpy(absolutePath, pwd, pwdLength);
  }
  absolutePath[pwdLength] = '/';
  memcpy(&absolutePath[pwdLength + 1], path, pathLength + 1);
  pathCollapse(absolutePath);

  return absolutePath;
}

#ifdef __cplusplus
}
#endif

#endif // FILESYSTEMS_FILESYSTEM_PATHS_H
