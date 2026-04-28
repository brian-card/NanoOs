///////////////////////////////////////////////////////////////////////////////
///
/// @file              Filesystem.cpp
///
/// @brief             Common filesystem functionality.
///
///////////////////////////////////////////////////////////////////////////////

#include "../user/NanoOsLibC.h"
#include "../user/NanoOsStdio.h"
#include "Filesystem.h"
#include "NanoOs.h"
#include "Scheduler.h"
#include "Tasks.h"

// Partition table constants
#define PARTITION_TABLE_OFFSET 0x1BE
#define PARTITION_ENTRY_SIZE 16

#define PARTITION_TYPE_NTFS_EXFAT 0x07
#define PARTITION_TYPE_FAT16_LBA 0x0E
#define PARTITION_TYPE_FAT16_LBA_EXTENDED 0x1E
#define PARTITION_TYPE_LINUX 0x83

#define PARTITION_LBA_OFFSET 8
#define PARTITION_SECTORS_OFFSET 12

/// @typedef FilesystemCommandHandler
///
/// @brief Definition of a filesystem command handler function.
typedef int (*FilesystemCommandHandler)(FilesystemState*, TaskMessage*);

/// @fn int filesystemOpenFileCommandHandler(
///   FilesystemState *filesystemState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_OPEN_FILE command.
///
/// @param filesystemState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int filesystemOpenFileCommandHandler(
  FilesystemState *filesystemState, TaskMessage *taskMessage
) {
  NanoOsFile *nanoOsFile = NULL;
  FilesystemFopenParameters *fopenParameters
    = (FilesystemFopenParameters*) taskMessageData(taskMessage);

  printDebugString("Opening file \"");
  printDebugString(pathname);
  printDebugString("\" in mode \"");
  printDebugString(mode);
  printDebugString("\"\n");

  if (filesystemState->driverState != NULL) {
    void *fileHandle = filesystemState->driverOpenFile(
      filesystemState->driverState,
      fopenParameters->pathname, fopenParameters->mode);
    if (fileHandle != NULL) {
      nanoOsFile = (NanoOsFile*) malloc(sizeof(NanoOsFile));
      if (nanoOsFile != NULL) {
        nanoOsFile->file = fileHandle;
        nanoOsFile->currentPosition = 0;
        nanoOsFile->fd = filesystemState->numOpenFiles + 3;
        nanoOsFile->owner = taskId(taskMessageFrom(taskMessage));
        filesystemState->numOpenFiles++;

        nanoOsFile->next = filesystemState->openFiles;
        nanoOsFile->prev = NULL;
        filesystemState->openFiles = nanoOsFile;
      } else {
        filesystemState->driverFclose(filesystemState->driverState, fileHandle);
      }
    } else {
      printString("ERROR: driverOpenFile returned NULL\n");
    }
  } else {
    printString("ERROR: driverState is not valid!\n");
  }

  taskMessageData(taskMessage) = nanoOsFile;
  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int filesystemCloseFileCommandHandler(
///   FilesystemState *filesystemState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_CLOSE_FILE command.
///
/// @param filesystemState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int filesystemCloseFileCommandHandler(
  FilesystemState *filesystemState, TaskMessage *taskMessage
) {
  // A note about the way this function is written:
  //
  // I used to have sensible variables in this function.  That worked fine most
  // of the time.  However, I was getting stack corruptions when debug messages
  // were enabled.  So, I had to reduce the number of variables declared here.
  // I know it's tempting, but *DO NOT* declare more variables in this function.
  // Yes, it would definitely be more clear if there were proper variables
  // declared and used in this function, but functionality comes first.
  //
  // JBC 2026-02-17
  FilesystemFcloseParameters *fcloseParameters
    = (FilesystemFcloseParameters*) taskMessageData(taskMessage);
  if (filesystemState->driverState != NULL) {
    fcloseParameters->returnValue = filesystemState->driverFclose(
      filesystemState->driverState, fcloseParameters->stream->file);
    if (filesystemState->numOpenFiles > 0) {
      filesystemState->numOpenFiles--;
    }
    if (fcloseParameters->stream->next != NULL) {
      fcloseParameters->stream->next->prev = fcloseParameters->stream->prev;
    }
    if (fcloseParameters->stream->prev != NULL) {
      fcloseParameters->stream->prev->next = fcloseParameters->stream->next;
    }
    if (fcloseParameters->stream == filesystemState->openFiles) {
      filesystemState->openFiles = fcloseParameters->stream->next;
    }
  }
  free(fcloseParameters->stream);

  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int filesystemReadFileCommandHandler(
///   FilesystemState *filesystemState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_READ_FILE command.
///
/// @param filesystemState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int filesystemReadFileCommandHandler(
  FilesystemState *filesystemState, TaskMessage *taskMessage
) {
  FilesystemIoCommandParameters *filesystemIoCommandParameters
    = (FilesystemIoCommandParameters*) taskMessageData(taskMessage);
  int32_t returnValue = 0;
  if (filesystemState->driverState != NULL) {
    uint32_t length = filesystemIoCommandParameters->length;
    if (length > 0x7fffffff) {
      // Make sure we don't overflow the maximum value of a signed 32-bit int.
      length = 0x7fffffff;
    }
    NanoOsFile *nanoOsFile = filesystemIoCommandParameters->file;
    returnValue = filesystemState->driverRead(filesystemState->driverState,
      filesystemIoCommandParameters->buffer, length, nanoOsFile->file);
    if (returnValue >= 0) {
      // Return value is the number of bytes read.  Set the length variable to
      // it and set it to 0 to indicate good status.
      nanoOsFile->currentPosition += returnValue;
      filesystemIoCommandParameters->length = returnValue;
      returnValue = 0;
    } else {
      // Return value is a negative error code.  Negate it.
      returnValue = -returnValue;
      // Tell the caller that we read nothing.
      filesystemIoCommandParameters->length = 0;
    }
  }

  taskMessageSetDone(taskMessage);
  return returnValue;
}

/// @fn int filesystemWriteFileCommandHandler(
///   FilesystemState *filesystemState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_WRITE_FILE command.
///
/// @param filesystemState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int filesystemWriteFileCommandHandler(
  FilesystemState *filesystemState, TaskMessage *taskMessage
) {
  FilesystemIoCommandParameters *filesystemIoCommandParameters
    = (FilesystemIoCommandParameters*) taskMessageData(taskMessage);
  int32_t returnValue = 0;
  if (filesystemState->driverState != NULL) {
    uint32_t length = filesystemIoCommandParameters->length;
    if (length > 0x7fffffff) {
      // Make sure we don't overflow the maximum value of a signed 32-bit int.
      length = 0x7fffffff;
    }
    NanoOsFile *nanoOsFile = filesystemIoCommandParameters->file;
    returnValue = filesystemState->driverWrite(filesystemState->driverState,
      filesystemIoCommandParameters->buffer,
      length, nanoOsFile->file);
    if (returnValue >= 0) {
      // Return value is the number of bytes written.  Set the length variable
      // to it and set it to 0 to indicate good status.
      nanoOsFile->currentPosition += returnValue;
      filesystemIoCommandParameters->length = returnValue;
      returnValue = 0;
    } else {
      // Return value is a negative error code.  Negate it.
      returnValue = -returnValue;
      // Tell the caller that we wrote nothing.
      filesystemIoCommandParameters->length = 0;
    }
  }

  taskMessageSetDone(taskMessage);
  return returnValue;
}

/// @fn int filesystemRemoveFileCommandHandler(
///   FilesystemState *filesystemState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_REMOVE_FILE command.
///
/// @param filesystemState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int filesystemRemoveFileCommandHandler(
  FilesystemState *filesystemState, TaskMessage *taskMessage
) {
  const char *pathname = (const char*) taskMessageData(taskMessage);
  int returnValue = 0;
  if (filesystemState->driverState != NULL) {
    returnValue = filesystemState->driverRemove(
      filesystemState->driverState, pathname);
  }

  taskMessageData(taskMessage) = (void*) ((intptr_t) returnValue);
  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int filesystemSeekFileCommandHandler(
///   FilesystemState *filesystemState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_SEEK_FILE command.
///
/// @param filesystemState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int filesystemSeekFileCommandHandler(
  FilesystemState *filesystemState, TaskMessage *taskMessage
) {
  FilesystemSeekParameters *filesystemSeekParameters
    = (FilesystemSeekParameters*) taskMessageData(taskMessage);
  int returnValue = 0;
  if (filesystemState->driverState != NULL) {
    NanoOsFile *nanoOsFile = filesystemSeekParameters->stream;
    returnValue = filesystemState->driverSeek(
      filesystemState->driverState, nanoOsFile->file,
      filesystemSeekParameters->offset,
      filesystemSeekParameters->whence);
    if (returnValue >= 0) {
      nanoOsFile->currentPosition = returnValue;
    }
  }

  taskMessageData(taskMessage) = (void*) ((intptr_t) returnValue);
  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int filesystemDumpOpenFilesCommandHandler(
///   FilesystemState *filesystemState, TaskMessage *taskMessage)
///
/// @brief Command handler for the FILESYSTEM_DUMP_OPEN_FILES command.  Walk
/// the open files list and display information about all of the files and
/// their owning processes.
///
/// @param filesystemState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int filesystemDumpOpenFilesCommandHandler(
  FilesystemState *filesystemState, TaskMessage *taskMessage
) {
  printString("Open files:\n");
  for (NanoOsFile *nanoOsFile = filesystemState->openFiles;
    nanoOsFile != NULL;
    nanoOsFile = nanoOsFile->next
  ) {
    printString("0x");
    printHex((uintptr_t) nanoOsFile);
    printString(": \"");
    printString(filesystemState->driverGetFilename(nanoOsFile->file));
    printString("\" owned by ");
    printInt(nanoOsFile->owner);
    printString("\n");
  }

  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int filesystemGetFileBlockMetadataCommandHandler(
///   FilesystemState *filesystemState, TaskMessage *taskMessage)
///
/// @brief Command handler for the FILESYSTEM_GET_FILE_BLOCK_METADATA command.
/// Populate a caller-supplied FileBlockMetadata structure for a given file.
///
/// @param filesystemState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int filesystemGetFileBlockMetadataCommandHandler(
  FilesystemState *filesystemState, TaskMessage *taskMessage
) {
  GetFileBlockMetadataArgs *args = msg_data(taskMessage);
  args->metadata->blockDevice = filesystemState->blockDevice;

  filesystemState->driverGetFileBlockMetadata(
    filesystemState->driverState, args->stream->file,
    &args->metadata->startBlock, &args->metadata->numBlocks);

  taskMessageSetDone(taskMessage);
  return 0;
}

/// @var filesystemCommandHandlers
///
/// @brief Array of FilesystemCommandHandler function pointers.
const FilesystemCommandHandler filesystemCommandHandlers[] = {
  filesystemOpenFileCommandHandler,      // FILESYSTEM_OPEN_FILE
  filesystemCloseFileCommandHandler,     // FILESYSTEM_CLOSE_FILE
  filesystemReadFileCommandHandler,      // FILESYSTEM_READ_FILE
  filesystemWriteFileCommandHandler,     // FILESYSTEM_WRITE_FILE
  filesystemRemoveFileCommandHandler,    // FILESYSTEM_REMOVE_FILE
  filesystemSeekFileCommandHandler,      // FILESYSTEM_SEEK_FILE
  filesystemDumpOpenFilesCommandHandler, // FILESYSTEM_DUMP_OPEN_FILES
  // FILESYSTEM_GET_FILE_BLOCK_METADATA:
  filesystemGetFileBlockMetadataCommandHandler,
};


/// @fn static void handleFilesystemMessages(FilesystemState *fs)
///
/// @brief Pop and handle all messages in the filesystem task's message
/// queue until there are no more.
///
/// @param fs A pointer to the FilesystemState object maintained by the
///   filesystem task.
///
/// @return This function returns no value.
static void handleFilesystemMessages(FilesystemState *filesystemState) {
  TaskMessage *msg = taskMessageQueuePop();
  while (msg != NULL) {
    FilesystemCommandResponse type = 
      (FilesystemCommandResponse) taskMessageType(msg);
    if (type < NUM_FILESYSTEM_COMMANDS) {
      printDebugString("Handling filesystem message type ");
      printDebugInt(type);
      printDebugString("\n");
      filesystemCommandHandlers[type](filesystemState, msg);
    } else {
      printInt(getRunningTaskId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": ERROR! Received unknown filesystem message type ");
      printInt(type);
      printString("\n");
    }
    msg = taskMessageQueuePop();
  }
}

/// @fn void* runFilesystem(void *args)
///
/// @brief Main task entry point for the FAT16 filesystem task.
///
/// @param args A pointer to an initialized BlockStorageDevice structure cast
///   to a void*.
///
/// @return This function never returns, but would return NULL if it did.
void* runFilesystem(void *args) {
  FilesystemState fs;
  memcpy(&fs, args, sizeof(fs));
  taskYield();
  printDebugString("runFilesystem: Allocating fs.blockBuffer\n");
  fs.blockBuffer = (uint8_t*) malloc(fs.blockSize);
  
  printDebugString("runFilesystem: Getting partition info\n");
  getPartitionInfo(&fs);
  printDebugString("runFilesystem: Initiallizing driverState\n");
  fs.driverInit(&fs);
  printDebugString("runFilesystem: Initialization complete\n");
  
  TaskMessage *msg = NULL;
  while (1) {
    msg = (TaskMessage*) taskYield();
    if (msg) {
      FilesystemCommandResponse type = 
        (FilesystemCommandResponse) taskMessageType(msg);
      if (type < NUM_FILESYSTEM_COMMANDS) {
        filesystemCommandHandlers[type](&fs, msg);
      }
    } else {
      handleFilesystemMessages(&fs);
    }
  }
  return NULL;
}

/// @fn int getPartitionInfo(FilesystemState *fs)
///
/// @brief Get information about the partition for the provided filesystem.
///
/// @param fs Pointer to the filesystem state structure maintained by the
///   filesystem task.
///
/// @return Returns 0 on success, negative error code on failure.
int getPartitionInfo(FilesystemState *fs) {
  if (fs->blockDevice->partitionNumber == 0) {
    printDebugString("getPartitionInfo: Partition number is 0\n");
    return -1;
  }

  printDebugString("getPartitionInfo: Reading block 0\n");
  if (fs->blockDevice->readBlocks(fs->blockDevice->context, 0, 1, 
      fs->blockSize, fs->blockBuffer) != 0
  ) {
    printDebugString("getPartitionInfo: Failed to read block 0\n");
    return -2;
  }
  printDebugString("getPartitionInfo: Got block 0\n");

  uint8_t *partitionTable = fs->blockBuffer + PARTITION_TABLE_OFFSET;
  uint8_t *entry
    = partitionTable
    + ((fs->blockDevice->partitionNumber - 1)
    * PARTITION_ENTRY_SIZE);
  uint8_t type = entry[4];
  
  if ((type == PARTITION_TYPE_FAT16_LBA)
    || (type == PARTITION_TYPE_FAT16_LBA_EXTENDED)
    || (type == PARTITION_TYPE_NTFS_EXFAT)
    || (type == PARTITION_TYPE_LINUX)
  ) {
    uint32_t lbaValue, sectorsValue;
    
    // Read LBA offset using readBytes for alignment safety
    printDebugString("getPartitionInfo: Reading LBA offset\n");
    readBytes(&lbaValue, &entry[PARTITION_LBA_OFFSET]);
    fs->startLba = lbaValue;
    
    // Read number of sectors using readBytes for alignment safety  
    printDebugString("getPartitionInfo: Reading partition sectors\n");
    readBytes(&sectorsValue, &entry[PARTITION_SECTORS_OFFSET]);
    fs->endLba = fs->startLba + sectorsValue - 1;
    
    printDebugString("getPartitionInfo: Returing good status\n");
    return 0;
  }
  
  printDebugString("getPartitionInfo: Invalid partition type\n");
  return -3;
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
  if ((pathname == NULL) || (*pathname == '\0')
    || (mode == NULL) || (*mode == '\0')
  ) {
    return NULL;
  }
  FilesystemFopenParameters fopenParameters = {
    .pathname = pathname,
    .mode = mode,
  };

  TaskMessage *msg = initSendTaskMessageToTaskId(
    SCHEDULER_STATE->rootFsTaskId, FILESYSTEM_OPEN_FILE,
    &fopenParameters, sizeof(fopenParameters), true);
  taskMessageWaitForDone(msg, NULL);
  FILE *file = (FILE*) taskMessageData(msg);
  taskMessageRelease(msg);

  return file;
}

/// @fn int filesystemFClose(FILE *stream)
///
/// @brief Implementation of the standard C fclose call.
///
/// @param stream A pointer to a previously-opened FILE object.
///
/// @return This function always succeeds and always returns 0.
int filesystemFClose(FILE *stream) {
  int returnValue = 0;

  if (stream != NULL) {
    FilesystemFcloseParameters fcloseParameters;
    fcloseParameters.stream = stream;
    fcloseParameters.returnValue = 0;

    TaskMessage *msg = initSendTaskMessageToTaskId(
      SCHEDULER_STATE->rootFsTaskId, FILESYSTEM_CLOSE_FILE,
      &fcloseParameters, sizeof(fcloseParameters), true);
    taskMessageWaitForDone(msg, NULL);

    if (fcloseParameters.returnValue != 0) {
      errno = -fcloseParameters.returnValue;
      returnValue = EOF;
    }

    taskMessageRelease(msg);
  }

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
    TaskMessage *msg = initSendTaskMessageToTaskId(
      SCHEDULER_STATE->rootFsTaskId, FILESYSTEM_REMOVE_FILE,
      (void*) pathname, strlen(pathname) + 1, true);
    taskMessageWaitForDone(msg, NULL);
    returnValue = (int) ((intptr_t) taskMessageData(msg));
    if (returnValue != 0) {
      // returnValue holds a negative errno.  Set errno for the current task
      // and return -1 like we're supposed to.
      errno = -returnValue;
      returnValue = -1;
    }
    taskMessageRelease(msg);
  }
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

  FilesystemSeekParameters filesystemSeekParameters = {
    .stream = stream,
    .offset = offset,
    .whence = whence,
  };
  TaskMessage *msg = initSendTaskMessageToTaskId(
    SCHEDULER_STATE->rootFsTaskId, FILESYSTEM_SEEK_FILE,
    &filesystemSeekParameters, sizeof(filesystemSeekParameters), true);
  taskMessageWaitForDone(msg, NULL);
  int returnValue = (int) ((intptr_t) taskMessageData(msg));
  taskMessageRelease(msg);
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

  FilesystemIoCommandParameters filesystemIoCommandParameters = {
    .file = stream,
    .buffer = ptr,
    .length = (uint32_t) (size * nmemb)
  };

  printDebugString(__func__);
  printDebugString(": Sending message to filesystem task to read ");
  printDebugInt(nmemb);
  printDebugString(" elements ");
  printDebugInt(size);
  printDebugString(" bytes in size from file 0x");
  printDebugHex((uintptr_t) stream);
  printDebugString(" into address 0x");
  printDebugHex((uintptr_t) ptr);
  printDebugString("\n");

  TaskMessage *taskMessage = initSendTaskMessageToTaskId(
    SCHEDULER_STATE->rootFsTaskId,
    FILESYSTEM_READ_FILE,
    /* data= */ &filesystemIoCommandParameters,
    /* size= */ sizeof(filesystemIoCommandParameters),
    true);
  taskMessageWaitForDone(taskMessage, NULL);
  returnValue = (filesystemIoCommandParameters.length / size);
  taskMessageRelease(taskMessage);

  printDebugString(__func__);
  printDebugString(": Returning ");
  printDebugInt(returnValue);
  printDebugString(" from read of file 0x");
  printDebugHex((uintptr_t) filesystemIoCommandParameters.file);
  printDebugString(" into address 0x");
  printDebugHex((uintptr_t) filesystemIoCommandParameters.buffer);
  printDebugString("\n");
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

  FilesystemIoCommandParameters filesystemIoCommandParameters = {
    .file = stream,
    .buffer = (void*) ptr,
    .length = (uint32_t) (size * nmemb)
  };
  TaskMessage *taskMessage = initSendTaskMessageToTaskId(
    SCHEDULER_STATE->rootFsTaskId,
    FILESYSTEM_WRITE_FILE,
    /* data= */ &filesystemIoCommandParameters,
    /* size= */ sizeof(filesystemIoCommandParameters),
    true);
  taskMessageWaitForDone(taskMessage, NULL);
  returnValue = (filesystemIoCommandParameters.length / size);
  taskMessageRelease(taskMessage);

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

  TaskMessage *taskMessage = getAvailableMessage();
  for (int ii = 0;
    (ii < MAX_GET_MESSAGE_RETRIES) && (taskMessage == NULL);
    ii++
  ) {
    taskYield();
    taskMessage = getAvailableMessage();
  }
  if (taskMessage == NULL) {
    printInt(getRunningTaskId());
    printString(": ");
    printString(__func__);
    printString(": ERROR: Out of task messages\n");
    return -ENOMEM;
  }

  taskMessageInit(taskMessage, FILESYSTEM_GET_FILE_BLOCK_METADATA,
    &args, sizeof(args), true);
  if (sendTaskMessageToTaskId(SCHEDULER_STATE->rootFsTaskId, taskMessage)
    != taskSuccess
  ) {
    printString("ERROR! Failed to send message to filesystem to get file "
      "block metadata\n");
    taskMessageRelease(taskMessage);
    return -EIO;
  }
  taskMessageWaitForDone(taskMessage, NULL);
  taskMessageRelease(taskMessage);

  return 0;
}

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

  FILE *stream = fopen(path, "r");
  if (stream == NULL) {
    printInt(getRunningTaskId());
    printString(": ");
    printString(__func__);
    printString(": ERROR! Could not open file \"");
    printString(path);
    printString("\"\n");
    return -EIO;
  }
  int returnValue = getFileBlockMetadataFromFile(stream, metadata);
  fclose(stream); stream = NULL;

  return returnValue;
}

