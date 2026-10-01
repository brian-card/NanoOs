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
#include "user/NanoOsStdio.h"

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

NANO_OS_KERNEL_TEST(env, unsetenv_rejects_invalid_names) {
  NANO_OS_ASSERT_EQ_INT(-1, nanoOsUnsetenv(NULL));
  NANO_OS_ASSERT_EQ_INT(EINVAL, errno);
  NANO_OS_ASSERT_EQ_INT(-1, nanoOsUnsetenv(""));
  NANO_OS_ASSERT_EQ_INT(EINVAL, errno);
  NANO_OS_ASSERT_EQ_INT(-1, nanoOsUnsetenv("HAS=EQUALS"));
  NANO_OS_ASSERT_EQ_INT(EINVAL, errno);
}

NANO_OS_KERNEL_TEST(env, unsetenv_ignores_a_missing_variable) {
  int count = envCount();
  NANO_OS_ASSERT_EQ_INT(0, nanoOsUnsetenv("NANOOS_TEST_NOT_THERE"));
  NANO_OS_ASSERT_EQ_INT(count, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, unsetenv_removes_a_variable) {
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_FIRST", "one", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_MIDDLE", "a value", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_LAST", "three", 1));
  int count = envCount();

  NANO_OS_ASSERT_EQ_INT(0, nanoOsUnsetenv("NANOOS_TEST_MIDDLE"));
  NANO_OS_ASSERT_NULL(envLookup("NANOOS_TEST_MIDDLE"));
  NANO_OS_ASSERT_STR_EQ("one", envLookup("NANOOS_TEST_FIRST"));
  NANO_OS_ASSERT_STR_EQ("three", envLookup("NANOOS_TEST_LAST"));
  NANO_OS_ASSERT_EQ_INT(count - 1, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());

  NANO_OS_ASSERT_EQ_INT(0, nanoOsUnsetenv("NANOOS_TEST_LAST"));
  NANO_OS_ASSERT_NULL(envLookup("NANOOS_TEST_LAST"));
  NANO_OS_ASSERT_STR_EQ("one", envLookup("NANOOS_TEST_FIRST"));
  NANO_OS_ASSERT_EQ_INT(count - 2, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, unsetenv_can_be_set_again_afterward) {
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_CYCLE", "before", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsUnsetenv("NANOOS_TEST_CYCLE"));
  NANO_OS_ASSERT_NULL(envLookup("NANOOS_TEST_CYCLE"));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_CYCLE", "after", 1));
  NANO_OS_ASSERT_STR_EQ("after", envLookup("NANOOS_TEST_CYCLE"));
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, unsetenv_draining_the_environment_yields_null) {
  while (envCount() > 0) {
    char name[64];
    char *entry = getRunningProcess()->envp[0];
    char *equalsAt = strchr(entry, '=');
    NANO_OS_ASSERT_NOT_NULL(equalsAt);
    size_t nameLen = (size_t) (equalsAt - entry);
    NANO_OS_ASSERT_TRUE(nameLen < sizeof(name));
    memcpy(name, entry, nameLen);
    name[nameLen] = '\0';

    NANO_OS_ASSERT_EQ_INT(0, nanoOsUnsetenv(name));
    NANO_OS_ASSERT_TRUE(envLayoutIsValid());
  }

  NANO_OS_ASSERT_NULL(getRunningProcess()->envp);
  NANO_OS_ASSERT_EQ_INT(0, nanoOsUnsetenv("NANOOS_TEST_ANYTHING"));

  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_REBUILT", "value", 1));
  NANO_OS_ASSERT_STR_EQ("value", envLookup("NANOOS_TEST_REBUILT"));
  NANO_OS_ASSERT_EQ_INT(1, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, setenv_accepts_a_value_from_another_variable) {
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_SRC", "/a/source/path", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_DST", "x", 1));
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_END", "tail", 1));
  int count = envCount();

  // The value aliases the block that setenv is about to grow and memmove.
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSetenv("NANOOS_TEST_DST", envLookup("NANOOS_TEST_SRC"), 1));
  NANO_OS_ASSERT_STR_EQ("/a/source/path", envLookup("NANOOS_TEST_DST"));
  NANO_OS_ASSERT_STR_EQ("/a/source/path", envLookup("NANOOS_TEST_SRC"));
  NANO_OS_ASSERT_STR_EQ("tail", envLookup("NANOOS_TEST_END"));
  NANO_OS_ASSERT_EQ_INT(count, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());

  // Same thing in the other direction, where the source sits after the target.
  NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv("NANOOS_TEST_SRC", "short", 1));
  NANO_OS_ASSERT_EQ_INT(0,
    nanoOsSetenv("NANOOS_TEST_SRC", envLookup("NANOOS_TEST_END"), 1));
  NANO_OS_ASSERT_STR_EQ("tail", envLookup("NANOOS_TEST_SRC"));
  NANO_OS_ASSERT_STR_EQ("tail", envLookup("NANOOS_TEST_END"));
  NANO_OS_ASSERT_EQ_INT(count, envCount());
  NANO_OS_ASSERT_TRUE(envLayoutIsValid());
}

NANO_OS_KERNEL_TEST(env, repeated_setenv_preserves_every_earlier_variable) {
  // Each setenv grows the envp block with the scheduler's own realloc, so this
  // covers the schedRealloc path rather than the user-space one.
  char name[16];
  char value[16];
  for (int ii = 0; ii < 12; ii++) {
    sprintf(name, "NANOOS_GROW_%d", ii);
    sprintf(value, "value_%d", ii);
    NANO_OS_ASSERT_EQ_INT(0, nanoOsSetenv(name, value, 1));
    NANO_OS_ASSERT_TRUE(envLayoutIsValid());

    for (int jj = 0; jj <= ii; jj++) {
      sprintf(name, "NANOOS_GROW_%d", jj);
      sprintf(value, "value_%d", jj);
      const char *found = envLookup(name);
      NANO_OS_ASSERT_NOT_NULL(found);
      NANO_OS_ASSERT_STR_EQ(value, found);
    }
  }
}
