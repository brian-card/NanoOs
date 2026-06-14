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

/// @file CloseFileCommandHandler.c
///
/// @brief Overlay implementation of an fclose command handler.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "../../filesystem/include/OverlayFilesystem.h"

// Prototypes used by this overlay.
int driverFclose(void *driverState, void *fileHandle);

/// @fn void* CloseFile(void *args)
///
/// @brief Overlay implementation of an fclose command handler.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a ProcessMessage.
///
/// @return Sets the returnValue member of the provided FilesystemFcloseArgs
/// to the errno for this operation.  This function always returns the
/// filesystemState pointer provided as args.
void* CloseFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
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

