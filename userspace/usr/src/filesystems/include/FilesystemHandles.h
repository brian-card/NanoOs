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
#include <stdlib.h>

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

/// @def FILESYSTEM_FILE_GENERATION_MASK
///
/// @brief The bits an open-file slot's generation can use.
#define FILESYSTEM_FILE_GENERATION_MASK \
  ((uint16_t) ((1u << FILESYSTEM_FILE_GENERATION_BITS) - 1u))

/// @fn static inline FILE* filesystemFileHandle(
///   const FilesystemState *filesystemState, const FilesystemOpenFile *slot)
///
/// @brief Get the handle that names an open-file slot.
///
/// @param filesystemState The filesystem's state.
/// @param slot The slot.
///
/// @return Returns the handle.
static inline FILE* filesystemFileHandle(
  const FilesystemState *filesystemState, const FilesystemOpenFile *slot
) {
  return (FILE*) (uintptr_t) (
    (((uintptr_t) slot->generation) << FILESYSTEM_FILE_INDEX_BITS)
    | (uintptr_t) (slot - filesystemState->openFileSlots));
}

/// @fn static FilesystemOpenFile* filesystemFileLookup(
///   const FilesystemState *filesystemState, ProcessMessage *processMessage,
///   const FILE *stream, uint8_t access)
///
/// @brief Find the slot a FILE handle names, without ever dereferencing the
/// handle, if the message's sender may use it.  Not inlined because a copy in
/// every handler doesn't fit the Agon's contiguous filesystem region as well
/// as one call does.
///
/// @param filesystemState The filesystem's state.
/// @param processMessage The message that carried the handle.
/// @param stream The handle.
/// @param access The DIRCAP_READ and DIRCAP_WRITE bits the operation needs.
///
/// @return Returns the slot if the handle is current, the sender owns the file,
/// and the file was opened with every bit of access, NULL if not.
static __attribute__((noinline, unused)) FilesystemOpenFile*
filesystemFileLookup(
  const FilesystemState *filesystemState, ProcessMessage *processMessage,
  const FILE *stream, uint8_t access
) {
  uintptr_t handle = (uintptr_t) stream;
  ProcessId sender = filesystemSenderPid(filesystemState, processMessage);
  uint8_t index = (uint8_t) (handle & (FILESYSTEM_MAX_OPEN_FILES - 1));
  if ((sender == 0) || (handle > (uintptr_t) UINT16_MAX)
    || (index >= filesystemState->numOpenFileSlots)
  ) {
    return NULL;
  }

  FilesystemOpenFile *slot = &filesystemState->openFileSlots[index];
  if ((slot->file == NULL)
    || (slot->generation != (uint16_t) (handle >> FILESYSTEM_FILE_INDEX_BITS))
    || (slot->file->owner != sender)
    || ((slot->file->access & access) != access)
  ) {
    return NULL;
  }

  return slot;
}

/// @fn static inline FilesystemOpenFile* filesystemFileAllocate(
///   FilesystemState *filesystemState, int *errorNumber)
///
/// @brief Find the free open-file slot with the lowest generation, growing the
/// table if every slot is in use.
///
/// @param filesystemState The filesystem's state.
/// @param errorNumber Where to store the errno value on failure.
///
/// @return Returns the slot, or NULL if the table can't grow.
static inline FilesystemOpenFile* filesystemFileAllocate(
  FilesystemState *filesystemState, int *errorNumber
) {
  uint8_t numSlots = filesystemState->numOpenFileSlots;
  FilesystemOpenFile *slot = NULL;
  for (uint8_t ii = 0; ii < numSlots; ii++) {
    FilesystemOpenFile *candidate = &filesystemState->openFileSlots[ii];
    if ((candidate->file == NULL)
      && ((slot == NULL) || (candidate->generation < slot->generation))
    ) {
      slot = candidate;
    }
  }
  if (slot != NULL) {
    return slot;
  }

  if (numSlots >= FILESYSTEM_MAX_OPEN_FILES) {
    *errorNumber = FILESYSTEM_EMFILE;
    return NULL;
  }
  FilesystemOpenFile *slots = (FilesystemOpenFile*) realloc(
    filesystemState->openFileSlots,
    (numSlots + FILESYSTEM_FILE_SLOT_GROWTH) * sizeof(FilesystemOpenFile));
  if (slots == NULL) {
    *errorNumber = FILESYSTEM_ENOMEM;
    return NULL;
  }
  for (uint8_t ii = numSlots; ii < numSlots + FILESYSTEM_FILE_SLOT_GROWTH;
    ii++
  ) {
    slots[ii].file = NULL;
    slots[ii].generation = 1;
  }
  filesystemState->openFileSlots = slots;
  filesystemState->numOpenFileSlots = numSlots + FILESYSTEM_FILE_SLOT_GROWTH;

  return &slots[numSlots];
}

/// @fn static inline void filesystemFileRelease(
///   FilesystemState *filesystemState, FilesystemOpenFile *slot)
///
/// @brief Free an open-file slot's file and move the slot to its next
/// generation, skipping 0 so that a handle is never NULL.
///
/// @param filesystemState The filesystem's state.
/// @param slot The slot.
///
/// @return This function returns no value.
static inline void filesystemFileRelease(
  FilesystemState *filesystemState, FilesystemOpenFile *slot
) {
  free(slot->file);
  slot->file = NULL;
  slot->generation = (uint16_t) ((slot->generation + 1u)
    & FILESYSTEM_FILE_GENERATION_MASK);
  if (slot->generation == 0) {
    slot->generation = 1;
  }
  if (filesystemState->numOpenFiles > 0) {
    filesystemState->numOpenFiles--;
  }
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
