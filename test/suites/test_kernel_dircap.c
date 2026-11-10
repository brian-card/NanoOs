///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.07.2026
///
/// @file              test_kernel_dircap.c
///
/// @brief             Kernel tests of how processes get, share, change, and
///                    release their directory capabilities.
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
#include "HalMock.h"

#include <string.h>

#include "kernel/Hal.h"
#include "kernel/Console.h"
#include "kernel/DirCapabilities.h"
#include "kernel/MemoryManager.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsDirCap.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsSignal.h"
#include "user/NanoOsSpawn.h"
#include "user/NanoOsStdio.h"
#include "user/NanoOsUnistd.h"

/// @var childStatus
///
/// @brief What the last child's directory capability call returned, or -1
/// before it has made one.
static volatile int childStatus;

/// @var reported
///
/// @brief Whether a child has copied its set into reportedSet.
static volatile bool reported;

/// @var reportedSetAddress
///
/// @brief The address of the set the reporting child held.
static DirCapabilities *volatile reportedSetAddress;

/// @var reportedPrivilege
///
/// @brief The privilege level the reporting child ran at.
static volatile PrivilegeLevel reportedPrivilege;

/// @var reportedHal
///
/// @brief The HAL capabilities the reporting child held.
static HalCapability *volatile reportedHal;

/// @var reportedUser
///
/// @brief The user ID setuidChild ended with.
static volatile UserId reportedUser;

/// @var reportedQueue
///
/// @brief The ready queue the reporting child belongs to.
static ProcessQueue *volatile reportedQueue;

/// @var reportedEnvp
///
/// @brief The environment the reporting child was given.
static char **volatile reportedEnvp;

/// @union ReportedSet
///
/// @brief Room for a copy of a set, aligned as a set is.
typedef union ReportedSet {
  DirCapabilities set;
  uint8_t bytes[128];
} ReportedSet;

/// @var reportedSet
///
/// @brief A copy of the set the reporting child held.
static ReportedSet reportedSet;

/// @var reportedPath
///
/// @brief The path of the program dircapChild stood in for.
static char reportedPath[64];

/// @fn static void reportSet(void)
///
/// @brief Copy the running process's set where the test can see it.
///
/// @return This function returns no value.
static void reportSet(void) {
  DirCapabilities *set = getRunningProcess()->dirCapabilities;
  reportedSetAddress = set;
  reportedPrivilege = getRunningProcess()->privilegeLevel;
  reportedQueue = getRunningProcess()->readyQueue;
  reportedHal = getRunningProcess()->halCapabilities;
  reportedEnvp = getRunningProcess()->envp;
  reportedUser = getRunningProcess()->userId;
  memset(&reportedSet, 0, sizeof(reportedSet));
  if ((set != NULL) && (set->size <= sizeof(reportedSet))) {
    memcpy(&reportedSet, set, set->size);
  }
  reported = true;
}

/// @fn static void* dircapChild(void *args)
///
/// @brief Stand in for a spawned program, chosen by argv[0]:  "idle" runs
/// until killed, "narrow" adds an entry below one it holds and reports its
/// set, "getty" and "mush" report their set and idle, and anything else
/// reports its set and exits.
///
/// @param args The ExecArgs of the spawn.
///
/// @return This function returns NULL.
static void* dircapChild(void *args) {
  ExecArgs *execArgs = (ExecArgs*) args;
  const char *name = execArgs->argv[0];
  strcpy(reportedPath, execArgs->pathname);
  if (strcmp(name, "narrow") == 0) {
    childStatus = nanoOsDircapAdd("/tmp/sub/", DIRCAP_READ);
  }
  if (strcmp(name, "idle") != 0) {
    reportSet();
  }
  if ((strcmp(name, "idle") == 0) || (strcmp(name, "getty") == 0)
    || (strcmp(name, "mush") == 0)
  ) {
    while (1) {
      processYield();
    }
  }
  return NULL;
}

/// @fn static void dircapPreBoot(void)
///
/// @brief Make spawned processes run dircapChild instead of an overlay.
///
/// @return This function returns no value.
static void dircapPreBoot(void) {
  halMockSetExecCommand(dircapChild);
}

/// @fn static pid_t spawnChild(char *name, posix_spawnattr_t *attr,
///   int *status)
///
/// @brief Spawn a dircapChild.
///
/// @param name The mode, passed as argv[0].
/// @param attr The spawn attribute, or NULL.
/// @param status Where to store what nanoOsSpawn returned, or NULL.
///
/// @return Returns the child's process ID, or 0 on failure.
static pid_t spawnChild(char *name, posix_spawnattr_t *attr, int *status) {
  pid_t pid = 0;
  char *argv[] = { name, NULL };
  childStatus = -1;
  reported = false;
  int returnValue = nanoOsSpawn(&pid, "/dctest", NULL, attr, argv, NULL);
  if (status != NULL) {
    *status = returnValue;
  }
  return (returnValue == 0) ? pid : 0;
}

/// @fn static bool spawnAndWait(char *name, posix_spawnattr_t *attr)
///
/// @brief Spawn a dircapChild and wait for it to exit.
///
/// @param name The mode, passed as argv[0].
/// @param attr The spawn attribute, or NULL.
///
/// @return Returns true if the child was spawned and waited for.
static bool spawnAndWait(char *name, posix_spawnattr_t *attr) {
  pid_t pid = spawnChild(name, attr, NULL);
  return (pid != 0) && (schedulerWaitpid(pid, NULL, 0) == pid);
}

/// @fn static bool yieldUntilReported(void)
///
/// @brief Yield until a child has reported its set, giving up after a while.
///
/// @return Returns true if a child reported.
static bool yieldUntilReported(void) {
  for (int ii = 0; (ii < 2000) && !reported; ii++) {
    processYield();
  }
  return reported;
}

/// @fn static DirCapabilities* narrowOwnSet(void)
///
/// @brief Narrow the running process's set so that it holds a block of its
/// own, with /tmp/ read-only.
///
/// @return Returns the process's set.
static DirCapabilities* narrowOwnSet(void) {
  NANO_OS_ASSERT_EQ_INT(0, nanoOsDircapAdd("/tmp/", DIRCAP_READ));
  DirCapabilities *set = getRunningProcess()->dirCapabilities;
  NANO_OS_ASSERT_NOT_NULL(set);
  NANO_OS_ASSERT_EQ_INT(1, set->refCount);
  return set;
}

/// @fn static bool setAllows(const DirCapabilities *set, const char *path,
///   uint8_t bits)
///
/// @brief Check a file path against a set.
///
/// @return Returns what dirCapabilityAllows returns.
static bool setAllows(const DirCapabilities *set, const char *path,
  uint8_t bits
) {
  return dirCapabilityAllows(set, path, strlen(path), false, bits);
}

NANO_OS_KERNEL_TEST(dircap, processes_start_with_the_compiled_sets) {
  NANO_OS_ASSERT_EQ_PTR(rootDirCapabilities,
    allProcesses[schedulerPid - 1].dirCapabilities);
  NANO_OS_ASSERT_EQ_PTR(kernelDirCapabilities,
    allProcesses[consolePid - 1].dirCapabilities);
  NANO_OS_ASSERT_EQ_PTR(kernelDirCapabilities,
    allProcesses[memoryManagerPid - 1].dirCapabilities);
  NANO_OS_ASSERT_EQ_PTR(supervisorDirCapabilities,
    getRunningProcess()->dirCapabilities);
}

NANO_OS_KERNEL_TEST_WITH(dircap,
  a_child_spawned_without_the_attribute_shares_its_parents_set, dircapPreBoot
) {
  DirCapabilities *parentSet = narrowOwnSet();

  pid_t pid = spawnChild("idle", NULL, NULL);
  NANO_OS_ASSERT_TRUE(pid != 0);
  NANO_OS_ASSERT_EQ_PTR(parentSet, allProcesses[pid - 1].dirCapabilities);
  NANO_OS_ASSERT_EQ_INT(2, parentSet->refCount);

  NANO_OS_ASSERT_EQ_INT(0, schedulerKillProcess(pid));
  NANO_OS_ASSERT_EQ_INT(pid, schedulerWaitpid(pid, NULL, 0));
  NANO_OS_ASSERT_EQ_INT(1, parentSet->refCount);
  NANO_OS_ASSERT_NULL(allProcesses[pid - 1].dirCapabilities);
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_childs_add_leaves_the_parents_set_unchanged,
  dircapPreBoot
) {
  DirCapabilities *parentSet = narrowOwnSet();
  ReportedSet before;
  memcpy(&before, parentSet, parentSet->size);

  NANO_OS_ASSERT_TRUE(spawnAndWait("narrow", NULL));
  NANO_OS_ASSERT_EQ_INT(0, childStatus);
  NANO_OS_ASSERT_TRUE(reported);
  NANO_OS_ASSERT_TRUE(reportedSetAddress != parentSet);
  NANO_OS_ASSERT_EQ_INT(before.set.numCapabilities + 1,
    reportedSet.set.numCapabilities);
  NANO_OS_ASSERT_TRUE(setAllows(&reportedSet.set, "/tmp/sub/x", DIRCAP_READ));

  NANO_OS_ASSERT_EQ_PTR(parentSet, getRunningProcess()->dirCapabilities);
  NANO_OS_ASSERT_EQ_INT(1, parentSet->refCount);
  NANO_OS_ASSERT_EQ_INT(0, memcmp(&before, parentSet, before.set.size));
}

NANO_OS_KERNEL_TEST_WITH(dircap,
  a_spawn_with_the_attribute_gives_the_child_exactly_those_entries,
  dircapPreBoot
) {
  posix_spawnattr_t attr = { .flags = 0, .dirCapabilities = NULL };
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawnattrAdddircap(&attr, "/tmp/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawnattrAdddircap(&attr, "/home/x/../briana", DIRCAP_LIST));
  NANO_OS_ASSERT_NOT_NULL(attr.dirCapabilities);

  NANO_OS_ASSERT_TRUE(spawnAndWait("report", &attr));
  NANO_OS_ASSERT_TRUE(reported);
  NANO_OS_ASSERT_TRUE(reportedSetAddress != attr.dirCapabilities);
  NANO_OS_ASSERT_EQ_INT(attr.dirCapabilities->size, reportedSet.set.size);
  NANO_OS_ASSERT_EQ_INT(2, reportedSet.set.numCapabilities);
  NANO_OS_ASSERT_EQ_INT(0, memcmp(attr.dirCapabilities->capabilities,
    reportedSet.set.capabilities,
    attr.dirCapabilities->size - DIR_CAPABILITIES_HEADER_SIZE));
  const DirCapability *first = &reportedSet.set.capabilities[0];
  const DirCapability *second = (const DirCapability*) &first->name[first->len];
  NANO_OS_ASSERT_EQ_INT(5, first->len);
  NANO_OS_ASSERT_EQ_INT(0, memcmp(first->name, "/tmp/", 5));
  NANO_OS_ASSERT_EQ_INT(13, second->len);
  NANO_OS_ASSERT_EQ_INT(0, memcmp(second->name, "/home/briana/", 13));
  NANO_OS_ASSERT_TRUE(setAllows(&reportedSet.set, "/home/briana/x",
    DIRCAP_LIST));
  NANO_OS_ASSERT_FALSE(setAllows(&reportedSet.set, "/tmp/x", DIRCAP_WRITE));
  NANO_OS_ASSERT_FALSE(setAllows(&reportedSet.set, "/usr/bin/x", DIRCAP_EXEC));
  free(attr.dirCapabilities);
}

/// @fn static int narrowLikeLogin(void)
///
/// @brief Narrow the running process's set the way login narrows a user's:
/// add rwcdlsx on /usr/ and r on /tmp/, then drop the base set's /.
///
/// @return Returns 0 on success, -1 if a call failed.
static int narrowLikeLogin(void) {
  if ((nanoOsDircapAdd("/usr/", DIRCAP_ALL) != 0)
    || (nanoOsDircapAdd("/tmp/", DIRCAP_READ) != 0)
  ) {
    return -1;
  }
  return nanoOsDircapDrop("/");
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_superset_entry_fails_the_spawn_with_EACCES,
  dircapPreBoot
) {
  NANO_OS_ASSERT_EQ_INT(0, narrowLikeLogin());
  posix_spawnattr_t attr = { .flags = 0, .dirCapabilities = NULL };
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawnattrAdddircap(&attr, "/tmp/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawnattrAdddircap(&attr, "/var/lib/", DIRCAP_READ));

  int status = 0;
  NANO_OS_ASSERT_EQ_INT(0, spawnChild("report", &attr, &status));
  NANO_OS_ASSERT_EQ_INT(EACCES, status);
  for (int ii = 0; ii < 100; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_FALSE(reported);
  free(attr.dirCapabilities);
}

NANO_OS_KERNEL_TEST(dircap, names_are_made_absolute_and_end_in_a_slash) {
  char *name = NULL;
  size_t length = 0;
  NANO_OS_ASSERT_EQ_INT(0, nanoOsDircapName("/a/./b//../c", &name, &length));
  NANO_OS_ASSERT_STR_EQ("/a/c/", name);
  NANO_OS_ASSERT_EQ_INT(5, length);
  free(name);
  NANO_OS_ASSERT_EQ_INT(0, nanoOsDircapName("/", &name, &length));
  NANO_OS_ASSERT_STR_EQ("/", name);
  NANO_OS_ASSERT_EQ_INT(1, length);
  free(name);
  NANO_OS_ASSERT_EQ_INT(0, nanoOsDircapName("rel/dir/", &name, &length));
  NANO_OS_ASSERT_STR_EQ("/rel/dir/", name);
  free(name);
  NANO_OS_ASSERT_EQ_INT(EINVAL, nanoOsDircapName("", &name, &length));
}

NANO_OS_KERNEL_TEST(dircap, get_copies_the_set_only_when_it_fits) {
  ReportedSet buffer;
  memset(&buffer, 0xa5, sizeof(buffer));
  int size = supervisorDirCapabilities->size;
  NANO_OS_ASSERT_EQ_INT(size, nanoOsDircapGet(NULL, 0));
  NANO_OS_ASSERT_EQ_INT(size, nanoOsDircapGet(&buffer.set, size - 1));
  NANO_OS_ASSERT_EQ_INT(0xa5, buffer.bytes[0]);
  NANO_OS_ASSERT_EQ_INT(size, nanoOsDircapGet(&buffer.set, sizeof(buffer)));
  NANO_OS_ASSERT_EQ_INT(0, memcmp(&buffer, supervisorDirCapabilities, size));
}

NANO_OS_KERNEL_TEST(dircap, add_and_drop_change_only_the_callers_set) {
  NANO_OS_ASSERT_EQ_INT(0, nanoOsDircapAdd("/usr/", DIRCAP_ALL));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsDircapDrop("/"));
  DirCapabilities *set = getRunningProcess()->dirCapabilities;
  NANO_OS_ASSERT_TRUE(set != supervisorDirCapabilities);

  NANO_OS_ASSERT_EQ_INT(-1, nanoOsDircapAdd("/var/lib/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(EACCES, errno);
  NANO_OS_ASSERT_EQ_PTR(set, getRunningProcess()->dirCapabilities);
  NANO_OS_ASSERT_FALSE(setAllows(set, "/tmp/x", DIRCAP_READ));
  NANO_OS_ASSERT_TRUE(setAllows(set, "/usr/x", DIRCAP_READ));
  NANO_OS_ASSERT_TRUE(dirCapabilitiesValid(supervisorDirCapabilities));
  NANO_OS_ASSERT_TRUE(setAllows(supervisorDirCapabilities, "/tmp/x",
    DIRCAP_READ));
}

/// @fn static ProcessDescriptor* startProcess(void* (*body)(void*),
///   UserId userId, bool supervisor)
///
/// @brief Run a function in a free process slot with the supervisor base set.
///
/// @param body The function for the process to run.
/// @param userId The user the process runs as.
/// @param supervisor Whether the process gets the running process's
///   supervisor-level IPC capabilities.
///
/// @return Returns a pointer to the process's ProcessDescriptor, or NULL if
/// it couldn't be started.
static ProcessDescriptor* startProcess(void* (*body)(void*), UserId userId,
  bool supervisor
) {
  ProcessDescriptor *processDescriptor = processQueuePop(halMockFreeQueue());
  if ((processDescriptor == NULL)
    || (processCreate(processDescriptor, body, NULL) != processSuccess)
  ) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  if (supervisor) {
    processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
    processDescriptor->ipcCapabilities = getRunningProcess()->ipcCapabilities;
    processDescriptor->numIpcCapabilities
      = getRunningProcess()->numIpcCapabilities;
  }
  processDescriptor->userId = userId;
  processDescriptor->dirCapabilities = supervisorDirCapabilities;
  return processDescriptor;
}

/// @fn static void* execNarrowed(void *args)
///
/// @brief Narrow the set, then exec "report".
///
/// @param args Unused.
///
/// @return This function returns NULL only if the exec fails.
static void* execNarrowed(void *args) {
  (void) args;
  childStatus = narrowLikeLogin();
  char *argv[] = { "report", NULL };
  schedulerExecve("/dctest", argv, NULL);
  childStatus = -errno;
  return NULL;
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_set_narrowed_before_execve_survives_it,
  dircapPreBoot
) {
  childStatus = -1;
  reported = false;
  ProcessDescriptor *processDescriptor
    = startProcess(execNarrowed, getRunningUid(), true);
  NANO_OS_ASSERT_NOT_NULL(processDescriptor);
  // Exec reads the caller's stdout, and the kernel's descriptors are never
  // freed.
  processDescriptor->fileDescriptors
    = allProcesses[consolePid - 1].fileDescriptors;
  processDescriptor->numFileDescriptors
    = allProcesses[consolePid - 1].numFileDescriptors;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);

  NANO_OS_ASSERT_TRUE(yieldUntilReported());
  NANO_OS_ASSERT_EQ_INT(0, childStatus);
  NANO_OS_ASSERT_TRUE(setAllows(&reportedSet.set, "/tmp/x", DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(setAllows(&reportedSet.set, "/tmp/x", DIRCAP_WRITE));
  NANO_OS_ASSERT_TRUE(setAllows(&reportedSet.set, "/usr/bin/x", DIRCAP_EXEC));
}

/// @var exitedSet
///
/// @brief The set loggedInShell held when it exited.
static DirCapabilities *volatile exitedSet;

/// @fn static void* loggedInShell(void *args)
///
/// @brief Stand in for a logged-in user's program in a shell slot:  narrow
/// the set, as login will, and exit.
///
/// @param args Unused.
///
/// @return This function returns NULL.
static void* loggedInShell(void *args) {
  (void) args;
  childStatus = narrowLikeLogin();
  exitedSet = getRunningProcess()->dirCapabilities;
  return NULL;
}

/// @fn static ProcessDescriptor* startShellSlot(UserId userId,
///   char *shellPath)
///
/// @brief Run loggedInShell in a free slot that restarts the way a shell
/// slot does.
///
/// @param userId The user the slot is logged in as.
/// @param shellPath The user's shell, or NULL once the user has logged out.
///
/// @return Returns a pointer to the slot's ProcessDescriptor.
static ProcessDescriptor* startShellSlot(UserId userId, char *shellPath) {
  childStatus = -1;
  reported = false;
  exitedSet = NULL;
  ProcessDescriptor *processDescriptor
    = startProcess(loggedInShell, userId, true);
  NANO_OS_ASSERT_NOT_NULL(processDescriptor);
  processDescriptor->restartFunction = restartOverlayShell;
  processDescriptor->restartArgs = NULL;
  processDescriptor->shellPath = shellPath;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return processDescriptor;
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_logged_out_slot_restarts_with_the_base_set,
  dircapPreBoot
) {
  startShellSlot(NO_USER_ID, NULL);
  NANO_OS_ASSERT_TRUE(yieldUntilReported());
  NANO_OS_ASSERT_EQ_INT(0, childStatus);
  NANO_OS_ASSERT_NOT_NULL(exitedSet);
  NANO_OS_ASSERT_TRUE(exitedSet != loginDirCapabilities);
  NANO_OS_ASSERT_EQ_PTR(loginDirCapabilities, reportedSetAddress);
  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_EXECUTIVE, reportedPrivilege);
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_restarted_users_shell_keeps_the_users_set,
  dircapPreBoot
) {
  startShellSlot(5, "/usr/bin/mush");
  NANO_OS_ASSERT_TRUE(yieldUntilReported());
  NANO_OS_ASSERT_EQ_INT(0, childStatus);
  NANO_OS_ASSERT_NOT_NULL(exitedSet);
  NANO_OS_ASSERT_EQ_PTR(exitedSet, reportedSetAddress);
  NANO_OS_ASSERT_EQ_INT(1, reportedSet.set.refCount);
  NANO_OS_ASSERT_FALSE(setAllows(&reportedSet.set, "/tmp/x", DIRCAP_WRITE));
}

/// @fn static void spawnRound(posix_spawnattr_t *attr)
///
/// @brief Spawn and wait for one child of each kind that changes reference
/// counts or allocates a set:  one that shares and adds, one that only
/// shares, and one that gets a copy of a spawn attribute's entries.
///
/// @param attr A spawn attribute with entries.
///
/// @return This function returns no value.
static void spawnRound(posix_spawnattr_t *attr) {
  NANO_OS_ASSERT_TRUE(spawnAndWait("narrow", NULL));
  NANO_OS_ASSERT_EQ_INT(0, childStatus);
  NANO_OS_ASSERT_TRUE(spawnAndWait("report", NULL));
  NANO_OS_ASSERT_TRUE(spawnAndWait("report", attr));
}

NANO_OS_KERNEL_TEST_WITH(dircap, the_heap_returns_to_its_level_after_spawns,
  dircapPreBoot
) {
  DirCapabilities *parentSet = narrowOwnSet();
  posix_spawnattr_t attr = { .flags = 0, .dirCapabilities = NULL };
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawnattrAdddircap(&attr, "/tmp/", DIRCAP_READ));

  for (int ii = 0; ii < 2; ii++) {
    spawnRound(&attr);
  }
  size_t before = getFreeMemory();
  for (int ii = 0; ii < 10; ii++) {
    spawnRound(&attr);
  }
  NANO_OS_ASSERT_EQ_INT((int) before, (int) getFreeMemory());
  NANO_OS_ASSERT_EQ_INT(1, parentSet->refCount);
  free(attr.dirCapabilities);
}

/// @var signalStatuses
///
/// @brief What signalSender's two signals returned, and the errno of each.
static volatile int signalStatuses[4];

/// @var signalTargets
///
/// @brief The processes signalSender signals:  one of another user, then one
/// of its own.
static volatile pid_t signalTargets[2];

/// @var signalsSent
///
/// @brief Whether signalSender has sent both signals.
static volatile bool signalsSent;

/// @fn static void* idleBody(void *args)
///
/// @brief Run until killed.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* idleBody(void *args) {
  (void) args;
  while (1) {
    processYield();
  }
  return NULL;
}

/// @fn static void* signalSender(void *args)
///
/// @brief Send signal 0 to each of signalTargets, record the results, and
/// idle.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* signalSender(void *args) {
  (void) args;
  for (int ii = 0; ii < 2; ii++) {
    signalStatuses[ii * 2] = schedulerSendSignal(signalTargets[ii], 0);
    signalStatuses[(ii * 2) + 1] = errno;
  }
  signalsSent = true;
  return idleBody(NULL);
}

NANO_OS_KERNEL_TEST(dircap, only_the_same_user_or_root_may_signal_a_process) {
  ProcessDescriptor *other = startProcess(idleBody, 5, false);
  ProcessDescriptor *same = startProcess(idleBody, 7, false);
  NANO_OS_ASSERT_NOT_NULL(other);
  NANO_OS_ASSERT_NOT_NULL(same);
  processQueuePush(other->readyQueue, other);
  processQueuePush(same->readyQueue, same);
  signalTargets[0] = other->processId;
  signalTargets[1] = same->processId;
  signalsSent = false;

  ProcessDescriptor *sender = startProcess(signalSender, 7, true);
  NANO_OS_ASSERT_NOT_NULL(sender);
  processQueuePush(sender->readyQueue, sender);
  for (int ii = 0; (ii < 2000) && !signalsSent; ii++) {
    processYield();
  }

  NANO_OS_ASSERT_TRUE(signalsSent);
  NANO_OS_ASSERT_EQ_INT(-1, signalStatuses[0]);
  NANO_OS_ASSERT_EQ_INT(EPERM, signalStatuses[1]);
  NANO_OS_ASSERT_EQ_INT(0, signalStatuses[2]);
  NANO_OS_ASSERT_EQ_INT(0, schedulerSendSignal(other->processId, 0));
}

NANO_OS_KERNEL_TEST(dircap, only_the_scheduler_may_set_a_processs_set) {
  ProcessDescriptor *self = getRunningProcess();
  NANO_OS_ASSERT_TRUE(self->processId != schedulerPid);
  DirCapabilities *before = self->dirCapabilities;
  NANO_OS_ASSERT_EQ_PTR(supervisorDirCapabilities, before);

  NANO_OS_ASSERT_EQ_INT(-EPERM, dirCapabilityPut(&self->dirCapabilities,
    "/nowhere/", 9, DIRCAP_ALL, schedRealloc));
  NANO_OS_ASSERT_EQ_PTR(before, self->dirCapabilities);
  NANO_OS_ASSERT_EQ_INT(-EPERM,
    schedulerSetDirCapabilities(self, rootDirCapabilities));
  NANO_OS_ASSERT_EQ_PTR(before, self->dirCapabilities);
}

NANO_OS_KERNEL_TEST(dircap, null_pointers_from_a_user_are_rejected) {
  char *name = NULL;
  size_t length = 0;
  posix_spawnattr_t attr = { .flags = 0, .dirCapabilities = NULL };

  NANO_OS_ASSERT_EQ_INT(EINVAL, nanoOsDircapName(NULL, &name, &length));
  NANO_OS_ASSERT_EQ_INT(EINVAL, nanoOsDircapName("/a", NULL, &length));
  NANO_OS_ASSERT_EQ_INT(EINVAL, nanoOsDircapName("/a", &name, NULL));
  NANO_OS_ASSERT_EQ_INT(-1, nanoOsDircapAdd(NULL, DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(EINVAL, errno);
  errno = 0;
  NANO_OS_ASSERT_EQ_INT(-1, nanoOsDircapDrop(NULL));
  NANO_OS_ASSERT_EQ_INT(EINVAL, errno);
  NANO_OS_ASSERT_EQ_INT(EINVAL,
    nanoOsSpawnattrAdddircap(NULL, "/tmp/", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(EINVAL,
    nanoOsSpawnattrAdddircap(&attr, NULL, DIRCAP_READ));
  NANO_OS_ASSERT_NULL(attr.dirCapabilities);
}

NANO_OS_KERNEL_TEST(dircap, a_message_with_no_payload_is_answered) {
  static const int commands[] = {
    SCHEDULER_ADD_DIR_CAPABILITY,
    SCHEDULER_DROP_DIR_CAPABILITY,
    SCHEDULER_GET_DIR_CAPABILITIES,
  };
  DirCapabilities *before = getRunningProcess()->dirCapabilities;

  for (size_t ii = 0; ii < sizeof(commands) / sizeof(commands[0]); ii++) {
    ProcessMessage *processMessage = initSendProcessMessageToPid(schedulerPid,
      SCHEDULER_COMMAND_SIGNATURE | commands[ii], NULL, 0, true);
    NANO_OS_ASSERT_NOT_NULL(processMessage);
    processMessageWaitForDone(processMessage, NULL);
    NANO_OS_ASSERT_TRUE(processMessageDone(processMessage));
    processMessageRelease(processMessage);
  }
  NANO_OS_ASSERT_EQ_PTR(before, getRunningProcess()->dirCapabilities);
}

NANO_OS_KERNEL_TEST(dircap, a_spawn_attribute_builds_a_well_formed_request) {
  posix_spawnattr_t attr = { .flags = 0, .dirCapabilities = NULL };
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawnattrAdddircap(&attr, "/tmp", DIRCAP_READ));
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawnattrAdddircap(&attr, "/home/", DIRCAP_LIST));
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawnattrAdddircap(&attr, "/tmp/", DIRCAP_WRITE));

  NANO_OS_ASSERT_NOT_NULL(attr.dirCapabilities);
  NANO_OS_ASSERT_TRUE(dirCapabilitiesValid(attr.dirCapabilities));
  NANO_OS_ASSERT_EQ_INT(2, attr.dirCapabilities->numCapabilities);
  const DirCapability *first = attr.dirCapabilities->capabilities;
  NANO_OS_ASSERT_EQ_INT(DIRCAP_WRITE, first->mask);
  NANO_OS_ASSERT_EQ_INT(5, first->len);
  NANO_OS_ASSERT_EQ_INT(0, memcmp("/tmp/", first->name, 5));
  free(attr.dirCapabilities);
}

NANO_OS_KERNEL_TEST(dircap, a_spawn_attribute_refuses_a_bad_entry) {
  posix_spawnattr_t attr = { .flags = 0, .dirCapabilities = NULL };
  NANO_OS_ASSERT_EQ_INT(EINVAL, nanoOsSpawnattrAdddircap(&attr, "/tmp/", 0));
  NANO_OS_ASSERT_EQ_INT(EINVAL, nanoOsSpawnattrAdddircap(&attr, "/tmp/", 0x80));

  char longName[260];
  memset(longName, 'a', sizeof(longName) - 1);
  longName[0] = '/';
  longName[sizeof(longName) - 1] = '\0';
  NANO_OS_ASSERT_EQ_INT(EINVAL,
    nanoOsSpawnattrAdddircap(&attr, longName, DIRCAP_READ));
  NANO_OS_ASSERT_NULL(attr.dirCapabilities);
}

extern HalCapability baseUserHalCapabilities[];
extern HalCapability baseExecutiveHalCapabilities[];

/// @var execPath
///
/// @brief The path execProgram asks for.
static char execPath[64];

/// @var execFullPath
///
/// @brief Whether execProgram uses the executive call.
static volatile bool execFullPath;

/// @var execFinished
///
/// @brief Whether execProgram's exec returned, which means that it failed.
static volatile bool execFinished;

/// @fn static void* execProgram(void *args)
///
/// @brief Exec execPath with argv[0] "report".
///
/// @param args Unused.
///
/// @return This function returns NULL only if the exec fails.
static void* execProgram(void *args) {
  (void) args;
  char *argv[] = { "report", NULL };
  if (execFullPath) {
    schedulerExecveFullPath(execPath, argv, NULL);
  } else {
    schedulerExecve(execPath, argv, NULL);
  }
  childStatus = errno;
  reportedPrivilege = getRunningProcess()->privilegeLevel;
  execFinished = true;
  return NULL;
}

/// @fn static void startExec(const char *path, PrivilegeLevel privilegeLevel)
///
/// @brief Run execProgram in a free slot at a privilege level.
///
/// @param path The path to exec.
/// @param privilegeLevel The level of the slot.
///
/// @return This function returns no value.
static void startExec(const char *path, PrivilegeLevel privilegeLevel) {
  strcpy(execPath, path);
  childStatus = -1;
  reported = false;
  execFinished = false;
  ProcessDescriptor *processDescriptor
    = startProcess(execProgram, getRunningUid(), true);
  NANO_OS_ASSERT_NOT_NULL(processDescriptor);
  processDescriptor->privilegeLevel = privilegeLevel;
  processDescriptor->readyQueue = allProcesses[consolePid - 1].readyQueue;
  if (privilegeLevel == PRIVILEGE_LEVEL_EXECUTIVE) {
    processDescriptor->halCapabilities = baseExecutiveHalCapabilities;
  }
  processDescriptor->fileDescriptors
    = allProcesses[consolePid - 1].fileDescriptors;
  processDescriptor->numFileDescriptors
    = allProcesses[consolePid - 1].numFileDescriptors;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
}

/// @fn static void expectExecRunsAt(const char *path, PrivilegeLevel from,
///   PrivilegeLevel expected, const char *expectedPath)
///
/// @brief Check the level and full path a program runs with after an exec.
///
/// @param path The path to exec.
/// @param from The level of the caller.
/// @param expected The level the program should run at.
/// @param expectedPath The full path the program should be loaded from.
///
/// @return This function returns no value.
static void expectExecRunsAt(const char *path, PrivilegeLevel from,
  PrivilegeLevel expected, const char *expectedPath
) {
  startExec(path, from);
  NANO_OS_ASSERT_TRUE(yieldUntilReported());
  NANO_OS_ASSERT_FALSE(execFinished);
  NANO_OS_ASSERT_EQ_INT(expected, reportedPrivilege);
  NANO_OS_ASSERT_STR_EQ(expectedPath, reportedPath);
}

/// @fn static void expectFullPathExecRefused(PrivilegeLevel privilegeLevel)
///
/// @brief Check that the executive call fails with EPERM at a level.
///
/// @param privilegeLevel The level of the caller.
///
/// @return This function returns no value.
static void expectFullPathExecRefused(PrivilegeLevel privilegeLevel) {
  execFullPath = true;
  startExec("/system/bin/login", privilegeLevel);
  for (int ii = 0; (ii < 2000) && !execFinished && !reported; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_TRUE(execFinished);
  NANO_OS_ASSERT_FALSE(reported);
  NANO_OS_ASSERT_EQ_INT(EPERM, childStatus);
  NANO_OS_ASSERT_EQ_INT(privilegeLevel, reportedPrivilege);
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_supervisor_exec_of_system_stays_in_userspace,
  dircapPreBoot
) {
  expectExecRunsAt("/system/bin/login", PRIVILEGE_LEVEL_SUPERVISOR,
    PRIVILEGE_LEVEL_SUPERVISOR, "/userspace/system/bin/login");
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_user_exec_of_secure_stays_in_userspace,
  dircapPreBoot
) {
  expectExecRunsAt("/secure/shadow", PRIVILEGE_LEVEL_USER,
    PRIVILEGE_LEVEL_USER, "/userspace/secure/shadow");
}

NANO_OS_KERNEL_TEST_WITH(dircap, exec_cannot_climb_out_of_userspace_through_dots,
  dircapPreBoot
) {
  expectExecRunsAt("/usr/../../system/bin/login", PRIVILEGE_LEVEL_SUPERVISOR,
    PRIVILEGE_LEVEL_SUPERVISOR, "/userspace/system/bin/login");
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_relative_exec_path_stays_in_userspace,
  dircapPreBoot
) {
  expectExecRunsAt("system/bin/login", PRIVILEGE_LEVEL_SUPERVISOR,
    PRIVILEGE_LEVEL_SUPERVISOR, "/userspace/system/bin/login");
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_user_api_exec_from_an_executive_drops_privilege,
  dircapPreBoot
) {
  expectExecRunsAt("/system/bin/login", PRIVILEGE_LEVEL_EXECUTIVE,
    PRIVILEGE_LEVEL_SUPERVISOR, "/userspace/system/bin/login");
}

NANO_OS_KERNEL_TEST_WITH(dircap, an_executive_full_path_exec_stays_executive,
  dircapPreBoot
) {
  execFullPath = true;
  expectExecRunsAt("/system/bin/login", PRIVILEGE_LEVEL_EXECUTIVE,
    PRIVILEGE_LEVEL_EXECUTIVE, "/system/bin/login");
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_full_path_exec_is_not_tied_to_a_directory_name,
  dircapPreBoot
) {
  execFullPath = true;
  expectExecRunsAt("/vendor/bin/x", PRIVILEGE_LEVEL_EXECUTIVE,
    PRIVILEGE_LEVEL_EXECUTIVE, "/vendor/bin/x");
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_full_path_exec_of_userspace_drops_privilege,
  dircapPreBoot
) {
  execFullPath = true;
  expectExecRunsAt("/userspace/usr/bin/mush", PRIVILEGE_LEVEL_EXECUTIVE,
    PRIVILEGE_LEVEL_SUPERVISOR, "/userspace/usr/bin/mush");
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_full_path_exec_cannot_hide_userspace_with_dots,
  dircapPreBoot
) {
  execFullPath = true;
  expectExecRunsAt("/system/../userspace/usr/bin/mush",
    PRIVILEGE_LEVEL_EXECUTIVE, PRIVILEGE_LEVEL_SUPERVISOR,
    "/userspace/usr/bin/mush");
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_supervisor_cannot_use_the_full_path_exec,
  dircapPreBoot
) {
  expectFullPathExecRefused(PRIVILEGE_LEVEL_SUPERVISOR);
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_user_cannot_use_the_full_path_exec,
  dircapPreBoot
) {
  expectFullPathExecRefused(PRIVILEGE_LEVEL_USER);
}

NANO_OS_KERNEL_TEST(dircap, the_full_path_exec_checks_its_arguments) {
  char *argv[] = { "report", NULL };
  NANO_OS_ASSERT_EQ_INT(-1, schedulerExecveFullPath(NULL, argv, NULL));
  NANO_OS_ASSERT_EQ_INT(EFAULT, errno);
  NANO_OS_ASSERT_EQ_INT(-1, schedulerExecveFullPath("/x", NULL, NULL));
  NANO_OS_ASSERT_EQ_INT(EFAULT, errno);
}

NANO_OS_KERNEL_TEST_WITH(dircap, exec_outside_the_system_tree_runs_at_supervisor,
  dircapPreBoot
) {
  expectExecRunsAt("/usr/bin/mush", PRIVILEGE_LEVEL_EXECUTIVE,
    PRIVILEGE_LEVEL_SUPERVISOR, "/userspace/usr/bin/mush");
  ProcessQueue *supervisorQueue = NULL;
  for (ProcessId pid = 1; supervisorQueue == NULL; pid++) {
    NANO_OS_ASSERT_TRUE(pid <= numProcesses);
    if ((allProcesses[pid - 1].privilegeLevel == PRIVILEGE_LEVEL_SUPERVISOR)
      && (allProcesses[pid - 1].readyQueue != NULL)
      && (allProcesses[pid - 1].readyQueue
        != allProcesses[consolePid - 1].readyQueue)
    ) {
      supervisorQueue = allProcesses[pid - 1].readyQueue;
    }
  }
  NANO_OS_ASSERT_EQ_PTR(supervisorQueue, reportedQueue);
  NANO_OS_ASSERT_EQ_PTR(baseUserHalCapabilities, reportedHal);
}

NANO_OS_KERNEL_TEST_WITH(dircap, exec_never_raises_privilege, dircapPreBoot) {
  expectExecRunsAt("/usr/bin/mush", PRIVILEGE_LEVEL_SUPERVISOR,
    PRIVILEGE_LEVEL_SUPERVISOR, "/userspace/usr/bin/mush");
}

NANO_OS_KERNEL_TEST_WITH(dircap, exec_keeps_a_user_at_user_level,
  dircapPreBoot
) {
  expectExecRunsAt("/usr/bin/mush", PRIVILEGE_LEVEL_USER,
    PRIVILEGE_LEVEL_USER, "/userspace/usr/bin/mush");
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_spawn_of_system_stays_in_userspace,
  dircapPreBoot
) {
  pid_t pid = 0;
  char *argv[] = { "report", NULL };
  getRunningProcess()->privilegeLevel = PRIVILEGE_LEVEL_SUPERVISOR;
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSpawn(&pid, "/tmp/../../system/bin/login", NULL, NULL, argv, NULL));
  NANO_OS_ASSERT_TRUE(yieldUntilReported());
  NANO_OS_ASSERT_STR_EQ("/userspace/system/bin/login", reportedPath);
  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_USER, reportedPrivilege);
}

NANO_OS_KERNEL_TEST_WITH(dircap, spawn_never_runs_a_child_at_executive_level,
  dircapPreBoot
) {
  ProcessQueue *freeQueue = halMockFreeQueue();
  ProcessDescriptor **slot = freeQueue->head;
  for (uint8_t ii = 0; ii < freeQueue->numElements; ii++) {
    slot[ii]->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  }
  NANO_OS_ASSERT_TRUE(spawnAndWait("report", NULL));
  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_USER, reportedPrivilege);
}

/// @fn static void* setuidChild(void *args)
///
/// @brief Call setuid with execPath's first byte selecting the ID:  'a' asks
/// for -1, anything else for user 5.
///
/// @param args Unused.
///
/// @return This function returns NULL.
static void* setuidChild(void *args) {
  (void) args;
  errno = 0;
  int returnValue = schedulerSetProcessUser(
    (execPath[0] == 'a') ? ((UserId) -1) : 5);
  childStatus = (returnValue == 0) ? 0 : errno;
  reportedUser = getRunningProcess()->userId;
  reported = true;
  return NULL;
}

/// @fn static void expectSetuid(UserId startId, PrivilegeLevel privilegeLevel,
///   char selector, int expectedStatus, UserId expectedId)
///
/// @brief Run setuidChild and check the result.
///
/// @param startId The user ID the process starts with.
/// @param privilegeLevel The level the process runs at.
/// @param selector What setuidChild asks for.
/// @param expectedStatus The errno expected, or 0 for success.
/// @param expectedId The user ID the process should end with.
///
/// @return This function returns no value.
static void expectSetuid(UserId startId, PrivilegeLevel privilegeLevel,
  char selector, int expectedStatus, UserId expectedId
) {
  execPath[0] = selector;
  childStatus = -1;
  reported = false;
  ProcessDescriptor *processDescriptor
    = startProcess(setuidChild, startId, true);
  NANO_OS_ASSERT_NOT_NULL(processDescriptor);
  processDescriptor->privilegeLevel = privilegeLevel;
  processDescriptor->readyQueue = getRunningProcess()->readyQueue;
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  NANO_OS_ASSERT_TRUE(yieldUntilReported());
  NANO_OS_ASSERT_EQ_INT(expectedStatus, childStatus);
  NANO_OS_ASSERT_EQ_INT(expectedId, reportedUser);
}

NANO_OS_KERNEL_TEST(dircap, a_supervisor_cannot_setuid) {
  expectSetuid(7, PRIVILEGE_LEVEL_SUPERVISOR, 'a', EPERM, 7);
  expectSetuid(7, PRIVILEGE_LEVEL_SUPERVISOR, 'b', EPERM, 7);
  expectSetuid(NO_USER_ID, PRIVILEGE_LEVEL_SUPERVISOR, 'b', EPERM, NO_USER_ID);
}

NANO_OS_KERNEL_TEST(dircap, a_user_process_cannot_setuid) {
  expectSetuid(7, PRIVILEGE_LEVEL_USER, 'a', EPERM, 7);
  expectSetuid(NO_USER_ID, PRIVILEGE_LEVEL_USER, 'b', EPERM, NO_USER_ID);
}

NANO_OS_KERNEL_TEST(dircap, an_executive_process_can_setuid) {
  expectSetuid(NO_USER_ID, PRIVILEGE_LEVEL_EXECUTIVE, 'b', 0, 5);
}

NANO_OS_KERNEL_TEST(dircap, an_executive_process_cannot_change_a_set_user) {
  expectSetuid(7, PRIVILEGE_LEVEL_EXECUTIVE, 'b', EACCES, 7);
}

/// @var consoleEchoArgv
///
/// @brief The arguments of a CONSOLE line's program in the tests.
static char *consoleEchoArgv[] = { "echo", NULL };

/// @var consoleProgram
///
/// @brief The program of a shell slot's CONSOLE line in the tests.
static ShellProgram consoleProgram = { NULL, consoleEchoArgv };

NANO_OS_KERNEL_TEST_WITH(dircap, a_console_program_outside_system_is_supervisor,
  dircapPreBoot
) {
  static char path[] = "/userspace/usr/bin/echo";
  consoleProgram.path = path;
  ProcessDescriptor *processDescriptor = startShellSlot(NO_USER_ID, NULL);
  processDescriptor->restartArgs = &consoleProgram;
  processDescriptor->privilegeLevel = PRIVILEGE_LEVEL_EXECUTIVE;
  NANO_OS_ASSERT_TRUE(yieldUntilReported());
  NANO_OS_ASSERT_EQ_PTR(supervisorDirCapabilities, reportedSetAddress);
  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_SUPERVISOR, reportedPrivilege);
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_console_program_in_system_is_executive,
  dircapPreBoot
) {
  static char path[] = "/system/bin/echo";
  consoleProgram.path = path;
  ProcessDescriptor *processDescriptor = startShellSlot(NO_USER_ID, NULL);
  processDescriptor->restartArgs = &consoleProgram;
  NANO_OS_ASSERT_TRUE(yieldUntilReported());
  NANO_OS_ASSERT_EQ_PTR(loginDirCapabilities, reportedSetAddress);
  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_EXECUTIVE, reportedPrivilege);
}

NANO_OS_KERNEL_TEST(dircap, the_login_set_holds_what_it_documents) {
  NANO_OS_ASSERT_TRUE(dirCapabilitiesValid(loginDirCapabilities));
  NANO_OS_ASSERT_TRUE(setAllows(loginDirCapabilities, "/system/bin/login/x",
    DIRCAP_EXEC | DIRCAP_FULL_PATH));
  NANO_OS_ASSERT_FALSE(setAllows(loginDirCapabilities, "/system/bin/login/x",
    DIRCAP_READ | DIRCAP_FULL_PATH));
  NANO_OS_ASSERT_TRUE(setAllows(loginDirCapabilities, "/secure/shadow",
    DIRCAP_READ | DIRCAP_FULL_PATH));
  NANO_OS_ASSERT_FALSE(setAllows(loginDirCapabilities, "/secure/shadow",
    DIRCAP_WRITE | DIRCAP_FULL_PATH));
  NANO_OS_ASSERT_TRUE(setAllows(loginDirCapabilities, "/usr/bin/mush",
    DIRCAP_EXEC));
  NANO_OS_ASSERT_TRUE(dirCapabilitiesValid(supervisorDirCapabilities));
  NANO_OS_ASSERT_TRUE(setAllows(supervisorDirCapabilities, "/menu/x",
    DIRCAP_ALL));
  NANO_OS_ASSERT_FALSE(setAllows(supervisorDirCapabilities,
    "/system/bin/login/x", DIRCAP_EXEC | DIRCAP_FULL_PATH));
  NANO_OS_ASSERT_FALSE(setAllows(supervisorDirCapabilities, "/secure/shadow",
    DIRCAP_READ | DIRCAP_FULL_PATH));
}

/// @fn static int spawnSystem(const char *name, posix_spawnattr_t *attr,
///   char *const envp[], pid_t *pid)
///
/// @brief Spawn a dircapChild that reports and exits as a system command.
///
/// @param name The system command's name.
/// @param attr The spawn attribute, which this sets POSIX_SPAWN_SYSTEM on.
/// @param envp The environment to ask for, or NULL.
/// @param pid Where to store the child's process ID.
///
/// @return Returns what nanoOsSpawn returned.
static int spawnSystem(const char *name, posix_spawnattr_t *attr,
  char *const envp[], pid_t *pid
) {
  char *argv[] = { "report", NULL };
  attr->flags |= POSIX_SPAWN_SYSTEM;
  childStatus = -1;
  reported = false;
  *pid = 0;
  return nanoOsSpawn(pid, name, NULL, attr, argv, envp);
}

/// @var systemCommandSetEntries
///
/// @brief The packed entries the set of a system command should hold.
static const char systemCommandSetEntries[]
  = "\x8f" "\x08" "/secure/" "\x01" "\x05" "/etc/";

NANO_OS_KERNEL_TEST_WITH(dircap, a_system_command_runs_at_executive_level,
  dircapPreBoot
) {
  ProcessDescriptor *parent = getRunningProcess();
  DirCapabilities *parentSet = parent->dirCapabilities;
  UserId parentUser = parent->userId;
  parent->userId = 3;
  posix_spawnattr_t attr = { 0, NULL };
  char envUser[] = "USER=root";
  char *envp[] = { envUser, NULL };
  pid_t pid = 0;
  NANO_OS_ASSERT_EQ_INT(0, spawnSystem("passwd", &attr, envp, &pid));
  NANO_OS_ASSERT_EQ_INT(pid, schedulerWaitpid(pid, NULL, 0));
  parent->userId = parentUser;
  NANO_OS_ASSERT_TRUE(reported);
  NANO_OS_ASSERT_STR_EQ("/system/commands/passwd", reportedPath);
  NANO_OS_ASSERT_EQ_INT(PRIVILEGE_LEVEL_EXECUTIVE, reportedPrivilege);
  NANO_OS_ASSERT_NOT_NULL(reportedQueue);
  NANO_OS_ASSERT_STR_EQ("executive ready", reportedQueue->name);
  NANO_OS_ASSERT_EQ_PTR(baseExecutiveHalCapabilities, reportedHal);
  NANO_OS_ASSERT_EQ_INT(3, reportedUser);
  NANO_OS_ASSERT_NULL(reportedEnvp);
  NANO_OS_ASSERT_EQ_INT(2, reportedSet.set.numCapabilities);
  NANO_OS_ASSERT_EQ_INT(DIR_CAPABILITIES_HEADER_SIZE
    + sizeof(systemCommandSetEntries) - 1, reportedSet.set.size);
  NANO_OS_ASSERT_EQ_INT(0, memcmp(reportedSet.set.capabilities,
    systemCommandSetEntries, sizeof(systemCommandSetEntries) - 1));
  NANO_OS_ASSERT_EQ_PTR(parentSet, parent->dirCapabilities);
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_system_command_needs_a_bare_name,
  dircapPreBoot
) {
  static const char *const names[] = {
    "", ".", "..", "bin/login", "../bin/login", "/system/bin/login",
    "x/../../bin/login", "x/..", "./passwd",
  };
  for (size_t ii = 0; ii < sizeof(names) / sizeof(names[0]); ii++) {
    posix_spawnattr_t attr = { 0, NULL };
    pid_t pid = 0;
    NANO_OS_ASSERT_EQ_INT(EINVAL, spawnSystem(names[ii], &attr, NULL, &pid));
    NANO_OS_ASSERT_EQ_INT(0, pid);
  }
  for (int ii = 0; ii < 50; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_FALSE(reported);
}

NANO_OS_KERNEL_TEST_WITH(dircap, a_system_command_cannot_be_narrowed_by_its_caller,
  dircapPreBoot
) {
  posix_spawnattr_t attr = { 0, NULL };
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSpawnattrAdddircap(&attr, "/etc/",
    DIRCAP_READ));
  pid_t pid = 0;
  NANO_OS_ASSERT_EQ_INT(EINVAL, spawnSystem("passwd", &attr, NULL, &pid));
  free(attr.dirCapabilities);
}

/// @fn static int sendRawSpawn(char *path, char **envp, short flags)
///
/// @brief Send the scheduler a spawn request built by hand, the way a program
/// that skipped nanoOsSpawn's checks could.
///
/// @param path The full path to put in the request.
/// @param envp The environment to put in the request, or NULL.
/// @param flags The POSIX_SPAWN_* flags.
///
/// @return Returns the errno the scheduler replied with.
static int sendRawSpawn(char *path, char **envp, short flags) {
  static char argv0[] = "report";
  char *argv[] = { argv0, NULL };
  posix_spawnattr_t attr = { flags, NULL };
  pid_t pid = 0;
  SpawnArgs spawnArgs = {
    .newPid = (uint8_t*) &pid,
    .path = path,
    .fileActions = NULL,
    .attrp = &attr,
    .argv = argv,
    .envp = envp,
  };
  SchedulerSpawnArgs schedulerSpawnArgs = { &spawnArgs, 0 };
  reported = false;
  ProcessMessage *processMessage = initSendProcessMessageToPid(schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_SPAWN,
    &schedulerSpawnArgs, sizeof(schedulerSpawnArgs), true);
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);
  return schedulerSpawnArgs.errorNumber;
}

NANO_OS_KERNEL_TEST_WITH(dircap, the_scheduler_checks_a_system_command_itself,
  dircapPreBoot
) {
  static char climbs[] = "/system/commands/../bin/login";
  static char outside[] = "/system/bin/login";
  static char dot[] = "/system/commands/..";
  static char through[] = "/system/commands/x/../../bin/login";
  static char good[] = "/system/commands/passwd";
  static char envUser[] = "USER=root";
  char *envp[] = { envUser, NULL };
  NANO_OS_ASSERT_EQ_INT(EINVAL, sendRawSpawn(climbs, NULL, POSIX_SPAWN_SYSTEM));
  NANO_OS_ASSERT_EQ_INT(EINVAL,
    sendRawSpawn(outside, NULL, POSIX_SPAWN_SYSTEM));
  NANO_OS_ASSERT_EQ_INT(EINVAL, sendRawSpawn(dot, NULL, POSIX_SPAWN_SYSTEM));
  NANO_OS_ASSERT_EQ_INT(EINVAL,
    sendRawSpawn(through, NULL, POSIX_SPAWN_SYSTEM));
  NANO_OS_ASSERT_EQ_INT(EINVAL, sendRawSpawn(good, envp, POSIX_SPAWN_SYSTEM));
  for (int ii = 0; ii < 50; ii++) {
    processYield();
  }
  NANO_OS_ASSERT_FALSE(reported);
}

NANO_OS_KERNEL_TEST(dircap, the_system_command_set_holds_what_it_documents) {
  DirCapabilities *set = (DirCapabilities*) malloc(
    DIR_CAPABILITIES_HEADER_SIZE + sizeof(systemCommandSetEntries) - 1);
  NANO_OS_ASSERT_NOT_NULL(set);
  set->refCount = 1;
  set->numCapabilities = 2;
  set->size = (uint16_t) (DIR_CAPABILITIES_HEADER_SIZE
    + sizeof(systemCommandSetEntries) - 1);
  memcpy(set->capabilities, systemCommandSetEntries,
    sizeof(systemCommandSetEntries) - 1);
  NANO_OS_ASSERT_TRUE(dirCapabilitiesValid(set));
  NANO_OS_ASSERT_TRUE(setAllows(set, "/secure/shadow",
    DIRCAP_READ | DIRCAP_WRITE | DIRCAP_CREATE | DIRCAP_DELETE
    | DIRCAP_FULL_PATH));
  NANO_OS_ASSERT_FALSE(setAllows(set, "/secure/shadow",
    DIRCAP_EXEC | DIRCAP_FULL_PATH));
  NANO_OS_ASSERT_TRUE(setAllows(set, "/etc/passwd", DIRCAP_READ));
  NANO_OS_ASSERT_FALSE(setAllows(set, "/etc/passwd", DIRCAP_WRITE));
  NANO_OS_ASSERT_FALSE(setAllows(set, "/system/bin/login/x",
    DIRCAP_EXEC | DIRCAP_FULL_PATH));
  free(set);
}

NANO_OS_KERNEL_TEST(dircap, getuid_reports_the_user_the_kernel_holds) {
  ProcessDescriptor *running = getRunningProcess();
  UserId userId = running->userId;
  // The kernel's uid_t is 16 bits and this file's is glibc's 32, so only the
  // low 16 bits of the result are defined here.
  running->userId = 4;
  UserId reportedId = (UserId) nanoOsGetuid();
  running->userId = NO_USER_ID;
  UserId noUser = (UserId) nanoOsGetuid();
  running->userId = userId;
  NANO_OS_ASSERT_EQ_INT(4, reportedId);
  NANO_OS_ASSERT_EQ_INT(-1, noUser);
}
