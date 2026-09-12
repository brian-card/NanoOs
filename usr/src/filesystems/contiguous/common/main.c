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
/// callOverlayFunction.  The command handlers themselves live once, under
/// usr/src/filesystems/common/, and are shared verbatim (via symlink) with
/// the overlay-filesystem packaging and (via #include) with the
/// kernel-linked filesystem build.

// Standard C includes
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"
#include "FilesystemUtils.h"

// Prototypes for the driver-agnostic pre-mount functions and for the command
// handler functions linked in from usr/src/filesystems/common/.
FilesystemState* filesystemInitDriver(FilesystemState *filesystemState);
FilesystemState* getPartitionInfoImpl(FilesystemState *filesystemState);
void* OpenFile(void *args);
void* CloseFile(void *args);
void* ReadFile(void *args);
void* WriteFile(void *args);
void* RemoveFile(void *args);
void* SeekFile(void *args);
void* DumpOpenFiles(void *args);
void* GetFileBlockMetadata(void *args);
void* EndOfFile(void *args);
void* Format(void *args);

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
  Format,               // FILESYSTEM_FORMAT
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
  fs.args = NULL;
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
