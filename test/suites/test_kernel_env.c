///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_env.c
///
/// @brief             Kernel tests for setenv and the layout of a process's
///                    environment block.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsLibC.h"
#include "user/NanoOsErrno.h"

static char* envLookup(const char *name) {
  char **envp = getRunningProcess()->envp;
  if (envp == NULL) {
    return NULL;
  }

  size_t nameLen = strlen(name);
  for (int ii = 0; envp[ii] != NULL; ii++) {
    if ((strncmp(envp[ii], name, nameLen) == 0)
      && (envp[ii][nameLen] == '=')
    ) {
      return &envp[ii][nameLen + 1];
    }
  }

  return NULL;
}

static int envCount(void) {
  char **envp = getRunningProcess()->envp;
  int count = 0;
  if (envp != NULL) {
    for (; envp[count] != NULL; count++);
  }

  return count;
}

// Every string must sit immediately after the previous one, envp[0] must be the
// base of the allocation, and the array must start at the base padded out to a
// multiple of sizeof(uintptr_t).
static bool envLayoutIsValid(void) {
  char **envp = getRunningProcess()->envp;
  if (envp == NULL) {
    return true;
  }

  size_t stringsBytes = 0;
  for (int ii = 0; envp[ii] != NULL; ii++) {
    if (envp[ii] != &envp[0][stringsBytes]) {
      return false;
    }
    stringsBytes += strlen(envp[ii]) + 1;
  }

  size_t paddedBytes
    = (stringsBytes + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);

  return (((char*) envp) == &envp[0][paddedBytes]);
}

NANO_OS_KERNEL_TEST(env, setenv_rejects_invalid_names) {
  NANO_OS_ASSERT_EQ_INT(-1, nanoOsSetenv(NULL, "x", 1));
  NANO_OS_ASSERT_EQ_INT(EINVAL, errno);
  NANO_OS_ASSERT_EQ_INT(-1, nanoOsSetenv("", "x", 1));
  NANO_OS_ASSERT_EQ_INT(EINVAL, errno);
  NANO_OS_ASSERT_EQ_INT(-1, nanoOsSetenv("HAS=EQUALS", "x", 1));
  NANO_OS_ASSERT_EQ_INT(EINVAL, errno);
}

NANO_OS_KERNEL_TEST(env, setenv_adds_variables) {
  int before = envCount();

  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_A", "alpha", 1));
  NANO_OS_ASSERT_NOT_NULL(envLookup("NANOOS_TEST_A"));
  NANO_OS_ASSERT_STR_EQ("alpha", envLookup("NANOOS_TEST_A"));
  NANO_OS_ASSERT_EQ_INT(before + 1, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());

  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_B", "bravo", 1));
  NANO_OS_ASSERT_STR_EQ("alpha", envLookup("NANOOS_TEST_A"));
  NANO_OS_ASSERT_STR_EQ("bravo", envLookup("NANOOS_TEST_B"));
  NANO_OS_ASSERT_EQ_INT(before + 2, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());

  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_C", "", 1));
  NANO_OS_ASSERT_STR_EQ("", envLookup("NANOOS_TEST_C"));
  NANO_OS_ASSERT_EQ_INT(before + 3, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, setenv_honors_overwrite_flag) {
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_KEEP", "first", 1));
  int count = envCount();

  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_KEEP", "second", 0));
  NANO_OS_ASSERT_STR_EQ("first", envLookup("NANOOS_TEST_KEEP"));
  NANO_OS_ASSERT_EQ_INT(count, envCount());

  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_KEEP", "second", 1));
  NANO_OS_ASSERT_STR_EQ("second", envLookup("NANOOS_TEST_KEEP"));
  NANO_OS_ASSERT_EQ_INT(count, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, setenv_ignores_a_value_it_already_holds) {
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_SELF", "self", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_TAIL", "tail", 1));
  int count = envCount();

  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSetenv("NANOOS_TEST_SELF", envLookup("NANOOS_TEST_SELF"), 1));
  NANO_OS_ASSERT_STR_EQ("self", envLookup("NANOOS_TEST_SELF"));
  NANO_OS_ASSERT_STR_EQ("tail", envLookup("NANOOS_TEST_TAIL"));
  NANO_OS_ASSERT_EQ_INT(count, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, setenv_grows_a_value_in_place) {
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_ONE", "1", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_TWO", "2", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_THREE", "3", 1));
  int count = envCount();

  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSetenv("NANOOS_TEST_ONE", "a much longer value than before", 1));
  NANO_OS_ASSERT_STR_EQ("a much longer value than before",
    envLookup("NANOOS_TEST_ONE"));
  NANO_OS_ASSERT_STR_EQ("2", envLookup("NANOOS_TEST_TWO"));
  NANO_OS_ASSERT_STR_EQ("3", envLookup("NANOOS_TEST_THREE"));
  NANO_OS_ASSERT_EQ_INT(count, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, setenv_shrinks_a_value_in_place) {
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSetenv("NANOOS_TEST_BIG", "an initially very long value", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_AFTER", "after", 1));
  int count = envCount();

  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_BIG", "s", 1));
  NANO_OS_ASSERT_STR_EQ("s", envLookup("NANOOS_TEST_BIG"));
  NANO_OS_ASSERT_STR_EQ("after", envLookup("NANOOS_TEST_AFTER"));
  NANO_OS_ASSERT_EQ_INT(count, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, setenv_preserves_the_inherited_environment) {
  char **envp = getRunningProcess()->envp;
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
  if (envp == NULL) {
    return;
  }

  int before = envCount();
  char firstEntry[64];
  strncpy(firstEntry, envp[0], sizeof(firstEntry) - 1);
  firstEntry[sizeof(firstEntry) - 1] = '\0';

  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_NEW", "value", 1));
  NANO_OS_ASSERT_EQ_INT(before + 1, envCount());
  NANO_OS_ASSERT_STR_EQ(firstEntry, getRunningProcess()->envp[0]);
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}
