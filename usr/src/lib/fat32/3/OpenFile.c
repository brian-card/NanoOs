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

/// @file OpenFile.c
///
/// @brief Overlay implementation of fopen for FAT32.

// Standard C includes
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "KernelProcesses.h"
#include "NanoOsUtils.h"
#include "OverlayFat32.h"

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Parse a C standard library mode string ("r", "w", "a" and their "+"
///        variants) into a set of boolean flags.
///
/// @param mode   The null-terminated mode string.
/// @param flags  [out] Populated with the parsed flags.
///
/// @return FAT32_SUCCESS on a recognised mode, FAT32_INVALID_PARAMETER
///         otherwise.
///
int fat32ParseMode(const char *mode, Fat32OpenMode *flags) {
  memset(flags, 0, sizeof(Fat32OpenMode));

  if ((mode == NULL) || (mode[0] == '\0')) {
    return FAT32_INVALID_PARAMETER;
  }

  switch (mode[0]) {
    case 'r':
      flags->canRead   = true;
      flags->mustExist = true;
      if (mode[1] == '+') {
        flags->canWrite = true;
      }
      break;

    case 'w':
      flags->canWrite  = true;
      flags->truncate  = true;
      if (mode[1] == '+') {
        flags->canRead = true;
      }
      break;

    case 'a':
      flags->canWrite    = true;
      flags->appendMode  = true;
      if (mode[1] == '+') {
        flags->canRead = true;
      }
      break;

    default:
      return FAT32_INVALID_PARAMETER;
  }

  return FAT32_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
///
/// @brief Open a file on a FAT32 filesystem.
///
/// @details Resolves the full path by walking intermediate directories,
///          searches the target directory for the file name, and applies the
///          semantics of the C standard library mode string:
///
///          "r"  — open for reading; file must exist.
///          "r+" — open for reading and writing; file must exist.
///          "w"  — open for writing; create or truncate.
///          "w+" — open for reading and writing; create or truncate.
///          "a"  — open for appending; create if absent.
///          "a+" — open for reading and appending; create if absent.
///
/// @param driverState  Pointer to a Fat32DriverState (passed as void*).
/// @param filePath     The null-terminated path to the file.
/// @param mode         The null-terminated C fopen mode string.
///
/// @return A pointer to a heap-allocated Fat32FileHandle on success, or NULL
///         on failure.
///
void* fat32Fopen(
    void *driverState,
    const char *filePath,
    const char *mode
) {
  Fat32DriverState *ds = (Fat32DriverState *) driverState;
  if ((ds == NULL) || (filePath == NULL) || (mode == NULL)) {
    return NULL;
  }

  // ---- Parse the mode string ----
  Fat32OpenMode modeFlags;
  if (fat32ParseMode(mode, &modeFlags) != FAT32_SUCCESS) {
    return NULL;
  }

  // ---- Resolve the parent directory ----
  uint32_t    parentCluster;
  const char *fileName = NULL;

  if (fat32ResolveParentDirectory(ds, filePath, &parentCluster, &fileName)
      != FAT32_SUCCESS) {
    return NULL;
  }

  if ((fileName == NULL) || (fileName[0] == '\0')) {
    return NULL;
  }

  // ---- Search for the file in the parent directory ----
  Fat32DirSearchResult searchResult;
  searchResult.longName = NULL;

  int searchStatus = fat32SearchDirectory(
    ds, parentCluster, fileName, &searchResult);

  if (searchStatus == FAT32_SUCCESS) {
    // File exists.

    // Reject attempts to open a directory as a regular file.
    if (searchResult.entry.attributes & FAT32_ATTR_DIRECTORY) {
      free(searchResult.longName);
      return NULL;
    }

    // "w" / "w+" — truncate the existing file to zero length.
    if (modeFlags.truncate) {
      if (fat32TruncateFile(ds, &searchResult) != FAT32_SUCCESS) {
        free(searchResult.longName);
        return NULL;
      }
    }
  } else if (searchStatus == FAT32_FILE_NOT_FOUND) {
    // File does not exist.

    if (modeFlags.mustExist) {
      // "r" / "r+" — the file must already be present.
      free(searchResult.longName);
      return NULL;
    }

    // "w", "w+", "a", "a+" — create a new, empty file.
    if (fat32CreateFileEntry(ds, parentCluster, fileName, &searchResult)
        != FAT32_SUCCESS) {
      free(searchResult.longName);
      return NULL;
    }
  } else {
    // An I/O or allocation error occurred during the search.
    free(searchResult.longName);
    return NULL;
  }

  // ---- Build the file handle ----
  Fat32FileHandle *handle
    = fat32CreateFileHandle(ds, &searchResult, &modeFlags);
  free(searchResult.longName);

  return (void *) handle;
}

/// @fn void* OpenFile(void *args)
///
/// @brief Overlay implementation of a FAT32 fopen function.
///
/// @param args A pointer to a FilesystemState, cast to a void*.  The args
///   member variable is a pointer to a FilesystemFopenArgs.
///
/// @return Sets the returnValue member of the provided FilesystemFopenArgs
/// to a valid FILE pointer on success, sets it to NULL on failure.  This
/// function always returns the filesystemState pointer provided as args.
void* OpenFile(void *args) {
  FilesystemState *filesystemState = (FilesystemState*) args;
  NanoOsFile *nanoOsFile = NULL;
  ProcessMessage *processMessage = (ProcessMessage*) filesystemState->args;
  FilesystemFopenArgs *fopenArgs
    = (FilesystemFopenArgs*) processMessageData(processMessage);

  printDebugString("Opening file \"");
  printDebugString(pathname);
  printDebugString("\" in mode \"");
  printDebugString(mode);
  printDebugString("\"\n");

  if (filesystemState->driverState != NULL) {
    void *fileHandle = fat32Fopen(
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
        filesystemState->driverFclose(filesystemState->driverState, fileHandle);
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

