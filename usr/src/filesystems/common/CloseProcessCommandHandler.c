///////////////////////////////////////////////////////////////////////////////
///
/// @file              CloseProcessCommandHandler.c
///
/// @brief             Close everything a process that has exited left open.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////


#include <stdlib.h>
#include <string.h>

// NanoOs includes
#ifndef NANO_OS_KERNEL_BUILD
#include "ExecutiveProcesses.h"
#include "NanoOsUtils.h"
#include "FilesystemUtils.h"
#endif // NANO_OS_KERNEL_BUILD
#include "../include/FilesystemHandles.h"

// Prototypes used by this handler.
int driverClosedir(void *driverState, void *dirHandle);
int driverFclose(void *driverState, void *fileHandle);

/// @fn static void closeProcessDirs(FilesystemState *filesystemState,
///   ProcessId pid)
///
/// @brief Close every directory a process has open.
///
/// @param filesystemState The filesystem's state.
/// @param pid The process.
///
/// @return This function returns no value.
static void closeProcessDirs(FilesystemState *filesystemState, ProcessId pid) {
  if (filesystemState->openDirs == NULL) {
    return;
  }

  uint8_t numSlots = filesystemNumDirSlots(filesystemState);
  for (uint8_t ii = 0; ii < numSlots; ii++) {
    FilesystemOpenDir *slot = &filesystemState->openDirs[ii];
    if (slot->owner == pid) {
      driverClosedir(filesystemState->driverState, slot->driverHandle);
      filesystemDirRelease(slot);
    }
  }
}

/// @fn static void closeProcessFiles(FilesystemState *filesystemState,
///   ProcessId pid)
///
/// @brief Close every file a process has open.
///
/// @param filesystemState The filesystem's state.
/// @param pid The process.
///
/// @return This function returns no value.
static void closeProcessFiles(FilesystemState *filesystemState, ProcessId pid) {
  NanoOsFile *file = filesystemState->openFiles;
  while (file != NULL) {
    NanoOsFile *next = file->next;
    if (file->owner == pid) {
      driverFclose(filesystemState->driverState, file->file);
      if (next != NULL) {
        next->prev = file->prev;
      }
      if (file->prev != NULL) {
        file->prev->next = next;
      } else {
        filesystemState->openFiles = next;
      }
      if (filesystemState->numOpenFiles > 0) {
        filesystemState->numOpenFiles--;
      }
      free(file);
    }
    file = next;
  }
}

/// @fn void* CloseProcess(void *args)
///
/// @brief Close everything a process that has exited left open, so that the
/// next process given its pid can't pass an ownership check on it.  Only a
/// kernel process may ask.
///
/// @param args A pointer to the FilesystemState, cast to a void*.
///
/// @return Returns args.
void* CloseProcess(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemCloseProcessArgs *closeProcessArgs
    = (FilesystemCloseProcessArgs*) processMessageData(processMessage);

  ProcessId sender = filesystemSenderPid(filesystemState, processMessage);
  if ((sender != 0) && (closeProcessArgs != NULL)
    && (closeProcessArgs->pid != 0)
    && (filesystemState->driverState != NULL)
    && (filesystemState->allProcesses[sender - 1].privilegeLevel
      == PRIVILEGE_LEVEL_KERNEL)
  ) {
    closeProcessDirs(filesystemState, closeProcessArgs->pid);
    closeProcessFiles(filesystemState, closeProcessArgs->pid);
  }

  processMessageSetDone(processMessage);
  return filesystemState;
}
