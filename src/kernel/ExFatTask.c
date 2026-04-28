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

#include <string.h>

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
typedef int (*ExFatCommandHandler)(FilesystemState*, TaskMessage*);

/// @fn int exFatTaskOpenFileCommandHandler(
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
int exFatTaskOpenFileCommandHandler(
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
    void *exFatFile = filesystemState->driverOpenFile(
      filesystemState->driverState,
      fopenParameters->pathname, fopenParameters->mode);
    if (exFatFile != NULL) {
      nanoOsFile = (NanoOsFile*) malloc(sizeof(NanoOsFile));
      if (nanoOsFile != NULL) {
        nanoOsFile->file = exFatFile;
        nanoOsFile->currentPosition = 0;
        nanoOsFile->fd = filesystemState->numOpenFiles + 3;
        nanoOsFile->owner = taskId(taskMessageFrom(taskMessage));
        filesystemState->numOpenFiles++;

        nanoOsFile->next = filesystemState->openFiles;
        nanoOsFile->prev = NULL;
        filesystemState->openFiles = nanoOsFile;
      } else {
        filesystemState->driverFclose(filesystemState->driverState, exFatFile);
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

/// @fn int exFatTaskCloseFileCommandHandler(
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
int exFatTaskCloseFileCommandHandler(
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

/// @fn int exFatTaskReadFileCommandHandler(
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
int exFatTaskReadFileCommandHandler(
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

/// @fn int exFatTaskWriteFileCommandHandler(
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
int exFatTaskWriteFileCommandHandler(
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

/// @fn int exFatTaskRemoveFileCommandHandler(
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
int exFatTaskRemoveFileCommandHandler(
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

/// @fn int exFatTaskSeekFileCommandHandler(
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
int exFatTaskSeekFileCommandHandler(
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

/// @fn int exFatTaskDumpOpenFilesCommandHandler(
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
int exFatTaskDumpOpenFilesCommandHandler(
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

/// @fn int exFatTaskGetFileBlockMetadataCommandHandler(
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
int exFatTaskGetFileBlockMetadataCommandHandler(
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

/// @var exFatCommandHandlers
///
/// @brief Array of ExFatCommandHandler function pointers.
const ExFatCommandHandler exFatCommandHandlers[] = {
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
static void exFatHandleFilesystemMessages(FilesystemState *filesystemState) {
  TaskMessage *msg = taskMessageQueuePop();
  while (msg != NULL) {
    FilesystemCommandResponse type = 
      (FilesystemCommandResponse) taskMessageType(msg);
    if (type < NUM_FILESYSTEM_COMMANDS) {
      printDebugString("Handling filesystem message type ");
      printDebugInt(type);
      printDebugString("\n");
      exFatCommandHandlers[type](filesystemState, msg);
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

/// @fn void* runExFatFilesystem(void *args)
///
/// @brief Main task entry point for the FAT16 filesystem task.
///
/// @param args A pointer to an initialized BlockStorageDevice structure cast
///   to a void*.
///
/// @return This function never returns, but would return NULL if it did.
void* runExFatFilesystem(void *args) {
  FilesystemState fs;
  memcpy(&fs, args, sizeof(fs));
  taskYield();
  printDebugString("runExFatFilesystem: Allocating FilesystemState\n");
  printDebugString("runExFatFilesystem: Allocating ExFatDriverState\n");
  printDebugString("runExFatFilesystem: Allocating fs.blockSize\n");
  fs.blockBuffer = (uint8_t*) malloc(fs.blockSize);
  
  printDebugString("runExFatFilesystem: Getting partition info\n");
  getPartitionInfo(&fs);
  printDebugString("runExFatFilesystem: Initiallizing driverState\n");
  fs.driverInit(&fs);
  printDebugString("runExFatFilesystem: Initialization complete\n");
  
  TaskMessage *msg = NULL;
  while (1) {
    msg = (TaskMessage*) taskYield();
    if (msg) {
      FilesystemCommandResponse type = 
        (FilesystemCommandResponse) taskMessageType(msg);
      if (type < NUM_FILESYSTEM_COMMANDS) {
        exFatCommandHandlers[type](&fs, msg);
      }
    } else {
      exFatHandleFilesystemMessages(&fs);
    }
  }
  return NULL;
}

