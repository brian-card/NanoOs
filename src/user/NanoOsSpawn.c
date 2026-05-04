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

/// @file NanoOsSpawn.c
///
/// @brief Kernel-side implementation of Unix spawn functionality.

#include <string.h>

#include "NanoOsErrno.h"
#include "NanoOsSpawn.h"
#include "../kernel/MemoryManager.h"
#include "../kernel/NanoOsTypes.h"
#include "../kernel/Scheduler.h"
#include "../kernel/Processes.h"

// Must come last
#include "NanoOsStdio.h"

/// @fn int nanoOsSpawnFileActionsInit(posix_spawn_file_actions_t *fileActions)
///
/// @brief NanoOs implementation of posix_spawn_file_actions_init.
///
/// @param fileActions Pointer to a posix_spawn_file_actions_t structure to
///   initialize.
///
/// @return Returns 0 on success, errno on failure.
int nanoOsSpawnFileActionsInit(posix_spawn_file_actions_t *fileActions) {
  fileActions->numDup2 = 0;

  return 0;
}

/// @fn int nanoOsSpawnFileActionsAdddup2(
///   posix_spawn_file_actions_t *fileActions,
///   int fildes,
///   int newfildes)
///
/// @brief NanoOs implementation of posix_spawn_file_actions_adddup2.
///
/// @param fileActions A pointer to a posix_spawn_file_actions_t structure to
///   add the dup2 data to.
/// @param fildes A file descriptor number representing the file descriptor that
///   will replace an existing file descriptor.
/// @param newfiledes The file descriptor number that the replacing file
///   descriptor will replace.
///
/// @return Returns 0 on success, errno on failure.
int nanoOsSpawnFileActionsAdddup2(
  posix_spawn_file_actions_t *fileActions,
  int fildes,
  int newfildes
) {
  int returnValue = 0;
  
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor == NULL) {
    // This should be impossible, but check anyway.
    returnValue = EOTHER;
    goto exit;
  }

  if (fileActions->numDup2
    >= (sizeof(fileActions->dup2) / sizeof(fileActions->dup2[0]))
  ) {
    // Too many actions to accommodate.
    returnValue = ENOMEM;
    goto exit;
  }

  fileActions->dup2[fileActions->numDup2].fd = newfildes;
  fileActions->dup2[fileActions->numDup2].dup
    = processDescriptor->fileDescriptors[fildes];
  // We have to increment refCount here so that it doesn't get freed if the
  // caller closes the file descriptor before the scheduler runs again.
  fileActions->dup2[fileActions->numDup2].dup->refCount++;
  fileActions->numDup2++;

exit:
  return returnValue;
}

/// @fn int nanoOsSpawnFileActionsDestroy(
///   posix_spawn_file_actions_t *fileActions)
///
/// @brief NanoOs implementation of posix_spawn_file_actions_destroy.
///
/// @param fileActions A pointer to the posix_spawn_file_actions_t to release
///   resources for.
///
/// @return Returns 0 on success, errno on failure.
int nanoOsSpawnFileActionsDestroy(posix_spawn_file_actions_t *fileActions) {
  // Nothing to do for this.
  (void) fileActions;

  return 0;
}

/// @fn int nanoOsSpawn(
///   pid_t *pid, const char *path,
///   const posix_spawn_file_actions_t *file_actions,
///   const posix_spawnattr_t *attrp,
///   char *const argv[], char *const envp[])
///
/// @brief NanoOs implementation of Unix posix_spawn function.
///
/// @param pathname The full, absolute path on disk to the program to run.
/// @param argv The NULL-terminated array of arguments for the command.  argv[0]
///   must be valid and should be the name of the program.
/// @param envp The NULL-terminated array of environment variables in
///   "name=value" format.  This array may be NULL.
///
/// @return This function will not return to the caller on success.  On failure,
/// -1 will be returned and the value of errno will be set to indicate the
/// reason for the failure.
int nanoOsSpawn(
  pid_t *pid, const char *path,
  const posix_spawn_file_actions_t *file_actions,
  const posix_spawnattr_t *attrp,
  char *const argv[], char *const envp[]
) {
  int returnValue = 0;
  if ((path == NULL) || (argv == NULL) || (argv[0] == NULL)) {
    return EFAULT;
  }

  SpawnArgs *spawnArgs = (SpawnArgs*) calloc(1, sizeof(SpawnArgs));
  if (spawnArgs == NULL) {
    return ENOMEM;
  }

  spawnArgs->newPid = pid;

  spawnArgs->path = (char*) malloc(strlen(path) + 1);
  if (spawnArgs->path == NULL) {
    returnValue = ENOMEM;
    goto freeSpawnArgs;
  }
  strcpy(spawnArgs->path, path);

  if (file_actions != NULL) {
    spawnArgs->fileActions = (posix_spawn_file_actions_t*) malloc(
      sizeof(posix_spawn_file_actions_t));
    if (spawnArgs->fileActions == NULL) {
      returnValue = ENOMEM;
      goto freeSpawnArgs;
    }
    memcpy(spawnArgs->fileActions, file_actions, sizeof(*file_actions));
  } else {
    spawnArgs->fileActions = NULL;
  }


  // Not doing anything intelligent with this arg yet.  We need to make a copy
  // rather than casting away the `const` qualifier here if we ever do use it.
  spawnArgs->attrp = (posix_spawnattr_t*) attrp;

  size_t argvLen = 0;
  for (; argv[argvLen] != NULL; argvLen++);
  argvLen++; // Account for the terminating NULL element
  spawnArgs->argv = (char**) calloc(1, argvLen * sizeof(char*));
  if (spawnArgs->argv == NULL) {
    returnValue = ENOMEM;
    goto freeSpawnArgs;
  }

  // argvLen is guaranteed to always be at least 1, so it's safe to run to
  // (argvLen - 1) here.
  size_t ii = 0;
  for (; ii < (argvLen - 1); ii++) {
    // We know that argv[ii] isn't NULL because of the calculation for argvLen
    // above, so it's safe to use strlen.
    spawnArgs->argv[ii] = (char*) malloc(strlen(argv[ii]) + 1);
    if (spawnArgs->argv[ii] == NULL) {
      returnValue = ENOMEM;
      goto freeSpawnArgs;
    }
    strcpy(spawnArgs->argv[ii], argv[ii]);
  }
  spawnArgs->argv[ii] = NULL; // NULL-terminate the array

  if (envp != NULL) {
    size_t envpLen = 0;
    for (; envp[envpLen] != NULL; envpLen++);
    envpLen++; // Account for the terminating NULL element
    spawnArgs->envp = (char**) calloc(1, envpLen * sizeof(char*));
    if (spawnArgs->envp == NULL) {
      returnValue = ENOMEM;
      goto freeSpawnArgs;
    }

    // envpLen is guaranteed to always be at least 1, so it's safe to run to
    // (envpLen - 1) here.
    for (ii = 0; ii < (envpLen - 1); ii++) {
      // We know that envp[ii] isn't NULL because of the calculation for envpLen
      // above, so it's safe to use strlen.
      spawnArgs->envp[ii] = (char*) malloc(strlen(envp[ii]) + 1);
      if (spawnArgs->envp[ii] == NULL) {
        returnValue = ENOMEM;
        goto freeSpawnArgs;
      }
      strcpy(spawnArgs->envp[ii], envp[ii]);
    }
    spawnArgs->envp[ii] = NULL; // NULL-terminate the array
  } else {
    spawnArgs->envp = NULL;
  }

  ProcessMessage *processMessage
    = initSendProcessMessageToProcessId(
    SCHEDULER_STATE->schedulerProcessId, SCHEDULER_SPAWN,
    spawnArgs, sizeof(*spawnArgs), true);
  if (processMessage == NULL) {
    // The only way this should be possible is if all available messages are
    // in use, so use ENOMEM as the errno.
    errno = ENOMEM;
    goto freeSpawnArgs;
  }

  processMessageWaitForDone(processMessage, NULL);
  returnValue = (int) ((intptr_t) processMessageData(processMessage));
  processMessageRelease(processMessage);

  return returnValue;

freeSpawnArgs:
  spawnArgs = spawnArgsDestroy(spawnArgs);

  return returnValue;
}

