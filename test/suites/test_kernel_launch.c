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

#include "kernel/Console.h"
#include "kernel/Filesystem.h"
#include "kernel/Hal.h"
#include "kernel/NanoOs.h"
#include "kernel/MemoryManager.h"
#include "kernel/OverlayFunctions.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"

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

  FileBlockMetadata fileBlockMetadata = {
    .blockDevice = &blockDevice,
    .numBlocks = overlaySize / 512,
  };
  for (size_t ii = 0; ii < overlaySize; ii++) {
    ((uint8_t*) overlayMap)[ii] = (uint8_t) (ii * 7);
  }

  char overlayId[] = "/usr/bin/test/main.overlay";
  OverlayFifoNode *pushed = overlayFifoPush(fileCache, overlayId,
    sizeof(overlayId), &fileBlockMetadata);
  memset(overlayMap, 0, overlaySize);

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

/// @var _failedRestartCalls
///
/// @brief Number of times failingRestart has been called.
static int _failedRestartCalls = 0;

/// @fn static int failingRestart(ProcessDescriptor *processDescriptor)
///
/// @brief Restart function that counts its calls and always fails
/// permanently.
static int failingRestart(ProcessDescriptor *processDescriptor) {
  (void) processDescriptor;
  _failedRestartCalls++;
  return -8; // -ENOENT
}

/// @fn static void* exitImmediately(void *args)
///
/// @brief Process body that exits as soon as it runs.
static void* exitImmediately(void *args) {
  return args;
}

NANO_OS_KERNEL_TEST(launch, failed_restart_is_attempted_once) {
  ProcessId firstUserSlot
    = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *processDescriptor = NULL;
  for (ProcessId pid = firstUserSlot; pid <= numProcesses; pid++) {
    if (processFinished(&allProcesses[pid - 1])) {
      processDescriptor = &allProcesses[pid - 1];
      break;
    }
  }
  NANO_OS_ASSERT_NOT_NULL(processDescriptor);

  NANO_OS_ASSERT_TRUE(processCreate(processDescriptor, exitImmediately, NULL)
    == processSuccess);
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->restartFunction = failingRestart;
  NANO_OS_ASSERT_EQ_INT(0,
    processQueuePush(processDescriptor->readyQueue, processDescriptor));

  for (int ii = 0; (ii < 100) && (_failedRestartCalls == 0); ii++) {
    processYield();
  }
  NANO_OS_ASSERT_EQ_INT(1, _failedRestartCalls);
  for (int ii = 0; ii < 100; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_EQ_INT(1, _failedRestartCalls);
}

NANO_OS_KERNEL_TEST(launch, one_getty_shell_per_console_port_by_default) {
  NANO_OS_ASSERT_EQ_INT(1, schedulerGetNumShells());
  NANO_OS_ASSERT_NULL(
    allProcesses[schedulerGetFirstShellPid() - 1].restartArgs);
}

NANO_OS_KERNEL_TEST(launch, unused_shell_slot_becomes_a_user_slot) {
  ProcessId unusedShellSlot
    = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *slot = &allProcesses[unusedShellSlot - 1];
  ProcessDescriptor *lastSlot = &allProcesses[numProcesses - 1];
  NANO_OS_ASSERT_TRUE(unusedShellSlot < numProcesses);
  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_USER, lastSlot->privilegeLevel);

  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_USER, slot->privilegeLevel);
  NANO_OS_ASSERT_EQ_PTR(lastSlot->ipcCapabilities, slot->ipcCapabilities);
  NANO_OS_ASSERT_EQ_INT(lastSlot->numIpcCapabilities,
    slot->numIpcCapabilities);
}

/// @var fakeOpens
///
/// @brief The number of files the fake filesystem has been asked to open.
static volatile int fakeOpens;

/// @var fakeOpenPath
///
/// @brief The path of the last open.
static char fakeOpenPath[64];

/// @var fakeOpenMode
///
/// @brief The mode of the last open the fake filesystem saw.
static char fakeOpenMode[4];

/// @var fakeOpenFullPath
///
/// @brief Whether the last open carried a full path.
static volatile bool fakeOpenFullPath;

/// @var fakePathCommands
///
/// @brief The number of rename, mkdir, and rmdir requests the fake filesystem
/// has seen.
static volatile int fakePathCommands;

/// @var fakePathCommand
///
/// @brief The FILESYSTEM_* command of the last rename, mkdir, or rmdir.
static volatile int fakePathCommand;

/// @var fakePathCommandPath
///
/// @brief The path of the last mkdir or rmdir, or the new path of the last
/// rename.
static char fakePathCommandPath[64];

/// @var fakePathCommandFullPath
///
/// @brief Whether the last rename, mkdir, or rmdir carried a full path.
static volatile bool fakePathCommandFullPath;

/// @fn static void* refusingFilesystem(void *args)
///
/// @brief Stand in for the filesystem, finding no file it's asked to open.
///
/// @param args Not used.
///
/// @return This function never returns.
static void* refusingFilesystem(void *args) {
  (void) args;
  while (1) {
    ProcessMessage *message = processMessageQueueWait(NULL);
    if (message == NULL) {
      continue;
    }

    if ((processMessageType(message) & FILESYSTEM_COMMAND_MASK)
      == FILESYSTEM_OPEN_FILE
    ) {
      FilesystemFopenArgs *fopenArgs
        = (FilesystemFopenArgs*) processMessageData(message);
      fopenArgs->returnValue = NULL;
      fakeOpens++;
      strcpy(fakeOpenPath, fopenArgs->pathname);
      strcpy(fakeOpenMode, fopenArgs->mode);
      fakeOpenFullPath
        = (processMessageType(message) & FILESYSTEM_FULL_PATH) != 0;
    }
    int command = (int) (processMessageType(message) & FILESYSTEM_COMMAND_MASK);
    if (command == FILESYSTEM_RENAME_FILE) {
      FilesystemRenameArgs *renameArgs
        = (FilesystemRenameArgs*) processMessageData(message);
      strcpy(fakePathCommandPath, renameArgs->newpath);
      renameArgs->returnValue = 0;
    } else if ((command == FILESYSTEM_MKDIR) || (command == FILESYSTEM_RMDIR)) {
      FilesystemPathArgs *pathArgs
        = (FilesystemPathArgs*) processMessageData(message);
      strcpy(fakePathCommandPath, pathArgs->pathname);
      pathArgs->returnValue = 0;
    }
    if ((command == FILESYSTEM_RENAME_FILE) || (command == FILESYSTEM_MKDIR)
      || (command == FILESYSTEM_RMDIR)
    ) {
      fakePathCommands++;
      fakePathCommand = command;
      fakePathCommandFullPath
        = (processMessageType(message) & FILESYSTEM_FULL_PATH) != 0;
    }
    processMessageSetDone(message);
  }
  return NULL;
}

/// @fn static void useRefusingFilesystem(void)
///
/// @brief Start refusingFilesystem and route filesystem messages to it.
///
/// @return This function returns no value.
static void useRefusingFilesystem(void) {
  fakeOpens = 0;
  fakePathCommands = 0;
  ProcessDescriptor *processDescriptor = processQueuePop(halMockFreeQueue());
  NANO_OS_ASSERT_NOT_NULL(processDescriptor);
  NANO_OS_ASSERT_EQ_INT(processSuccess,
    processCreate(processDescriptor, refusingFilesystem, NULL));
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->userId = ROOT_USER_ID;
  // The scheduler's blocking send only runs the levels above supervisor.
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_KERNEL;
  processDescriptor->readyQueue = allProcesses[consolePid - 1].readyQueue;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  rootFilesystemPid = processDescriptor->processId;

  ProcessDescriptor *running = getRunningProcess();
  static IpcCapability ipcCapabilities[64];
  NANO_OS_ASSERT_TRUE(running->numIpcCapabilities + 1 < 64);
  memcpy(ipcCapabilities, running->ipcCapabilities,
    sizeof(IpcCapability) * running->numIpcCapabilities);
  ipcCapabilityInsert(ipcCapabilities, running->numIpcCapabilities,
    rootFilesystemPid, FILESYSTEM_COMMAND_SIGNATURE, 0);
  running->ipcCapabilities = ipcCapabilities;
  running->numIpcCapabilities++;
  for (size_t ii = 0; ii < running->numIpcCapabilities; ii++) {
    if (ipcCapabilities[ii].destinationPid == rootFilesystemPid) {
      ipcCapabilities[ii].messageTypes = UINT32_MAX;
    }
  }

  for (int ii = 0; ii < 10; ii++) {
    processYield();
  }
}

/// @fn static void useFileCacheOfOne(void)
///
/// @brief Pre-boot hook that makes the mock platform launch with a one-deep
/// overlay file cache, which leaves the heap room for the code under test.
static void useFileCacheOfOne(void) {
  halMockSetFileCacheDepth(1);
}

/// @fn static int failingReadBlocks(void *context, uint32_t startBlock,
///   uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer)
///
/// @brief Stand in for a block device that can't be read.
///
/// @param context Unused.
/// @param startBlock Unused.
/// @param numBlocks Unused.
/// @param blockSize Unused.
/// @param buffer Unused.
///
/// @return Returns -EIO.
static int failingReadBlocks(void *context, uint32_t startBlock,
  uint32_t numBlocks, uint16_t blockSize, uint8_t *buffer
) {
  (void) context;
  (void) startBlock;
  (void) numBlocks;
  (void) blockSize;
  (void) buffer;
  return -EIO;
}

/// @fn static OverlayFifoNode* pushCachedOverlay(char *overlayId,
///   size_t overlayIdSize)
///
/// @brief Put an overlay in the file cache.
///
/// @param overlayId The full path of the overlay.
/// @param overlayIdSize The size of overlayId, including its NUL.
///
/// @return Returns the cache node.
static OverlayFifoNode* pushCachedOverlay(char *overlayId,
  size_t overlayIdSize
) {
  static BlockDevice blockDevice = {
    .readBlocks = failingReadBlocks,
    .blockSize = 512,
  };
  FileBlockMetadata fileBlockMetadata = {
    .blockDevice = &blockDevice,
    .startBlock = 1,
    .numBlocks = 1,
  };
  return overlayFifoPush(fileCache, overlayId, overlayIdSize,
    &fileBlockMetadata);
}

NANO_OS_KERNEL_TEST_WITH(launch, a_named_overlay_cannot_hit_a_system_cache_entry,
  useFileCacheOfOne
) {
  useRefusingFilesystem();
  char overlayId[] = "/system/bin/logger/f.overlay";
  NANO_OS_ASSERT_NOT_NULL(pushCachedOverlay(overlayId, sizeof(overlayId)));

  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  NANO_OS_ASSERT_NULL(callOverlayFunctionFromFileCache(
    "/system/bin/logger", "f", "run", NULL));
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
  NANO_OS_ASSERT_STR_EQ("/system/bin/logger/f.overlay", fakeOpenPath);
  NANO_OS_ASSERT_FALSE(fakeOpenFullPath);
}

NANO_OS_KERNEL_TEST_WITH(launch, a_named_overlay_cannot_climb_out_of_userspace,
  useFileCacheOfOne
) {
  useRefusingFilesystem();
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_USER;
  NANO_OS_ASSERT_NULL(callOverlayFunctionFromFileCache(
    "/usr/../../system/bin/logger", "f", "run", NULL));
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
  NANO_OS_ASSERT_STR_EQ("/system/bin/logger/f.overlay", fakeOpenPath);
  NANO_OS_ASSERT_FALSE(fakeOpenFullPath);
}

NANO_OS_KERNEL_TEST(launch, a_user_overlay_is_opened_as_a_user_api_path) {
  useRefusingFilesystem();
  FileBlockMetadata metadata;
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_USER;
  NANO_OS_ASSERT_TRUE(
    getFileBlockMetadataFromPath("/userspace/usr/bin/x/main.overlay", &metadata)
    < 0);
  NANO_OS_ASSERT_STR_EQ("/usr/bin/x/main.overlay", fakeOpenPath);
  NANO_OS_ASSERT_FALSE(fakeOpenFullPath);
}

NANO_OS_KERNEL_TEST(launch, an_overlay_is_opened_to_run_and_not_to_read) {
  useRefusingFilesystem();
  FileBlockMetadata metadata;
  NANO_OS_ASSERT_TRUE(
    getFileBlockMetadataFromPath("/userspace/usr/bin/x/main.overlay", &metadata)
    < 0);
  NANO_OS_ASSERT_STR_EQ("rx", fakeOpenMode);
}

NANO_OS_KERNEL_TEST(launch, a_system_overlay_is_opened_by_its_full_path) {
  useRefusingFilesystem();
  FileBlockMetadata metadata;
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  NANO_OS_ASSERT_TRUE(
    getFileBlockMetadataFromPath("/system/bin/x/main.overlay", &metadata) < 0);
  NANO_OS_ASSERT_STR_EQ("/system/bin/x/main.overlay", fakeOpenPath);
  NANO_OS_ASSERT_TRUE(fakeOpenFullPath);
}

/// @fn static void useOverlayCommands(void)
///
/// @brief Pre-boot hook that makes programs overlays, which are what the exec
/// check covers.
static void useOverlayCommands(void) {
  halMockSetExecCommand(execOverlayCommand);
}

/// @var execSet
///
/// @brief The set the exec tests give the running process.
static union {
  DirCapabilities set;
  uint8_t bytes[64];
} execSet;

/// @fn static void holdOnly(uint8_t mask, const char *name)
///
/// @brief Give the running process a set of one entry and the refusing
/// filesystem.
///
/// @param mask The entry's mask.
/// @param name The entry's name.
///
/// @return This function returns no value.
static void holdOnly(uint8_t mask, const char *name) {
  useRefusingFilesystem();
  size_t length = strlen(name);
  memset(&execSet, 0, sizeof(execSet));
  execSet.set.numCapabilities = 1;
  execSet.set.size = (uint16_t) (DIR_CAPABILITIES_HEADER_SIZE
    + DIR_CAPABILITY_SIZE(length));
  execSet.set.capabilities[0].mask = mask;
  execSet.set.capabilities[0].len = (uint8_t) length;
  memcpy(execSet.set.capabilities[0].name, name, length);
  getRunningProcess()->dirCapabilities = &execSet.set;
}

/// @fn static int trySpawn(const char *path)
///
/// @brief Spawn a program.
///
/// @param path The path to the program.
///
/// @return Returns what posix_spawn returned.
static int trySpawn(const char *path) {
  pid_t pid = 0;
  char *argv[] = { "x", NULL };
  return nanoOsSpawn(&pid, path, NULL, NULL, argv, NULL);
}

/// @fn static int tryExec(const char *path)
///
/// @brief Exec a program, which fails in every test that uses this.
///
/// @param path The path to the program.
///
/// @return Returns errno.
static int tryExec(const char *path) {
  char *argv[] = { "x", NULL };
  errno = 0;
  schedulerExecve(path, argv, NULL);
  return errno;
}

/// @fn static int trySpawnRawPath(char *path)
///
/// @brief Send the scheduler a spawn whose path didn't come through the API.
///
/// @param path The full path, which need not be collapsed.
///
/// @return Returns the errno value the scheduler reported.
static int trySpawnRawPath(char *path) {
  uint8_t newPid = 0;
  char *argv[] = { "x", NULL };
  SpawnArgs *spawnArgs = (SpawnArgs*) calloc(1, sizeof(SpawnArgs));
  NANO_OS_ASSERT_NOT_NULL(spawnArgs);
  spawnArgs->newPid = &newPid;
  spawnArgs->path = path;
  spawnArgs->argv = argv;
  SchedulerSpawnArgs schedulerSpawnArgs = {
    .spawnArgs = spawnArgs,
    .errorNumber = 0,
  };
  ProcessMessage *message = initSendProcessMessageToPid(schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_SPAWN,
    &schedulerSpawnArgs, sizeof(schedulerSpawnArgs), true);
  NANO_OS_ASSERT_NOT_NULL(message);
  processMessageWaitForDone(message, NULL);
  processMessageRelease(message);
  free(spawnArgs);
  return schedulerSpawnArgs.errorNumber;
}

NANO_OS_KERNEL_TEST_WITH(launch, exec_needs_x_on_the_program,
  useOverlayCommands
) {
  holdOnly((uint8_t) (DIRCAP_ALL & ~DIRCAP_EXEC), "/usr/bin/");
  NANO_OS_ASSERT_EQ_INT(EACCES, tryExec("/usr/bin/x"));
  NANO_OS_ASSERT_EQ_INT(0, fakeOpens);
}

NANO_OS_KERNEL_TEST_WITH(launch, a_refused_exec_leaves_the_caller_as_it_was,
  useOverlayCommands
) {
  holdOnly(DIRCAP_READ, "/usr/bin/");
  ProcessDescriptor *running = getRunningProcess();
  void *overlayNamespace = running->overlayNamespace;
  char **envp = running->envp;
  NANO_OS_ASSERT_EQ_INT(EACCES, tryExec("/usr/bin/x"));
  NANO_OS_ASSERT_EQ_PTR(overlayNamespace, running->overlayNamespace);
  NANO_OS_ASSERT_EQ_PTR(envp, running->envp);
  NANO_OS_ASSERT_EQ_PTR(&execSet.set, running->dirCapabilities);
}

NANO_OS_KERNEL_TEST_WITH(launch, exec_with_x_goes_on_to_load_the_program,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC, "/usr/bin/");
  NANO_OS_ASSERT_TRUE(tryExec("/usr/bin/x") != EACCES);
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
  NANO_OS_ASSERT_STR_EQ("/userspace/usr/bin/x/main.overlay", fakeOpenPath);
}

NANO_OS_KERNEL_TEST_WITH(launch, spawn_needs_x_on_the_program,
  useOverlayCommands
) {
  holdOnly((uint8_t) (DIRCAP_ALL & ~DIRCAP_EXEC), "/usr/bin/");
  NANO_OS_ASSERT_EQ_INT(EACCES, trySpawn("/usr/bin/x"));
  NANO_OS_ASSERT_EQ_INT(0, fakeOpens);
}

NANO_OS_KERNEL_TEST_WITH(launch, spawn_with_x_goes_on_to_load_the_program,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC, "/usr/bin/");
  NANO_OS_ASSERT_TRUE(trySpawn("/usr/bin/x") != EACCES);
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
}

NANO_OS_KERNEL_TEST_WITH(launch, refused_spawns_do_not_use_up_process_slots,
  useOverlayCommands
) {
  holdOnly(DIRCAP_READ, "/usr/bin/");
  for (int ii = 0; ii < (2 * (int) numProcesses); ii++) {
    NANO_OS_ASSERT_EQ_INT(EACCES, trySpawn("/usr/bin/x"));
  }
}

NANO_OS_KERNEL_TEST_WITH(launch, a_capability_on_the_program_itself_covers_it,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC, "/usr/bin/x/");
  NANO_OS_ASSERT_TRUE(trySpawn("/usr/bin/x") != EACCES);
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
}

NANO_OS_KERNEL_TEST_WITH(launch, a_near_miss_gives_no_exec,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC, "/usr/bin/x/");
  NANO_OS_ASSERT_EQ_INT(EACCES, trySpawn("/usr/bin/xy"));
  NANO_OS_ASSERT_EQ_INT(EACCES, trySpawn("/usr/bin"));
}

NANO_OS_KERNEL_TEST_WITH(launch, a_normal_path_needs_a_normal_entry,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC | DIRCAP_FULL_PATH, "/usr/bin/");
  NANO_OS_ASSERT_EQ_INT(EACCES, trySpawn("/usr/bin/x"));
  NANO_OS_ASSERT_EQ_INT(0, fakeOpens);
}

NANO_OS_KERNEL_TEST_WITH(launch, a_full_path_needs_a_full_path_entry,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC, "/system/bin/");
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  char *argv[] = { "x", NULL };
  errno = 0;
  schedulerExecveFullPath("/system/bin/x", argv, NULL);
  NANO_OS_ASSERT_EQ_INT(EACCES, errno);
  NANO_OS_ASSERT_EQ_INT(0, fakeOpens);
}

NANO_OS_KERNEL_TEST_WITH(launch, a_full_path_entry_allows_a_full_path_exec,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC | DIRCAP_FULL_PATH, "/system/bin/");
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  char *argv[] = { "x", NULL };
  errno = 0;
  schedulerExecveFullPath("/system/bin/x", argv, NULL);
  NANO_OS_ASSERT_TRUE(errno != EACCES);
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
  NANO_OS_ASSERT_STR_EQ("/system/bin/x/main.overlay", fakeOpenPath);
}

NANO_OS_KERNEL_TEST_WITH(launch, a_full_path_entry_on_the_program_covers_it,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC | DIRCAP_FULL_PATH, "/system/bin/x/");
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  char *argv[] = { "x", NULL };
  errno = 0;
  schedulerExecveFullPath("/system/bin/x", argv, NULL);
  NANO_OS_ASSERT_TRUE(errno != EACCES);
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
}

NANO_OS_KERNEL_TEST_WITH(launch, the_scheduler_collapses_a_path_before_checking,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC, "/usr/");
  static char escape[] = "/userspace/usr/../../secure/x";
  NANO_OS_ASSERT_EQ_INT(EACCES, trySpawnRawPath(escape));
  NANO_OS_ASSERT_EQ_INT(0, fakeOpens);

  static char inside[] = "/userspace/tmp/../usr/bin/x";
  NANO_OS_ASSERT_TRUE(trySpawnRawPath(inside) != EACCES);
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
  NANO_OS_ASSERT_STR_EQ("/userspace/usr/bin/x/main.overlay", fakeOpenPath);
}

NANO_OS_KERNEL_TEST_WITH(launch, a_process_with_no_set_cannot_exec,
  useOverlayCommands
) {
  holdOnly(DIRCAP_EXEC, "/usr/bin/");
  getRunningProcess()->dirCapabilities = NULL;
  NANO_OS_ASSERT_EQ_INT(EACCES, trySpawn("/usr/bin/x"));
}

NANO_OS_KERNEL_TEST(launch, an_executive_can_open_a_full_path) {
  useRefusingFilesystem();
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  NANO_OS_ASSERT_NULL(filesystemFopenFullPath("/secure/shadow", "r"));
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
  NANO_OS_ASSERT_STR_EQ("/secure/shadow", fakeOpenPath);
  NANO_OS_ASSERT_TRUE(fakeOpenFullPath);
}

NANO_OS_KERNEL_TEST(launch, fopen_never_asks_for_a_full_path) {
  useRefusingFilesystem();
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  NANO_OS_ASSERT_NULL(filesystemFopen("/secure/shadow", "r"));
  NANO_OS_ASSERT_EQ_INT(1, fakeOpens);
  NANO_OS_ASSERT_FALSE(fakeOpenFullPath);
}

NANO_OS_KERNEL_TEST(launch, a_supervisor_cannot_open_a_full_path) {
  useRefusingFilesystem();
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
  errno = 0;
  NANO_OS_ASSERT_NULL(filesystemFopenFullPath("/secure/shadow", "r"));
  NANO_OS_ASSERT_EQ_INT(EPERM, errno);
  NANO_OS_ASSERT_EQ_INT(0, fakeOpens);
}

/// @fn static void expectPathCommand(int command, const char *path,
///   bool fullPath)
///
/// @brief Check that the fake filesystem saw exactly one rename, mkdir, or
/// rmdir, and what it carried.
///
/// @param command The FILESYSTEM_* command expected.
/// @param path The path expected, or the new path for a rename.
/// @param fullPath Whether the request should carry a full path.
///
/// @return This function returns no value.
static void expectPathCommand(int command, const char *path, bool fullPath) {
  NANO_OS_ASSERT_EQ_INT(1, fakePathCommands);
  NANO_OS_ASSERT_EQ_INT(command, fakePathCommand);
  NANO_OS_ASSERT_STR_EQ(path, fakePathCommandPath);
  NANO_OS_ASSERT_EQ_INT(fullPath, fakePathCommandFullPath);
  fakePathCommands = 0;
}

NANO_OS_KERNEL_TEST(launch, an_executive_can_change_full_paths) {
  useRefusingFilesystem();
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  NANO_OS_ASSERT_EQ_INT(0, filesystemMkdirFullPath("/secure/lock", 0));
  expectPathCommand(FILESYSTEM_MKDIR, "/secure/lock", true);
  NANO_OS_ASSERT_EQ_INT(0, filesystemRmdirFullPath("/secure/lock"));
  expectPathCommand(FILESYSTEM_RMDIR, "/secure/lock", true);
  NANO_OS_ASSERT_EQ_INT(0,
    filesystemRenameFullPath("/secure/new", "/secure/old"));
  expectPathCommand(FILESYSTEM_RENAME_FILE, "/secure/old", true);
}

NANO_OS_KERNEL_TEST(launch, the_user_path_calls_never_ask_for_a_full_path) {
  useRefusingFilesystem();
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  NANO_OS_ASSERT_EQ_INT(0, filesystemMkdir("/tmp/lock", 0));
  expectPathCommand(FILESYSTEM_MKDIR, "/tmp/lock", false);
  NANO_OS_ASSERT_EQ_INT(0, filesystemRmdir("/tmp/lock"));
  expectPathCommand(FILESYSTEM_RMDIR, "/tmp/lock", false);
  NANO_OS_ASSERT_EQ_INT(0, filesystemRename("/tmp/new", "/tmp/old"));
  expectPathCommand(FILESYSTEM_RENAME_FILE, "/tmp/old", false);
}

NANO_OS_KERNEL_TEST(launch, a_supervisor_cannot_change_full_paths) {
  useRefusingFilesystem();
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
  errno = 0;
  NANO_OS_ASSERT_EQ_INT(-1, filesystemMkdirFullPath("/secure/lock", 0));
  NANO_OS_ASSERT_EQ_INT(EPERM, errno);
  errno = 0;
  NANO_OS_ASSERT_EQ_INT(-1, filesystemRmdirFullPath("/secure/lock"));
  NANO_OS_ASSERT_EQ_INT(EPERM, errno);
  errno = 0;
  NANO_OS_ASSERT_EQ_INT(-1,
    filesystemRenameFullPath("/secure/new", "/secure/shadow"));
  NANO_OS_ASSERT_EQ_INT(EPERM, errno);
  NANO_OS_ASSERT_EQ_INT(0, fakePathCommands);
}

NANO_OS_KERNEL_TEST(launch, the_userspace_rule_is_exact) {
  NANO_OS_ASSERT_TRUE(filesystemPathIsUserspace("/userspace"));
  NANO_OS_ASSERT_TRUE(filesystemPathIsUserspace("/userspace/usr/bin/x"));
  NANO_OS_ASSERT_FALSE(filesystemPathIsUserspace("/userspaces/x"));
  NANO_OS_ASSERT_FALSE(filesystemPathIsUserspace("/system/bin/x"));
}

NANO_OS_KERNEL_TEST(launch, a_user_path_is_collapsed_before_userspace_is_added) {
  char buffer[64];
  NANO_OS_ASSERT_EQ_INT(16,
    (int) userspacePathCopy(buffer, "/usr/../../etc/x/../y"));
  NANO_OS_ASSERT_STR_EQ("/userspace/etc/y", buffer);
  NANO_OS_ASSERT_EQ_INT(10, (int) userspacePathCopy(buffer, "/"));
  NANO_OS_ASSERT_STR_EQ("/userspace", buffer);
}
