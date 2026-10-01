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

/// @fn size_t spawnArgsSize(const char *path,
///   const posix_spawn_file_actions_t *fileActions, char *const argv[])
///
/// @brief Calculate the size of the single block that holds a SpawnArgs
///   structure and all its contents.
///
/// @param path The path to the executable the spawned process will run.
/// @param fileActions The file actions to use for the posix_spawn operation.
///   This parameter may be NULL.
/// @param argv The argument array to use for the posix_spawn call.
///
/// @return Returns the total size of the contiguous block in bytes.
static __attribute__((noinline)) size_t spawnArgsSize(const char *path,
  const posix_spawn_file_actions_t *fileActions, char *const argv[]
) {
  size_t numArgs = 0;
  size_t stringBytes = 0;
  for (; argv[numArgs] != NULL; numArgs++) {
    stringBytes += strlen(argv[numArgs]) + 1;
  }
  numArgs++;

  size_t fileActionBytes = 0;
  if (fileActions != NULL) {
    fileActionBytes = sizeof(posix_spawn_file_actions_t);
  }

  return sizeof(SpawnArgs) + fileActionBytes + (numArgs * sizeof(char*))
    + stringBytes + strlen(path) + 1;
}

/// @fn void spawnArgsFill(SpawnArgs *spawnArgs, pid_t *pid, const char *path,
///   const posix_spawn_file_actions_t *fileActions,
///   const posix_spawnattr_t *attrp, char *const argv[])
///
/// @brief Copy a SpawnArgs structure and everything it to at into a zeroed
/// block of contiguous memory.
///
/// @param spawnArgs A pointer to SpawnArgs structure to populate.
/// @param pid The address the spawned process's PID is to be written to.
/// @param path The path to the executable the spawned process will run.
/// @param fileActions The file actions to copy into the block.  This parameter
///   may be NULL
/// @param attrp The spawn attributes.  This parameter is currently only set
///   by pointer.  Its contents, if any, are not copied into the block.
/// @param argv The argument array the spawned process will use.
///
/// @return This function returns no value.
static __attribute__((noinline)) void spawnArgsFill(SpawnArgs *spawnArgs,
  pid_t *pid, const char *path,
  const posix_spawn_file_actions_t *fileActions,
  const posix_spawnattr_t *attrp, char *const argv[]
) {
  spawnArgs->newPid = pid;
  spawnArgs->attrp = (posix_spawnattr_t*) attrp;

  char *nextField = &((char*) spawnArgs)[sizeof(SpawnArgs)];
  if (fileActions != NULL) {
    spawnArgs->fileActions = (posix_spawn_file_actions_t*) nextField;
    memcpy(spawnArgs->fileActions, fileActions, sizeof(*fileActions));
    nextField += sizeof(posix_spawn_file_actions_t);
  }

  size_t numArgs = 0;
  for (; argv[numArgs] != NULL; numArgs++);
  numArgs++;

  spawnArgs->argv = (char**) nextField;
  char *nextString = (char*) &spawnArgs->argv[numArgs];
  size_t index = 0;
  for (; index < (numArgs - 1); index++) {
    spawnArgs->argv[index] = nextString;
    strcpy(nextString, argv[index]);
    nextString += strlen(argv[index]) + 1;
  }

  spawnArgs->path = nextString;
  strcpy(spawnArgs->path, path);
}

/// @fn size_t envpBlockSize(char *const envp[])
///
/// @brief Calculate the size of the the contiguous block that holds an
/// environment's strings and the array that points to them.
///
/// @param envp The environment array of environment variables to calcualte the
///   size of.
///
/// @return Returns the size of the contiguous block in bytes or 0 if the
/// environment is empty.
static __attribute__((noinline)) size_t envpBlockSize(char *const envp[]) {
  size_t numVariables = 0;
  size_t stringBytes = 0;
  for (; envp[numVariables] != NULL; numVariables++) {
    stringBytes += strlen(envp[numVariables]) + 1;
  }
  if (stringBytes == 0) {
    return 0;
  }
  numVariables++;

  return ((stringBytes + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1))
    + (numVariables * sizeof(char*));
}

/// @fn char** envpBlockFill(char *block, char *const envp[])
///
/// @brief Copy an environment's strings and its array into a zeroed block
/// of contiguous memory.
///
/// @param block A pointer to the contiguous block of memory to populate.
/// @param envp The process's environment array to copy into the block.
///
/// @return Returns the address of the array within the block.  Note:  This is
/// *NOT* the address of the start of the block.  That address may be found at
/// returnValue[0].
static __attribute__((noinline)) char** envpBlockFill(char *block,
  char *const envp[]
) {
  size_t numVariables = 0;
  size_t stringBytes = 0;
  for (; envp[numVariables] != NULL; numVariables++) {
    stringBytes += strlen(envp[numVariables]) + 1;
  }

  char **envpArray = (char**) &block[
    (stringBytes + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1)];
  char *nextString = block;
  for (size_t index = 0; index < numVariables; index++) {
    envpArray[index] = nextString;
    strcpy(nextString, envp[index]);
    nextString += strlen(envp[index]) + 1;
  }

  return envpArray;
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
  if ((pid == NULL) || (path == NULL) || (argv == NULL) || (argv[0] == NULL)) {
    return EFAULT;
  }

  SpawnArgs *spawnArgs
    = (SpawnArgs*) calloc(1, spawnArgsSize(path, file_actions, argv));
  if (spawnArgs == NULL) {
    return ENOMEM;
  }
  spawnArgsFill(spawnArgs, pid, path, file_actions, attrp, argv);

  if (envp != NULL) {
    size_t envpBytes = envpBlockSize(envp);
    if (envpBytes > 0) {
      char *envpBlock = (char*) calloc(1, envpBytes);
      if (envpBlock == NULL) {
        returnValue = ENOMEM;
        goto freeSpawnArgs;
      }
      spawnArgs->envp = envpBlockFill(envpBlock, envp);
    }
  }

  SchedulerSpawnArgs schedulerSpawnArgs = {
    .spawnArgs = spawnArgs,
    .errorNumber = 0,
  };
  ProcessMessage *processMessage
    = initSendProcessMessageToPid(
    schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_SPAWN,
    &schedulerSpawnArgs, sizeof(schedulerSpawnArgs), true);
  if (processMessage == NULL) {
    // The only way this should be possible is if all available messages are
    // in use, so use ENOMEM as the errno.
    errno = ENOMEM;
    goto freeSpawnArgs;
  }

  processMessageWaitForDone(processMessage, NULL);
  returnValue = schedulerSpawnArgs.errorNumber;
  processMessageRelease(processMessage);

  if (returnValue != 0) {
    goto freeSpawnArgs;
  }

  return returnValue;

freeSpawnArgs:
  spawnArgs = spawnArgsDestroy(spawnArgs);

  return returnValue;
}

