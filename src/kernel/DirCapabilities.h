///////////////////////////////////////////////////////////////////////////////
///
/// @file              DirCapabilities.h
///
/// @brief             Directory capabilities:  the directories a process may
///                    use, and what it may do in each.
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

#ifndef DIR_CAPABILITIES_H
#define DIR_CAPABILITIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @def DIRCAP_READ
///
/// @brief Open a file for reading.
#define DIRCAP_READ   ((uint8_t) 0x01)

/// @def DIRCAP_WRITE
///
/// @brief Open a file for writing, appending, or reading and writing, or
/// replace an existing file by renaming another over it.
#define DIRCAP_WRITE  ((uint8_t) 0x02)

/// @def DIRCAP_CREATE
///
/// @brief Create a file or directory, or be the destination of a rename to a
/// name that doesn't exist yet.  With DIRCAP_DELETE, also replace an existing
/// file by rename.
#define DIRCAP_CREATE ((uint8_t) 0x04)

/// @def DIRCAP_DELETE
///
/// @brief Remove a file or directory, or be the source of a rename.  With
/// DIRCAP_CREATE, also replace an existing file by rename.
#define DIRCAP_DELETE ((uint8_t) 0x08)

/// @def DIRCAP_LIST
///
/// @brief Open a directory to read its entries.
#define DIRCAP_LIST   ((uint8_t) 0x10)

/// @def DIRCAP_STAT
///
/// @brief Get the status of a file or directory.
#define DIRCAP_STAT   ((uint8_t) 0x20)

/// @def DIRCAP_EXEC
///
/// @brief Run a program.
#define DIRCAP_EXEC   ((uint8_t) 0x40)

/// @def DIRCAP_ALL
///
/// @brief Every capability bit.
#define DIRCAP_ALL    ((uint8_t) 0x7f)

/// @def DIRCAP_FULL_PATH
///
/// @brief Marks an entry whose name is a full path on the volume rather than
/// a path under /userspace.  An entry only matches requests of its own kind.
#define DIRCAP_FULL_PATH ((uint8_t) 0x80)

/// @struct DirCapability
///
/// @brief One directory and the operations allowed in it and in every
/// directory below it.  Entries are packed back to back, so the next one
/// starts at &name[len].
///
/// @param mask The DIRCAP_* bits allowed.
/// @param len The length of name, including its trailing '/'.
/// @param name The canonical absolute directory path, ending in '/' and not
///   NUL-terminated.
typedef struct DirCapability {
  uint8_t mask;
  uint8_t len;
  char    name[1];
} DirCapability;

/// @struct DirCapabilities
///
/// @brief A process's set of directory capabilities.
///
/// @param size The total size of the block, in bytes.
/// @param refCount The number of processes sharing the block, or 0 for a
///   static set that is never modified or freed.
/// @param numCapabilities The number of entries in capabilities.
/// @param capabilities The packed entries.
typedef struct DirCapabilities {
  uint16_t      size;
  uint8_t       refCount;
  uint8_t       numCapabilities;
  DirCapability capabilities[1];
} DirCapabilities;

/// @fn static inline uint8_t dirCapabilityMaskFromLetters(const char *letters)
///
/// @brief Convert the permission letters of a capabilities file to a mask.
///
/// @param letters The NUL-terminated letters.
///
/// @return Returns the DIRCAP_* bits, or 0 if a letter is invalid or there
/// are none.
static inline uint8_t dirCapabilityMaskFromLetters(const char *letters) {
  uint8_t mask = 0;
  for (; *letters != '\0'; letters++) {
    if (*letters == 'r') {
      mask |= DIRCAP_READ;
    } else if (*letters == 'w') {
      mask |= DIRCAP_WRITE;
    } else if (*letters == 'c') {
      mask |= DIRCAP_CREATE;
    } else if (*letters == 'd') {
      mask |= DIRCAP_DELETE;
    } else if (*letters == 'l') {
      mask |= DIRCAP_LIST;
    } else if (*letters == 's') {
      mask |= DIRCAP_STAT;
    } else if (*letters == 'x') {
      mask |= DIRCAP_EXEC;
    } else {
      return 0;
    }
  }
  return mask;
}

/// @fn static inline uint8_t dirCapabilitySplitLine(char *line, char **fields)
///
/// @brief Split a line of a capabilities file in place.  A comment or a blank
/// line has no fields.
///
/// @param line The NUL-terminated line.
/// @param fields Where to store pointers to the three fields.
///
/// @return Returns the number of fields, up to four, so that a result other
/// than 0 or 3 is a malformed line.
static inline uint8_t dirCapabilitySplitLine(char *line, char **fields) {
  uint8_t numFields = 0;
  bool inField = false;
  for (char *cursor = line; *cursor != '\0'; cursor++) {
    bool isSpace = (*cursor == ' ') || (*cursor == '\t')
      || (*cursor == '\r') || (*cursor == '\n');
    if (isSpace) {
      *cursor = '\0';
      inField = false;
    } else if (!inField) {
      if ((numFields == 0) && (*cursor == '#')) {
        return 0;
      } else if (numFields == 3) {
        return 4;
      }
      fields[numFields++] = cursor;
      inField = true;
    }
  }
  return numFields;
}

/// @def DIR_CAPABILITIES_HEADER_SIZE
///
/// @brief The size of a DirCapabilities block with no entries.
#define DIR_CAPABILITIES_HEADER_SIZE offsetof(DirCapabilities, capabilities)

/// @def DIR_CAPABILITY_SIZE
///
/// @brief The size of an entry whose name is nameLength bytes long.
#define DIR_CAPABILITY_SIZE(nameLength) \
  (offsetof(DirCapability, name) + (nameLength))

/// @def DIR_CAPABILITIES_STATIC
///
/// @brief The type of a static set whose packed entries are the bytes of the
/// string literal entryBytes.  Its layout matches DirCapabilities, plus the
/// literal's NUL, which size doesn't count.
#define DIR_CAPABILITIES_STATIC(entryBytes) \
  struct { \
    uint16_t size; \
    uint8_t  refCount; \
    uint8_t  numCapabilities; \
    char     capabilities[sizeof(entryBytes)]; \
  }

/// @def DIR_CAPABILITIES_STATIC_INITIALIZER
///
/// @brief The initializer for a DIR_CAPABILITIES_STATIC(entryBytes) set with
/// numEntries entries.
#define DIR_CAPABILITIES_STATIC_INITIALIZER(numEntries, entryBytes) { \
    .size = DIR_CAPABILITIES_HEADER_SIZE + sizeof(entryBytes) - 1, \
    .refCount = 0, \
    .numCapabilities = (numEntries), \
    .capabilities = entryBytes, \
  }

/// @typedef DirCapabilitiesRealloc
///
/// @brief The allocator a set is changed with:  realloc semantics, where a
/// size of 0 frees the block.
typedef void* (*DirCapabilitiesRealloc)(void *ptr, size_t size);

bool dirCapabilityAllows(const DirCapabilities *set,
  const char *path, size_t pathLength, bool isDirectory, uint8_t bits);
int dirCapabilityPut(DirCapabilities **setp,
  const char *name, size_t nameLength, uint8_t mask,
  DirCapabilitiesRealloc reallocFn);
int dirCapabilityAdd(DirCapabilities **setp,
  const char *name, size_t nameLength, uint8_t mask,
  DirCapabilitiesRealloc reallocFn);
int dirCapabilityDrop(DirCapabilities **setp,
  const char *name, size_t nameLength, DirCapabilitiesRealloc reallocFn);
bool dirCapabilitiesValid(const DirCapabilities *set);
int dirCapabilitiesCopy(DirCapabilities **copyp,
  const DirCapabilities *source, const DirCapabilities *bound,
  DirCapabilitiesRealloc reallocFn);
DirCapabilities* dirCapabilitiesShare(DirCapabilities *set);
void dirCapabilitiesRelease(DirCapabilities *set,
  DirCapabilitiesRealloc reallocFn);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // DIR_CAPABILITIES_H
