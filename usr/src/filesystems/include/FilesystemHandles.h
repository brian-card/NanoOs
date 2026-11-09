///////////////////////////////////////////////////////////////////////////////
///
/// @file              FilesystemHandles.h
///
/// @brief             Checks on the open files and directories a filesystem
///                    command handler is handed.
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


#ifndef FILESYSTEM_HANDLES_H
#define FILESYSTEM_HANDLES_H

#include <stdbool.h>
#include <stdint.h>

#include "FilesystemUtils.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def FILESYSTEM_EBADF
///
/// @brief NanoOs's EBADF.  A kernel-linked handler would get the host's value
/// from <errno.h>.
#define FILESYSTEM_EBADF 13

/// @def FILESYSTEM_EMFILE
///
/// @brief NanoOs's EMFILE.
#define FILESYSTEM_EMFILE 29

/// @def FILESYSTEM_ENOMEM
///
/// @brief NanoOs's ENOMEM.
#define FILESYSTEM_ENOMEM 3

/// @def FILESYSTEM_EACCES
///
/// @brief The value of EACCES in src/user/NanoOsErrno.h, which the handlers
/// can't include.
#define FILESYSTEM_EACCES 4

/// @def FILESYSTEM_DIR_GENERATION_MASK
///
/// @brief The bits a slot's generation can use.
#define FILESYSTEM_DIR_GENERATION_MASK \
  ((uint16_t) ((1u << FILESYSTEM_DIR_GENERATION_BITS) - 1u))

/// @fn static inline ProcessId filesystemSenderPid(
///   const FilesystemState *filesystemState, ProcessMessage *processMessage)
///
/// @brief Get the process ID of a message's stamped sender.
///
/// @param filesystemState The filesystem's state.
/// @param processMessage The message.
///
/// @return Returns the sender's process ID, or 0 if the sender isn't in
/// allProcesses.
static inline ProcessId filesystemSenderPid(
  const FilesystemState *filesystemState, ProcessMessage *processMessage
) {
  const ProcessDescriptor *sender = processMessageFrom(processMessage);
  if ((sender < filesystemState->allProcesses)
    || (sender >= &filesystemState->allProcesses[filesystemState->numProcesses])
  ) {
    return 0;
  }

  return processPid(sender);
}

/// @fn static inline bool filesystemFileAllowed(
///   const FilesystemState *filesystemState, ProcessMessage *processMessage,
///   const NanoOsFile *file, uint8_t access)
///
/// @brief Determine whether a message's sender may use an open file.
///
/// @param filesystemState The filesystem's state.
/// @param processMessage The message.
/// @param file The file, which may be NULL.
/// @param access The DIRCAP_READ and DIRCAP_WRITE bits the operation needs.
///
/// @return Returns true if the sender owns the file and it was opened with
/// every bit of access, false if not.
static inline bool filesystemFileAllowed(
  const FilesystemState *filesystemState, ProcessMessage *processMessage,
  const NanoOsFile *file, uint8_t access
) {
  ProcessId sender = filesystemSenderPid(filesystemState, processMessage);
  return (sender != 0) && (file != NULL) && (file->owner == sender)
    && ((file->access & access) == access);
}

/// @fn static inline uint8_t filesystemNumDirSlots(
///   const FilesystemState *filesystemState)
///
/// @brief Get the number of slots in the table of open directories, which is
/// one per process, up to what a handle can address.
///
/// @param filesystemState The filesystem's state.
///
/// @return Returns the number of slots.
static inline uint8_t filesystemNumDirSlots(
  const FilesystemState *filesystemState
) {
  return (filesystemState->numProcesses < FILESYSTEM_MAX_OPEN_DIRS)
    ? (uint8_t) filesystemState->numProcesses
    : (uint8_t) FILESYSTEM_MAX_OPEN_DIRS;
}

/// @fn static inline FilesystemOpenDir* filesystemDirLookup(
///   const FilesystemState *filesystemState, ProcessMessage *processMessage,
///   const DIR *dirp)
///
/// @brief Find the slot a DIR handle names, without ever dereferencing the
/// handle.
///
/// @param filesystemState The filesystem's state.
/// @param processMessage The message that carried the handle.
/// @param dirp The handle.
///
/// @return Returns the slot if the handle is current and the sender owns it,
/// NULL if not.
static inline FilesystemOpenDir* filesystemDirLookup(
  const FilesystemState *filesystemState, ProcessMessage *processMessage,
  const DIR *dirp
) {
  uintptr_t handle = (uintptr_t) dirp;
  ProcessId sender = filesystemSenderPid(filesystemState, processMessage);
  uint8_t index = (uint8_t) (handle & (FILESYSTEM_MAX_OPEN_DIRS - 1));
  if ((sender == 0) || (filesystemState->openDirs == NULL)
    || (handle > (uintptr_t) UINT16_MAX)
    || (index >= filesystemNumDirSlots(filesystemState))
  ) {
    return NULL;
  }

  FilesystemOpenDir *slot = &filesystemState->openDirs[index];
  if ((slot->owner == 0) || (slot->owner != sender)
    || (slot->generation != (uint16_t) (handle >> FILESYSTEM_DIR_INDEX_BITS))
  ) {
    return NULL;
  }

  return slot;
}

/// @fn static inline void filesystemDirRelease(FilesystemOpenDir *slot)
///
/// @brief Free a slot and move it to its next generation, skipping 0 so that
/// a handle is never NULL.
///
/// @param slot The slot.
///
/// @return This function returns no value.
static inline void filesystemDirRelease(FilesystemOpenDir *slot) {
  slot->ino = 0;
  slot->driverHandle = NULL;
  slot->owner = 0;
  slot->flags = 0;
  slot->generation = (uint16_t) ((slot->generation + 1u)
    & FILESYSTEM_DIR_GENERATION_MASK);
  if (slot->generation == 0) {
    slot->generation = 1;
  }
}

#ifdef __cplusplus
}
#endif

#endif // FILESYSTEM_HANDLES_H
