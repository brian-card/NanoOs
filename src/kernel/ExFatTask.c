////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
///
/// @file              ExFatTask.c
///
/// @brief             exFAT task implementation for NanoOs.
///
///////////////////////////////////////////////////////////////////////////////

#include "ExFatTask.h"
#include "ExFatFilesystem.h"
#include "NanoOs.h"
#include "Tasks.h"

// Must come last
#include "../user/NanoOsStdio.h"
#include "Filesystem.h"

/// @typedef ExFatCommandHandler
///
/// @brief Definition of a filesystem command handler function.
typedef int (*ExFatCommandHandler)(ExFatDriverState*, TaskMessage*);

/// @fn int exFatTaskOpenFileCommandHandler(
///   ExFatDriverState *driverState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_OPEN_FILE command.
///
/// @param filesystemState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int exFatTaskOpenFileCommandHandler(
  ExFatDriverState *driverState, TaskMessage *taskMessage
) {
  NanoOsFile *nanoOsFile = NULL;
  const char *pathname = nanoOsMessageDataPointer(taskMessage, char*);
  const char *mode = nanoOsMessageFuncPointer(taskMessage, char*);

  printDebugString("Opening file \"");
  printDebugString(pathname);
  printDebugString("\" in mode \"");
  printDebugString(mode);
  printDebugString("\"\n");

  if (driverState->driverStateValid) {
    ExFatFileHandle *exFatFile = exFatOpenFile(driverState, pathname, mode);
    if (exFatFile != NULL) {
      nanoOsFile = (NanoOsFile*) malloc(sizeof(NanoOsFile));
      if (nanoOsFile != NULL) {
        nanoOsFile->file = exFatFile;
        nanoOsFile->currentPosition = exFatFile->currentPosition;
        nanoOsFile->fd = driverState->filesystemState->numOpenFiles + 3;
        nanoOsFile->owner = taskId(taskMessageFrom(taskMessage));
        driverState->filesystemState->numOpenFiles++;

        nanoOsFile->next = driverState->filesystemState->openFiles;
        nanoOsFile->prev = NULL;
        driverState->filesystemState->openFiles = nanoOsFile;
      } else {
        exFatFclose(driverState, exFatFile);
      }
    }
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  nanoOsMessage->data = (intptr_t) nanoOsFile;
  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int exFatTaskCloseFileCommandHandler(
///   ExFatDriverState *driverState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_CLOSE_FILE command.
///
/// @param driverState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int exFatTaskCloseFileCommandHandler(
  ExFatDriverState *driverState, TaskMessage *taskMessage
) {
  (void) driverState;

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
    = nanoOsMessageDataPointer(taskMessage, FilesystemFcloseParameters*);
  if (driverState->driverStateValid) {
    fcloseParameters->returnValue = exFatFclose(
      driverState, (ExFatFileHandle*) fcloseParameters->stream->file);
    if (driverState->filesystemState->numOpenFiles > 0) {
      driverState->filesystemState->numOpenFiles--;
    }
    if (fcloseParameters->stream->next != NULL) {
      fcloseParameters->stream->next->prev = fcloseParameters->stream->prev;
    }
    if (fcloseParameters->stream->prev != NULL) {
      fcloseParameters->stream->prev->next = fcloseParameters->stream->next;
    }
    if (fcloseParameters->stream == driverState->filesystemState->openFiles) {
      driverState->filesystemState->openFiles = fcloseParameters->stream->next;
    }
  }
  free(fcloseParameters->stream);

  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int exFatTaskReadFileCommandHandler(
///   ExFatDriverState *driverState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_READ_FILE command.
///
/// @param driverState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int exFatTaskReadFileCommandHandler(
  ExFatDriverState *driverState, TaskMessage *taskMessage
) {
  FilesystemIoCommandParameters *filesystemIoCommandParameters
    = nanoOsMessageDataPointer(taskMessage, FilesystemIoCommandParameters*);
  int32_t returnValue = 0;
  if (driverState->driverStateValid) {
    uint32_t length = filesystemIoCommandParameters->length;
    if (length > 0x7fffffff) {
      // Make sure we don't overflow the maximum value of a signed 32-bit int.
      length = 0x7fffffff;
    }
    NanoOsFile *nanoOsFile = filesystemIoCommandParameters->file;
    ExFatFileHandle *exFatFile = (ExFatFileHandle*) nanoOsFile->file;
    returnValue = exFatRead(driverState,
      filesystemIoCommandParameters->buffer, length, exFatFile);
    nanoOsFile->currentPosition = exFatFile->currentPosition;
    if (returnValue >= 0) {
      // Return value is the number of bytes read.  Set the length variable to
      // it and set it to 0 to indicate good status.
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

/// @fn int exFatTaskWriteFileCommandHandler(
///   ExFatDriverState *driverState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_WRITE_FILE command.
///
/// @param driverState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int exFatTaskWriteFileCommandHandler(
  ExFatDriverState *driverState, TaskMessage *taskMessage
) {
  FilesystemIoCommandParameters *filesystemIoCommandParameters
    = nanoOsMessageDataPointer(taskMessage, FilesystemIoCommandParameters*);
  int32_t returnValue = 0;
  if (driverState->driverStateValid) {
    uint32_t length = filesystemIoCommandParameters->length;
    if (length > 0x7fffffff) {
      // Make sure we don't overflow the maximum value of a signed 32-bit int.
      length = 0x7fffffff;
    }
    NanoOsFile *nanoOsFile = filesystemIoCommandParameters->file;
    ExFatFileHandle *exFatFile = (ExFatFileHandle*) nanoOsFile->file;
    returnValue = exFatWrite(driverState,
      filesystemIoCommandParameters->buffer,
      length, exFatFile);
    nanoOsFile->currentPosition = exFatFile->currentPosition;
    if (returnValue >= 0) {
      // Return value is the number of bytes written.  Set the length variable
      // to it and set it to 0 to indicate good status.
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

/// @fn int exFatTaskRemoveFileCommandHandler(
///   ExFatDriverState *driverState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_REMOVE_FILE command.
///
/// @param driverState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int exFatTaskRemoveFileCommandHandler(
  ExFatDriverState *driverState, TaskMessage *taskMessage
) {
  const char *pathname = nanoOsMessageDataPointer(taskMessage, char*);
  int returnValue = 0;
  if (driverState->driverStateValid) {
    returnValue = exFatRemove(driverState, pathname);
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  nanoOsMessage->data = (intptr_t) returnValue;
  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int exFatTaskSeekFileCommandHandler(
///   ExFatDriverState *driverState, TaskMessage *taskMessage)
///
/// @brief Command handler for FILESYSTEM_SEEK_FILE command.
///
/// @param driverState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int exFatTaskSeekFileCommandHandler(
  ExFatDriverState *driverState, TaskMessage *taskMessage
) {
  FilesystemSeekParameters *filesystemSeekParameters
    = nanoOsMessageDataPointer(taskMessage, FilesystemSeekParameters*);
  int returnValue = 0;
  if (driverState->driverStateValid) {
    NanoOsFile *nanoOsFile = filesystemSeekParameters->stream;
    ExFatFileHandle *exFatFile = (ExFatFileHandle*) nanoOsFile->file;
    returnValue = exFatSeek(driverState, exFatFile,
      filesystemSeekParameters->offset,
      filesystemSeekParameters->whence);
    nanoOsFile->currentPosition = exFatFile->currentPosition;
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  nanoOsMessage->data = (intptr_t) returnValue;
  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int exFatTaskDumpOpenFilesCommandHandler(
///   ExFatDriverState *driverState, TaskMessage *taskMessage)
///
/// @brief Command handler for the FILESYSTEM_DUMP_OPEN_FILES command.  Walk
/// the open files list and display information about all of the files and
/// their owning processes.
///
/// @param driverState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int exFatTaskDumpOpenFilesCommandHandler(
  ExFatDriverState *driverState, TaskMessage *taskMessage
) {
  printString("Open files:\n");
  for (NanoOsFile *nanoOsFile = driverState->filesystemState->openFiles;
    nanoOsFile != NULL;
    nanoOsFile = nanoOsFile->next
  ) {
    ExFatFileHandle *exFatFile = (ExFatFileHandle*) nanoOsFile->file;
    printString("0x");
    printHex((uintptr_t) nanoOsFile);
    printString(": \"");
    printString(exFatFile->fileName);
    printString("\" owned by ");
    printInt(nanoOsFile->owner);
    printString("\n");
  }

  taskMessageSetDone(taskMessage);
  return 0;
}

/// @fn int exFatTaskGetFileBlockMetadataCommandHandler(
///   ExFatDriverState *driverState, TaskMessage *taskMessage)
///
/// @brief Command handler for the FILESYSTEM_GET_FILE_BLOCK_METADATA command.
/// Populate a caller-supplied FileBlockMetadata structure for a given file.
///
/// @param driverState A pointer to the FilesystemState object maintained
///   by the filesystem task.
/// @param taskMessage A pointer to the TaskMessage that was received by
///   the filesystem task.
///
/// @return Returns 0 on success, a standard POSIX error code on failure.
int exFatTaskGetFileBlockMetadataCommandHandler(
  ExFatDriverState *driverState, TaskMessage *taskMessage
) {
  GetFileBlockMetadataArgs *args = msg_data(taskMessage);
  args->metadata->blockDevice = driverState->filesystemState->blockDevice;

  ExFatFileHandle *exFatFile = (ExFatFileHandle*) args->stream->file;
  args->metadata->startBlock
    = exFatFile->firstCluster * driverState->sectorsPerCluster;
  args->metadata->numBlocks
    = (uint32_t) ((exFatFile->fileSize + (driverState->bytesPerSector - 1))
    / ((uint64_t) driverState->bytesPerSector));

  taskMessageSetDone(taskMessage);
  return 0;
}

/// @var filesystemCommandHandlers
///
/// @brief Array of ExFatCommandHandler function pointers.
const ExFatCommandHandler filesystemCommandHandlers[] = {
  exFatTaskOpenFileCommandHandler,      // FILESYSTEM_OPEN_FILE
  exFatTaskCloseFileCommandHandler,     // FILESYSTEM_CLOSE_FILE
  exFatTaskReadFileCommandHandler,      // FILESYSTEM_READ_FILE
  exFatTaskWriteFileCommandHandler,     // FILESYSTEM_WRITE_FILE
  exFatTaskRemoveFileCommandHandler,    // FILESYSTEM_REMOVE_FILE
  exFatTaskSeekFileCommandHandler,      // FILESYSTEM_SEEK_FILE
  exFatTaskDumpOpenFilesCommandHandler, // FILESYSTEM_DUMP_OPEN_FILES
  // FILESYSTEM_GET_FILE_BLOCK_METADATA:
  exFatTaskGetFileBlockMetadataCommandHandler,
};


/// @fn static void exFatHandleFilesystemMessages(FilesystemState *fs)
///
/// @brief Pop and handle all messages in the filesystem task's message
/// queue until there are no more.
///
/// @param fs A pointer to the FilesystemState object maintained by the
///   filesystem task.
///
/// @return This function returns no value.
static void exFatHandleFilesystemMessages(ExFatDriverState *driverState) {
  TaskMessage *msg = taskMessageQueuePop();
  while (msg != NULL) {
    FilesystemCommandResponse type = 
      (FilesystemCommandResponse) taskMessageType(msg);
    if (type < NUM_FILESYSTEM_COMMANDS) {
      printDebugString("Handling filesystem message type ");
      printDebugInt(type);
      printDebugString("\n");
      filesystemCommandHandlers[type](driverState, msg);
    } else {
      printString("ERROR! Received unknown filesystem message type ");
      printInt(type);
      printString("\n");
    }
    msg = taskMessageQueuePop();
  }
}

/// @fn void* runExFatFilesystem(void *args)
///
/// @brief Main task entry point for the FAT16 filesystem task.
///
/// @param args A pointer to an initialized BlockStorageDevice structure cast
///   to a void*.
///
/// @return This function never returns, but would return NULL if it did.
void* runExFatFilesystem(void *args) {
  taskYield();
  printDebugString("runExFatFilesystem: Allocating FilesystemState\n");
  FilesystemState *fs = (FilesystemState*) calloc(1, sizeof(FilesystemState));
  printDebugString("runExFatFilesystem: Allocating ExFatDriverState\n");
  ExFatDriverState *driverState
    = (ExFatDriverState*) calloc(1, sizeof(ExFatDriverState));
  fs->blockDevice = (BlockStorageDevice*) args;
  fs->blockSize = fs->blockDevice->blockSize;
  
  printDebugString("runExFatFilesystem: Allocating fs->blockSize\n");
  fs->blockBuffer = (uint8_t*) malloc(fs->blockSize);
  printDebugString("runExFatFilesystem: Getting partition info\n");
  getPartitionInfo(fs);
  printDebugString("runExFatFilesystem: Initiallizing driverState\n");
  exFatInitialize(driverState, fs);
  printDebugString("runExFatFilesystem: Initialization complete\n");
  
  TaskMessage *msg = NULL;
  while (1) {
    msg = (TaskMessage*) taskYield();
    if (msg) {
      FilesystemCommandResponse type = 
        (FilesystemCommandResponse) taskMessageType(msg);
      if (type < NUM_FILESYSTEM_COMMANDS) {
        filesystemCommandHandlers[type](driverState, msg);
      }
    } else {
      exFatHandleFilesystemMessages(driverState);
    }
  }
  return NULL;
}

