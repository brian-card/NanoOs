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

/// @file DumpOpenFilesCommandHandler.c
///
/// @brief Overlay implementation of a FILESYSTEM_DUMP_OPEN_FILES command
/// handler.

// Standard C includes
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "../../filesystem/include/OverlayFilesystem.h"

// Prototypes used by this overlay.
const char *driverGetFilename(void *fileHandle);

/// @fn void* DumpOpenFiles(void *args)
///
/// @brief Overlay implementation of a dumpOpenFiles function.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the returnValue values of the provided
/// FilesystemDumpOpenFilesArgs to the value that is to be used by the calling
/// process.  This function always returns the filesystemState pointer provided
/// as args.
void* DumpOpenFiles(void *args) {
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

