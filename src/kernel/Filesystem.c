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

/// @fn FILE* filesystemFOpen(const char *pathname, const char *mode)
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
FILE* filesystemFOpen(const char *pathname, const char *mode) {
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
      pathname, strlen(pathname) + 1, true);
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

