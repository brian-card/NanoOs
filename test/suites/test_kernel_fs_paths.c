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

static void requestFullPath(void) {
  pathMessage.type |= FILESYSTEM_FULL_PATH;
}

static void expectReal(const char *expected, const char *path) {
  char *input = strdup(path);
  char *resolved = filesystemAbsolutePath(&pathState, &pathMessage, input);
  NANO_OS_ASSERT_NOT_NULL(resolved);
  NANO_OS_ASSERT_TRUE(resolved != input);
  NANO_OS_ASSERT_STR_EQ(expected, resolved);
  free(resolved);
  free(input);
}

static void expectResolved(const char *expected, const char *path) {
  char real[128];
  strcpy(real, "/userspace");
  if (strcmp(expected, "/") != 0) {
    strcat(real, expected);
  }
  expectReal(real, path);
}

static void expectUnchanged(const char *path) {
  char *input = strdup(path);
  NANO_OS_ASSERT_EQ_PTR(input,
    filesystemAbsolutePath(&pathState, &pathMessage, input));
  NANO_OS_ASSERT_STR_EQ(path, input);
  free(input);
}

static void runAs(PrivilegeLevel privilegeLevel) {
  getRunningProcess()->privilegeLevel = privilegeLevel;
}

NANO_OS_KERNEL_TEST(fs_paths, collapsed_absolute_path_is_passed_through) {
  pathFixture();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWD", "/etc", 1));
  expectResolved("/", "/");
  expectResolved("/usr/bin", "/usr/bin");
  expectResolved("/home/briana/.mush_history", "/home/briana/.mush_history");
  expectResolved("/a/..b/.c/...", "/a/..b/.c/...");
}

NANO_OS_KERNEL_TEST(fs_paths, absolute_path_is_collapsed) {
  pathFixture();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWD", "/etc", 1));
  expectResolved("/bin", "/usr/../bin");
  expectResolved("/etc/passwd", "/home/briana/../../etc/passwd");
  expectResolved("/etc/passwd", "/../etc/passwd");
  expectResolved("/etc/passwd", "//etc//passwd");
  expectResolved("/etc", "/./etc/.");
  expectResolved("/etc", "/etc/");
  expectResolved("/", "/..");
  expectResolved("/", "//");
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

NANO_OS_KERNEL_TEST(fs_paths, an_executive_full_path_is_used_as_given) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_EXECUTIVE);
  requestFullPath();
  expectUnchanged("/system/bin/login/main.overlay");
  expectUnchanged("/secure/shadow");
  expectUnchanged("/system");
  expectUnchanged("/userspace/etc/passwd");
}

NANO_OS_KERNEL_TEST(fs_paths, a_full_path_is_not_tied_to_any_directory_name) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_EXECUTIVE);
  requestFullPath();
  expectUnchanged("/vendor/firmware/x");
}

NANO_OS_KERNEL_TEST(fs_paths, a_kernel_full_path_is_used_as_given) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_KERNEL);
  requestFullPath();
  expectUnchanged("/system/data/config.sys");
  expectUnchanged("/secure/shadow");
}

NANO_OS_KERNEL_TEST(fs_paths, a_full_path_is_collapsed_into_place) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_EXECUTIVE);
  requestFullPath();
  expectReal("/system/bin/login", "/usr/../system/bin//login");
  expectReal("/secure", "/secure/");
}

NANO_OS_KERNEL_TEST(fs_paths, a_full_path_ignores_the_callers_pwd) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_EXECUTIVE);
  requestFullPath();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWD", "/tmp", 1));
  expectReal("/secure/shadow", "secure/shadow");
}

NANO_OS_KERNEL_TEST(fs_paths, a_user_api_path_from_an_executive_stays_in_userspace) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_EXECUTIVE);
  expectReal("/userspace/secure/shadow", "/secure/shadow");
  expectReal("/userspace/system/bin/login", "/system/bin/login");
}

NANO_OS_KERNEL_TEST(fs_paths, a_kernel_user_api_path_stays_in_userspace) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_KERNEL);
  expectReal("/userspace/etc/hostname", "/etc/hostname");
  expectReal("/userspace/system/data/config.sys", "/system/data/config.sys");
}

NANO_OS_KERNEL_TEST(fs_paths, a_forged_full_path_is_ignored_below_executive) {
  pathFixture();
  requestFullPath();
  runAs(PRIVILEGE_LEVEL_SUPERVISOR);
  expectReal("/userspace/secure/shadow", "/secure/shadow");
  runAs(PRIVILEGE_LEVEL_USER);
  expectReal("/userspace/system/bin/login", "/system/bin/login");
  expectReal("/userspace/system/x", "/../system/x");
}

NANO_OS_KERNEL_TEST(fs_paths, an_executive_process_uses_userspace_otherwise) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_EXECUTIVE);
  expectReal("/userspace/etc/passwd", "/etc/passwd");
  expectReal("/userspace/userspace/x", "/userspace/x");
  expectReal("/userspace/usr/bin", "/system/../usr/bin");
  expectReal("/userspace", "/");
}

NANO_OS_KERNEL_TEST(fs_paths, a_supervisor_cannot_name_the_system_tree) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_SUPERVISOR);
  expectReal("/userspace/system/bin/login", "/system/bin/login");
  expectReal("/userspace/secure/shadow", "/secure/shadow");
  expectReal("/userspace/system", "/system");
  expectReal("/userspace/secure/shadow", "/etc/../../secure/shadow");
}

NANO_OS_KERNEL_TEST(fs_paths, a_user_cannot_name_the_system_tree) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_USER);
  expectReal("/userspace/secure/shadow", "/secure/shadow");
  expectReal("/userspace/system/x", "/../../system/x");
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("PWD", "/tmp", 1));
  expectReal("/userspace/secure/shadow", "../secure/shadow");
}

NANO_OS_KERNEL_TEST(fs_paths, a_user_cannot_reach_the_real_userspace_parent) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_USER);
  expectReal("/userspace/userspace/etc", "/userspace/etc");
  expectReal("/userspace", "/userspace/..");
}

NANO_OS_KERNEL_TEST(fs_paths, an_unknown_sender_is_not_trusted) {
  pathFixture();
  runAs(PRIVILEGE_LEVEL_KERNEL);
  requestFullPath();
  pathState.numProcesses = 0;
  expectReal("/userspace/secure/shadow", "/secure/shadow");
}

NANO_OS_KERNEL_TEST(fs_paths, a_capability_uses_the_callers_name) {
  size_t length = 0;
  NANO_OS_ASSERT_STR_EQ("/etc/x",
    filesystemCallerPath("/userspace/etc/x", false, &length));
  NANO_OS_ASSERT_EQ_INT(6, (int) length);
  NANO_OS_ASSERT_STR_EQ("/system/x",
    filesystemCallerPath("/system/x", true, &length));
  NANO_OS_ASSERT_EQ_INT(9, (int) length);

  const char *root = filesystemCallerPath("/userspace", false, &length);
  NANO_OS_ASSERT_EQ_INT(1, (int) length);
  NANO_OS_ASSERT_EQ_INT('/', root[0]);
}
