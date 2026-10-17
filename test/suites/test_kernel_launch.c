///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_launch.c
///
/// @brief             Kernel tests for the platform launchProcesses hook and
///                    the overlay file cache it allocates.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"
#include "HalMock.h"

#include <string.h>

#include "kernel/Hal.h"
#include "kernel/MemoryManager.h"
#include "kernel/OverlayFunctions.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"

void* callOverlayFunctionFromFileCache(const void *overlayDir,
  const void *overlay, const char *function, void *args);

extern NanoOsOverlayMap *overlayMap;

/// @fn static void useFileCacheOfTen(void)
///
/// @brief Pre-boot hook that makes the mock platform launch with a ten-deep
/// overlay file cache.
static void useFileCacheOfTen(void) {
  halMockSetFileCacheDepth(10);
}

NANO_OS_KERNEL_TEST(launch, every_schedulable_process_is_queued) {
  NANO_OS_ASSERT_NULL(allProcesses[schedulerPid - 1].readyQueue);
  for (ProcessId pid = 2; pid <= numProcesses; pid++) {
    NANO_OS_ASSERT_NOT_NULL(allProcesses[pid - 1].readyQueue);
  }
}

NANO_OS_KERNEL_TEST(launch, no_process_is_on_a_ready_queue_twice) {
  ProcessQueue *queues[NUM_READY_QUEUES] = { NULL };
  uint8_t numQueues = 0;
  for (ProcessId pid = 2; pid <= numProcesses; pid++) {
    ProcessQueue *queue = allProcesses[pid - 1].readyQueue;
    bool seen = false;
    for (uint8_t ii = 0; ii < numQueues; ii++) {
      seen |= (queues[ii] == queue);
    }
    if ((queue != NULL) && (seen == false)) {
      NANO_OS_ASSERT_TRUE(numQueues < NUM_READY_QUEUES);
      queues[numQueues++] = queue;
    }
  }

  for (ProcessId pid = 2; pid <= numProcesses; pid++) {
    int count = 0;
    for (uint8_t ii = 0; ii < numQueues; ii++) {
      ProcessDescriptor **slot = queues[ii]->head;
      for (uint8_t jj = 0; jj < queues[ii]->numElements; jj++) {
        count += (*slot == &allProcesses[pid - 1]);
        slot++;
        if (slot == queues[ii]->end) {
          slot = queues[ii]->processes;
        }
      }
    }
    NANO_OS_ASSERT_TRUE(count <= 1);
  }
}

NANO_OS_KERNEL_TEST(launch, user_slots_are_queued_at_user_level) {
  ProcessId firstUserSlot
    = schedulerGetFirstShellPid() + schedulerGetNumShells();
  NANO_OS_ASSERT_TRUE(firstUserSlot <= numProcesses);
  for (ProcessId pid = firstUserSlot; pid <= numProcesses; pid++) {
    NANO_OS_ASSERT_STR_EQ("user ready",
      allProcesses[pid - 1].readyQueue->name);
  }
}

NANO_OS_KERNEL_TEST(launch, only_shell_slots_get_a_restart_function) {
  ProcessId firstShellPid = schedulerGetFirstShellPid();
  ProcessId firstUserSlot = firstShellPid + schedulerGetNumShells();
  for (ProcessId pid = firstShellPid; pid < firstUserSlot; pid++) {
    NANO_OS_ASSERT_NOT_NULL(allProcesses[pid - 1].restartFunction);
  }
  for (ProcessId pid = firstUserSlot; pid <= numProcesses; pid++) {
    NANO_OS_ASSERT_NULL(allProcesses[pid - 1].restartFunction);
  }
}

NANO_OS_KERNEL_TEST(launch, launched_slots_call_overlays_from_files) {
  for (ProcessId pid = schedulerGetFirstShellPid(); pid <= numProcesses;
    pid++
  ) {
    NANO_OS_ASSERT_TRUE(allProcesses[pid - 1].callOverlayFunction
      == callOverlayFunctionFromFile);
  }
  NANO_OS_ASSERT_NULL(fileCache);
}

NANO_OS_KERNEL_TEST_WITH(launch, launched_slots_call_overlays_from_the_cache,
  useFileCacheOfTen
) {
  for (ProcessId pid = schedulerGetFirstShellPid(); pid <= numProcesses;
    pid++
  ) {
    NANO_OS_ASSERT_TRUE(allProcesses[pid - 1].callOverlayFunction
      == callOverlayFunctionFromFileCache);
  }
}

NANO_OS_KERNEL_TEST_WITH(launch, file_cache_holds_ten_overlay_sized_nodes,
  useFileCacheOfTen
) {
  NANO_OS_ASSERT_NOT_NULL(fileCache);
  NANO_OS_ASSERT_EQ_INT(10, fileCache->size);
  NANO_OS_ASSERT_EQ_INT(0, fileCache->numElements);
  NANO_OS_ASSERT_EQ_INT(0, fileCache->head);
  NANO_OS_ASSERT_EQ_INT(0, fileCache->tail);

  size_t overlaySize = HAL->memory->overlaySize;
  NANO_OS_ASSERT_TRUE(overlaySize > 0);
  uint8_t *firstNodeMemory = fileCache->nodes[0].memory;
  NANO_OS_ASSERT_TRUE(firstNodeMemory
    >= (uint8_t*) &fileCache->nodes[fileCache->size]);
  for (uint8_t ii = 0; ii < fileCache->size; ii++) {
    NANO_OS_ASSERT_TRUE(fileCache->nodes[ii].memory
      == firstNodeMemory + (ii * overlaySize));
    NANO_OS_ASSERT_EQ_INT(0, fileCache->nodes[ii].missesRemaining);
  }

  MemNode *memNode = &((MemNode*) fileCache)[-1];
  NANO_OS_ASSERT_EQ_INT((long long) schedulerPid, (long long) memNode->owner);
  NANO_OS_ASSERT_TRUE((uint8_t*) fileCache + memNode->size
    >= firstNodeMemory + (fileCache->size * overlaySize));
}

NANO_OS_KERNEL_TEST_WITH(launch, file_cache_returns_a_pushed_overlay,
  useFileCacheOfTen
) {
  size_t overlaySize = HAL->memory->overlaySize;
  BlockDevice blockDevice;
  memset(&blockDevice, 0, sizeof(blockDevice));
  blockDevice.blockSize = 512;

  ProcessDescriptor *self = getRunningProcess();
  self->overlay.fileBlockMetadata.blockDevice = &blockDevice;
  self->overlay.fileBlockMetadata.numBlocks = overlaySize / 512;
  for (size_t ii = 0; ii < overlaySize; ii++) {
    ((uint8_t*) overlayMap)[ii] = (uint8_t) (ii * 7);
  }

  char overlayId[] = "/usr/bin/test/main.overlay";
  OverlayFifoNode *pushed = overlayFifoPush(fileCache, overlayId,
    sizeof(overlayId));
  memset(overlayMap, 0, overlaySize);
  self->overlay.fileBlockMetadata.blockDevice = NULL;

  NANO_OS_ASSERT_NOT_NULL(pushed);
  NANO_OS_ASSERT_EQ_INT(1, fileCache->numElements);
  OverlayFifoNode *found = overlayFifoGet(fileCache, overlayId,
    sizeof(overlayId));
  NANO_OS_ASSERT_TRUE(found == pushed);
  NANO_OS_ASSERT_TRUE(found->fileBlockMetadata.blockDevice == &blockDevice);
  NANO_OS_ASSERT_EQ_INT((long long) (overlaySize / 512),
    (long long) found->fileBlockMetadata.numBlocks);
  for (size_t ii = 0; ii < overlaySize; ii++) {
    if (found->memory[ii] != (uint8_t) (ii * 7)) {
      NANO_OS_FAIL("cached overlay does not match what was pushed");
    }
  }
}

NANO_OS_KERNEL_TEST(launch, replacing_an_overlay_copies_its_id) {
  char overlayId[] = "/usr/bin/test/other.overlay";
  OverlayDescriptor overlay;
  memset(&overlay, 0, sizeof(overlay));
  overlay.overlayId = overlayId;
  overlay.overlayIdLength = sizeof(overlayId);

  NANO_OS_ASSERT_EQ_INT(0,
    schedulerReplaceOverlay(OVERLAY_SAME_NAMESPACE, &overlay));
  memset(overlayId, 0, sizeof(overlayId));

  ProcessDescriptor *self = getRunningProcess();
  NANO_OS_ASSERT_TRUE(self->overlay.overlayId != (void*) overlayId);
  NANO_OS_ASSERT_EQ_INT((long long) sizeof(overlayId),
    (long long) self->overlay.overlayIdLength);
  NANO_OS_ASSERT_STR_EQ("/usr/bin/test/other.overlay",
    (const char*) self->overlay.overlayId);

  overlay.overlayId = NULL;
  overlay.overlayIdLength = 0;
  NANO_OS_ASSERT_EQ_INT(0,
    schedulerReplaceOverlay(OVERLAY_SAME_NAMESPACE, &overlay));
  NANO_OS_ASSERT_NULL(self->overlay.overlayId);
}
