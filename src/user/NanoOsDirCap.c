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

///////////////////////////////////////////////////////////////////////////////
///
/// @file              NanoOsDirCap.c
///
/// @brief             Kernel-side implementation of the calls a process uses
///                    to see and narrow its own directory capabilities.
///
///////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "NanoOsDirCap.h"
#include "NanoOsErrno.h"
#include "../kernel/MemoryManager.h"
#include "../kernel/NanoOsTypes.h"
#include "../kernel/Processes.h"
#include "../kernel/Scheduler.h"

// Must come last
#include "NanoOsStdio.h"

/// @fn int nanoOsDircapName(const char *directory, char **name,
///   size_t *nameLength)
///
/// @brief Turn a directory path into the canonical name a capability uses:
/// absolute, collapsed, and ending in '/'.  A relative path is taken
/// relative to the running process's PWD, or to / if it has none.
///
/// @param directory The directory path.
/// @param name Where to store the name, which the caller must free.  It is
///   NUL-terminated, but the terminator isn't part of the name.
/// @param nameLength Where to store the length of the name.
///
/// @return Returns 0 on success, EINVAL if directory is NULL or empty, or
/// ENOMEM if memory ran out.
int nanoOsDircapName(const char *directory, char **name, size_t *nameLength) {
  if ((directory == NULL) || (*directory == '\0')
    || (name == NULL) || (nameLength == NULL)
  ) {
    return EINVAL;
  }

  const char *pwd = "";
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if ((*directory != '/') && (processDescriptor != NULL)
    && (processDescriptor->envp != NULL)
  ) {
    for (char **envp = processDescriptor->envp; *envp != NULL; envp++) {
      if (strncmp(*envp, "PWD=/", 5) == 0) {
        pwd = &(*envp)[4];
        break;
      }
    }
  }

  size_t pwdLength = strlen(pwd);
  size_t directoryLength = strlen(directory);
  char *path = (char*) malloc(pwdLength + directoryLength + 3);
  if (path == NULL) {
    return ENOMEM;
  }
  memcpy(path, pwd, pwdLength);
  path[pwdLength] = '/';
  memcpy(&path[pwdLength + 1], directory, directoryLength + 1);

  size_t length = pathCollapse(path);
  if (length > 1) {
    path[length++] = '/';
    path[length] = '\0';
  }
  *name = path;
  *nameLength = length;
  return 0;
}

/// @fn static int nanoOsDircapChange(SchedulerCommand command,
///   const char *directory, uint8_t mask)
///
/// @brief Ask the scheduler to add an entry to the running process's set, or
/// to drop one.
///
/// @param command SCHEDULER_ADD_DIR_CAPABILITY or
///   SCHEDULER_DROP_DIR_CAPABILITY.
/// @param directory The directory the entry names.
/// @param mask The DIRCAP_* bits to allow.  Not used to drop.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
static int nanoOsDircapChange(SchedulerCommand command,
  const char *directory, uint8_t mask
) {
  SchedulerDirCapabilityArgs schedulerDirCapabilityArgs = {
    .name = NULL,
    .nameLength = 0,
    .mask = mask,
    .errorNumber = 0,
  };
  char *name = NULL;
  int errorNumber = nanoOsDircapName(directory, &name,
    &schedulerDirCapabilityArgs.nameLength);
  if (errorNumber != 0) {
    errno = errorNumber;
    return -1;
  }
  schedulerDirCapabilityArgs.name = name;

  ProcessMessage *processMessage = initSendProcessMessageToPid(schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | command,
    &schedulerDirCapabilityArgs, sizeof(schedulerDirCapabilityArgs), true);
  if (processMessage == NULL) {
    free(name);
    errno = ENOMEM;
    return -1;
  }
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);
  free(name);

  if (schedulerDirCapabilityArgs.errorNumber != 0) {
    errno = schedulerDirCapabilityArgs.errorNumber;
    return -1;
  }
  return 0;
}

/// @fn int nanoOsDircapAdd(const char *directory, uint8_t mask)
///
/// @brief Add a capability for a directory to the running process's set, or
/// narrow the one it holds.  The set must already allow every bit of the mask
/// on the directory.
///
/// @param directory The directory, absolute or relative to PWD.
/// @param mask The DIRCAP_* bits to allow.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
int nanoOsDircapAdd(const char *directory, uint8_t mask) {
  return nanoOsDircapChange(SCHEDULER_ADD_DIR_CAPABILITY, directory, mask);
}

/// @fn int nanoOsDircapDrop(const char *directory)
///
/// @brief Remove the capability for a directory from the running process's
/// set.  Dropping one it doesn't hold succeeds and changes nothing.
///
/// @param directory The directory, absolute or relative to PWD.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
int nanoOsDircapDrop(const char *directory) {
  return nanoOsDircapChange(SCHEDULER_DROP_DIR_CAPABILITY, directory, 0);
}

/// @fn int nanoOsDircapGet(DirCapabilities *buffer, size_t bufferSize)
///
/// @brief Copy the running process's set into a buffer, if it fits.
///
/// @param buffer The buffer, or NULL to only get the size.
/// @param bufferSize The size of buffer, in bytes.
///
/// @return Returns the size of the set in bytes, which is more than
/// bufferSize if nothing was copied, or -1 and sets errno on failure.
int nanoOsDircapGet(DirCapabilities *buffer, size_t bufferSize) {
  SchedulerGetDirCapabilitiesArgs schedulerGetDirCapabilitiesArgs = {
    .buffer = buffer,
    .bufferSize = bufferSize,
    .size = 0,
    .errorNumber = 0,
  };
  ProcessMessage *processMessage = initSendProcessMessageToPid(schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_GET_DIR_CAPABILITIES,
    &schedulerGetDirCapabilitiesArgs, sizeof(schedulerGetDirCapabilitiesArgs),
    true);
  if (processMessage == NULL) {
    errno = ENOMEM;
    return -1;
  }
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

  if (schedulerGetDirCapabilitiesArgs.errorNumber != 0) {
    errno = schedulerGetDirCapabilitiesArgs.errorNumber;
    return -1;
  }
  return (int) schedulerGetDirCapabilitiesArgs.size;
}
