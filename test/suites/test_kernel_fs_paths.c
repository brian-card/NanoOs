///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_fs_paths.c
///
/// @brief             Kernel tests for how the filesystem command handlers
///                    resolve the paths they're sent.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <stdlib.h>
#include <string.h>

#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsLibC.h"

#define NANO_OS_KERNEL_BUILD
#include "../../usr/src/filesystems/include/FilesystemPaths.h"

static FilesystemState pathState;
static ProcessMessage pathMessage;

static void pathFixture(void) {
  memset(&pathState, 0, sizeof(pathState));
  pathState.allProcesses = allProcesses;
  pathState.numProcesses = numProcesses;
  memset(&pathMessage, 0, sizeof(pathMessage));
  pathMessage.from.coro = getRunningProcess()->mainThread;
}

static void expectResolved(const char *expected, const char *path) {
  char *input = strdup(path);
  char *resolved = filesystemAbsolutePath(&pathState, &pathMessage, input);
  NANO_OS_ASSERT_NOT_NULL(resolved);
  NANO_OS_ASSERT_TRUE(resolved != input);
  NANO_OS_ASSERT_STR_EQ(expected, resolved);
  free(resolved);
  free(input);
}

NANO_OS_KERNEL_TEST(fs_paths, absolute_path_is_passed_through) {
  pathFixture();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWD", "/etc", 1));
  char path[] = "/usr/../bin";
  NANO_OS_ASSERT_EQ_PTR(path,
    filesystemAbsolutePath(&pathState, &pathMessage, path));
  NANO_OS_ASSERT_STR_EQ("/usr/../bin", path);
}

NANO_OS_KERNEL_TEST(fs_paths, relative_path_is_resolved_against_pwd) {
  pathFixture();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWD", "/usr/bin", 1));
  expectResolved("/usr/bin/catfile", "catfile");
  expectResolved("/usr/etc/issue", "../etc/./issue");
  expectResolved("/usr/bin", ".");
  expectResolved("/", "../../..");
}

NANO_OS_KERNEL_TEST(fs_paths, root_pwd_does_not_double_the_slash) {
  pathFixture();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWD", "/", 1));
  expectResolved("/etc/issue", "etc/issue");
}

NANO_OS_KERNEL_TEST(fs_paths, missing_pwd_resolves_against_root) {
  pathFixture();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWDX", "/usr", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsUnsetenv("PWD"));
  expectResolved("/etc/issue", "etc/issue");
}

NANO_OS_KERNEL_TEST(fs_paths, relative_pwd_resolves_against_root) {
  pathFixture();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWD", "usr", 1));
  expectResolved("/etc/issue", "etc/issue");
}

NANO_OS_KERNEL_TEST(fs_paths, unknown_sender_resolves_against_root) {
  pathFixture();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWD", "/usr", 1));
  pathState.numProcesses = 0;
  expectResolved("/etc/issue", "etc/issue");
}
