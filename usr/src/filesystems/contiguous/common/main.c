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

/// @file main.c
///
/// @brief Entrypoint into the contiguous filesystem driver.  Unlike the
/// overlay-filesystem packaging, every command handler is linked directly
/// into this one binary, so dispatch is a plain function call instead of
/// callOverlayFunction.

// Standard C includes
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"
#include "FilesystemUtils.h"

// Prototypes for the driver functions linked in from the selected driver.
FilesystemState* filesystemInitDriver(FilesystemState *filesystemState);
FilesystemState* getPartitionInfoImpl(FilesystemState *filesystemState);
void* driverFopen(void *driverState, const char *filePath, const char *mode);
int driverFclose(void *driverState, void *fileHandle);
int driverFread(
    void *driverState, void *ptr, uint32_t length, void *fileHandle);
int driverFwrite(
    void *driverState, void *ptr, uint32_t length, void *fileHandle);
int driverRemove(void *driverState, const char *pathname);
int driverFseek(void *driverState, void *fileHandle, long offset, int whence);
const char *driverGetFilename(void *fileHandle);
int driverGetFileBlockMetadata(
    void *driverState, void *fileHandle,
    uint32_t *startBlock, uint32_t *numBlocks);
int driverFeof(void *fileHandle);

/// @fn void* filesystemOpenFileCommandHandler(FilesystemState *filesystemState)
///
/// @brief Command handler for an fopen call.
///
/// @param filesystemState A pointer to the FilesystemState managed by the
///   process.  The args member variable will contain the processMessage that
///   was received from the client-side fopen call.  The data payload of the
///   message will contain the actual arguments for the call.
///
/// @return Sets the returnValue member of the provided FilesystemFopenArgs to
/// the value that is to be used by the calling process, i.e. the open file on
/// success and NULL on failure. This function always returns the
/// filesystemState pointer argument provided.
void* filesystemOpenFileCommandHandler(FilesystemState *filesystemState) {
  NanoOsFile *nanoOsFile = NULL;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemFopenArgs *fopenArgs
    = (FilesystemFopenArgs*) processMessageData(processMessage);

  if (filesystemState->driverState != NULL) {
    void *fileHandle = driverFopen(
      filesystemState->driverState,
      fopenArgs->pathname, fopenArgs->mode);
    if (fileHandle != NULL) {
      nanoOsFile = (NanoOsFile*) malloc(sizeof(NanoOsFile));
      if (nanoOsFile != NULL) {
        nanoOsFile->file = fileHandle;
        nanoOsFile->currentPosition = 0;
        nanoOsFile->fd = fopenArgs->fd;
        nanoOsFile->owner = processPid(processMessageFrom(processMessage));
        filesystemState->numOpenFiles++;

        nanoOsFile->next = filesystemState->openFiles;
        nanoOsFile->prev = NULL;
        if (filesystemState->openFiles != NULL) {
          filesystemState->openFiles->prev = nanoOsFile;
        }
        filesystemState->openFiles = nanoOsFile;
      } else {
        driverFclose(filesystemState->driverState, fileHandle);
      }
    } else {
      printString("ERROR: driverFopen returned NULL\n");
    }
  } else {
    printString("ERROR: driverState is not valid!\n");
  }

  fopenArgs->returnValue = nanoOsFile;
  processMessageSetDone(processMessage);
  return filesystemState;
}

/// @fn void* filesystemCloseFileCommandHandler(
///   FilesystemState *filesystemState)
///
/// @brief Command handler for an fclose call.
///
/// @param filesystemState A pointer to the FilesystemState managed by the
///   process.  The args member variable will contain the processMessage that
///   was received from the client-side fclose call.  The data payload of the
///   message will contain the actual arguments for the call.
///
/// @return Sets the returnValue member of the provided FilesystemFcloseArgs to
/// the value that is to be used by the calling process, i.e. 0 on success and
/// -errno on failure. This function always returns the filesystemState pointer
/// argument provided.
void* filesystemCloseFileCommandHandler(FilesystemState *filesystemState) {
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemFcloseArgs *fcloseArgs
    = (FilesystemFcloseArgs*) processMessageData(processMessage);
  if (filesystemState->driverState != NULL) {
    fcloseArgs->returnValue = driverFclose(
      filesystemState->driverState, fcloseArgs->stream->file);
    if (filesystemState->numOpenFiles > 0) {
      filesystemState->numOpenFiles--;
    }
    if (fcloseArgs->stream->next != NULL) {
      fcloseArgs->stream->next->prev = fcloseArgs->stream->prev;
    }
    if (fcloseArgs->stream->prev != NULL) {
      fcloseArgs->stream->prev->next = fcloseArgs->stream->next;
    }
    if (fcloseArgs->stream == filesystemState->openFiles) {
      filesystemState->openFiles = fcloseArgs->stream->next;
    }
  }
  free(fcloseArgs->stream);

  processMessageSetDone(processMessage);
  return filesystemState;
}

/// @fn void* filesystemReadFileCommandHandler(FilesystemState *filesystemState)
///
/// @brief Command handler for an fread call.
///
/// @param filesystemState A pointer to the FilesystemState managed by the
///   process.  The args member variable will contain the processMessage that
///   was received from the client-side fread call.  The data payload of the
///   message will contain the actual arguments for the call.
///
/// @return Sets the length member of the provided FilesystemIoCommandArgs to
/// the value that is to be used by the calling process. This function always
/// returns the filesystemState pointer argument provided.
void* filesystemReadFileCommandHandler(FilesystemState *filesystemState) {
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemIoCommandArgs *filesystemIoCommandArgs
    = (FilesystemIoCommandArgs*) processMessageData(processMessage);
  int returnValue = 0;
  if (filesystemState->driverState != NULL) {
    uint32_t length = filesystemIoCommandArgs->length;
    if (length > (uint32_t) (~0u >> 1)) {
      // Clamp to the platform INT_MAX: driverFread/driverFwrite return an int.
      length = (uint32_t) (~0u >> 1);
    }
    NanoOsFile *nanoOsFile = filesystemIoCommandArgs->file;
    returnValue = driverFread(filesystemState->driverState,
      filesystemIoCommandArgs->buffer, length, nanoOsFile->file);
    if (returnValue >= 0) {
      // Return value is the number of bytes read.  Set the length variable to
      // it and set it to 0 to indicate good status.
      nanoOsFile->currentPosition += returnValue;
      filesystemIoCommandArgs->length = returnValue;
    } else {
      // Return value is a negative error code.  Negate it.
      // Tell the caller that we read nothing.
      filesystemIoCommandArgs->length = 0;
    }
  }

  processMessageSetDone(processMessage);
  return filesystemState;
}

/// @fn void* filesystemWriteFileCommandHandler(
///   FilesystemState *filesystemState)
///
/// @brief Command handler for an fwrite call.
///
/// @param filesystemState A pointer to the FilesystemState managed by the
///   process.  The args member variable will contain the processMessage that
///   was received from the client-side fwrite call.  The data payload of the
///   message will contain the actual arguments for the call.
///
/// @return Sets the length member of the provided FilesystemIoCommandArgs to
/// the value that is to be used by the calling process. This function always
/// returns the filesystemState pointer argument provided.
void* filesystemWriteFileCommandHandler(FilesystemState *filesystemState) {
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemIoCommandArgs *filesystemIoCommandArgs
    = (FilesystemIoCommandArgs*) processMessageData(processMessage);
  int returnValue = 0;
  if (filesystemState->driverState != NULL) {
    uint32_t length = filesystemIoCommandArgs->length;
    if (length > (uint32_t) (~0u >> 1)) {
      // Clamp to the platform INT_MAX: driverFread/driverFwrite return an int.
      length = (uint32_t) (~0u >> 1);
    }
    NanoOsFile *nanoOsFile = filesystemIoCommandArgs->file;
    returnValue = driverFwrite(filesystemState->driverState,
      filesystemIoCommandArgs->buffer,
      length, nanoOsFile->file);
    if (returnValue >= 0) {
      // Return value is the number of bytes written.  Set the length variable
      // to it and set it to 0 to indicate good status.
      nanoOsFile->currentPosition += returnValue;
      filesystemIoCommandArgs->length = returnValue;
    } else {
      // Return value is a negative error code.  Negate it.
      returnValue = -returnValue;
      // Tell the caller that we wrote nothing.
      filesystemIoCommandArgs->length = 0;
    }
  }

  processMessageSetDone(processMessage);
  return filesystemState;
}

/// @fn void* filesystemRemoveFileCommandHandler(
///   FilesystemState *filesystemState)
///
/// @brief Command handler for a remove call.
///
/// @param filesystemState A pointer to the FilesystemState managed by the
///   process.  The args member variable will contain the processMessage that
///   was received from the client-side remove call.  The data payload of the
///   message will contain the actual arguments for the call.
///
/// @return Sets the returnValue member of the provided FilesystemRemoveArgs to
/// the value that is to be used by the calling process, i.e. 0 on success and
/// -errno on failure. This function always returns the filesystemState pointer
/// argument provided.
void* filesystemRemoveFileCommandHandler(FilesystemState *filesystemState) {
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemRemoveArgs *filesystemRemoveArgs
    = (FilesystemRemoveArgs*) processMessageData(processMessage);
  int returnValue = 0;
  if (filesystemState->driverState != NULL) {
    returnValue = driverRemove(
      filesystemState->driverState, filesystemRemoveArgs->pathname);
  }

  filesystemRemoveArgs->returnValue = returnValue;
  processMessageSetDone(processMessage);
  return filesystemState;
}

/// @fn void* filesystemSeekFileCommandHandler(FilesystemState *filesystemState)
///
/// @brief Command handler for an fseek call.
///
/// @param filesystemState A pointer to the FilesystemState managed by the
///   process.  The args member variable will contain the processMessage that
///   was received from the client-side fseek call.  The data payload of the
///   message will contain the actual arguments for the call.
///
/// @return Sets the returnValue and errorNumber members of the provided
/// FilesystemSeekArgs to the value that is to be used by the calling process.
/// This function always returns the filesystemState pointer argument provided.
void* filesystemSeekFileCommandHandler(FilesystemState *filesystemState) {
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemSeekArgs *filesystemSeekArgs
    = (FilesystemSeekArgs*) processMessageData(processMessage);
  int returnValue = 0;
  if (filesystemState->driverState != NULL) {
    NanoOsFile *nanoOsFile = filesystemSeekArgs->stream;
    errno = 0;
    returnValue = driverFseek(
      filesystemState->driverState, nanoOsFile->file,
      filesystemSeekArgs->offset,
      filesystemSeekArgs->whence);
    if (returnValue >= 0) {
      nanoOsFile->currentPosition = returnValue;
    }
  }

  filesystemSeekArgs->returnValue = returnValue;
  filesystemSeekArgs->errorNumber = errno;
  processMessageSetDone(processMessage);
  return filesystemState;
}

/// @fn void* filesystemDumpOpenFilesCommandHandler(
///   FilesystemState *filesystemState)
///
/// @brief Command handler for a dumpOpenFiles call.
///
/// @param filesystemState A pointer to the FilesystemState managed by the
///   process.  The args member variable will contain the processMessage that
///   was received from the client-side dumpOpenFiles call.  The data payload of
///   the message will contain the actual arguments for the call.
///
/// @return Sets the returnValue member of the provided
/// FilesystemDumpOpenFilesArgs to the value that is to be used by the calling
/// process.  This function always returns the filesystemState pointer argument
/// provided.
void* filesystemDumpOpenFilesCommandHandler(FilesystemState *filesystemState) {
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemDumpOpenFilesArgs *filesystemDumpOpenFilesArgs
    = (FilesystemDumpOpenFilesArgs*) processMessageData(processMessage);

  printString("Open files:\n");
  for (NanoOsFile *nanoOsFile = filesystemState->openFiles;
    nanoOsFile != NULL;
    nanoOsFile = nanoOsFile->next
  ) {
    printString("0x");
    printHex((uintptr_t) nanoOsFile);
    printString(": \"");
    printString(driverGetFilename(nanoOsFile->file));
    printString("\" owned by ");
    printInt(nanoOsFile->owner);
    printString("\n");
  }

  filesystemDumpOpenFilesArgs->returnValue = 0;
  processMessageSetDone(processMessage);
  return filesystemState;
}

/// @fn void* filesystemGetFileBlockMetadataCommandHandler(
///   FilesystemState *filesystemState)
///
/// @brief Command handler for a getFileBlockMetadata call.
///
/// @param filesystemState A pointer to the FilesystemState managed by the
///   process.  The args member variable will contain the processMessage that
///   was received from the client-side getFileBlockMetadata call.  The data
///   payload of the message will contain the actual arguments for the call.
///
/// @return Sets the metadata member of the provided GetFileBlockMetadataArgs to
//  the value that is to be used by the calling process.  This function always
/// returns the filesystemState pointer argument provided.
void* filesystemGetFileBlockMetadataCommandHandler(
  FilesystemState *filesystemState
) {
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  GetFileBlockMetadataArgs *metadataArgs
    = (GetFileBlockMetadataArgs*) processMessageData(processMessage);
  metadataArgs->metadata->blockDevice = filesystemState->blockDevice;

  driverGetFileBlockMetadata(
    filesystemState->driverState, metadataArgs->stream->file,
    &metadataArgs->metadata->startBlock, &metadataArgs->metadata->numBlocks);

  processMessageSetDone(processMessage);
  return filesystemState;
}

/// @fn void* filesystemEndOfFileCommandHandler(void *args)
///
/// @brief Command handler for an feof call.
///
/// @param filesystemState A pointer to the FilesystemState managed by the
///   process.  The args member variable will contain the processMessage that
///   was received from the client-side feof call.  The data payload of the
///   message will contain the actual arguments for the call.
///
/// @return Sets the returnValue member of the provided FeofArgs to the value
/// that is to be used by the calling process, i.e. 0 if the end of the file has
/// not been reached and non-zero otherwise.  This function always returns the
/// filesystemState pointer argument provided.
void* filesystemEndOfFileCommandHandler(FilesystemState *filesystemState) {
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FeofArgs *feofArgs = (FeofArgs*) processMessageData(processMessage);
  feofArgs->returnValue = 0;

  if (filesystemState->driverState != NULL) {
    feofArgs->returnValue = driverFeof(feofArgs->stream->file);
  }

  processMessageSetDone(processMessage);
  return filesystemState;
}

/// @typedef FilesystemCommandHandler
///
/// @brief A command handler function, indexed by FilesystemCommandResponse.
typedef void* (*FilesystemCommandHandler)(FilesystemState *filesystemState);

/// @var filesystemCommandHandlers
///
/// @brief Array of command handlers, indexed by FilesystemCommandResponse.
static const FilesystemCommandHandler filesystemCommandHandlers[] = {
  filesystemOpenFileCommandHandler,             // FILESYSTEM_OPEN_FILE
  filesystemCloseFileCommandHandler,            // FILESYSTEM_CLOSE_FILE
  filesystemReadFileCommandHandler,             // FILESYSTEM_READ_FILE
  filesystemWriteFileCommandHandler,            // FILESYSTEM_WRITE_FILE
  filesystemRemoveFileCommandHandler,           // FILESYSTEM_REMOVE_FILE
  filesystemSeekFileCommandHandler,             // FILESYSTEM_SEEK_FILE
  filesystemDumpOpenFilesCommandHandler,        // FILESYSTEM_DUMP_OPEN_FILES
  // FILESYSTEM_GET_FILE_BLOCK_METADATA:
  filesystemGetFileBlockMetadataCommandHandler,
  filesystemEndOfFileCommandHandler,            // FILESYSTEM_END_OF_FILE
};

void* main(void *args) {
  FilesystemState fs;
  memcpy(&fs, args, sizeof(fs));
  ((FilesystemState*) args)->driverState = (void*) ((intptr_t) 1);
  processYield();
  printDebugString("runFilesystem: Allocating fs.blockBuffer\n");
  fs.blockBuffer = (uint8_t*) malloc(fs.blockSize);

  printDebugString("runFilesystem: Getting partition info\n");
  getPartitionInfoImpl(&fs);
  printDebugString("runFilesystem: Initiallizing driverState\n");
  filesystemInitDriver(&fs);
  printDebugString("runFilesystem: Initialization complete\n");

  while (1) {
    ProcessMessage *msg = processMessageQueueWait(NULL);
    while (msg != NULL) {
      if ((processMessageType(msg) & 0xffffffffffffff00)
        != FILESYSTEM_COMMAND_SIGNATURE
      ) {
        printString("ERROR: ");
        printString(__func__);
        printString(" received unknown signature 0x");
        printHex(processMessageType(msg) & 0xffffffffffffff00);
        printString(" from process ");
        printInt(processPid(processMessageFrom(msg)));
        printString("\n");
        msg = processMessageQueuePop();
        continue;
      }

      FilesystemCommandResponse type =
        (FilesystemCommandResponse) (processMessageType(msg) & 0xff);
      if (type >= NUM_FILESYSTEM_COMMANDS) {
        printString(__func__);
        printString(": ERROR: Received unknown filesystem message type ");
        printInt(type);
        printString(" from process ");
        printInt(processPid(processMessageFrom(msg)));
        printString("\n");
      }

      printDebugString("Handling filesystem message type ");
      printDebugInt(type);
      printDebugString("\n");

      fs.args = msg;
      if (filesystemCommandHandlers[type](&fs) != &fs) {
        printString(__func__);
        printString("ERROR: Calling the filesystem command handler failed\n");
      }

      msg = processMessageQueuePop();
    }
  }

  return 0;
}

