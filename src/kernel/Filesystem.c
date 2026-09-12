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
// usr/src/filesystems/drivers/common/GetPartitionInfoImpl.c into their own
// source trees; the kernel-linked build here pulls it in with #include
// instead, since Arduino IDE only discovers source files physically under
// src/kernel.
#define NANO_OS_KERNEL_BUILD
#include "../../usr/src/filesystems/drivers/common/GetPartitionInfoImpl.c"

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
        (FilesystemCommandResponse) (processMessageType(msg) & 0xff);
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
  logDebug("runFilesystem: Initialization complete\n");

  handleFilesystemMessages(&fs);
  return NULL;
}

/// @fn FILE* filesystemFopen(const char *pathname, const char *mode)
///
/// @brief Implementation of the standard C fopen call.
///
/// @param pathname The full pathname to the file.  NOTE:  This implementation
///   can only open files in the root directory.  Subdirectories are NOT
///   supported.
/// @param mode The standard C file mode to open the file as.
///
/// @return Returns a pointer to an initialized FILE object on success, NULL on
/// failure.
FILE* filesystemFopen(const char *pathname, const char *mode) {
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
  void *check = realloc(processDescriptor->fileDescriptors,
    (numFileDescriptors + 1) * sizeof(FileDescriptor*));
  if (check == NULL) {
    errno = ENOMEM;
    goto exit; // return NULL
  }
  processDescriptor->fileDescriptors = (FileDescriptor**) check;
  processDescriptor->fileDescriptors[numFileDescriptors]
    = (FileDescriptor*) calloc(1, sizeof(FileDescriptor));
  if (processDescriptor->fileDescriptors[numFileDescriptors] == NULL) {
    errno = ENOMEM;
    goto freeFileDescriptor;
  }

  FilesystemFopenArgs fopenArgs;
  memset(&fopenArgs, 0, sizeof(fopenArgs));
  fopenArgs.pathname = (char*) malloc(strlen(pathname) + 1);
  if (fopenArgs.pathname == NULL) {
    goto freeFileDescriptor;
  }
  strcpy(fopenArgs.pathname, pathname);

  fopenArgs.mode = (char*) malloc(strlen(mode) + 1);
  if (fopenArgs.mode == NULL) {
    goto freePathname;
  }
  strcpy(fopenArgs.mode, mode);

  fopenArgs.fd = numFileDescriptors;

  ProcessMessage *msg = initSendProcessMessageToPid(
    SCHEDULER_STATE->rootFsPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_OPEN_FILE,
    &fopenArgs, sizeof(fopenArgs), true);
  processMessageWaitForDone(msg, NULL);
  free(fopenArgs.mode);
  free(fopenArgs.pathname);

  file = fopenArgs.returnValue;
  processMessageRelease(msg);
  if (file == NULL) {
    goto freeFileDescriptor;
  }

  processDescriptor->fileDescriptors[numFileDescriptors]->file = file;
  processDescriptor->fileDescriptors[numFileDescriptors]->refCount = 1;
  processDescriptor->numFileDescriptors = numFileDescriptors + 1;

  goto exit;

freePathname:
  free(fopenArgs.pathname);

freeFileDescriptor:
  free(processDescriptor->fileDescriptors[numFileDescriptors]);
  processDescriptor->fileDescriptors[numFileDescriptors] = NULL;
  // We need to shrink the fileDescriptors array back down to its original size.
  // Since we're reducing the amount of memory consumed, this is guaranteed to
  // be successful and not relocate the pointer, so no need to do anything with
  // the return value.
  realloc(processDescriptor->fileDescriptors,
    numFileDescriptors * sizeof(FileDescriptor*));

exit:
  return file;
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
  int returnValue = 0;

  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor == NULL) {
    // This should be impossible, but check anyway.
    errno = EOTHER;
    returnValue = EOF;
    goto exit; // return NULL
  }

  if (stream == NULL) {
    errno = EBADF;
    returnValue = EOF;
    goto exit;
  }

  if (stream->fd >= processDescriptor->numFileDescriptors) {
    errno = EBADF;
    returnValue = EOF;
    goto exit;
  }

  FileDescriptor *fileDescriptor
    = processDescriptor->fileDescriptors[stream->fd];
  if (fileDescriptor == NULL) {
    errno = EBADF;
    returnValue = EOF;
    goto exit;
  }

  fileDescriptor->refCount--;
  if (fileDescriptor->refCount == 0) {
    int fd = stream->fd;

    FilesystemFcloseArgs fcloseArgs;
    fcloseArgs.stream = stream;
    fcloseArgs.returnValue = 0;

    ProcessMessage *msg = initSendProcessMessageToPid(
      SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_CLOSE_FILE,
      &fcloseArgs, sizeof(fcloseArgs), true);
    processMessageWaitForDone(msg, NULL);

    if (fcloseArgs.returnValue != 0) {
      errno = -fcloseArgs.returnValue;
      returnValue = EOF;
    }

    processMessageRelease(msg);

    free(fileDescriptor); fileDescriptor = NULL;
    processDescriptor->fileDescriptors[fd] = NULL;
  }

exit:
  return returnValue;
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
    FilesystemRemoveArgs filesystemRemoveArgs;
    memset(&filesystemRemoveArgs, 0, sizeof(filesystemRemoveArgs));
    filesystemRemoveArgs.pathname = (char*) malloc(strlen(pathname) + 1);
    if (filesystemRemoveArgs.pathname == NULL) {
      errno = ENOMEM;
      return -1;
    }
    strcpy(filesystemRemoveArgs.pathname, pathname);

    ProcessMessage *msg = initSendProcessMessageToPid(
      SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_REMOVE_FILE,
      &filesystemRemoveArgs, sizeof(filesystemRemoveArgs), true);
    processMessageWaitForDone(msg, NULL);
    free(filesystemRemoveArgs.pathname); filesystemRemoveArgs.pathname = NULL;
    returnValue = filesystemRemoveArgs.returnValue;
    if (returnValue != 0) {
      // returnValue holds a negative errno.  Set errno for the current process
      // and return -1 like we're supposed to.
      errno = -returnValue;
      returnValue = -1;
    }
    processMessageRelease(msg);
  }
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
  Fat32FormatArgs fat32FormatArgs;
  memset(&fat32FormatArgs, 0, sizeof(fat32FormatArgs));
  fat32FormatArgs.signature = FAT32_FORMAT_SIGNATURE;
  fat32FormatArgs.clusterSize = clusterSize;
  if ((volumeLabel != NULL) && (*volumeLabel != '\0')) {
    fat32FormatArgs.volumeLabel = (char*) malloc(strlen(volumeLabel) + 1);
    if (fat32FormatArgs.volumeLabel == NULL) {
      errno = ENOMEM;
      return -1;
    }
    strcpy(fat32FormatArgs.volumeLabel, volumeLabel);
  }

  ProcessMessage *msg = initSendProcessMessageToPid(
    SCHEDULER_STATE->rootFsPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_FORMAT,
    &fat32FormatArgs, sizeof(fat32FormatArgs), true);
  processMessageWaitForDone(msg, NULL);
  free(fat32FormatArgs.volumeLabel); fat32FormatArgs.volumeLabel = NULL;
  if (fat32FormatArgs.returnValue != 0) {
    // returnValue holds a FAT32 driver error code, not an errno value; there
    // is no clean mapping between the two, so report a generic I/O error.
    errno = EIO;
    returnValue = -1;
  }
  processMessageRelease(msg);
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
  if (stream == NULL) {
    return -1;
  }

  FilesystemSeekArgs filesystemSeekArgs = {
    .stream = stream,
    .offset = offset,
    .whence = whence,
    .returnValue = 0,
    .errorNumber = 0,
  };
  ProcessMessage *msg = initSendProcessMessageToPid(
    SCHEDULER_STATE->rootFsPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_SEEK_FILE,
    &filesystemSeekArgs, sizeof(filesystemSeekArgs), true);
  processMessageWaitForDone(msg, NULL);
  int returnValue = filesystemSeekArgs.returnValue;
  errno = filesystemSeekArgs.errorNumber;
  processMessageRelease(msg);
  return returnValue;
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

  FilesystemIoCommandArgs filesystemIoCommandArgs = {
    .file = stream,
    .buffer = ptr,
    .length = (uint32_t) (size * nmemb)
  };

  logDebug("Sending message to filesystem process to read %ld "
    "elements %ld bytes in size from file 0x%lx into address 0x%lx\n",
    (long int) nmemb, (long int) size,
    (unsigned long int) (uintptr_t) stream,
    (unsigned long int) (uintptr_t) ptr);

  ProcessMessage *processMessage = initSendProcessMessageToPid(
    SCHEDULER_STATE->rootFsPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_READ_FILE,
    /* data= */ &filesystemIoCommandArgs,
    /* size= */ sizeof(filesystemIoCommandArgs),
    true);
  processMessageWaitForDone(processMessage, NULL);
  returnValue = (filesystemIoCommandArgs.length / size);
  processMessageRelease(processMessage);

  logDebug("Returning %ld from read of file 0x%lx into address 0x%lx\n",
    (long int) returnValue,
    (unsigned long int) (uintptr_t) filesystemIoCommandArgs.file,
    (unsigned long int) (uintptr_t) filesystemIoCommandArgs.buffer);
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

  FilesystemIoCommandArgs filesystemIoCommandArgs = {
    .file = stream,
    .buffer = (void*) ptr,
    .length = (uint32_t) (size * nmemb)
  };
  ProcessMessage *processMessage = initSendProcessMessageToPid(
    SCHEDULER_STATE->rootFsPid,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_WRITE_FILE,
    /* data= */ &filesystemIoCommandArgs,
    /* size= */ sizeof(filesystemIoCommandArgs),
    true);
  processMessageWaitForDone(processMessage, NULL);
  returnValue = (filesystemIoCommandArgs.length / size);
  processMessageRelease(processMessage);

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

  GetFileBlockMetadataArgs args = {
    .stream = stream,
    .metadata = metadata,
  };

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
    return -ENOMEM;
  }

  processMessageInit(processMessage,
    FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_GET_FILE_BLOCK_METADATA,
    &args, sizeof(args), true);
  if (sendProcessMessageToPid(SCHEDULER_STATE->rootFsPid, processMessage)
    != processSuccess
  ) {
    logError("Failed to send message to filesystem to get file "
      "block metadata\n");
    processMessageRelease(processMessage);
    return -EIO;
  }
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

  return 0;
}

/// @var _readMode
///
/// @brief fopen() mode string used to open a file for reading.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _readMode[] KEEP_IN_FLASH = "r";

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

  FILE *stream = fopen(path, _readMode);
  if (stream == NULL) {
    logError("Could not open file \"%s\"\n", path);
    return -EIO;
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
  FeofArgs feofArgs = {
    .stream = stream,
    .returnValue = 0,
  };

  if (stream != NULL) {
    ProcessMessage *processMessage = initSendProcessMessageToPid(
      SCHEDULER_STATE->rootFsPid,
      FILESYSTEM_COMMAND_SIGNATURE | FILESYSTEM_END_OF_FILE,
      &feofArgs, sizeof(feofArgs), true);
    processMessageWaitForDone(processMessage, NULL);
    processMessageRelease(processMessage);
  }

  return feofArgs.returnValue;
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
  long returnValue = -1;

  if (stream != NULL) {
    returnValue = (long) stream->currentPosition;
  }

  return returnValue;
}

