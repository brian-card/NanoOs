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

///////////////////////////////////////////////////////////////////////////////
///
/// @file              DirCapabilities.c
///
/// @brief             Matching and changing sets of directory capabilities.
///
///////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "DirCapabilities.h"
#include "../user/NanoOsErrno.h"

/// @fn static inline DirCapability* dirCapabilityNext(
///   const DirCapability *capability)
///
/// @brief Get the entry that follows one in its set.
///
/// @param capability The entry.
///
/// @return Returns a pointer to the next entry.
static inline DirCapability* dirCapabilityNext(
  const DirCapability *capability
) {
  return (DirCapability*) &capability->name[capability->len];
}

/// @fn static inline bool dirCapabilityMatches(
///   const DirCapability *capability, const char *path, size_t pathLength,
///   bool isDirectory)
///
/// @brief Determine whether an entry covers a path:  its name is a prefix of
/// the path, or of the path with a '/' appended when the path is a directory.
///
/// @param capability The entry.
/// @param path The canonical absolute path.
/// @param pathLength The length of path.
/// @param isDirectory Whether path names a directory being used as one.
///
/// @return Returns true if the entry covers the path, false if not.
static inline bool dirCapabilityMatches(const DirCapability *capability,
  const char *path, size_t pathLength, bool isDirectory
) {
  size_t length = capability->len;
  if (length <= pathLength) {
    return memcmp(capability->name, path, length) == 0;
  }

  // A name always ends in '/', so this is the name equal to path + "/".
  return isDirectory && (length == pathLength + 1)
    && (memcmp(capability->name, path, pathLength) == 0);
}

/// @fn static DirCapability* dirCapabilityFind(const DirCapabilities *set,
///   const char *name, size_t nameLength)
///
/// @brief Find the entry with exactly a given name.
///
/// @param set The set to search.
/// @param name The name to find.
/// @param nameLength The length of name.
///
/// @return Returns a pointer to the entry, or NULL if the set doesn't hold it.
static DirCapability* dirCapabilityFind(const DirCapabilities *set,
  const char *name, size_t nameLength
) {
  const DirCapability *capability = set->capabilities;
  for (uint8_t ii = 0; ii < set->numCapabilities; ii++) {
    if ((capability->len == nameLength)
      && (memcmp(capability->name, name, nameLength) == 0)
    ) {
      // We have to cast away the const qualifier here.
      return (DirCapability*) capability;
    }
    capability = dirCapabilityNext(capability);
  }

  return NULL;
}

/// @fn static bool dirCapabilityNameIsValid(const char *name,
///   size_t nameLength)
///
/// @brief Determine whether a name is a canonical absolute directory path
/// ending in '/':  no empty, ".", or ".." components.
///
/// @param name The name to check.
/// @param nameLength The length of name.
///
/// @return Returns true if the name is valid, false if not.
static bool dirCapabilityNameIsValid(const char *name, size_t nameLength) {
  if ((nameLength == 0) || (nameLength > UINT8_MAX)
    || (name[0] != '/') || (name[nameLength - 1] != '/')
  ) {
    return false;
  }

  size_t start = 1;
  for (size_t ii = 1; ii < nameLength; ii++) {
    if (name[ii] == '\0') {
      return false;
    } else if (name[ii] != '/') {
      continue;
    }

    size_t componentLength = ii - start;
    if ((componentLength == 0)
      || ((name[start] == '.')
        && ((componentLength == 1)
          || ((componentLength == 2) && (name[start + 1] == '.'))))
    ) {
      return false;
    }
    start = ii + 1;
  }

  return true;
}

/// @fn static DirCapabilities* dirCapabilitiesUnshare(
///   DirCapabilities **setp, size_t newSize, DirCapabilitiesRealloc reallocFn)
///
/// @brief Give the caller a block of newSize bytes, holding the set's current
/// contents, that no other process shares.  A shared or static set is copied
/// and the caller's reference to it released.
///
/// @param setp A pointer to the caller's set, updated on success.
/// @param newSize The size the block must have.  It is at least the set's
///   current size.
/// @param reallocFn The allocator.
///
/// @return Returns the writable block on success, NULL if memory ran out, in
/// which case *setp is unchanged.
static DirCapabilities* dirCapabilitiesUnshare(DirCapabilities **setp,
  size_t newSize, DirCapabilitiesRealloc reallocFn
) {
  DirCapabilities *set = *setp;
  DirCapabilities *writable = NULL;
  if (set->refCount == 1) {
    writable = (newSize == set->size) ? set : reallocFn(set, newSize);
  } else {
    writable = reallocFn(NULL, newSize);
    if (writable != NULL) {
      memcpy(writable, set, set->size);
      writable->refCount = 1;
      dirCapabilitiesRelease(set, reallocFn);
    }
  }

  if (writable != NULL) {
    *setp = writable;
  }
  return writable;
}

/// @fn bool dirCapabilityAllows(const DirCapabilities *set,
///   const char *path, size_t pathLength, bool isDirectory, uint8_t bits)
///
/// @brief Determine whether a set allows an operation on a path:  every bit
/// it needs is held by some entry that covers the path.
///
/// @param set The caller's set.
/// @param path The canonical absolute path.
/// @param pathLength The length of path.
/// @param isDirectory Whether path names a directory being used as one, such
///   as by opendir, rather than an entry being created or removed.
/// @param bits The DIRCAP_* bits the operation needs.
///
/// @return Returns true if the operation is allowed, false if not.
bool dirCapabilityAllows(const DirCapabilities *set,
  const char *path, size_t pathLength, bool isDirectory, uint8_t bits
) {
  if ((set == NULL) || (path == NULL) || (bits == 0)) {
    return false;
  }

  uint8_t held = 0;
  const DirCapability *capability = set->capabilities;
  for (uint8_t ii = 0; ii < set->numCapabilities; ii++) {
    if (dirCapabilityMatches(capability, path, pathLength, isDirectory)) {
      held |= capability->mask;
      if ((held & bits) == bits) {
        return true;
      }
    }
    capability = dirCapabilityNext(capability);
  }

  return false;
}

/// @fn int dirCapabilityPut(DirCapabilities **setp,
///   const char *name, size_t nameLength, uint8_t mask,
///   DirCapabilitiesRealloc reallocFn)
///
/// @brief Add an entry to a set, or replace the mask of the entry it already
/// has with that name, without checking the mask against the set.  Used to
/// build a set whose entries are checked elsewhere.
///
/// @param setp A pointer to the set, updated if the block changes.  A NULL
///   set is created.
/// @param name The canonical absolute directory path, ending in '/'.
/// @param nameLength The length of name.
/// @param mask The DIRCAP_* bits to allow.
/// @param reallocFn The allocator.
///
/// @return Returns 0 on success, -EINVAL for an invalid name or mask, -ENOSPC
/// if the set is full, or -ENOMEM if memory ran out.
int dirCapabilityPut(DirCapabilities **setp,
  const char *name, size_t nameLength, uint8_t mask,
  DirCapabilitiesRealloc reallocFn
) {
  if ((setp == NULL) || (name == NULL)
    || (mask == 0) || ((mask & ~DIRCAP_ALL) != 0)
    || !dirCapabilityNameIsValid(name, nameLength)
  ) {
    return -EINVAL;
  }

  DirCapabilities *set = *setp;
  if (set == NULL) {
    set = (DirCapabilities*) reallocFn(NULL, DIR_CAPABILITIES_HEADER_SIZE);
    if (set == NULL) {
      return -ENOMEM;
    }
    set->size = DIR_CAPABILITIES_HEADER_SIZE;
    set->refCount = 1;
    set->numCapabilities = 0;
    *setp = set;
  }

  DirCapability *existing = dirCapabilityFind(set, name, nameLength);
  if (existing != NULL) {
    if (existing->mask == mask) {
      return 0;
    }

    size_t offset = (size_t) ((char*) existing - (char*) set);
    set = dirCapabilitiesUnshare(setp, set->size, reallocFn);
    if (set == NULL) {
      return -ENOMEM;
    }
    ((DirCapability*) ((char*) set + offset))->mask = mask;
    return 0;
  }

  size_t oldSize = set->size;
  size_t newSize = oldSize + DIR_CAPABILITY_SIZE(nameLength);
  if ((newSize > UINT16_MAX) || (set->numCapabilities == UINT8_MAX)) {
    return -ENOSPC;
  }
  set = dirCapabilitiesUnshare(setp, newSize, reallocFn);
  if (set == NULL) {
    return -ENOMEM;
  }

  DirCapability *capability = (DirCapability*) ((char*) set + oldSize);
  capability->mask = mask;
  capability->len = (uint8_t) nameLength;
  memcpy(capability->name, name, nameLength);
  set->size = (uint16_t) newSize;
  set->numCapabilities++;
  return 0;
}

/// @fn int dirCapabilityAdd(DirCapabilities **setp,
///   const char *name, size_t nameLength, uint8_t mask,
///   DirCapabilitiesRealloc reallocFn)
///
/// @brief Add an entry to a set, or replace the mask of the entry it already
/// has with that name.  The set must already allow every bit of the mask on
/// the name.
///
/// @param setp A pointer to the caller's set, updated if the block changes.
/// @param name The canonical absolute directory path, ending in '/'.
/// @param nameLength The length of name.
/// @param mask The DIRCAP_* bits to allow.
/// @param reallocFn The allocator.
///
/// @return Returns 0 on success, -EINVAL for an invalid name or an empty mask,
/// -EACCES if the set doesn't already allow the mask, -ENOSPC if the set is
/// full, or -ENOMEM if memory ran out.
int dirCapabilityAdd(DirCapabilities **setp,
  const char *name, size_t nameLength, uint8_t mask,
  DirCapabilitiesRealloc reallocFn
) {
  if ((setp == NULL) || (*setp == NULL) || (name == NULL)
    || (mask == 0) || ((mask & ~DIRCAP_ALL) != 0)
    || !dirCapabilityNameIsValid(name, nameLength)
  ) {
    return -EINVAL;
  } else if (!dirCapabilityAllows(*setp, name, nameLength, false, mask)) {
    return -EACCES;
  }

  return dirCapabilityPut(setp, name, nameLength, mask, reallocFn);
}

/// @fn int dirCapabilityDrop(DirCapabilities **setp,
///   const char *name, size_t nameLength, DirCapabilitiesRealloc reallocFn)
///
/// @brief Remove the entry with a given name from a set.
///
/// @param setp A pointer to the caller's set, updated if the block changes.
/// @param name The name of the entry to remove.
/// @param nameLength The length of name.
/// @param reallocFn The allocator.
///
/// @return Returns 0 on success, including when the set has no such entry,
/// -EINVAL for a NULL argument, or -ENOMEM if memory ran out.
int dirCapabilityDrop(DirCapabilities **setp,
  const char *name, size_t nameLength, DirCapabilitiesRealloc reallocFn
) {
  if ((setp == NULL) || (*setp == NULL) || (name == NULL)) {
    return -EINVAL;
  }

  DirCapabilities *set = *setp;
  DirCapability *capability = dirCapabilityFind(set, name, nameLength);
  if (capability == NULL) {
    return 0;
  }

  size_t offset = (size_t) ((char*) capability - (char*) set);
  size_t capabilitySize = DIR_CAPABILITY_SIZE(capability->len);
  size_t newSize = set->size - capabilitySize;
  size_t tailSize = set->size - offset - capabilitySize;

  if (set->refCount == 1) {
    memmove((char*) set + offset, (char*) set + offset + capabilitySize,
      tailSize);
    // A failed shrink leaves the larger block, which is still correct.
    DirCapabilities *shrunk = reallocFn(set, newSize);
    if (shrunk != NULL) {
      set = shrunk;
    }
  } else {
    DirCapabilities *copy = reallocFn(NULL, newSize);
    if (copy == NULL) {
      return -ENOMEM;
    }
    memcpy(copy, set, offset);
    memcpy((char*) copy + offset, (char*) set + offset + capabilitySize,
      tailSize);
    copy->refCount = 1;
    dirCapabilitiesRelease(set, reallocFn);
    set = copy;
  }

  set->size = (uint16_t) newSize;
  set->numCapabilities--;
  *setp = set;
  return 0;
}

/// @fn bool dirCapabilitiesValid(const DirCapabilities *set)
///
/// @brief Determine whether a block is a well-formed set:  its entries fill
/// exactly its size, and each has a valid name and mask.
///
/// @param set The block to check.
///
/// @return Returns true if the set is well formed, false if not.
bool dirCapabilitiesValid(const DirCapabilities *set) {
  if ((set == NULL) || (set->size < DIR_CAPABILITIES_HEADER_SIZE)) {
    return false;
  }

  size_t offset = DIR_CAPABILITIES_HEADER_SIZE;
  for (uint8_t ii = 0; ii < set->numCapabilities; ii++) {
    if (offset + DIR_CAPABILITY_SIZE(0) > set->size) {
      return false;
    }
    const DirCapability *capability
      = (const DirCapability*) ((const char*) set + offset);
    offset += DIR_CAPABILITY_SIZE(capability->len);
    if ((offset > set->size) || (capability->mask == 0)
      || ((capability->mask & ~DIRCAP_ALL) != 0)
      || !dirCapabilityNameIsValid(capability->name, capability->len)
    ) {
      return false;
    }
  }

  return offset == set->size;
}

/// @fn int dirCapabilitiesCopy(DirCapabilities **copyp,
///   const DirCapabilities *source, const DirCapabilities *bound,
///   DirCapabilitiesRealloc reallocFn)
///
/// @brief Copy a set into a new block that no other process shares, after
/// checking that it is well formed and that bound allows every entry.
///
/// @param copyp Where to store the new block.  Unchanged on failure.
/// @param source The set to copy, which may be in another process's memory.
/// @param bound The set every entry of source must be allowed by.
/// @param reallocFn The allocator.
///
/// @return Returns 0 on success, -EINVAL if source is malformed, -EACCES if
/// bound doesn't allow an entry, or -ENOMEM if memory ran out.
int dirCapabilitiesCopy(DirCapabilities **copyp,
  const DirCapabilities *source, const DirCapabilities *bound,
  DirCapabilitiesRealloc reallocFn
) {
  if ((copyp == NULL) || !dirCapabilitiesValid(source)) {
    return -EINVAL;
  }

  const DirCapability *capability = source->capabilities;
  for (uint8_t ii = 0; ii < source->numCapabilities; ii++) {
    if (!dirCapabilityAllows(bound, capability->name, capability->len, false,
      capability->mask)
    ) {
      return -EACCES;
    }
    capability = dirCapabilityNext(capability);
  }

  DirCapabilities *copy = (DirCapabilities*) reallocFn(NULL, source->size);
  if (copy == NULL) {
    return -ENOMEM;
  }
  memcpy(copy, source, source->size);
  copy->refCount = 1;
  *copyp = copy;
  return 0;
}

/// @fn DirCapabilities* dirCapabilitiesShare(DirCapabilities *set)
///
/// @brief Take another reference to a set, for a process that starts with
/// the same set as its parent.  Nothing is allocated or copied.
///
/// @param set The set to share.
///
/// @return Returns set on success, or NULL if set is NULL or already has the
/// most references a set can have.
DirCapabilities* dirCapabilitiesShare(DirCapabilities *set) {
  if ((set == NULL) || (set->refCount == UINT8_MAX)) {
    return NULL;
  } else if (set->refCount != 0) {
    set->refCount++;
  }

  return set;
}

/// @fn void dirCapabilitiesRelease(DirCapabilities *set,
///   DirCapabilitiesRealloc reallocFn)
///
/// @brief Release a reference to a set, freeing it when the last reference
/// goes.  A static set is never freed.
///
/// @param set The set to release.  NULL is ignored.
/// @param reallocFn The allocator.
///
/// @return This function returns no value.
void dirCapabilitiesRelease(DirCapabilities *set,
  DirCapabilitiesRealloc reallocFn
) {
  if ((set == NULL) || (set->refCount == 0)) {
    return;
  }

  set->refCount--;
  if (set->refCount == 0) {
    reallocFn(set, 0);
  }
}
