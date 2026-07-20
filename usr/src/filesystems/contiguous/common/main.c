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
int32_t driverFread(
    void *driverState, void *ptr, uint32_t length, void *fileHandle);
int32_t driverFwrite(
    void *driverState, void *ptr, uint32_t length, void *fileHandle);
int driverRemove(void *driverState, const char *pathname);
int driverFseek(void *driverState, void *fileHandle, long offset, int whence);
const char *driverGetFilename(void *fileHandle);
int driverGetFileBlockMetadata(
    void *driverState, void *fileHandle,
    uint32_t *startBlock, uint32_t *numBlocks);
int driverFeof(void *fileHandle);

/// @fn static void* OpenFile(void *args)
///
/// @brief Contiguous implementation of an fopen function.
static void* OpenFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
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

/// @fn static void* CloseFile(void *args)
///
/// @brief Contiguous implementation of an fclose function.
static void* CloseFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
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

/// @fn static void* ReadFile(void *args)
///
/// @brief Contiguous implementation of an fread function.
static void* ReadFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemIoCommandArgs *filesystemIoCommandArgs
    = (FilesystemIoCommandArgs*) processMessageData(processMessage);
  int32_t returnValue = 0;
  if (filesystemState->driverState != NULL) {
    uint32_t length = filesystemIoCommandArgs->length;
    if (length > 0x7fffffff) {
      // Make sure we don't overflow the maximum value of a signed 32-bit int.
      length = 0x7fffffff;
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

/// @fn static void* WriteFile(void *args)
///
/// @brief Contiguous implementation of an fwrite function.
static void* WriteFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemIoCommandArgs *filesystemIoCommandArgs
    = (FilesystemIoCommandArgs*) processMessageData(processMessage);
  int32_t returnValue = 0;
  if (filesystemState->driverState != NULL) {
    uint32_t length = filesystemIoCommandArgs->length;
    if (length > 0x7fffffff) {
      // Make sure we don't overflow the maximum value of a signed 32-bit int.
      length = 0x7fffffff;
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

/// @fn static void* RemoveFile(void *args)
///
/// @brief Contiguous implementation of a remove function.
static void* RemoveFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
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

/// @fn static void* SeekFile(void *args)
///
/// @brief Contiguous implementation of an fseek function.
static void* SeekFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
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

/// @fn static void* DumpOpenFiles(void *args)
///
/// @brief Contiguous implementation of a dumpOpenFiles function.
static void* DumpOpenFiles(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
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

/// @fn static void* GetFileBlockMetadata(void *args)
///
/// @brief Contiguous implementation of a getFileBlockMetadata function.
static void* GetFileBlockMetadata(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
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

/// @fn void* EndOfFile(void *args)
///
/// @brief Contiguous implementation of an feof function.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the metadata value of the provided GetFileBlockMetadataArgs to
//  the value that is to be used by the calling process.  This function always
/// returns the filesystemState pointer provided as args.
void* EndOfFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
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
typedef void* (*FilesystemCommandHandler)(void *args);

/// @var filesystemCommandHandlers
///
/// @brief Array of command handlers, indexed by FilesystemCommandResponse.
static const FilesystemCommandHandler filesystemCommandHandlers[] = {
  OpenFile,             // FILESYSTEM_OPEN_FILE
  CloseFile,            // FILESYSTEM_CLOSE_FILE
  ReadFile,             // FILESYSTEM_READ_FILE
  WriteFile,            // FILESYSTEM_WRITE_FILE
  RemoveFile,           // FILESYSTEM_REMOVE_FILE
  SeekFile,             // FILESYSTEM_SEEK_FILE
  DumpOpenFiles,        // FILESYSTEM_DUMP_OPEN_FILES
  GetFileBlockMetadata, // FILESYSTEM_GET_FILE_BLOCK_METADATA
  EndOfFile,            // FILESYSTEM_END_OF_FILE
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

