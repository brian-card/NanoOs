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

#include <stdbool.h>
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

/// @fn static inline bool filesystemPathIsCollapsed(const char *path)
///
/// @brief Determine whether an absolute path is already in the form that
/// pathCollapse produces, so that resolving it needs no copy.
///
/// @param path The absolute path to check.
///
/// @return Returns true if path has no empty, "." or ".." components and no
/// trailing '/' (other than the root itself), false otherwise.
static inline bool filesystemPathIsCollapsed(const char *path) {
  for (const char *slash = path; *slash != '\0'; slash++) {
    if (*slash != '/') {
      continue;
    }
    const char *component = slash + 1;
    if ((*component == '/') || ((*component == '\0') && (slash != path))) {
      return false;
    }
    if ((component[0] == '.')
      && ((component[1] == '/') || (component[1] == '\0')
        || ((component[1] == '.')
          && ((component[2] == '/') || (component[2] == '\0'))))
    ) {
      return false;
    }
  }

  return true;
}

/// @fn static inline bool filesystemMessageHasFullPath(
///   FilesystemState *filesystemState, ProcessMessage *processMessage)
///
/// @brief Determine whether a request's path is the full path on the volume.
/// Only a kernel or executive process may send one.
///
/// @param filesystemState A pointer to the filesystem process's
///   FilesystemState.
/// @param processMessage The ProcessMessage that carried the path.
///
/// @return Returns true if the path is the full path, false if it is under
/// /userspace.
static inline bool filesystemMessageHasFullPath(
  FilesystemState *filesystemState, ProcessMessage *processMessage
) {
  const ProcessDescriptor *from = processMessageFrom(processMessage);
  return ((processMessageType(processMessage) & FILESYSTEM_FULL_PATH) != 0)
    && (from >= filesystemState->allProcesses)
    && (from < filesystemState->allProcesses + filesystemState->numProcesses)
    && (from->privilegeLevel <= PRIVILEGE_LEVEL_EXECUTIVE);
}

/// @fn static inline char* filesystemAbsolutePath(
///   FilesystemState *filesystemState, ProcessMessage *processMessage,
///   char *path)
///
/// @brief Resolve a path sent by another process into the real path on the
/// volume:  make it absolute with the sender's PWD (or "/" if it has none),
/// collapse it, and put it under /userspace unless the request carries a full
/// path.
///
/// @param filesystemState A pointer to the filesystem process's
///   FilesystemState.
/// @param processMessage The ProcessMessage that carried the path.
/// @param path The path as provided by the sending process.
///
/// @return Returns path itself if it is NULL or is already the real path, a
/// newly allocated real path that the caller must free otherwise, or NULL if
/// the new path could not be allocated.
static inline char* filesystemAbsolutePath(
  FilesystemState *filesystemState, ProcessMessage *processMessage,
  char *path
) {
  if (path == NULL) {
    return path;
  }

  const ProcessDescriptor *from = processMessageFrom(processMessage);
  bool validSender = (from >= filesystemState->allProcesses)
    && (from < filesystemState->allProcesses + filesystemState->numProcesses);
  bool fullPath = filesystemMessageHasFullPath(filesystemState, processMessage);

  const char *pwd = NULL;
  if (*path == '/') {
    if (fullPath && filesystemPathIsCollapsed(path)) {
      return path;
    }
  } else if (!fullPath && validSender && (from->envp != NULL)) {
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
  char *realPath = (char*) malloc(
    FILESYSTEM_USERSPACE_LENGTH + pwdLength + pathLength + 2);
  if (realPath == NULL) {
    return NULL;
  }
  char *absolutePath = &realPath[FILESYSTEM_USERSPACE_LENGTH];
  if (pwd != NULL) {
    memcpy(absolutePath, pwd, pwdLength);
  }
  absolutePath[pwdLength] = '/';
  memcpy(&absolutePath[pwdLength + 1], path, pathLength + 1);
  size_t length = pathCollapse(absolutePath);

  if (fullPath) {
    memmove(realPath, absolutePath, length + 1);
    return realPath;
  }

  realPath[0] = '/';
  realPath[1] = 'u';
  realPath[2] = 's';
  realPath[3] = 'e';
  realPath[4] = 'r';
  realPath[5] = 's';
  realPath[6] = 'p';
  realPath[7] = 'a';
  realPath[8] = 'c';
  realPath[9] = 'e';
  if (length == 1) {
    // The driver rejects a path that ends in a separator.
    absolutePath[0] = '\0';
  }

  return realPath;
}

/// @fn static inline const char* filesystemCallerPath(const char *realPath,
///   bool fullPath, size_t *length)
///
/// @brief Get the name that a capability uses for a real path.
///
/// @param realPath A path returned by filesystemAbsolutePath.
/// @param fullPath Whether the request carried a full path.
/// @param length Where to store the length of the name.
///
/// @return Returns the name, which is not NUL-terminated for the directory
/// callers see as /.
static inline const char* filesystemCallerPath(const char *realPath,
  bool fullPath, size_t *length
) {
  size_t realLength = strlen(realPath);
  if (fullPath) {
    *length = realLength;
    return realPath;
  } else if (realLength == FILESYSTEM_USERSPACE_LENGTH) {
    *length = 1;
    return realPath;
  }

  *length = realLength - FILESYSTEM_USERSPACE_LENGTH;
  return &realPath[FILESYSTEM_USERSPACE_LENGTH];
}

#ifdef __cplusplus
}
#endif

#endif // FILESYSTEMS_FILESYSTEM_PATHS_H
