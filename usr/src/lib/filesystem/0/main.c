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
/// @brief Entrypoint into the overlay filesystem driver.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFilesystem.h"

/// @typedef FilesystemCommandHandler
///
/// @brief Definition of the metadata required to handle a filesystem command.
typedef struct FilesystemCommandHandler {
  void *overlay;
  const char *function;
} FilesystemCommandHandler;

/// @var filesystemCommandHandlers
///
/// @brief Array of FilesystemCommandHandler metadata.
const FilesystemCommandHandler filesystemCommandHandlers[] = {
  // FILESYSTEM_OPEN_FILE:
  {OPEN_FILE_OVERLAY, "OpenFile"},
  // FILESYSTEM_CLOSE_FILE:
  {CLOSE_FILE_OVERLAY, "CloseFile"},
  // FILESYSTEM_READ_FILE:
  {READ_FILE_OVERLAY, "ReadFile"},
  // FILESYSTEM_WRITE_FILE:
  {WRITE_FILE_OVERLAY, "WriteFile"},
  // FILESYSTEM_REMOVE_FILE:
  {REMOVE_FILE_OVERLAY, "RemoveFile"},
  // FILESYSTEM_SEEK_FILE:
  {SEEK_FILE_OVERLAY, "SeekFile"},
  // FILESYSTEM_DUMP_OPEN_FILES:
  {DUMP_OPEN_FILES_OVERLAY, "DumpOpenFiles"},
  // FILESYSTEM_GET_FILE_BLOCK_METADATA:
  {GET_FILE_BLOCK_META_OVERLAY, "GetFileBlockMeta"},
};

void* main(void *args) {
  FilesystemState fs;
  memcpy(&fs, args, sizeof(fs));
  processYield();
  printDebugString("runFilesystem: Allocating fs.blockBuffer\n");
  fs.blockBuffer = (uint8_t*) malloc(fs.blockSize);
  
  printDebugString("runFilesystem: Getting partition info\n");
  callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    GET_PARTITION_INFO_OVERLAY, "GetPartitionInfo", &fs);
  printDebugString("runFilesystem: Initiallizing driverState\n");
  callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    DRIVER_INIT_OVERLAY, "DriverInit", &fs);
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

      fs.args = processMessageData(msg);
      if (callOverlayFunction(
        OVERLAY_SAME_NAMESPACE,
        filesystemCommandHandlers[type].overlay,
        filesystemCommandHandlers[type].function,
        &fs) != &fs
      ) {
        printString(__func__);
        printString("ERROR: Calling the ");
        printString(filesystemCommandHandlers[type].function);
        printString(" overlay function failed\n");
      }

      msg = processMessageQueuePop();
    }
  }

  return 0;
}

