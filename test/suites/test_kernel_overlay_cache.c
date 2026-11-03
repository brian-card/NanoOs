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

/// @file test_kernel_overlay_cache.c
///
/// @brief Kernel tests for the overlay file cache's FIFO.

#include "NanoOsTest.h"

#include <stdlib.h>
#include <string.h>

#include "kernel/MemoryManager.h"
#include "kernel/Overlay.h"
#include "kernel/OverlayFunctions.h"
#include "kernel/Processes.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsSpawn.h"

#include "HalMock.h"

#define CACHE_TEST_DEPTH 3

// Defined in Overlay.c
extern NanoOsOverlayMap *overlayMap;

static const char *_cacheTestIds[] = {
  "/usr/bin/one/main.overlay",
  "/usr/bin/two/main.overlay",
  "/usr/bin/six/main.overlay",
  "/usr/bin/ten/main.overlay",
  "/usr/bin/two/Edit.overlay",
};

/// @fn static void overlayCacheTestPreBoot(void)
///
/// @brief Give the kernel under test an overlay file cache.
static void overlayCacheTestPreBoot(void) {
  halMockSetFileCacheDepth(CACHE_TEST_DEPTH);
}

/// @fn static OverlayFifoNode* overlayCacheTestPush(const char *id)
///
/// @brief Push an ID into the file cache with a one-block stand-in overlay.
///
/// @param id The NUL-terminated overlay ID to push.
///
/// @return Returns the value returned by overlayFifoPush.
static OverlayFifoNode* overlayCacheTestPush(const char *id) {
  static BlockDevice blockDevice = { .blockSize = 16 };
  FileBlockMetadata standIn = {
    .blockDevice = &blockDevice, .startBlock = 1, .numBlocks = 1,
  };
  return overlayFifoPush(
    fileCache, (void*) id, (uint8_t) (strlen(id) + 1), &standIn);
}

/// @fn static OverlayFifoNode* overlayCacheTestGet(const char *id)
///
/// @brief Look up an ID in the file cache.
///
/// @param id The NUL-terminated overlay ID to look up.
///
/// @return Returns the value returned by overlayFifoGet.
static OverlayFifoNode* overlayCacheTestGet(const char *id) {
  return overlayFifoGet(fileCache, (void*) id, (uint8_t) (strlen(id) + 1));
}

NANO_OS_KERNEL_TEST_WITH(overlay_cache, full_cache_finds_every_entry,
  overlayCacheTestPreBoot
) {
  NANO_OS_ASSERT_NOT_NULL(fileCache);
  for (int ii = 0; ii < CACHE_TEST_DEPTH; ii++) {
    NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[ii]));
  }

  for (int ii = 0; ii < CACHE_TEST_DEPTH; ii++) {
    OverlayFifoNode *node = overlayCacheTestGet(_cacheTestIds[ii]);
    NANO_OS_ASSERT_NOT_NULL(node);
    NANO_OS_ASSERT_STR_EQ(_cacheTestIds[ii], (char*) node->overlayId);
  }
}

NANO_OS_KERNEL_TEST_WITH(overlay_cache, eviction_keeps_the_new_entry,
  overlayCacheTestPreBoot
) {
  for (int ii = 0; ii <= CACHE_TEST_DEPTH; ii++) {
    NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[ii]));
  }

  // Every entry had the same misses remaining, so the oldest one goes.
  NANO_OS_ASSERT_NULL(overlayCacheTestGet(_cacheTestIds[0]));
  for (int ii = 1; ii <= CACHE_TEST_DEPTH; ii++) {
    OverlayFifoNode *node = overlayCacheTestGet(_cacheTestIds[ii]);
    NANO_OS_ASSERT_NOT_NULL(node);
    NANO_OS_ASSERT_STR_EQ(_cacheTestIds[ii], (char*) node->overlayId);
  }
}

NANO_OS_KERNEL_TEST_WITH(overlay_cache, push_after_eviction_and_pop_keeps_entries,
  overlayCacheTestPreBoot
) {
  for (int ii = 0; ii <= CACHE_TEST_DEPTH; ii++) {
    NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[ii]));
  }
  NANO_OS_ASSERT_NOT_NULL(overlayFifoPop(fileCache));
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[4]));

  NANO_OS_ASSERT_NULL(overlayCacheTestGet(_cacheTestIds[0]));
  NANO_OS_ASSERT_NULL(overlayCacheTestGet(_cacheTestIds[1]));
  for (int ii = 2; ii <= 4; ii++) {
    OverlayFifoNode *node = overlayCacheTestGet(_cacheTestIds[ii]);
    NANO_OS_ASSERT_NOT_NULL(node);
    NANO_OS_ASSERT_STR_EQ(_cacheTestIds[ii], (char*) node->overlayId);
  }
}

NANO_OS_KERNEL_TEST_WITH(overlay_cache, push_takes_no_memory_from_the_caller,
  overlayCacheTestPreBoot
) {
  // Exhaust the heap so that any allocation the push makes on the caller's
  // behalf fails.  Nothing smaller than an ID can still be free afterward.
  void *blocks[256];
  int numBlocks = 0;
  size_t blockSize = 65536;
  while ((blockSize >= 16) && (numBlocks < 256)) {
    blocks[numBlocks] = malloc(blockSize);
    if (blocks[numBlocks] == NULL) {
      blockSize /= 2;
    } else {
      numBlocks++;
    }
  }

  OverlayFifoNode *node = overlayCacheTestPush(_cacheTestIds[0]);
  for (int ii = 0; ii < numBlocks; ii++) {
    free(blocks[ii]);
  }

  NANO_OS_ASSERT_TRUE(blockSize < 16);
  NANO_OS_ASSERT_NOT_NULL(node);
  node = overlayCacheTestGet(_cacheTestIds[0]);
  NANO_OS_ASSERT_NOT_NULL(node);
  NANO_OS_ASSERT_STR_EQ(_cacheTestIds[0], (char*) node->overlayId);
}

NANO_OS_KERNEL_TEST_WITH(overlay_cache, overlong_id_is_not_cached,
  overlayCacheTestPreBoot
) {
  char id[OVERLAY_FIFO_ID_SIZE + 1];
  memset(id, 'a', sizeof(id) - 1);
  id[sizeof(id) - 1] = '\0';

  errno = 0;
  NANO_OS_ASSERT_NULL(overlayCacheTestPush(id));
  NANO_OS_ASSERT_EQ_INT(ENAMETOOLONG, errno);
  NANO_OS_ASSERT_EQ_INT(0, fileCache->numElements);
}

/// @fn static void overlayCacheTestSpawnPreBoot(void)
///
/// @brief Give the kernel under test an overlay file cache and overlay-based
/// commands.
static void overlayCacheTestSpawnPreBoot(void) {
  overlayCacheTestPreBoot();
  halMockSetExecCommand(execOverlayCommand);
}

NANO_OS_KERNEL_TEST_WITH(overlay_cache, spawn_finds_a_cached_main_overlay,
  overlayCacheTestSpawnPreBoot
) {
  // There's no root filesystem here, so only the cache can resolve the path.
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush("/cachetest/main.overlay"));

  pid_t pid = 0;
  char *argv[] = { "cachetest", NULL };
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawn(&pid, "/cachetest", NULL, NULL, argv, NULL));
  NANO_OS_ASSERT_TRUE(pid > 0);
}

// Defined in Scheduler.c
int schedulerLoadOverlay(ProcessDescriptor *processDescriptor, char **envp);

/// @var _cacheTestDiskReads
///
/// @brief The number of times cacheTestReadBlocks has been called.
static int _cacheTestDiskReads = 0;

/// @fn static int cacheTestReadBlocks(void *context, uint32_t startBlock,
///   uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer)
///
/// @brief Count a disk read and fill buffer with a valid overlay header.
///
/// @param context Unused.
/// @param startBlock Unused.
/// @param numBlocks Unused.
/// @param blockSize Unused.
/// @param buffer The overlay memory to fill.
///
/// @return Returns 0.
static int cacheTestReadBlocks(void *context, uint32_t startBlock,
  uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer
) {
  (void) context;
  (void) startBlock;
  (void) numBlocks;
  (void) blockSize;
  _cacheTestDiskReads++;
  NanoOsOverlayMap *map = (NanoOsOverlayMap*) buffer;
  memset(&map->header, 0, sizeof(map->header));
  map->header.version = NANO_OS_OVERLAY_VERSION;
  map->header.magic = NANO_OS_OVERLAY_MAGIC;
  return 0;
}

/// @fn static void cacheTestInitDescriptor(
///   ProcessDescriptor *processDescriptor, const char *id)
///
/// @brief Set up a process descriptor whose file overlay is read through
/// cacheTestReadBlocks.
///
/// @param processDescriptor The descriptor to set up.
/// @param id The NUL-terminated overlay ID of the process's overlay.
///
/// @return This function returns no value.
static void cacheTestInitDescriptor(
  ProcessDescriptor *processDescriptor, const char *id
) {
  static BlockDevice blockDevice = {
    .readBlocks = cacheTestReadBlocks,
    .blockSize = 512,
  };
  memset(processDescriptor, 0, sizeof(*processDescriptor));
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_USER;
  processDescriptor->overlay.fileBlockMetadata.blockDevice = &blockDevice;
  processDescriptor->overlay.fileBlockMetadata.startBlock = 7;
  processDescriptor->overlay.fileBlockMetadata.numBlocks = 1;
  processDescriptor->overlay.overlayId = (void*) id;
  processDescriptor->overlay.overlayIdLength = (uint8_t) (strlen(id) + 1);
}

/// @fn static int cacheTestSwitchTo(ProcessDescriptor *processDescriptor)
///
/// @brief Load a process's overlay as the scheduler does when it resumes the
/// process after another overlay has run.
///
/// @param processDescriptor The descriptor whose overlay to load.
///
/// @return Returns the value returned by schedulerLoadOverlay.
static int cacheTestSwitchTo(ProcessDescriptor *processDescriptor) {
  overlayMap->header.overlay.fileBlockMetadata.startBlock = 0;
  return schedulerLoadOverlay(processDescriptor, NULL);
}

NANO_OS_KERNEL_TEST_WITH(overlay_cache, evicted_overlay_is_cached_again_on_load,
  overlayCacheTestPreBoot
) {
  ProcessDescriptor processDescriptor;
  cacheTestInitDescriptor(&processDescriptor, _cacheTestIds[0]);
  _cacheTestDiskReads = 0;

  NANO_OS_ASSERT_EQ_INT(0, cacheTestSwitchTo(&processDescriptor));
  NANO_OS_ASSERT_EQ_INT(1, _cacheTestDiskReads);
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestGet(_cacheTestIds[0]));

  for (int ii = 1; ii <= CACHE_TEST_DEPTH; ii++) {
    NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[ii]));
  }
  NANO_OS_ASSERT_NULL(overlayCacheTestGet(_cacheTestIds[0]));

  NANO_OS_ASSERT_EQ_INT(0, cacheTestSwitchTo(&processDescriptor));
  NANO_OS_ASSERT_EQ_INT(2, _cacheTestDiskReads);
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestGet(_cacheTestIds[0]));

  NANO_OS_ASSERT_EQ_INT(0, cacheTestSwitchTo(&processDescriptor));
  NANO_OS_ASSERT_EQ_INT(2, _cacheTestDiskReads);
}

NANO_OS_KERNEL_TEST_WITH(overlay_cache, load_from_cache_marks_the_overlay_used,
  overlayCacheTestPreBoot
) {
  ProcessDescriptor processDescriptor;
  cacheTestInitDescriptor(&processDescriptor, "/usr/lib/x/main.overlay");

  // Leave the hand on the process's node with no misses remaining.
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[0]));
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[1]));
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[2]));
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[3]));
  NANO_OS_ASSERT_EQ_INT(0, cacheTestSwitchTo(&processDescriptor));
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[4]));
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[0]));
  OverlayFifoNode *node = processDescriptor.overlay.overlayFifoNode;
  NANO_OS_ASSERT_NOT_NULL(node);
  NANO_OS_ASSERT_STR_EQ("/usr/lib/x/main.overlay", (char*) node->overlayId);
  NANO_OS_ASSERT_EQ_INT(0, node->missesRemaining);

  NANO_OS_ASSERT_EQ_INT(0, cacheTestSwitchTo(&processDescriptor));
  NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[1]));
  NANO_OS_ASSERT_NOT_NULL(overlayFifoGet(fileCache,
    processDescriptor.overlay.overlayId,
    processDescriptor.overlay.overlayIdLength));
}

NANO_OS_KERNEL_TEST_WITH(overlay_cache, stale_node_finds_an_overlay_cached_again,
  overlayCacheTestPreBoot
) {
  ProcessDescriptor first;
  ProcessDescriptor second;
  cacheTestInitDescriptor(&first, _cacheTestIds[0]);
  cacheTestInitDescriptor(&second, _cacheTestIds[0]);
  _cacheTestDiskReads = 0;

  NANO_OS_ASSERT_EQ_INT(0, cacheTestSwitchTo(&first));
  for (int ii = 1; ii <= CACHE_TEST_DEPTH; ii++) {
    NANO_OS_ASSERT_NOT_NULL(overlayCacheTestPush(_cacheTestIds[ii]));
  }
  NANO_OS_ASSERT_EQ_INT(0, cacheTestSwitchTo(&second));
  NANO_OS_ASSERT_EQ_INT(2, _cacheTestDiskReads);

  NANO_OS_ASSERT_EQ_INT(0, cacheTestSwitchTo(&first));
  NANO_OS_ASSERT_EQ_INT(2, _cacheTestDiskReads);
  NANO_OS_ASSERT_TRUE(first.overlay.overlayFifoNode
    == second.overlay.overlayFifoNode);
}
