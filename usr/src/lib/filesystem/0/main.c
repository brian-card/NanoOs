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
/// @brief Entrypoint into the FAT32 filesystem driver.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "Filesystem.h"

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
  {((void*) 3), "OpenFile"},
  // FILESYSTEM_CLOSE_FILE:
  {((void*) 4), "CloseFile"},
  // FILESYSTEM_READ_FILE:
  {((void*) 5), "ReadFile"},
  // FILESYSTEM_WRITE_FILE:
  {((void*) 6), "WriteFile"},
  // FILESYSTEM_REMOVE_FILE:
  {((void*) 7), "RemoveFile"},
  // FILESYSTEM_SEEK_FILE:
  {((void*) 8), "SeekFile"},
  // FILESYSTEM_DUMP_OPEN_FILES:
  {((void*) 9), "DumpOpenFiles"},
  // FILESYSTEM_GET_FILE_BLOCK_METADATA:
  {((void*) 10), "GetFileBlockMeta"},
};

void* main(void *args) {
  FilesystemState fs;
  memcpy(&fs, args, sizeof(fs));
  processYield();
  printDebugString("runFilesystem: Allocating fs.blockBuffer\n");
  fs.blockBuffer = (uint8_t*) malloc(fs.blockSize);
  
  printDebugString("runFilesystem: Getting partition info\n");
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, ((void*) 1),
    "GetPartitionInfo", &fs);
  printDebugString("runFilesystem: Initiallizing driverState\n");
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, ((void*) 2),
    "DriverInit", &fs);
  printDebugString("runFilesystem: Initialization complete\n");
  
  while (1) {
    ProcessMessage *msg = processMessageQueueWait(NULL);
    while (msg != NULL) {
      if ((processMessageType(msg) & 0xffffffffffffff00)
        != FILESYSTEM_COMMAND_SIGNATURE
      ) {
        printString("Error: ");
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
        printString(": ERROR! Received unknown filesystem message type ");
        printInt(type);
        printString(" from process ");
        printInt(processPid(processMessageFrom(msg)));
        printString("\n");
      }

      printDebugString("Handling filesystem message type ");
      printDebugInt(type);
      printDebugString("\n");

      fs.args = processMessageData(msg);
      callOverlayFunction(
        OVERLAY_SAME_NAMESPACE,
        filesystemCommandHandlers[type].overlay,
        filesystemCommandHandlers[type].function,
        &fs);

      msg = processMessageQueuePop();
    }
  }

  return 0;
}

