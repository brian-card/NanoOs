///////////////////////////////////////////////////////////////////////////////
///
/// @file              Filesystem.c
///
/// @brief             Common filesystem functionality.
///
///////////////////////////////////////////////////////////////////////////////

#include "../user/NanoOsLibC.h"
#include "Filesystem.h"
#include "Logger.h"
#include "NanoOs.h"
#include "Scheduler.h"
#include "Processes.h"

#include "../user/NanoOsStdio.h"

// getPartitionInfoImpl is filesystem-agnostic (it only parses the MBR
// partition table), so it's compiled in unconditionally and shared by every
// filesystem build -- contiguous and overlay reach it by symlinking
// userspace/usr/src/filesystems/drivers/common/GetPartitionInfoImpl.c into their own
// source trees; the kernel-linked build here pulls it in with #include
// instead, since Arduino IDE only discovers source files physically under
// src/kernel.
#define NANO_OS_KERNEL_BUILD
#include "../../userspace/usr/src/filesystems/drivers/common/GetPartitionInfoImpl.c"

/// @var rootFilesystemPid
///
/// @brief The well-known ProcessId of the root filesystem, or 0 if none is
/// running.  Set once, directly, when the filesystem process is created in
/// HalCommon.c's halCommonInitRootFilesystem.
ProcessId rootFilesystemPid = 0;

/// @fn static void handleFilesystemMessages(FilesystemState *fs)
///
/// @brief Pop and handle all messages in the filesystem process's message
/// queue until there are no more.
///
/// @param fs A pointer to the FilesystemState object maintained by the
///   filesystem process.
///
/// @return This function never returns.
static void handleFilesystemMessages(FilesystemState *filesystemState) {
  while (1) {
    ProcessMessage *msg = processMessageQueueWait(NULL);
    while (msg != NULL) {
      if ((processMessageType(msg) & 0xffffffffffffff00)
        != FILESYSTEM_COMMAND_SIGNATURE
      ) {
        logError("Received unknown signature 0x%lx "
          "from process %ld\n",
          (unsigned long int) (processMessageType(msg)
            & 0xffffffffffffff00),
          (long int) processPid(processMessageFrom(msg)));
        msg = processMessageQueuePop();
        continue;
      }

      FilesystemCommandResponse type =
        (FilesystemCommandResponse) (processMessageType(msg)
          & FILESYSTEM_COMMAND_MASK);
      if (type >= NUM_FILESYSTEM_COMMANDS) {
        logError("Received unknown filesystem message type "
          "%ld from process %ld\n", (long int) type,
          (long int) processPid(processMessageFrom(msg)));
      } else if (filesystemState->commandHandlers == NULL) {
        // No driver is linked directly into this build; this process only
        // exists because restartBuiltinFilesystem is always compiled
        // (HalCommon.c is shared), even on targets that never call it.
        logError("No filesystem driver linked in to handle command type "
          "%ld from process %ld\n", (long int) type,
          (long int) processPid(processMessageFrom(msg)));
      } else {
        logDebug("Handling filesystem message type %ld\n",
          (long int) type);
        filesystemState->args = msg;
        if (filesystemState->commandHandlers[type](filesystemState)
          != filesystemState
        ) {
          logError("Calling the filesystem command handler failed\n");
        }
      }

      msg = processMessageQueuePop();
    }
  }
}

/// @fn void* runFilesystem(void *args)
///
/// @brief Main process entry point for the filesystem process.
///
/// @param args A pointer to an initialized FilesystemState, cast to a
///   void*.  Its driverInit and commandHandlers members, if any driver is
///   linked directly into this build, are expected to already be populated
///   (see restartBuiltinFilesystem in HalCommon.c); Filesystem.c itself
///   never names a specific driver, so this function works whether or not
///   one is linked in.
///
/// @return This function never returns, but would return NULL if it did.
void* runFilesystem(void *args) {
  FilesystemState fs;
  memcpy(&fs, args, sizeof(fs));
  ((FilesystemState*) args)->driverState = (void*) ((intptr_t) 1);
  processYield();
  logDebug("runFilesystem: Allocating fs.blockBuffer\n");
  fs.blockBuffer = (uint8_t*) malloc(fs.blockSize);
  if (fs.blockBuffer == NULL) {
    logError("Could not allocate fs.blockBuffer\n");
    logError("Halting filesystem process\n");
    // Unblock the caller's wait loop before going into handleFilesystemMessages
    // below (see the matching driverState write-back further down for why
    // this is (void*) 2, not fs.driverState itself, when it's NULL here).
    ((FilesystemState*) args)->driverState = (void*) ((intptr_t) 2);
    // All the command handlers handle state not being initialized, so just go
    // into handleFilesystemMessages and block.
    handleFilesystemMessages(&fs);
  }

  logDebug("runFilesystem: Getting partition info\n");
  getPartitionInfoImpl(&fs);
  if (fs.driverInit != NULL) {
    logDebug("runFilesystem: Initiallizing driverState\n");
    fs.driverInit(&fs);
  }
  // getPartitionInfoImpl and driverInit both stash their result in fs.args
  // (see their own documentation); neither result is checked here, matching
  // this function's prior behavior.  Clear it before fs.args takes on its
  // other meaning: a pointer to the ProcessMessage being handled.
  fs.args = NULL;
  // args still points at the caller's (restartBuiltinFilesystem's) stack
  // copy of this struct, which only ever saw the (void*) 1 placeholder
  // written above before the first processYield.  The caller is spinning
  // in a wait loop on that copy's driverState field, so it needs the real
  // outcome written back through the original pointer -- otherwise it
  // thinks we're done as soon as we started, before the driver (or lack
  // thereof, on failure) is known.  (void*) 2 stands in for "finished, but
  // driverInit failed" so the caller can tell that apart from "hasn't run
  // yet" (NULL) -- it must never collide with a real driverState pointer.
  ((FilesystemState*) args)->driverState =
    (fs.driverState != NULL) ? fs.driverState : (void*) ((intptr_t) 2);
  logDebug("runFilesystem: Initialization complete\n");

  handleFilesystemMessages(&fs);
  return NULL;
}

/// @fn static FILE* filesystemOpenFile(const char *pathname, const char *mode,
///   int64_t pathFlag)
///
/// @brief Open a file for the running process.
///
/// @param pathname The path to the file.
/// @param mode The standard C file mode to open the file as.
/// @param pathFlag FILESYSTEM_FULL_PATH if pathname is the full path on the
///   volume, 0 if it's a path under /userspace.
///
/// @return Returns a pointer to an initialized FILE object on success, NULL on
/// failure.
static FILE* filesystemOpenFile(const char *pathname, const char *mode,
  int64_t pathFlag
) {
  FILE *file = NULL;

  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor == NULL) {
    // This should be impossible, but check anyway.
    errno = EOTHER;
    goto exit; // return NULL
  }
  
  if ((pathname == NULL) || (*pathname == '\0')
    || (mode == NULL) || (*mode == '\0')
  ) {
    errno = EINVAL;
    goto exit; // return NULL
  }

  uint8_t numFileDescriptors = processDescriptor->numFileDescriptors;
  uint8_t fd = 0;
  while ((fd < numFileDescriptors)
    && (processDescriptor->fileDescriptors[fd] != NULL)
  ) {
    fd++;
  }
  if (fd == numFileDescriptors) {
    void *check = realloc(processDescriptor->fileDescriptors,
      (numFileDescriptors + 1) * sizeof(FileDescriptor*));
    if (check == NULL) {
      errno = ENOMEM;
      goto exit; // return NULL
    }
    processDescriptor->fileDescriptors = (FileDescriptor**) check;
  }
  processDescriptor->fileDescriptors[fd]
    = (FileDescriptor*) calloc(1, sizeof(FileDescriptor));
  if (processDescriptor->fileDescriptors[fd] == NULL) {
    errno = ENOMEM;
    goto freeFileDescriptor;
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemFopenArgs, fopenArgs);
  if (fopenArgs == NULL) {
    errno = ENOMEM;
    goto freeFileDescriptor;
  }
  fopenArgs->now = time(NULL);
  char *pathnameCopy = (char*) malloc(strlen(pathname) + 1);
  if (pathnameCopy == NULL) {
    MESSAGE_ARGS_FREE(fopenArgs);
    goto freeFileDescriptor;
  }
  strcpy(pathnameCopy, pathname);
  char *modeCopy = (char*) malloc(strlen(mode) + 1);
  if (modeCopy == NULL) {
    goto freePathname;
  }
  strcpy(modeCopy, mode);
  fopenArgs->pathname = (char*) MESSAGE_BUFFER_SHARE(
    pathnameCopy, strlen(pathnameCopy) + 1, MESSAGE_BUFFER_IN,
    rootFilesystemPid);
  fopenArgs->mode = (char*) MESSAGE_BUFFER_SHARE(
    modeCopy, strlen(modeCopy) + 1, MESSAGE_BUFFER_IN, rootFilesystemPid);
  if ((fopenArgs->pathname == NULL) || (fopenArgs->mode == NULL)) {
    MESSAGE_BUFFER_UNSHARE(fopenArgs->mode, modeCopy, MESSAGE_BUFFER_IN);
    free(modeCopy);
    goto freePathname;
  }

  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_OPEN_FILE | pathFlag,
    fopenArgs, sizeof(*fopenArgs), true);
  processMessageWaitForDone(msg, NULL);
  MESSAGE_BUFFER_UNSHARE(fopenArgs->mode, modeCopy, MESSAGE_BUFFER_IN);
  MESSAGE_BUFFER_UNSHARE(fopenArgs->pathname, pathnameCopy,
    MESSAGE_BUFFER_IN);
  free(modeCopy);
  free(pathnameCopy);

  file = fopenArgs->returnValue;
  processMessageRelease(msg);
  if (file == NULL) {
    errno = fopenArgs->errorNumber;
    MESSAGE_ARGS_FREE(fopenArgs);
    goto freeFileDescriptor;
  }
  MESSAGE_ARGS_FREE(fopenArgs);

  processDescriptor->fileDescriptors[fd]->file = file;
  processDescriptor->fileDescriptors[fd]->streamFd = fd;
  processDescriptor->fileDescriptors[fd]->refCount = 1;
  processDescriptor->numFilesystemHandles++;
  if (fd == numFileDescriptors) {
    processDescriptor->numFileDescriptors = numFileDescriptors + 1;
  }

  goto exit;

freePathname:
  MESSAGE_BUFFER_UNSHARE(fopenArgs->pathname, pathnameCopy,
    MESSAGE_BUFFER_IN);
  free(pathnameCopy);
  MESSAGE_ARGS_FREE(fopenArgs);

freeFileDescriptor:
  free(processDescriptor->fileDescriptors[fd]);
  processDescriptor->fileDescriptors[fd] = NULL;
  if (fd == numFileDescriptors) {
    // We need to shrink the fileDescriptors array back down to its original
    // size.  Since we're reducing the amount of memory consumed, this is
    // guaranteed to be successful and not relocate the pointer, so no need to
    // do anything with the return value.
    realloc(processDescriptor->fileDescriptors,
      numFileDescriptors * sizeof(FileDescriptor*));
  }

exit:
  return file;
}

/// @fn FILE* filesystemFopen(const char *pathname, const char *mode)
///
/// @brief Implementation of the standard C fopen call.  The path is always
/// under /userspace.
///
/// @param pathname The path to the file.
/// @param mode The standard C file mode to open the file as.
///
/// @return Returns a pointer to an initialized FILE object on success, NULL on
/// failure.
FILE* filesystemFopen(const char *pathname, const char *mode) {
  return filesystemOpenFile(pathname, mode, 0);
}

/// @fn FILE* filesystemFopenFullPath(const char *pathname, const char *mode)
///
/// @brief Open a file by its full path on the volume, for a kernel or
/// executive process.
///
/// @param pathname The full path to the file.
/// @param mode The standard C file mode to open the file as.
///
/// @return Returns a pointer to an initialized FILE object on success, NULL on
/// failure.  errno is EPERM if the caller is below executive level.
FILE* filesystemFopenFullPath(const char *pathname, const char *mode) {
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if ((processDescriptor == NULL)
    || (processDescriptor->privilegeLevel > PRIVILEGE_LEVEL_EXECUTIVE)
  ) {
    errno = EPERM;
    return NULL;
  }

  return filesystemOpenFile(pathname, mode, FILESYSTEM_FULL_PATH);
}

/// @var _userspacePath
///
/// @brief The directory that every path a process names through the User API
/// is under.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _userspacePath[] KEEP_IN_FLASH = "/userspace";

/// @fn bool filesystemPathIsUserspace(const char *path)
///
/// @brief Determine whether a full, collapsed path is /userspace or below it.
///
/// @param path The path to check.
///
/// @return Returns true if the path is in /userspace, false if not.
bool filesystemPathIsUserspace(const char *path) {
  return (strncmp(path, _userspacePath, FILESYSTEM_USERSPACE_LENGTH) == 0)
    && ((path[FILESYSTEM_USERSPACE_LENGTH] == '/')
      || (path[FILESYSTEM_USERSPACE_LENGTH] == '\0'));
}

/// @fn size_t userspacePathCopy(char *destination, const char *path)
///
/// @brief Write the full path of a path that a process named.  The path is
/// collapsed before /userspace goes on the front so that it can't climb out.
///
/// @param destination Where to write the full path, which must have room for
///   FILESYSTEM_USERSPACE_LENGTH plus the length of path plus 2 bytes.
/// @param path The path the process named.
///
/// @return Returns the length of the full path.
size_t userspacePathCopy(char *destination, const char *path) {
  char *absolutePath = &destination[FILESYSTEM_USERSPACE_LENGTH];
  absolutePath[0] = '/';
  strcpy(&absolutePath[1], path);
  size_t length = pathCollapse(absolutePath);
  memcpy(destination, _userspacePath, FILESYSTEM_USERSPACE_LENGTH);
  if (length == 1) {
    absolutePath[0] = '\0';
    length = 0;
  }

  return FILESYSTEM_USERSPACE_LENGTH + length;
}

/// @fn char* userspacePathNew(const char *path)
///
/// @brief Allocate the full path of a path that a process named.
///
/// @param path The path the process named.
///
/// @return Returns the full path, which the caller must free, or NULL if
/// memory ran out.
char* userspacePathNew(const char *path) {
  char *fullPath
    = (char*) malloc(FILESYSTEM_USERSPACE_LENGTH + strlen(path) + 2);
  if (fullPath != NULL) {
    userspacePathCopy(fullPath, path);
  }

  return fullPath;
}

/// @fn int filesystemCloseFileDescriptor(int fd)
///
/// @brief Close one of the running process's file descriptors that holds an
/// open file.  The file itself is closed when its last descriptor is.
///
/// @param fd The file descriptor to close.
///
/// @return Returns 0 on success, sets errno to the appropriate value and
/// returns EOF on failure.
int filesystemCloseFileDescriptor(int fd) {
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor == NULL) {
    // This should be impossible, but check anyway.
    errno = EOTHER;
    return EOF;
  }

  if ((fd < 0) || (fd >= processDescriptor->numFileDescriptors)
    || (processDescriptor->fileDescriptors[fd] == NULL)
    || (processDescriptor->fileDescriptors[fd]->file == NULL)
  ) {
    errno = EBADF;
    return EOF;
  }

  FileDescriptor *fileDescriptor = processDescriptor->fileDescriptors[fd];
  processDescriptor->fileDescriptors[fd] = NULL;
  fileDescriptor->refCount--;
  if (fileDescriptor->refCount > 0) {
    return 0;
  }

  int returnValue = 0;
  MESSAGE_ARGS_ALLOCATE(FilesystemFcloseArgs, fcloseArgs);
  if (fcloseArgs == NULL) {
    errno = ENOMEM;
    return EOF;
  }
  fcloseArgs->stream = fileDescriptor->file;
  fcloseArgs->returnValue = 0;

  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_CLOSE_FILE,
    fcloseArgs, sizeof(*fcloseArgs), true);
  processMessageWaitForDone(msg, NULL);

  if (fcloseArgs->returnValue != 0) {
    errno = -fcloseArgs->returnValue;
    returnValue = EOF;
  } else if (processDescriptor->numFilesystemHandles > 0) {
    processDescriptor->numFilesystemHandles--;
  }

  processMessageRelease(msg);
  MESSAGE_ARGS_FREE(fcloseArgs);
  free(fileDescriptor);

  return returnValue;
}

/// @fn int filesystemFclose(FILE *stream)
///
/// @brief Implementation of the standard C fclose call.
///
/// @param stream A pointer to a previously-opened FILE object.
///
/// @return Returns 0 on success, sets errno to the appropriate value and
/// returns EOF on failure.
int filesystemFclose(FILE *stream) {
  if (stream == NULL) {
    errno = EBADF;
    return EOF;
  }

  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor == NULL) {
    // This should be impossible, but check anyway.
    errno = EOTHER;
    return EOF;
  }

  FileDescriptor *fileDescriptor = filesystemStreamFileDescriptor(stream);
  if ((fileDescriptor == NULL)
    || (fileDescriptor->streamFd >= processDescriptor->numFileDescriptors)
    || (processDescriptor->fileDescriptors[fileDescriptor->streamFd]
      != fileDescriptor)
  ) {
    errno = EBADF;
    return EOF;
  }

  return filesystemCloseFileDescriptor(fileDescriptor->streamFd);
}

/// @fn FileDescriptor* filesystemStreamFileDescriptor(FILE *stream)
///
/// @brief Find the running process's FileDescriptor for an open stream.
///
/// @param stream The stream.
///
/// @return Returns the FileDescriptor, or NULL if no file descriptor of the
/// running process holds the stream.
FileDescriptor* filesystemStreamFileDescriptor(FILE *stream) {
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if ((stream == NULL) || (processDescriptor == NULL)) {
    return NULL;
  }

  for (uint8_t fd = 0; fd < processDescriptor->numFileDescriptors; fd++) {
    FileDescriptor *fileDescriptor = processDescriptor->fileDescriptors[fd];
    if ((fileDescriptor != NULL) && (fileDescriptor->file == stream)) {
      return fileDescriptor;
    }
  }

  return NULL;
}

/// @fn int filesystemRemove(const char *pathname)
///
/// @brief Implementation of the standard C remove call.
///
/// @param pathname The full pathname to the file.  NOTE:  This implementation
///   can only open files in the root directory.  Subdirectories are NOT
///   supported.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int filesystemRemove(const char *pathname) {
  int returnValue = 0;
  if ((pathname != NULL) && (*pathname != '\0')) {
    MESSAGE_ARGS_ALLOCATE(FilesystemRemoveArgs, filesystemRemoveArgs);
    if (filesystemRemoveArgs == NULL) {
      errno = ENOMEM;
      return -1;
    }
    char *pathnameCopy = (char*) malloc(strlen(pathname) + 1);
    if (pathnameCopy == NULL) {
      MESSAGE_ARGS_FREE(filesystemRemoveArgs);
      errno = ENOMEM;
      return -1;
    }
    strcpy(pathnameCopy, pathname);
    filesystemRemoveArgs->pathname = (char*) MESSAGE_BUFFER_SHARE(
      pathnameCopy, strlen(pathnameCopy) + 1, MESSAGE_BUFFER_IN,
      rootFilesystemPid);
    if (filesystemRemoveArgs->pathname == NULL) {
      free(pathnameCopy);
      MESSAGE_ARGS_FREE(filesystemRemoveArgs);
      errno = ENOMEM;
      return -1;
    }

    ProcessMessage *msg = initSendProcessMessageToPid(
      rootFilesystemPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_REMOVE_FILE,
      filesystemRemoveArgs, sizeof(*filesystemRemoveArgs), true);
    processMessageWaitForDone(msg, NULL);
    MESSAGE_BUFFER_UNSHARE(filesystemRemoveArgs->pathname, pathnameCopy,
      MESSAGE_BUFFER_IN);
    free(pathnameCopy);
    returnValue = filesystemRemoveArgs->returnValue;
    if (returnValue != 0) {
      // returnValue holds a negative errno.  Set errno for the current process
      // and return -1 like we're supposed to.
      errno = -returnValue;
      returnValue = -1;
    }
    processMessageRelease(msg);
    MESSAGE_ARGS_FREE(filesystemRemoveArgs);
  } else {
    errno = ENOENT;
    returnValue = -1;
  }
  return returnValue;
}

/// @fn static int filesystemRenamePaths(const char *oldpath,
///   const char *newpath, int pathFlag)
///
/// @brief Rename a file for the running process.  It isn't inlined so that it
/// stays in the Agon's flash, where its body has always been.
///
/// @param oldpath The path to the file to rename.
/// @param newpath The path the file is to have.
/// @param pathFlag FILESYSTEM_FULL_PATH if the paths are full paths on the
///   volume, 0 if they're paths under /userspace.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
static __attribute__((noinline)) int filesystemRenamePaths(
  const char *oldpath, const char *newpath, int pathFlag
) {
  if ((oldpath == NULL) || (*oldpath == '\0')
    || (newpath == NULL) || (*newpath == '\0')
  ) {
    errno = ENOENT;
    return -1;
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemRenameArgs, filesystemRenameArgs);
  if (filesystemRenameArgs == NULL) {
    errno = ENOMEM;
    return -1;
  }
  size_t oldpathSize = strlen(oldpath) + 1;
  size_t pathsSize = oldpathSize + strlen(newpath) + 1;
  char *paths = (char*) malloc(pathsSize);
  if (paths == NULL) {
    MESSAGE_ARGS_FREE(filesystemRenameArgs);
    errno = ENOMEM;
    return -1;
  }
  strcpy(paths, oldpath);
  strcpy(paths + oldpathSize, newpath);
  filesystemRenameArgs->oldpath = (char*) MESSAGE_BUFFER_SHARE(
    paths, pathsSize, MESSAGE_BUFFER_IN, rootFilesystemPid);
  if (filesystemRenameArgs->oldpath == NULL) {
    free(paths);
    MESSAGE_ARGS_FREE(filesystemRenameArgs);
    errno = ENOMEM;
    return -1;
  }
  filesystemRenameArgs->newpath = filesystemRenameArgs->oldpath + oldpathSize;

  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_RENAME_FILE | pathFlag,
    filesystemRenameArgs, sizeof(*filesystemRenameArgs), true);
  processMessageWaitForDone(msg, NULL);
  MESSAGE_BUFFER_UNSHARE(filesystemRenameArgs->oldpath, paths,
    MESSAGE_BUFFER_IN);
  free(paths);
  int returnValue = filesystemRenameArgs->returnValue;
  if (returnValue != 0) {
    // returnValue holds a negative errno.  Set errno for the current process
    // and return -1 like we're supposed to.
    errno = -returnValue;
    returnValue = -1;
  }
  processMessageRelease(msg);
  MESSAGE_ARGS_FREE(filesystemRenameArgs);

  return returnValue;
}

/// @fn int filesystemRename(const char *oldpath, const char *newpath)
///
/// @brief Implementation of the standard C rename call.  The paths are always
/// under /userspace.
///
/// @param oldpath The path to the file to rename.
/// @param newpath The path the file is to have.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int filesystemRename(const char *oldpath, const char *newpath) {
  return filesystemRenamePaths(oldpath, newpath, 0);
}

/// @fn static int filesystemSendPathCommand(
///   FilesystemCommandResponse command, const char *pathname)
///
/// @brief Send a filesystem command whose only argument is a path and wait
/// for its result.
///
/// @param command The FILESYSTEM_* command to send, with FILESYSTEM_FULL_PATH
///   or'd in if pathname is the full path on the volume.
/// @param pathname The path to operate on.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
static int filesystemSendPathCommand(
  FilesystemCommandResponse command, const char *pathname
) {
  if ((pathname == NULL) || (*pathname == '\0')) {
    errno = ENOENT;
    return -1;
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemPathArgs, filesystemPathArgs);
  if (filesystemPathArgs == NULL) {
    errno = ENOMEM;
    return -1;
  }
  char *pathnameCopy = (char*) malloc(strlen(pathname) + 1);
  if (pathnameCopy == NULL) {
    MESSAGE_ARGS_FREE(filesystemPathArgs);
    errno = ENOMEM;
    return -1;
  }
  strcpy(pathnameCopy, pathname);
  filesystemPathArgs->pathname = (char*) MESSAGE_BUFFER_SHARE(
    pathnameCopy, strlen(pathnameCopy) + 1, MESSAGE_BUFFER_IN,
    rootFilesystemPid);
  if (filesystemPathArgs->pathname == NULL) {
    free(pathnameCopy);
    MESSAGE_ARGS_FREE(filesystemPathArgs);
    errno = ENOMEM;
    return -1;
  }
  filesystemPathArgs->now = time(NULL);

  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid, FILESYSTEM_COMMAND_SIGNATURE | command,
    filesystemPathArgs, sizeof(*filesystemPathArgs), true);
  processMessageWaitForDone(msg, NULL);
  MESSAGE_BUFFER_UNSHARE(filesystemPathArgs->pathname, pathnameCopy,
    MESSAGE_BUFFER_IN);
  free(pathnameCopy);
  int returnValue = filesystemPathArgs->returnValue;
  if (returnValue != 0) {
    errno = -returnValue;
    returnValue = -1;
  }
  processMessageRelease(msg);
  MESSAGE_ARGS_FREE(filesystemPathArgs);

  return returnValue;
}

/// @fn int filesystemMkdir(const char *pathname, mode_t mode)
///
/// @brief Implementation of the standard POSIX mkdir call.
///
/// @param pathname The full pathname of the directory to create.
/// @param mode The permissions for the new directory.  Ignored, since the
///   filesystem does not store permissions.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int filesystemMkdir(const char *pathname, mode_t mode) {
  (void) mode;
  return filesystemSendPathCommand(FILESYSTEM_MKDIR, pathname);
}

/// @fn int filesystemRmdir(const char *pathname)
///
/// @brief Implementation of the standard POSIX rmdir call.
///
/// @param pathname The full pathname of the empty directory to remove.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int filesystemRmdir(const char *pathname) {
  return filesystemSendPathCommand(FILESYSTEM_RMDIR, pathname);
}

/// @fn static bool filesystemCallerIsExecutive(void)
///
/// @brief Determine whether the running process may name a full path on the
/// volume, and set errno to EPERM if not.
///
/// @return Returns true if the running process is at kernel or executive
/// level, false if not.
static bool filesystemCallerIsExecutive(void) {
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if ((processDescriptor == NULL)
    || (processDescriptor->privilegeLevel > PRIVILEGE_LEVEL_EXECUTIVE)
  ) {
    errno = EPERM;
    return false;
  }
  return true;
}

/// @fn int filesystemRenameFullPath(const char *oldpath, const char *newpath)
///
/// @brief Rename a file by full paths on the volume, for a kernel or executive
/// process.
///
/// @param oldpath The full path to the file to rename.
/// @param newpath The full path the file is to have.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
/// errno is EPERM if the caller is below executive level.
int filesystemRenameFullPath(const char *oldpath, const char *newpath) {
  if (!filesystemCallerIsExecutive()) {
    return -1;
  }
  return filesystemRenamePaths(oldpath, newpath, FILESYSTEM_FULL_PATH);
}

/// @fn int filesystemMkdirFullPath(const char *pathname, mode_t mode)
///
/// @brief Create a directory by its full path on the volume, for a kernel or
/// executive process.
///
/// @param pathname The full path of the directory to create.
/// @param mode The permissions for the new directory.  Ignored, since the
///   filesystem does not store permissions.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
/// errno is EPERM if the caller is below executive level.
int filesystemMkdirFullPath(const char *pathname, mode_t mode) {
  (void) mode;
  if (!filesystemCallerIsExecutive()) {
    return -1;
  }
  return filesystemSendPathCommand(
    (FilesystemCommandResponse) (FILESYSTEM_MKDIR | FILESYSTEM_FULL_PATH),
    pathname);
}

/// @fn int filesystemRmdirFullPath(const char *pathname)
///
/// @brief Remove an empty directory by its full path on the volume, for a
/// kernel or executive process.
///
/// @param pathname The full path of the directory to remove.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
/// errno is EPERM if the caller is below executive level.
int filesystemRmdirFullPath(const char *pathname) {
  if (!filesystemCallerIsExecutive()) {
    return -1;
  }
  return filesystemSendPathCommand(
    (FilesystemCommandResponse) (FILESYSTEM_RMDIR | FILESYSTEM_FULL_PATH),
    pathname);
}

/// @struct NanoOsDirStream
///
/// @brief A caller's open directory.
///
/// @param handle The filesystem's handle for the directory.
/// @param entry The buffer readdir copies entries into, which only grows.
/// @param entrySize The number of bytes at entry.
struct NanoOsDirStream {
  void          *handle;
  struct dirent *entry;
  size_t         entrySize;
};

/// @def DIR_STREAM_FIRST_ENTRY_SIZE
///
/// @brief The size of a new directory stream's entry buffer: room for an 8.3
/// name and its NUL.
#define DIR_STREAM_FIRST_ENTRY_SIZE (offsetof(struct dirent, d_name) + 13)

/// @fn static void* filesystemOpendirHandle(const char *pathname)
///
/// @brief Ask the filesystem to open a directory.
///
/// @param pathname The full pathname to the directory.
///
/// @return Returns the filesystem's handle for the directory on success, NULL
/// and sets the value of errno on failure.
static void* filesystemOpendirHandle(const char *pathname) {
  MESSAGE_ARGS_ALLOCATE(FilesystemOpendirArgs, filesystemOpendirArgs);
  if (filesystemOpendirArgs == NULL) {
    errno = ENOMEM;
    return NULL;
  }
  char *pathnameCopy = (char*) malloc(strlen(pathname) + 1);
  if (pathnameCopy == NULL) {
    MESSAGE_ARGS_FREE(filesystemOpendirArgs);
    errno = ENOMEM;
    return NULL;
  }
  strcpy(pathnameCopy, pathname);
  filesystemOpendirArgs->pathname = (char*) MESSAGE_BUFFER_SHARE(
    pathnameCopy, strlen(pathnameCopy) + 1, MESSAGE_BUFFER_IN,
    rootFilesystemPid);
  if (filesystemOpendirArgs->pathname == NULL) {
    free(pathnameCopy);
    MESSAGE_ARGS_FREE(filesystemOpendirArgs);
    errno = ENOMEM;
    return NULL;
  }

  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_OPEN_DIR,
    filesystemOpendirArgs, sizeof(*filesystemOpendirArgs), true);
  processMessageWaitForDone(msg, NULL);
  MESSAGE_BUFFER_UNSHARE(filesystemOpendirArgs->pathname, pathnameCopy,
    MESSAGE_BUFFER_IN);
  free(pathnameCopy);
  void *returnValue = filesystemOpendirArgs->returnValue;
  if (returnValue == NULL) {
    errno = filesystemOpendirArgs->errorNumber;
  } else {
    getRunningProcess()->numFilesystemHandles++;
  }
  processMessageRelease(msg);
  MESSAGE_ARGS_FREE(filesystemOpendirArgs);
  return returnValue;
}

/// @fn DIR* filesystemOpendir(const char *pathname)
///
/// @brief Implementation of the standard C opendir call.
///
/// @param pathname The full pathname to the directory.
///
/// @return Returns a pointer to an opened DIR object on success, NULL and
/// sets the value of errno on failure.
DIR* filesystemOpendir(const char *pathname) {
  if ((pathname == NULL) || (*pathname == '\0')) {
    errno = EINVAL;
    return NULL;
  }

  DIR *dirp = (DIR*) malloc(sizeof(*dirp));
  if (dirp == NULL) {
    errno = ENOMEM;
    return NULL;
  }

  dirp->entry = (struct dirent*) malloc(DIR_STREAM_FIRST_ENTRY_SIZE);
  dirp->entrySize = DIR_STREAM_FIRST_ENTRY_SIZE;
  dirp->handle = NULL;
  if (dirp->entry == NULL) {
    errno = ENOMEM;
  } else {
    dirp->handle = filesystemOpendirHandle(pathname);
  }

  if (dirp->handle == NULL) {
    free(dirp->entry);
    free(dirp);
    return NULL;
  }
  return dirp;
}

/// @fn static int filesystemReaddirExchange(DIR *dirp,
///   FilesystemReaddirArgs *filesystemReaddirArgs)
///
/// @brief Send one readdir for a directory stream and wait for the reply.
/// Having this out of line keeps the retry after growing the entry buffer
/// from duplicating the exchange.
///
/// @param dirp The directory stream.
/// @param filesystemReaddirArgs The arguments, with handle and repeat set.
///
/// @return Returns the errorNumber the filesystem reported, or ENOMEM.
static __attribute__((noinline)) int filesystemReaddirExchange(DIR *dirp,
  FilesystemReaddirArgs *filesystemReaddirArgs
) {
  filesystemReaddirArgs->entry = (struct dirent*) MESSAGE_BUFFER_SHARE(
    dirp->entry, dirp->entrySize, MESSAGE_BUFFER_OUT, rootFilesystemPid);
  if ((dirp->entry != NULL) && (filesystemReaddirArgs->entry == NULL)) {
    return ENOMEM;
  }
  filesystemReaddirArgs->entrySize = dirp->entrySize;
  filesystemReaddirArgs->errorNumber = 0;

  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_READ_DIR,
    filesystemReaddirArgs, sizeof(*filesystemReaddirArgs), true);
  processMessageWaitForDone(msg, NULL);
  processMessageRelease(msg);
  MESSAGE_BUFFER_UNSHARE(filesystemReaddirArgs->entry, dirp->entry,
    MESSAGE_BUFFER_OUT);

  return filesystemReaddirArgs->errorNumber;
}

/// @fn struct dirent* filesystemReaddir(DIR *dirp)
///
/// @brief Implementation of the standard C readdir call.
///
/// @param dirp A pointer to a previously-opened DIR object.
///
/// @return Returns a pointer to the next directory entry in dirp, or NULL at
/// the end of the directory or on failure.  The returned pointer is valid
/// until the next call to filesystemReaddir or filesystemClosedir on the same
/// dirp.  errno is left unchanged when NULL is returned because the
/// directory was simply exhausted; it is set to the appropriate value when
/// NULL is returned because of a real error.
struct dirent* filesystemReaddir(DIR *dirp) {
  if (dirp == NULL) {
    errno = EBADF;
    return NULL;
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemReaddirArgs, filesystemReaddirArgs);
  if (filesystemReaddirArgs == NULL) {
    errno = ENOMEM;
    return NULL;
  }
  filesystemReaddirArgs->handle = dirp->handle;
  filesystemReaddirArgs->repeat = false;

  int errorNumber = filesystemReaddirExchange(dirp, filesystemReaddirArgs);
  if (errorNumber == ERANGE) {
    struct dirent *entry = (struct dirent*) realloc(
      dirp->entry, filesystemReaddirArgs->entrySize);
    if (entry == NULL) {
      errorNumber = ENOMEM;
    } else {
      dirp->entry = entry;
      dirp->entrySize = filesystemReaddirArgs->entrySize;
      filesystemReaddirArgs->repeat = true;
      errorNumber = filesystemReaddirExchange(dirp, filesystemReaddirArgs);
    }
  }

  struct dirent *returnValue = NULL;
  if (errorNumber != 0) {
    errno = errorNumber;
  } else if (filesystemReaddirArgs->entrySize != 0) {
    returnValue = dirp->entry;
  }
  MESSAGE_ARGS_FREE(filesystemReaddirArgs);
  return returnValue;
}

/// @fn int filesystemClosedir(DIR *dirp)
///
/// @brief Implementation of the standard C closedir call.
///
/// @param dirp A pointer to a previously-opened DIR object.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int filesystemClosedir(DIR *dirp) {
  if (dirp == NULL) {
    errno = EBADF;
    return -1;
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemClosedirArgs, filesystemClosedirArgs);
  if (filesystemClosedirArgs == NULL) {
    errno = ENOMEM;
    return -1;
  }
  filesystemClosedirArgs->handle = dirp->handle;
  filesystemClosedirArgs->returnValue = 0;
  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_CLOSE_DIR,
    filesystemClosedirArgs, sizeof(*filesystemClosedirArgs), true);
  processMessageWaitForDone(msg, NULL);
  int returnValue = 0;
  if (filesystemClosedirArgs->returnValue != 0) {
    // The only way the driver can fail a closedir is a bad handle.
    errno = EBADF;
    returnValue = -1;
  } else if (getRunningProcess()->numFilesystemHandles > 0) {
    getRunningProcess()->numFilesystemHandles--;
  }
  processMessageRelease(msg);
  MESSAGE_ARGS_FREE(filesystemClosedirArgs);
  free(dirp->entry);
  free(dirp);
  return returnValue;
}

/// @fn int filesystemLstat(const char *pathname, struct stat *statbuf)
///
/// @brief Implementation of the standard C lstat call.
///
/// @param pathname The full pathname to the file.
/// @param statbuf A pointer to a caller-supplied struct stat to populate.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int filesystemLstat(const char *pathname, struct stat *statbuf) {
  if ((pathname == NULL) || (*pathname == '\0') || (statbuf == NULL)) {
    errno = EINVAL;
    return -1;
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemLstatArgs, filesystemLstatArgs);
  if (filesystemLstatArgs == NULL) {
    errno = ENOMEM;
    return -1;
  }
  char *pathnameCopy = (char*) malloc(strlen(pathname) + 1);
  if (pathnameCopy == NULL) {
    MESSAGE_ARGS_FREE(filesystemLstatArgs);
    errno = ENOMEM;
    return -1;
  }
  strcpy(pathnameCopy, pathname);
  filesystemLstatArgs->pathname = (char*) MESSAGE_BUFFER_SHARE(
    pathnameCopy, strlen(pathnameCopy) + 1, MESSAGE_BUFFER_IN,
    rootFilesystemPid);
  filesystemLstatArgs->statbuf = (struct stat*) MESSAGE_BUFFER_SHARE(
    statbuf, sizeof(*statbuf), MESSAGE_BUFFER_OUT, rootFilesystemPid);
  if ((filesystemLstatArgs->pathname == NULL)
    || (filesystemLstatArgs->statbuf == NULL)
  ) {
    MESSAGE_BUFFER_UNSHARE(filesystemLstatArgs->pathname, pathnameCopy,
      MESSAGE_BUFFER_IN);
    MESSAGE_BUFFER_UNSHARE(filesystemLstatArgs->statbuf, statbuf,
      MESSAGE_BUFFER_OUT);
    free(pathnameCopy);
    MESSAGE_ARGS_FREE(filesystemLstatArgs);
    errno = ENOMEM;
    return -1;
  }

  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_LSTAT,
    filesystemLstatArgs, sizeof(*filesystemLstatArgs), true);
  processMessageWaitForDone(msg, NULL);
  MESSAGE_BUFFER_UNSHARE(filesystemLstatArgs->pathname, pathnameCopy,
    MESSAGE_BUFFER_IN);
  free(pathnameCopy);
  MESSAGE_BUFFER_UNSHARE(filesystemLstatArgs->statbuf, statbuf,
    MESSAGE_BUFFER_OUT);
  int returnValue = filesystemLstatArgs->returnValue;
  if (returnValue != 0) {
    errno = filesystemLstatArgs->errorNumber;
    returnValue = -1;
  }
  processMessageRelease(msg);
  MESSAGE_ARGS_FREE(filesystemLstatArgs);
  return returnValue;
}

/// @fn int filesystemDirstat(const struct dirent *entry,
///   struct stat *statbuf)
///
/// @brief Populate a struct stat for the entry that readdir most recently
/// returned from a DIR.
///
/// @param entry The entry readdir returned.
/// @param statbuf A pointer to a caller-supplied struct stat to populate.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int filesystemDirstat(const struct dirent *entry, struct stat *statbuf) {
  if ((entry == NULL) || (statbuf == NULL)) {
    errno = EINVAL;
    return -1;
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemDirstatArgs, filesystemDirstatArgs);
  if (filesystemDirstatArgs == NULL) {
    errno = ENOMEM;
    return -1;
  }
  filesystemDirstatArgs->handle = entry->priv;
  filesystemDirstatArgs->statbuf = (struct stat*) MESSAGE_BUFFER_SHARE(
    statbuf, sizeof(*statbuf), MESSAGE_BUFFER_OUT, rootFilesystemPid);
  if (filesystemDirstatArgs->statbuf == NULL) {
    MESSAGE_ARGS_FREE(filesystemDirstatArgs);
    errno = ENOMEM;
    return -1;
  }
  filesystemDirstatArgs->returnValue = 0;
  filesystemDirstatArgs->errorNumber = 0;

  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_DIRSTAT,
    filesystemDirstatArgs, sizeof(*filesystemDirstatArgs), true);
  processMessageWaitForDone(msg, NULL);
  MESSAGE_BUFFER_UNSHARE(filesystemDirstatArgs->statbuf, statbuf,
    MESSAGE_BUFFER_OUT);
  int returnValue = filesystemDirstatArgs->returnValue;
  if (returnValue != 0) {
    errno = filesystemDirstatArgs->errorNumber;
    returnValue = -1;
  }
  processMessageRelease(msg);
  MESSAGE_ARGS_FREE(filesystemDirstatArgs);
  return returnValue;
}

/// @fn int fat32Format(const char *volumeLabel, uint32_t clusterSize)
///
/// @brief Format the root filesystem's partition as FAT32.
///
/// @note Named for FAT32, not "filesystemFormat": unlike fopen/fread/fwrite,
/// a format call's parameters are inherently specific to the filesystem type
/// being written, so there is no filesystem-agnostic name for it to share.
///
/// @param volumeLabel A string containing the volume label to write, up to 11
///   characters.  May be NULL or empty for no label.
/// @param clusterSize The desired cluster size in bytes, or 0 to use a
///   default derived from the size of the partition.
///
/// @return Returns 0 on success, -1 and sets the value of errno on failure.
int fat32Format(const char *volumeLabel, uint32_t clusterSize) {
  int returnValue = 0;
  MESSAGE_ARGS_ALLOCATE(Fat32FormatArgs, fat32FormatArgs);
  if (fat32FormatArgs == NULL) {
    errno = ENOMEM;
    return -1;
  }
  fat32FormatArgs->signature = FAT32_FORMAT_SIGNATURE;
  fat32FormatArgs->clusterSize = clusterSize;
  char *volumeLabelCopy = NULL;
  if ((volumeLabel != NULL) && (*volumeLabel != '\0')) {
    volumeLabelCopy = (char*) malloc(strlen(volumeLabel) + 1);
    if (volumeLabelCopy == NULL) {
      MESSAGE_ARGS_FREE(fat32FormatArgs);
      errno = ENOMEM;
      return -1;
    }
    strcpy(volumeLabelCopy, volumeLabel);
    fat32FormatArgs->volumeLabel = (char*) MESSAGE_BUFFER_SHARE(
      volumeLabelCopy, strlen(volumeLabelCopy) + 1, MESSAGE_BUFFER_IN,
      rootFilesystemPid);
    if (fat32FormatArgs->volumeLabel == NULL) {
      free(volumeLabelCopy);
      MESSAGE_ARGS_FREE(fat32FormatArgs);
      errno = ENOMEM;
      return -1;
    }
  }

  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_FORMAT,
    fat32FormatArgs, sizeof(*fat32FormatArgs), true);
  processMessageWaitForDone(msg, NULL);
  MESSAGE_BUFFER_UNSHARE(fat32FormatArgs->volumeLabel, volumeLabelCopy,
    MESSAGE_BUFFER_IN);
  free(volumeLabelCopy);
  if (fat32FormatArgs->returnValue != 0) {
    // returnValue holds a FAT32 driver error code, not an errno value; there
    // is no clean mapping between the two, so report a generic I/O error.
    errno = EIO;
    returnValue = -1;
  }
  processMessageRelease(msg);
  MESSAGE_ARGS_FREE(fat32FormatArgs);
  return returnValue;
}

/// @fn int filesystemFSeek(FILE *stream, long offset, int whence)
///
/// @brief Implementation of the standard C fseek call.
///
/// @param stream A pointer to a previously-opened FILE object.
/// @param offset A signed integer value that will be added to the specified
///   position.
/// @param whence The location within the file to apply the offset to.  Valid
///   values are SEEK_SET (the beginning of the file), SEEK_CUR (the current
///   file positon), and SEEK_END (the end of the file).
///
/// @return Returns 0 on success, -1 on failure.
int filesystemFSeek(FILE *stream, long offset, int whence) {
  return (filesystemSeekAndTell(stream, offset, whence) < 0) ? -1 : 0;
}

/// @fn long filesystemSeekAndTell(FILE *stream, long offset, int whence)
///
/// @brief Move the position within an open FILE stream and report where it
/// ends up, for fseek and ftell.
///
/// @param stream A pointer to a previously-opened FILE.
/// @param offset A signed integer value that will be added to the specified
///   position.
/// @param whence The location within the file to apply the offset to.
///
/// @return Returns the new position on success, -1 on failure.
long filesystemSeekAndTell(FILE *stream, long offset, int whence) {
  if (stream == NULL) {
    return -1;
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemSeekArgs, filesystemSeekArgs);
  if (filesystemSeekArgs == NULL) {
    return -1;
  }
  filesystemSeekArgs->stream = stream;
  filesystemSeekArgs->offset = offset;
  filesystemSeekArgs->whence = whence;
  filesystemSeekArgs->position = -1;
  filesystemSeekArgs->returnValue = 0;
  filesystemSeekArgs->errorNumber = 0;
  ProcessMessage *msg = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_SEEK_FILE,
    filesystemSeekArgs, sizeof(*filesystemSeekArgs), true);
  processMessageWaitForDone(msg, NULL);
  long position = (filesystemSeekArgs->returnValue == 0)
    ? filesystemSeekArgs->position : -1;
  errno = filesystemSeekArgs->errorNumber;
  processMessageRelease(msg);
  MESSAGE_ARGS_FREE(filesystemSeekArgs);
  return position;
}

/// @fn size_t filesystemFRead(
///   void *ptr, size_t size, size_t nmemb, FILE *stream)
///
/// @brief Read data from a previously-opened file.
///
/// @param ptr A pointer to the memory to read data into.
/// @param size The size, in bytes, of each element that is to be read from the
///   file.
/// @param nmemb The number of elements that are to be read from the file.
/// @param stream A pointer to the previously-opened file.
///
/// @return Returns the total number of objects successfully read from the
/// file.
size_t filesystemFRead(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  size_t returnValue = 0;
  if ((ptr == NULL) || (size == 0) || (nmemb == 0) || (stream == NULL)) {
    // Nothing to do.
    return returnValue; // 0
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemIoCommandArgs, filesystemIoCommandArgs);
  if (filesystemIoCommandArgs == NULL) {
    return returnValue; // 0
  }
  filesystemIoCommandArgs->file = stream;
  filesystemIoCommandArgs->buffer = MESSAGE_BUFFER_SHARE(
    ptr, size * nmemb, MESSAGE_BUFFER_INOUT, rootFilesystemPid);
  if (filesystemIoCommandArgs->buffer == NULL) {
    MESSAGE_ARGS_FREE(filesystemIoCommandArgs);
    errno = ENOMEM;
    return returnValue; // 0
  }
  filesystemIoCommandArgs->length = (uint32_t) (size * nmemb);

  logDebug("Sending message to filesystem process to read %ld "
    "elements %ld bytes in size from file 0x%lx into address 0x%lx\n",
    (long int) nmemb, (long int) size,
    (unsigned long int) (uintptr_t) stream,
    (unsigned long int) (uintptr_t) ptr);

  ProcessMessage *processMessage = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_READ_FILE,
    /* data= */ filesystemIoCommandArgs,
    /* size= */ sizeof(*filesystemIoCommandArgs),
    true);
  processMessageWaitForDone(processMessage, NULL);
  MESSAGE_BUFFER_UNSHARE(filesystemIoCommandArgs->buffer, ptr,
    MESSAGE_BUFFER_INOUT);
  returnValue = (filesystemIoCommandArgs->length / size);
  if (filesystemIoCommandArgs->errorNumber != 0) {
    errno = filesystemIoCommandArgs->errorNumber;
  }
  processMessageRelease(processMessage);
  MESSAGE_ARGS_FREE(filesystemIoCommandArgs);

  logDebug("Returning %ld from read of file 0x%lx into address 0x%lx\n",
    (long int) returnValue,
    (unsigned long int) (uintptr_t) stream,
    (unsigned long int) (uintptr_t) ptr);
  return returnValue;
}

/// @fn size_t filesystemFWrite(
///   const void *ptr, size_t size, size_t nmemb, FILE *stream)
///
/// @brief Write data to a previously-opened file.
///
/// @param ptr A pointer to the memory to write data from.
/// @param size The size, in bytes, of each element that is to be written to
///   the file.
/// @param nmemb The number of elements that are to be written to the file.
/// @param stream A pointer to the previously-opened file.
///
/// @return Returns the total number of objects successfully written to the
/// file.
size_t filesystemFWrite(
  const void *ptr, size_t size, size_t nmemb, FILE *stream
) {
  size_t returnValue = 0;
  if ((ptr == NULL) || (size == 0) || (nmemb == 0) || (stream == NULL)) {
    // Nothing to do.
    return returnValue; // 0
  }

  MESSAGE_ARGS_ALLOCATE(FilesystemIoCommandArgs, filesystemIoCommandArgs);
  if (filesystemIoCommandArgs == NULL) {
    return returnValue; // 0
  }
  filesystemIoCommandArgs->file = stream;
  filesystemIoCommandArgs->buffer = MESSAGE_BUFFER_SHARE(
    (void*) ptr, size * nmemb, MESSAGE_BUFFER_IN, rootFilesystemPid);
  if (filesystemIoCommandArgs->buffer == NULL) {
    MESSAGE_ARGS_FREE(filesystemIoCommandArgs);
    errno = ENOMEM;
    return returnValue; // 0
  }
  filesystemIoCommandArgs->length = (uint32_t) (size * nmemb);
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    rootFilesystemPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_WRITE_FILE,
    /* data= */ filesystemIoCommandArgs,
    /* size= */ sizeof(*filesystemIoCommandArgs),
    true);
  processMessageWaitForDone(processMessage, NULL);
  MESSAGE_BUFFER_UNSHARE(filesystemIoCommandArgs->buffer, (void*) ptr,
    MESSAGE_BUFFER_IN);
  returnValue = (filesystemIoCommandArgs->length / size);
  if (filesystemIoCommandArgs->errorNumber != 0) {
    errno = filesystemIoCommandArgs->errorNumber;
  }
  processMessageRelease(processMessage);
  MESSAGE_ARGS_FREE(filesystemIoCommandArgs);

  return returnValue;
}

/// @fn int getFileBlockMetadataFromFile(FILE *stream,
///   FileBlockMetadata *metadata)
///
/// @brief Get the block-level metadata for a given file.
///
/// @param stream A pointer to a previously-opened FILE.
/// @param metadata A pointer to a FileBlockMetadata structure the caller wants
///   populated.
///
/// @return Returns 0 on success, -errno on failure.
int getFileBlockMetadataFromFile(FILE *stream, FileBlockMetadata *metadata) {
  if ((stream == NULL) || (metadata == NULL)) {
    return -EINVAL;
  }

  MESSAGE_ARGS_ALLOCATE(GetFileBlockMetadataArgs, args);
  if (args == NULL) {
    return -ENOMEM;
  }
  args->stream = stream;
  args->metadata = (FileBlockMetadata*) MESSAGE_BUFFER_SHARE(
    metadata, sizeof(*metadata), MESSAGE_BUFFER_OUT, rootFilesystemPid);
  if (args->metadata == NULL) {
    MESSAGE_ARGS_FREE(args);
    return -ENOMEM;
  }

  ProcessMessage *processMessage = getAvailableMessage();
  for (int ii = 0;
    (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
    ii++
  ) {
    processYield();
    processMessage = getAvailableMessage();
  }
  if (processMessage == NULL) {
    logError("ERROR: Out of process messages\n");
    MESSAGE_BUFFER_UNSHARE(args->metadata, metadata, MESSAGE_BUFFER_OUT);
    MESSAGE_ARGS_FREE(args);
    return -ENOMEM;
  }

  processMessageInit(processMessage,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_GET_FILE_BLOCK_METADATA,
    args, sizeof(*args), true);
  if (sendProcessMessageToPid(rootFilesystemPid, processMessage)
    != processSuccess
  ) {
    logError("Failed to send message to filesystem to get file "
      "block metadata\n");
    processMessageRelease(processMessage);
    MESSAGE_BUFFER_UNSHARE(args->metadata, metadata, MESSAGE_BUFFER_OUT);
    MESSAGE_ARGS_FREE(args);
    return -EIO;
  }
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);
  MESSAGE_BUFFER_UNSHARE(args->metadata, metadata, MESSAGE_BUFFER_OUT);
  int returnValue = -args->errorNumber;
  MESSAGE_ARGS_FREE(args);

  return returnValue;
}

/// @var _overlayOpenMode
///
/// @brief fopen() mode string the filesystem takes as a request to run the
/// file, which needs execute rather than read permission.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _overlayOpenMode[] KEEP_IN_FLASH = "rx";

/// @fn int getFileBlockMetadataFromPath(const char *path,
///   FileBlockMetadata *metadata)
///
/// @brief Get the block-level metadata for a given path.
///
/// @param path A string representing a path to a file on the filesystem.
/// @param metadata A pointer to a FileBlockMetadata structure the caller wants
///   populated.
///
/// @return Returns 0 on success, -errno on failure.
int getFileBlockMetadataFromPath(const char *path,
  FileBlockMetadata *metadata
) {
  if ((path == NULL) || (metadata == NULL)) {
    return -EINVAL;
  }

  // A process below executive level only ever runs overlays under /userspace,
  // and the filesystem would ignore a full path from it.
  bool underUserspace = filesystemPathIsUserspace(path);
  FILE *stream = filesystemOpenFile(
    underUserspace ? &path[FILESYSTEM_USERSPACE_LENGTH] : path,
    _overlayOpenMode,
    underUserspace ? 0 : FILESYSTEM_FULL_PATH);
  if (stream == NULL) {
    int errorNumber = (errno != 0) ? errno : EIO;
    logError("Could not open file \"%s\"\n", path);
    return -errorNumber;
  }
  int returnValue = getFileBlockMetadataFromFile(stream, metadata);
  fclose(stream); stream = NULL;

  return returnValue;
}

/// @fn int filesystemEndOfFile(FILE *stream)
///
/// @brief Determine whether or not an open FILE is positioned at its end.
///
/// @param stream A pointer to a previously-opened FILE.
///
/// @return Returns 0 if the provided FILE is not positioned at its end, nonzero
/// if it is.
int filesystemEndOfFile(FILE *stream) {
  MESSAGE_ARGS_ALLOCATE(FeofArgs, feofArgs);
  if (feofArgs == NULL) {
    return 0;
  }
  feofArgs->stream = stream;
  feofArgs->returnValue = 0;
  feofArgs->errorNumber = 0;

  if (stream != NULL) {
    ProcessMessage *processMessage = initSendProcessMessageToPid(
      rootFilesystemPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_END_OF_FILE,
      feofArgs, sizeof(*feofArgs), true);
    processMessageWaitForDone(processMessage, NULL);
    if (feofArgs->errorNumber != 0) {
      errno = feofArgs->errorNumber;
    }
    processMessageRelease(processMessage);
  }

  int returnValue = feofArgs->returnValue;
  MESSAGE_ARGS_FREE(feofArgs);
  return returnValue;
}

/// @fn long filesystemFtell(FILE *stream)
///
/// @brief Get the current position within an open FILE stream.
///
/// @param stream A pointer to a previously-opened FILE.
///
/// @return Returns the current offeset into the provided stream on success,
/// -1 on failure.
long filesystemFtell(FILE *stream) {
  return filesystemSeekAndTell(stream, 0, SEEK_CUR);
}

