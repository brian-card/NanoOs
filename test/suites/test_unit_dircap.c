///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.07.2026
///
/// @file              test_unit_dircap.c
///
/// @brief             Unit tests for matching and changing sets of directory
///                    capabilities.
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

#include "NanoOsTest.h"

#include <stdlib.h>
#include <string.h>

#include "kernel/DirCapabilities.h"
#include "user/NanoOsErrno.h"

/// @struct Entry
///
/// @brief One entry of a set a test starts from.
typedef struct Entry {
  const char *name;
  uint8_t mask;
} Entry;

static int liveBlocks;
static int allocations;

/// @fn static void* countingRealloc(void *ptr, size_t size)
///
/// @brief Allocate with the host's allocator, counting the blocks allocated
/// and the blocks still live.
///
/// @return Returns what realloc returns, or NULL when size is 0.
static void* countingRealloc(void *ptr, size_t size) {
  if (size == 0) {
    if (ptr != NULL) {
      free(ptr);
      liveBlocks--;
    }
    return NULL;
  }
  allocations++;
  if (ptr == NULL) {
    void *block = malloc(size);
    if (block != NULL) {
      liveBlocks++;
    }
    return block;
  }
  return realloc(ptr, size);
}

/// @fn static void* failingRealloc(void *ptr, size_t size)
///
/// @brief Fail every allocation, the way an exhausted heap does.
///
/// @return Returns NULL.
static void* failingRealloc(void *ptr, size_t size) {
  (void) ptr;
  (void) size;
  allocations++;
  return NULL;
}

/// @fn static size_t fillSet(void *block, uint8_t refCount,
///   const Entry *entries, size_t numEntries)
///
/// @brief Write a set into a block, or only measure it when block is NULL.
///
/// @return Returns the set's size in bytes.
static size_t fillSet(void *block, uint8_t refCount, const Entry *entries,
  size_t numEntries
) {
  size_t size = DIR_CAPABILITIES_HEADER_SIZE;
  for (size_t ii = 0; ii < numEntries; ii++) {
    size_t length = strlen(entries[ii].name);
    if (block != NULL) {
      DirCapability *capability = (DirCapability*) ((char*) block + size);
      capability->mask = entries[ii].mask;
      capability->len = (uint8_t) length;
      memcpy(capability->name, entries[ii].name, length);
    }
    size += DIR_CAPABILITY_SIZE(length);
  }
  if (block != NULL) {
    DirCapabilities *set = (DirCapabilities*) block;
    set->size = (uint16_t) size;
    set->refCount = refCount;
    set->numCapabilities = (uint8_t) numEntries;
  }
  return size;
}

/// @fn static DirCapabilities* makeSet(const Entry *entries,
///   size_t numEntries)
///
/// @brief Allocate a dynamic set, held by one process, from a list of entries.
///
/// @return Returns the set.
static DirCapabilities* makeSet(const Entry *entries, size_t numEntries) {
  DirCapabilities *set = (DirCapabilities*) countingRealloc(NULL,
    fillSet(NULL, 1, entries, numEntries));
  NANO_OS_ASSERT_NOT_NULL(set);
  fillSet(set, 1, entries, numEntries);
  allocations = 0;
  return set;
}

/// @fn static void resetCounts(void)
///
/// @brief Start a test with no blocks counted.
///
/// @return This function returns no value.
static void resetCounts(void) {
  liveBlocks = 0;
  allocations = 0;
}

/// @fn static bool allows(const DirCapabilities *set, const char *path,
///   bool isDirectory, uint8_t bits)
///
/// @brief Check a path against a set.
///
/// @return Returns what dirCapabilityAllows returns.
static bool allows(const DirCapabilities *set, const char *path,
  bool isDirectory, uint8_t bits
) {
  return dirCapabilityAllows(set, path, strlen(path), isDirectory, bits);
}

/// @fn static int add(DirCapabilities **setp, const char *name, uint8_t mask)
///
/// @brief Add an entry with the counting allocator.
///
/// @return Returns what dirCapabilityAdd returns.
static int add(DirCapabilities **setp, const char *name, uint8_t mask) {
  return dirCapabilityAdd(setp, name, strlen(name), mask, countingRealloc);
}

/// @fn static int drop(DirCapabilities **setp, const char *name)
///
/// @brief Drop an entry with the counting allocator.
///
/// @return Returns what dirCapabilityDrop returns.
static int drop(DirCapabilities **setp, const char *name) {
  return dirCapabilityDrop(setp, name, strlen(name), countingRealloc);
}

/// @fn static void release(DirCapabilities *set)
///
/// @brief Release a set with the counting allocator.
///
/// @return This function returns no value.
static void release(DirCapabilities *set) {
  dirCapabilitiesRelease(set, countingRealloc);
}

NANO_OS_TEST(unit_dircap, a_prefix_matches_the_directory_and_below) {
  resetCounts();
  Entry entries[] = {{"/home/briana/", DIRCAP_READ}};
  DirCapabilities *set = makeSet(entries, 1);
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/notes.txt", false,
    DIRCAP_READ));
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/a/b/c", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/briana/notes.txt", false,
    DIRCAP_WRITE));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/other/notes.txt", false,
    DIRCAP_READ));
  release(set);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}

NANO_OS_TEST(unit_dircap, a_near_miss_does_not_match) {
  resetCounts();
  Entry briana[] = {{"/home/briana/", DIRCAP_ALL}};
  DirCapabilities *set = makeSet(briana, 1);
  NANO_OS_ASSERT_FALSE(allows(set, "/home/bri", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/bri", true, DIRCAP_LIST));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/brianax/f", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/brianax", true, DIRCAP_LIST));
  release(set);

  Entry bri[] = {{"/home/bri/", DIRCAP_ALL}};
  set = makeSet(bri, 1);
  NANO_OS_ASSERT_FALSE(allows(set, "/home/briana/f", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/briana", true, DIRCAP_LIST));
  release(set);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}

NANO_OS_TEST(unit_dircap, a_name_that_differs_only_in_case_does_not_match) {
  resetCounts();
  Entry entries[] = {{"/home/briana/", DIRCAP_ALL}};
  DirCapabilities *set = makeSet(entries, 1);
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/f", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(set, "/Home/briana/f", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/Briana/f", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(set, "/HOME/BRIANA", true, DIRCAP_LIST));
  release(set);
}

NANO_OS_TEST(unit_dircap, a_directory_matches_its_own_name_with_a_slash) {
  resetCounts();
  Entry entries[] = {{"/home/briana/", DIRCAP_LIST}};
  DirCapabilities *set = makeSet(entries, 1);
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana", true, DIRCAP_LIST));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/briana", false, DIRCAP_LIST));
  NANO_OS_ASSERT_FALSE(allows(set, "/home", true, DIRCAP_LIST));
  release(set);

  Entry root[] = {{"/", DIRCAP_LIST}};
  set = makeSet(root, 1);
  NANO_OS_ASSERT_TRUE(allows(set, "/", true, DIRCAP_LIST));
  NANO_OS_ASSERT_TRUE(allows(set, "/etc", true, DIRCAP_LIST));
  release(set);
}

NANO_OS_TEST(unit_dircap, a_capabilitys_own_directory_belongs_to_its_parent) {
  resetCounts();
  Entry own[] = {{"/home/briana/", DIRCAP_ALL}};
  DirCapabilities *set = makeSet(own, 1);
  NANO_OS_ASSERT_FALSE(allows(set, "/home/briana", false, DIRCAP_DELETE));
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/sub", false, DIRCAP_DELETE));
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/a/b", false, DIRCAP_CREATE));
  release(set);

  Entry parent[] = {{"/home/", DIRCAP_DELETE}};
  set = makeSet(parent, 1);
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana", false, DIRCAP_DELETE));
  release(set);
}

NANO_OS_TEST(unit_dircap, bits_from_different_entries_combine) {
  resetCounts();
  Entry entries[] = {
    {"/home/", DIRCAP_READ},
    {"/home/briana/", DIRCAP_WRITE},
  };
  DirCapabilities *set = makeSet(entries, 2);
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/f", false,
    DIRCAP_READ | DIRCAP_WRITE));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/f", false,
    DIRCAP_READ | DIRCAP_WRITE));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/f", false, 0));
  NANO_OS_ASSERT_FALSE(allows(NULL, "/home/f", false, DIRCAP_READ));
  release(set);
}

NANO_OS_TEST(unit_dircap, a_subset_can_be_added) {
  resetCounts();
  Entry entries[] = {{"/home/", DIRCAP_READ | DIRCAP_WRITE | DIRCAP_LIST}};
  DirCapabilities *set = makeSet(entries, 1);
  NANO_OS_ASSERT_EQ_INT(0, add(&set, "/home/briana/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(2, set->numCapabilities);
  NANO_OS_ASSERT_EQ_INT(1, set->refCount);
  NANO_OS_ASSERT_EQ_INT(fillSet(NULL, 1, (Entry[]) {
    {"/home/", 0}, {"/home/briana/", 0}}, 2), set->size);
  NANO_OS_ASSERT_EQ_INT(0, drop(&set, "/home/"));
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/f", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/briana/f", false, DIRCAP_WRITE));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/f", false, DIRCAP_READ));
  release(set);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}

NANO_OS_TEST(unit_dircap, a_superset_cannot_be_added) {
  resetCounts();
  Entry entries[] = {{"/home/", DIRCAP_READ | DIRCAP_LIST}};
  DirCapabilities *set = makeSet(entries, 1);
  DirCapabilities *before = set;
  NANO_OS_ASSERT_EQ_INT(-EACCES,
    add(&set, "/home/briana/", DIRCAP_READ | DIRCAP_DELETE));
  NANO_OS_ASSERT_EQ_INT(-EACCES, add(&set, "/etc/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(-EACCES, add(&set, "/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_PTR(before, set);
  NANO_OS_ASSERT_EQ_INT(1, set->numCapabilities);
  NANO_OS_ASSERT_EQ_INT(0, allocations);
  release(set);
}

NANO_OS_TEST(unit_dircap, adding_a_held_name_replaces_its_mask) {
  resetCounts();
  Entry entries[] = {{"/etc/", DIRCAP_READ | DIRCAP_WRITE}};
  DirCapabilities *set = makeSet(entries, 1);
  NANO_OS_ASSERT_EQ_INT(0, add(&set, "/etc/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(1, set->numCapabilities);
  NANO_OS_ASSERT_TRUE(allows(set, "/etc/passwd", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(set, "/etc/passwd", false, DIRCAP_WRITE));
  NANO_OS_ASSERT_EQ_INT(-EACCES,
    add(&set, "/etc/", DIRCAP_READ | DIRCAP_WRITE));
  NANO_OS_ASSERT_FALSE(allows(set, "/etc/passwd", false, DIRCAP_WRITE));
  release(set);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}

NANO_OS_TEST(unit_dircap, the_last_entry_can_be_dropped) {
  resetCounts();
  Entry entries[] = {{"/tmp/", DIRCAP_ALL}};
  DirCapabilities *set = makeSet(entries, 1);
  NANO_OS_ASSERT_EQ_INT(0, drop(&set, "/tmp/"));
  NANO_OS_ASSERT_EQ_INT(0, set->numCapabilities);
  NANO_OS_ASSERT_EQ_INT(DIR_CAPABILITIES_HEADER_SIZE, set->size);
  NANO_OS_ASSERT_FALSE(allows(set, "/tmp/f", false, DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(0, drop(&set, "/tmp/"));
  NANO_OS_ASSERT_EQ_INT(-EACCES, add(&set, "/tmp/", DIRCAP_READ));
  release(set);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}

NANO_OS_TEST(unit_dircap, dropping_a_name_not_held_changes_nothing) {
  resetCounts();
  Entry entries[] = {{"/home/briana/", DIRCAP_ALL}};
  DirCapabilities *set = makeSet(entries, 1);
  DirCapabilities *before = set;
  NANO_OS_ASSERT_EQ_INT(0, drop(&set, "/home/"));
  NANO_OS_ASSERT_EQ_INT(0, drop(&set, "/home/briana"));
  NANO_OS_ASSERT_EQ_PTR(before, set);
  NANO_OS_ASSERT_EQ_INT(1, set->numCapabilities);
  NANO_OS_ASSERT_EQ_INT(0, allocations);
  release(set);
}

NANO_OS_TEST(unit_dircap, long_names_pack_back_to_back) {
  resetCounts();
  Entry entries[] = {{"/home/", DIRCAP_ALL}};
  DirCapabilities *set = makeSet(entries, 1);
  NANO_OS_ASSERT_EQ_INT(0, add(&set, "/home/briana/projects/nanoos/",
    DIRCAP_READ | DIRCAP_WRITE));
  NANO_OS_ASSERT_EQ_INT(0, add(&set, "/home/briana/tmp/", DIRCAP_CREATE));
  NANO_OS_ASSERT_EQ_INT(0, drop(&set, "/home/"));
  NANO_OS_ASSERT_EQ_INT(2, set->numCapabilities);
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/projects/nanoos/a.c", false,
    DIRCAP_READ | DIRCAP_WRITE));
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/tmp/vi12", false,
    DIRCAP_CREATE));
  NANO_OS_ASSERT_FALSE(allows(set, "/home/briana/tmp/vi12", false,
    DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(0, drop(&set, "/home/briana/projects/nanoos/"));
  NANO_OS_ASSERT_EQ_INT(1, set->numCapabilities);
  NANO_OS_ASSERT_TRUE(allows(set, "/home/briana/tmp/vi12", false,
    DIRCAP_CREATE));
  NANO_OS_ASSERT_EQ_INT(fillSet(NULL, 1, (Entry[]) {
    {"/home/briana/tmp/", 0}}, 1), set->size);
  release(set);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}

NANO_OS_TEST(unit_dircap, invalid_names_and_masks_are_refused) {
  resetCounts();
  Entry entries[] = {{"/", DIRCAP_ALL}};
  DirCapabilities *set = makeSet(entries, 1);
  const char *invalid[] = {
    "", "/home", "home/", "/home//", "//", "/home/./", "/home/../", "/./",
    "/../",
  };
  for (size_t ii = 0; ii < sizeof(invalid) / sizeof(invalid[0]); ii++) {
    NANO_OS_ASSERT_EQ_INT(-EINVAL, add(&set, invalid[ii], DIRCAP_READ));
  }
  NANO_OS_ASSERT_EQ_INT(-EINVAL,
    dirCapabilityAdd(&set, "/a\0b/", 5, DIRCAP_READ, countingRealloc));
  NANO_OS_ASSERT_EQ_INT(-EINVAL, add(&set, "/home/", 0));
  NANO_OS_ASSERT_EQ_INT(-EINVAL, add(&set, "/home/", 0x80));
  NANO_OS_ASSERT_EQ_INT(0, add(&set, "/.config/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(0, add(&set, "/a..b/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(3, set->numCapabilities);
  release(set);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}

NANO_OS_TEST(unit_dircap, sharing_allocates_nothing) {
  resetCounts();
  Entry entries[] = {{"/home/", DIRCAP_ALL}};
  DirCapabilities *parent = makeSet(entries, 1);
  DirCapabilities *child = dirCapabilitiesShare(parent);
  NANO_OS_ASSERT_EQ_PTR(parent, child);
  NANO_OS_ASSERT_EQ_INT(2, parent->refCount);
  NANO_OS_ASSERT_EQ_INT(0, allocations);
  release(child);
  NANO_OS_ASSERT_EQ_INT(1, parent->refCount);
  NANO_OS_ASSERT_EQ_INT(1, liveBlocks);
  release(parent);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
  NANO_OS_ASSERT_NULL(dirCapabilitiesShare(NULL));
}

NANO_OS_TEST(unit_dircap, a_change_to_a_shared_set_leaves_the_other_holder) {
  resetCounts();
  Entry entries[] = {{"/home/", DIRCAP_ALL}, {"/tmp/", DIRCAP_ALL}};
  DirCapabilities *parent = makeSet(entries, 2);
  uint16_t parentSize = parent->size;

  DirCapabilities *child = dirCapabilitiesShare(parent);
  NANO_OS_ASSERT_EQ_INT(0, add(&child, "/home/briana/", DIRCAP_READ));
  NANO_OS_ASSERT_TRUE(child != parent);
  NANO_OS_ASSERT_EQ_INT(1, child->refCount);
  NANO_OS_ASSERT_EQ_INT(1, parent->refCount);
  NANO_OS_ASSERT_EQ_INT(2, parent->numCapabilities);
  NANO_OS_ASSERT_EQ_INT(parentSize, parent->size);
  NANO_OS_ASSERT_EQ_INT(3, child->numCapabilities);

  DirCapabilities *second = dirCapabilitiesShare(parent);
  NANO_OS_ASSERT_EQ_INT(0, drop(&second, "/home/"));
  NANO_OS_ASSERT_TRUE(second != parent);
  NANO_OS_ASSERT_TRUE(allows(parent, "/home/f", false, DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(allows(second, "/home/f", false, DIRCAP_READ));
  NANO_OS_ASSERT_TRUE(allows(second, "/tmp/f", false, DIRCAP_READ));

  DirCapabilities *third = dirCapabilitiesShare(parent);
  NANO_OS_ASSERT_EQ_INT(0, add(&third, "/tmp/", DIRCAP_READ));
  NANO_OS_ASSERT_TRUE(third != parent);
  NANO_OS_ASSERT_TRUE(allows(parent, "/tmp/f", false, DIRCAP_WRITE));
  NANO_OS_ASSERT_FALSE(allows(third, "/tmp/f", false, DIRCAP_WRITE));

  release(child);
  release(second);
  release(third);
  release(parent);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}

NANO_OS_TEST(unit_dircap, a_static_set_is_never_changed_or_freed) {
  resetCounts();
  static union {
    DirCapabilities set;
    uint8_t bytes[32];
  } root;
  Entry entries[] = {{"/", DIRCAP_ALL}};
  fillSet(&root, 0, entries, 1);
  uint8_t original[sizeof(root.bytes)];
  memcpy(original, root.bytes, sizeof(original));

  DirCapabilities *set = dirCapabilitiesShare(&root.set);
  NANO_OS_ASSERT_EQ_PTR(&root.set, set);
  release(set);
  NANO_OS_ASSERT_EQ_INT(0, allocations);

  NANO_OS_ASSERT_EQ_INT(0, add(&set, "/home/", DIRCAP_READ));
  NANO_OS_ASSERT_TRUE(set != &root.set);
  NANO_OS_ASSERT_EQ_INT(1, set->refCount);
  release(set);

  set = &root.set;
  NANO_OS_ASSERT_EQ_INT(0, drop(&set, "/"));
  NANO_OS_ASSERT_TRUE(set != &root.set);
  release(set);

  set = &root.set;
  NANO_OS_ASSERT_EQ_INT(0, add(&set, "/", DIRCAP_READ));
  NANO_OS_ASSERT_TRUE(set != &root.set);
  release(set);

  NANO_OS_ASSERT_EQ_INT(0, memcmp(original, root.bytes, sizeof(original)));
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}

NANO_OS_TEST(unit_dircap, running_out_of_memory_leaves_the_set_unchanged) {
  resetCounts();
  Entry entries[] = {{"/home/", DIRCAP_ALL}};
  DirCapabilities *parent = makeSet(entries, 1);
  DirCapabilities *set = parent;
  NANO_OS_ASSERT_EQ_INT(-ENOMEM, dirCapabilityAdd(&set, "/home/briana/",
    strlen("/home/briana/"), DIRCAP_READ, failingRealloc));
  NANO_OS_ASSERT_EQ_PTR(parent, set);
  NANO_OS_ASSERT_EQ_INT(1, set->numCapabilities);

  DirCapabilities *child = dirCapabilitiesShare(parent);
  NANO_OS_ASSERT_EQ_INT(-ENOMEM,
    dirCapabilityDrop(&child, "/home/", strlen("/home/"), failingRealloc));
  NANO_OS_ASSERT_EQ_INT(-ENOMEM, dirCapabilityAdd(&child, "/home/",
    strlen("/home/"), DIRCAP_READ, failingRealloc));
  NANO_OS_ASSERT_EQ_PTR(parent, child);
  NANO_OS_ASSERT_EQ_INT(2, parent->refCount);
  NANO_OS_ASSERT_TRUE(allows(parent, "/home/f", false, DIRCAP_WRITE));
  release(child);
  release(parent);
  NANO_OS_ASSERT_EQ_INT(0, liveBlocks);
}
