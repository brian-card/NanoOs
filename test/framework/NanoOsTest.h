///////////////////////////////////////////////////////////////////////////////
///
/// @file              NanoOsTest.h
///
/// @brief             Minimal xUnit-style test framework for the NanoOs kernel
///                    test harness.  Emits TAP (Test Anything Protocol) so the
///                    output can be consumed by prove(1), CI, or read directly.
///
/// Two kinds of tests:
///
///   NANO_OS_TEST(suite, name)         - a plain test.  The body runs directly
///                                       in the process that called main().
///
///   NANO_OS_KERNEL_TEST(suite, name)  - a kernel test.  The body runs inside a
///                                       freshly booted NanoOs kernel, as a
///                                       real process, via the registered
///                                       kernel runner (see KernelTestHarness).
///                                       Each kernel test gets its own boot and
///                                       teardown, so suites are isolated.
///
/// Assertions (all fatal - they abort the current test body via longjmp and
/// mark it failed, but the run continues with the next test):
///
///   NANO_OS_ASSERT_TRUE(cond)
///   NANO_OS_ASSERT_FALSE(cond)
///   NANO_OS_ASSERT_EQ_INT(expected, actual)
///   NANO_OS_ASSERT_NE_INT(a, b)
///   NANO_OS_ASSERT_EQ_PTR(expected, actual)
///   NANO_OS_ASSERT_NOT_NULL(ptr)
///   NANO_OS_ASSERT_NULL(ptr)
///   NANO_OS_ASSERT_STR_EQ(expected, actual)
///   NANO_OS_FAIL(msg)
///
///////////////////////////////////////////////////////////////////////////////

#ifndef NANO_OS_TEST_H
#define NANO_OS_TEST_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @typedef NanoOsTestFn
///
/// @brief Signature of a test body.
typedef void (*NanoOsTestFn)(void);

/// @typedef NanoOsTestKernelRunner
///
/// @brief Signature of the function that runs a kernel test body inside a
/// booted kernel.  Returns 0 if the kernel booted and tore down cleanly,
/// non-zero if the harness itself failed (which fails the test regardless of
/// what the body asserted).
typedef int (*NanoOsTestKernelRunner)(NanoOsTestFn body);

/// @struct NanoOsTestCase
///
/// @brief One registered test.
typedef struct NanoOsTestCase {
  const char           *suite;
  const char           *name;
  NanoOsTestFn           fn;
  bool                   isKernelTest;
  struct NanoOsTestCase *next;
} NanoOsTestCase;

/// @fn void nanoOsTestRegister(NanoOsTestCase *testCase)
///
/// @brief Add a test to the global registry.  Called automatically by the
/// NANO_OS_TEST / NANO_OS_KERNEL_TEST macros via a constructor.
void nanoOsTestRegister(NanoOsTestCase *testCase);

/// @fn void nanoOsTestSetKernelRunner(NanoOsTestKernelRunner runner)
///
/// @brief Install the runner used to execute NANO_OS_KERNEL_TEST bodies.  If
/// no runner is installed, kernel tests are reported as skipped.
void nanoOsTestSetKernelRunner(NanoOsTestKernelRunner runner);

/// @fn int nanoOsTestRunAll(const char *filter)
///
/// @brief Run every registered test whose "suite/name" contains the substring
/// filter (filter may be NULL to run everything).  Emits a TAP stream on
/// stdout.
///
/// @return The number of failed tests (0 == all good).
int nanoOsTestRunAll(const char *filter);

// --- internals used by the macros and assertions -------------------------

/// @fn jmp_buf* nanoOsTestAbortBuffer(void)
///
/// @brief The setjmp buffer the current test body longjmps back to on a failed
/// assertion.  Only valid while a test body is executing.
jmp_buf* nanoOsTestAbortBuffer(void);

/// @fn void nanoOsTestRecordFailure(const char *file, int line, const char *fmt, ...)
///
/// @brief Record a failure diagnostic for the currently running test and abort
/// its body.
void nanoOsTestRecordFailure(const char *file, int line, const char *fmt, ...)
  __attribute__((format(printf, 3, 4), noreturn));

/// @fn void nanoOsTestForceFail(const char *fmt, ...)
///
/// @brief Mark the current test failed with a diagnostic, WITHOUT aborting via
/// longjmp.  For harness-level failures that happen outside a test body (e.g. a
/// kernel that would not boot).
void nanoOsTestForceFail(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));

// --- kernel-runner / cross-process result plumbing ----------------------
//
// A kernel test body runs in a forked child.  The child serializes its
// pass/fail + diagnostic with nanoOsTestChildEnd(); the parent resets state
// with nanoOsTestParentBegin() before the fork and folds the child's result
// back in with nanoOsTestParentEnd() after it.

/// @fn void nanoOsTestParentBegin(void)
void nanoOsTestParentBegin(void);

/// @fn void nanoOsTestChildEnd(int fd)
void nanoOsTestChildEnd(int fd);

/// @fn void nanoOsTestParentEnd(int fd)
void nanoOsTestParentEnd(int fd);

#define NANO_OS_TEST_ABORT() longjmp(*nanoOsTestAbortBuffer(), 1)

#define NANO_OS_TEST_REGISTER_(suiteId, nameId, kernelFlag)                  \
  static void suiteId##_##nameId##_body(void);                               \
  static NanoOsTestCase suiteId##_##nameId##_case = {                        \
    #suiteId, #nameId, suiteId##_##nameId##_body, kernelFlag, NULL           \
  };                                                                         \
  __attribute__((constructor))                                               \
  static void suiteId##_##nameId##_register(void) {                          \
    nanoOsTestRegister(&suiteId##_##nameId##_case);                          \
  }                                                                          \
  static void suiteId##_##nameId##_body(void)

#define NANO_OS_TEST(suite, name)        NANO_OS_TEST_REGISTER_(suite, name, false)
#define NANO_OS_KERNEL_TEST(suite, name) NANO_OS_TEST_REGISTER_(suite, name, true)

// --- assertions --------------------------------------------------------

#define NANO_OS_FAIL(msg) \
  nanoOsTestRecordFailure(__FILE__, __LINE__, "%s", (msg))

#define NANO_OS_ASSERT_TRUE(cond)                                            \
  do {                                                                      \
    if (!(cond)) {                                                          \
      nanoOsTestRecordFailure(__FILE__, __LINE__,                           \
        "expected true: %s", #cond);                                        \
    }                                                                      \
  } while (0)

#define NANO_OS_ASSERT_FALSE(cond)                                           \
  do {                                                                      \
    if ((cond)) {                                                           \
      nanoOsTestRecordFailure(__FILE__, __LINE__,                           \
        "expected false: %s", #cond);                                       \
    }                                                                      \
  } while (0)

#define NANO_OS_ASSERT_EQ_INT(expected, actual)                              \
  do {                                                                      \
    long long _e = (long long) (expected);                                  \
    long long _a = (long long) (actual);                                    \
    if (_e != _a) {                                                         \
      nanoOsTestRecordFailure(__FILE__, __LINE__,                           \
        "%s == %s : expected %lld, got %lld",                               \
        #expected, #actual, _e, _a);                                        \
    }                                                                      \
  } while (0)

#define NANO_OS_ASSERT_NE_INT(a, b)                                          \
  do {                                                                      \
    long long _a = (long long) (a);                                         \
    long long _b = (long long) (b);                                         \
    if (_a == _b) {                                                         \
      nanoOsTestRecordFailure(__FILE__, __LINE__,                           \
        "%s != %s : both are %lld", #a, #b, _a);                            \
    }                                                                      \
  } while (0)

#define NANO_OS_ASSERT_EQ_PTR(expected, actual)                              \
  do {                                                                      \
    const void *_e = (const void *) (expected);                             \
    const void *_a = (const void *) (actual);                               \
    if (_e != _a) {                                                         \
      nanoOsTestRecordFailure(__FILE__, __LINE__,                           \
        "%s == %s : expected %p, got %p", #expected, #actual, _e, _a);      \
    }                                                                      \
  } while (0)

#define NANO_OS_ASSERT_NOT_NULL(ptr)                                         \
  do {                                                                      \
    if ((const void *) (ptr) == NULL) {                                     \
      nanoOsTestRecordFailure(__FILE__, __LINE__,                           \
        "expected non-NULL: %s", #ptr);                                     \
    }                                                                      \
  } while (0)

#define NANO_OS_ASSERT_NULL(ptr)                                             \
  do {                                                                      \
    if ((const void *) (ptr) != NULL) {                                     \
      nanoOsTestRecordFailure(__FILE__, __LINE__,                           \
        "expected NULL: %s (= %p)", #ptr, (const void *) (ptr));            \
    }                                                                      \
  } while (0)

#define NANO_OS_ASSERT_STR_EQ(expected, actual)                              \
  do {                                                                      \
    const char *_e = (expected);                                            \
    const char *_a = (actual);                                              \
    if ((_e == NULL) || (_a == NULL) || (nanoOsTestStrcmp(_e, _a) != 0)) {  \
      nanoOsTestRecordFailure(__FILE__, __LINE__,                           \
        "%s == %s : expected \"%s\", got \"%s\"", #expected, #actual,       \
        _e ? _e : "(null)", _a ? _a : "(null)");                            \
    }                                                                      \
  } while (0)

/// @fn int nanoOsTestStrcmp(const char *a, const char *b)
///
/// @brief strcmp used by NANO_OS_ASSERT_STR_EQ.  Wrapped so the framework does
/// not have to pull in a NanoOs libc header that redefines str* symbols.
int nanoOsTestStrcmp(const char *a, const char *b);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NANO_OS_TEST_H
