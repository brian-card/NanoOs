///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_user_api.c
///
/// @brief             Tests that the user API's entry points handle NULL
///                    pointers.
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

#include <stdarg.h>
#include <string.h>

#include "kernel/Console.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "user/NanoOsApi.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsStdio.h"
#include "user/NanoOsSpawn.h"

// The kernel's headers rename these, which would rename the table's members.
#undef fread
#undef fwrite
#undef remove
#undef stderr
#undef stdin
#undef stdout
#undef vfprintf
#undef vsnprintf

/// @def USER_API
///
/// @brief The table a user process calls through, so that a table entry that
/// bypasses its check fails a test.
#define USER_API (nanoOsApi.userApi)

/// @fn static int callVsnprintf(char *buffer, size_t size,
///   const char *format, ...)
///
/// @brief Call the user API's vsnprintf with variadic arguments.
///
/// @return Returns what vsnprintf returns.
static int callVsnprintf(char *buffer, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int returnValue = USER_API->vsnprintf(buffer, size, format, args);
  va_end(args);
  return returnValue;
}

/// @fn static int callVfprintf(FILE *stream, const char *format, ...)
///
/// @brief Call the user API's vfprintf with variadic arguments.
///
/// @return Returns what vfprintf returns.
static int callVfprintf(FILE *stream, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int returnValue = USER_API->vfprintf(stream, format, args);
  va_end(args);
  return returnValue;
}

NANO_OS_KERNEL_TEST(user_api, copies_with_a_null_pointer_do_nothing) {
  char buffer[4] = "abc";
  NANO_OS_ASSERT_EQ_PTR(buffer, USER_API->memcpy(buffer, NULL, 2));
  NANO_OS_ASSERT_NULL(USER_API->memcpy(NULL, buffer, 2));
  NANO_OS_ASSERT_EQ_PTR(buffer, USER_API->memmove(buffer, NULL, 2));
  NANO_OS_ASSERT_NULL(USER_API->memmove(NULL, buffer, 2));
  NANO_OS_ASSERT_EQ_PTR(buffer, USER_API->strcpy(buffer, NULL));
  NANO_OS_ASSERT_NULL(USER_API->strcpy(NULL, buffer));
  NANO_OS_ASSERT_EQ_PTR(buffer, USER_API->strncpy(buffer, NULL, 2));
  NANO_OS_ASSERT_NULL(USER_API->strncpy(NULL, buffer, 2));
  NANO_OS_ASSERT_EQ_PTR(buffer, USER_API->strcat(buffer, NULL));
  NANO_OS_ASSERT_NULL(USER_API->strcat(NULL, buffer));
  NANO_OS_ASSERT_EQ_PTR(buffer, USER_API->strncat(buffer, NULL, 2));
  NANO_OS_ASSERT_NULL(USER_API->strncat(NULL, buffer, 2));
  NANO_OS_ASSERT_NULL(USER_API->memset(NULL, 'x', 2));
  NANO_OS_ASSERT_STR_EQ("abc", buffer);
}

NANO_OS_KERNEL_TEST(user_api, a_null_pointer_sorts_before_any_other) {
  NANO_OS_ASSERT_EQ_INT(0, USER_API->strcmp(NULL, NULL));
  NANO_OS_ASSERT_EQ_INT(-1, USER_API->strcmp(NULL, "a"));
  NANO_OS_ASSERT_EQ_INT(1, USER_API->strcmp("a", NULL));
  NANO_OS_ASSERT_EQ_INT(0, USER_API->strncmp(NULL, NULL, 1));
  NANO_OS_ASSERT_EQ_INT(-1, USER_API->strncmp(NULL, "a", 1));
  NANO_OS_ASSERT_EQ_INT(1, USER_API->strncmp("a", NULL, 1));
  NANO_OS_ASSERT_EQ_INT(0, USER_API->memcmp(NULL, NULL, 1));
  NANO_OS_ASSERT_EQ_INT(-1, USER_API->memcmp(NULL, "a", 1));
  NANO_OS_ASSERT_EQ_INT(1, USER_API->memcmp("a", NULL, 1));
}

NANO_OS_KERNEL_TEST(user_api, searches_of_a_null_pointer_find_nothing) {
  NANO_OS_ASSERT_NULL(USER_API->strstr(NULL, "a"));
  NANO_OS_ASSERT_NULL(USER_API->strstr("a", NULL));
  NANO_OS_ASSERT_NULL(USER_API->strchr(NULL, 'a'));
  NANO_OS_ASSERT_NULL(USER_API->strrchr(NULL, 'a'));
  NANO_OS_ASSERT_EQ_INT(0, (int) USER_API->strspn(NULL, "a"));
  NANO_OS_ASSERT_EQ_INT(0, (int) USER_API->strspn("a", NULL));
  NANO_OS_ASSERT_EQ_INT(0, (int) USER_API->strcspn(NULL, "a"));
  NANO_OS_ASSERT_EQ_INT(0, (int) USER_API->strcspn("a", NULL));
  NANO_OS_ASSERT_EQ_INT(0, (int) USER_API->strlen(NULL));
}

NANO_OS_KERNEL_TEST(user_api, the_table_still_reaches_the_real_routines) {
  char buffer[8] = "";
  USER_API->strcpy(buffer, "ab");
  USER_API->strcat(buffer, "cd");
  NANO_OS_ASSERT_STR_EQ("abcd", buffer);
  NANO_OS_ASSERT_EQ_INT(4, (int) USER_API->strlen(buffer));
  NANO_OS_ASSERT_EQ_INT(0, USER_API->strcmp(buffer, "abcd"));
  NANO_OS_ASSERT_EQ_PTR(&buffer[2], USER_API->strchr(buffer, 'c'));
}

NANO_OS_KERNEL_TEST(user_api, vsnprintf_with_no_buffer_only_measures) {
  NANO_OS_ASSERT_EQ_INT(3, callVsnprintf(NULL, 16, "abc"));
}

NANO_OS_KERNEL_TEST(user_api, vfprintf_refuses_a_null_stream_or_format) {
  NANO_OS_ASSERT_EQ_INT(-1, callVfprintf(NULL, "abc"));
  NANO_OS_ASSERT_EQ_INT(-1, callVfprintf(USER_API->stdout, NULL));
}

NANO_OS_KERNEL_TEST(user_api, console_io_with_a_null_buffer_transfers_nothing) {
  NANO_OS_ASSERT_EQ_INT(0,
    (int) USER_API->fwrite(NULL, 1, 4, USER_API->stdout));
  NANO_OS_ASSERT_EQ_INT(0,
    (int) USER_API->fwrite(NULL, 1, 4, USER_API->stderr));
}

NANO_OS_KERNEL_TEST(user_api, reading_input_into_a_null_buffer_reads_nothing) {
  static ConsoleBuffer pending;
  strcpy(pending.buffer, "abc");
  pending.rawInput = true;
  setProcessStorage(FGETS_CONSOLE_BUFFER_KEY, &pending);

  NANO_OS_ASSERT_EQ_INT(0,
    (int) USER_API->fread(NULL, 1, 4, USER_API->stdin));
  NANO_OS_ASSERT_STR_EQ("abc", pending.buffer);
  setProcessStorage(FGETS_CONSOLE_BUFFER_KEY, NULL);
}

NANO_OS_KERNEL_TEST(user_api, a_console_write_of_no_elements_writes_nothing) {
  NANO_OS_ASSERT_EQ_INT(0,
    (int) USER_API->fwrite("abc", 0, 1, USER_API->stdout));
  NANO_OS_ASSERT_EQ_INT(0,
    (int) USER_API->fwrite("abc", 1, 0, USER_API->stdout));
}

NANO_OS_KERNEL_TEST(user_api, removing_a_null_path_fails) {
  errno = 0;
  NANO_OS_ASSERT_EQ_INT(-1, USER_API->remove(NULL));
  NANO_OS_ASSERT_EQ_INT(ENOENT, errno);
}

NANO_OS_KERNEL_TEST(user_api, ttyname_r_refuses_a_null_buffer) {
  NANO_OS_ASSERT_EQ_INT(EINVAL, USER_API->ttyname_r(0, NULL, 32));
}

NANO_OS_KERNEL_TEST(user_api, pipe_refuses_a_null_array) {
  ProcessDescriptor *self = getRunningProcess();
  uint8_t numFileDescriptors = self->numFileDescriptors;
  errno = 0;
  NANO_OS_ASSERT_EQ_INT(-1, USER_API->pipe(NULL));
  NANO_OS_ASSERT_EQ_INT(EFAULT, errno);
  NANO_OS_ASSERT_EQ_INT(numFileDescriptors, self->numFileDescriptors);
}

NANO_OS_KERNEL_TEST(user_api, localtime_r_refuses_null_pointers) {
  time_t now = 0;
  struct tm result;
  NANO_OS_ASSERT_NULL(USER_API->nanoOsLocaltime_r(NULL, &result));
  NANO_OS_ASSERT_NULL(USER_API->nanoOsLocaltime_r(&now, NULL));
}

NANO_OS_KERNEL_TEST(user_api, nanosleep_refuses_a_null_duration) {
  errno = 0;
  NANO_OS_ASSERT_EQ_INT(-1, USER_API->nanoOsNanosleep(NULL, NULL));
  NANO_OS_ASSERT_EQ_INT(EFAULT, errno);
}

NANO_OS_KERNEL_TEST(user_api, spawn_file_actions_refuse_a_null_pointer) {
  NANO_OS_ASSERT_EQ_INT(EINVAL,
    USER_API->posix_spawn_file_actions_init(NULL));
  NANO_OS_ASSERT_EQ_INT(EINVAL,
    USER_API->posix_spawn_file_actions_adddup2(NULL, 0, 1));
}

/// @var BAD_FDS
///
/// @brief File descriptor numbers that are out of range for any process.
static const int BAD_FDS[] = {-1, -1000000, 1000000};

/// @def NUM_BAD_FDS
///
/// @brief The number of elements in BAD_FDS.
#define NUM_BAD_FDS ((int) (sizeof(BAD_FDS) / sizeof(BAD_FDS[0])))

/// @fn static int openPipeAndCloseOneEnd(int *openFd)
///
/// @brief Make a pipe and close its write end, for a closed descriptor number
/// inside the process's table.
///
/// @param openFd Where to store the read end, which stays open.
///
/// @return Returns the closed descriptor number.
static int openPipeAndCloseOneEnd(int *openFd) {
  int fds[2] = {-1, -1};
  NANO_OS_ASSERT_EQ_INT(0, USER_API->pipe(fds));
  NANO_OS_ASSERT_EQ_INT(0, USER_API->close(fds[1]));
  *openFd = fds[0];
  return fds[1];
}

/// @fn static void assertEbadf(int returnValue)
///
/// @brief Assert that a call failed with EBADF.
///
/// @param returnValue What the call returned.
///
/// @return This function returns no value.
static void assertEbadf(int returnValue) {
  NANO_OS_ASSERT_EQ_INT(-1, returnValue);
  NANO_OS_ASSERT_EQ_INT(EBADF, errno);
  errno = 0;
}

NANO_OS_KERNEL_TEST(user_api, close_refuses_a_bad_descriptor) {
  for (int ii = 0; ii < NUM_BAD_FDS; ii++) {
    assertEbadf(USER_API->close(BAD_FDS[ii]));
  }

  int openFd = -1;
  int closedFd = openPipeAndCloseOneEnd(&openFd);
  assertEbadf(USER_API->close(closedFd));
}

NANO_OS_KERNEL_TEST(user_api, close_refuses_the_descriptor_just_past_the_end) {
  ProcessDescriptor *self = getRunningProcess();
  int fds[2] = {-1, -1};
  NANO_OS_ASSERT_EQ_INT(0, USER_API->pipe(fds));
  FileDescriptor *last = self->fileDescriptors[self->numFileDescriptors - 1];
  NANO_OS_ASSERT_NOT_NULL(last);
  int refCount = last->refCount;

  self->numFileDescriptors--;
  assertEbadf(USER_API->close(self->numFileDescriptors));
  self->numFileDescriptors++;

  NANO_OS_ASSERT_EQ_PTR(last,
    self->fileDescriptors[self->numFileDescriptors - 1]);
  NANO_OS_ASSERT_EQ_INT(refCount, last->refCount);
}

NANO_OS_KERNEL_TEST(user_api, dup_refuses_a_bad_descriptor) {
  ProcessDescriptor *self = getRunningProcess();
  for (int ii = 0; ii < NUM_BAD_FDS; ii++) {
    assertEbadf(USER_API->dup(BAD_FDS[ii]));
  }

  int openFd = -1;
  int closedFd = openPipeAndCloseOneEnd(&openFd);
  uint8_t numFileDescriptors = self->numFileDescriptors;
  assertEbadf(USER_API->dup(closedFd));
  NANO_OS_ASSERT_EQ_INT(numFileDescriptors, self->numFileDescriptors);
}

NANO_OS_KERNEL_TEST(user_api, dup2_refuses_a_bad_descriptor) {
  int openFd = -1;
  int closedFd = openPipeAndCloseOneEnd(&openFd);
  for (int ii = 0; ii < NUM_BAD_FDS; ii++) {
    assertEbadf(USER_API->dup2(BAD_FDS[ii], openFd));
    assertEbadf(USER_API->dup2(openFd, BAD_FDS[ii]));
  }
  assertEbadf(USER_API->dup2(closedFd, openFd));
  NANO_OS_ASSERT_NOT_NULL(getRunningProcess()->fileDescriptors[openFd]);
}

NANO_OS_KERNEL_TEST(user_api, dup2_onto_a_closed_descriptor_opens_it) {
  ProcessDescriptor *self = getRunningProcess();
  int openFd = -1;
  int closedFd = openPipeAndCloseOneEnd(&openFd);
  FileDescriptor *open = self->fileDescriptors[openFd];
  int refCount = open->refCount;

  NANO_OS_ASSERT_EQ_INT(closedFd, USER_API->dup2(openFd, closedFd));
  NANO_OS_ASSERT_EQ_PTR(open, self->fileDescriptors[closedFd]);
  NANO_OS_ASSERT_EQ_INT(refCount + 1, open->refCount);
}

NANO_OS_KERNEL_TEST(user_api, dup2_onto_itself_changes_nothing) {
  ProcessDescriptor *self = getRunningProcess();
  int fds[2] = {-1, -1};
  NANO_OS_ASSERT_EQ_INT(0, USER_API->pipe(fds));
  FileDescriptor *open = self->fileDescriptors[fds[0]];
  FileDescriptor *otherEnd = self->fileDescriptors[fds[1]];
  int refCount = open->refCount;

  NANO_OS_ASSERT_EQ_INT(fds[0], USER_API->dup2(fds[0], fds[0]));
  NANO_OS_ASSERT_EQ_PTR(open, self->fileDescriptors[fds[0]]);
  NANO_OS_ASSERT_EQ_INT(refCount, open->refCount);
  NANO_OS_ASSERT_EQ_PTR(open, otherEnd->pipeEnd);
}

NANO_OS_KERNEL_TEST(user_api, spawn_dup2_refuses_a_bad_descriptor) {
  posix_spawn_file_actions_t fileActions;
  NANO_OS_ASSERT_EQ_INT(0,
    USER_API->posix_spawn_file_actions_init(&fileActions));
  int openFd = -1;
  int closedFd = openPipeAndCloseOneEnd(&openFd);
  FileDescriptor *open = getRunningProcess()->fileDescriptors[openFd];
  int refCount = open->refCount;

  for (int ii = 0; ii < NUM_BAD_FDS; ii++) {
    NANO_OS_ASSERT_EQ_INT(EBADF, USER_API->posix_spawn_file_actions_adddup2(
      &fileActions, BAD_FDS[ii], 1));
  }
  NANO_OS_ASSERT_EQ_INT(EBADF, USER_API->posix_spawn_file_actions_adddup2(
    &fileActions, closedFd, 1));
  NANO_OS_ASSERT_EQ_INT(EBADF, USER_API->posix_spawn_file_actions_adddup2(
    &fileActions, openFd, -1));
  NANO_OS_ASSERT_EQ_INT(0, fileActions.numDup2);
  NANO_OS_ASSERT_EQ_INT(refCount, open->refCount);
}

/// @fn static void* exitingChild(void *args)
///
/// @brief Stand in for a spawned program that exits at once.
///
/// @param args The ExecArgs of the spawn, which aren't used.
///
/// @return This function returns NULL.
static void* exitingChild(void *args) {
  (void) args;
  return NULL;
}

/// @fn static void useExitingChild(void)
///
/// @brief Make spawned processes run exitingChild instead of an overlay.
///
/// @return This function returns no value.
static void useExitingChild(void) {
  halMockSetExecCommand(exitingChild);
}

/// @fn static void spawnAndAssertDescriptorSurvives(int targetFd)
///
/// @brief Spawn a child with a file action that targets a descriptor number
/// the child doesn't have, and assert that the parent's descriptor survives.
///
/// @param targetFd The out-of-range descriptor number in the child.
///
/// @return This function returns no value.
static void spawnAndAssertDescriptorSurvives(int targetFd) {
  int openFd = -1;
  (void) openPipeAndCloseOneEnd(&openFd);
  FileDescriptor *open = getRunningProcess()->fileDescriptors[openFd];
  int refCount = open->refCount;

  posix_spawn_file_actions_t fileActions;
  fileActions.numDup2 = 1;
  fileActions.dup2[0].fd = targetFd;
  fileActions.dup2[0].dup = open;
  open->refCount++;

  pid_t pid = 0;
  char *argv[] = { (char*) "child", NULL };
  NANO_OS_ASSERT_EQ_INT(0, USER_API->posix_spawn(&pid, "/child", &fileActions,
    NULL, argv, NULL));
  NANO_OS_ASSERT_EQ_INT(pid, USER_API->waitpid(pid, NULL, 0));

  NANO_OS_ASSERT_EQ_PTR(open, getRunningProcess()->fileDescriptors[openFd]);
  NANO_OS_ASSERT_EQ_INT(refCount, open->refCount);
}

NANO_OS_KERNEL_TEST_WITH(user_api,
  a_spawn_past_the_childs_table_keeps_the_descriptor, useExitingChild
) {
  spawnAndAssertDescriptorSurvives(99);
}

NANO_OS_KERNEL_TEST_WITH(user_api,
  a_spawn_to_a_negative_fd_keeps_the_descriptor, useExitingChild
) {
  spawnAndAssertDescriptorSurvives(-1);
}
