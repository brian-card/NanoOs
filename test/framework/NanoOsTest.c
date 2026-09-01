///////////////////////////////////////////////////////////////////////////////
///
/// @file              NanoOsTest.c
///
/// @brief             Implementation of the minimal xUnit-style test framework.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/// @var _head
///
/// @brief Head of the singly linked list of registered tests, in registration
/// order (constructors append, so this ends up in link order which is stable
/// per build).
static NanoOsTestCase *_head = NULL;
static NanoOsTestCase *_tail = NULL;

/// @var _kernelRunner
///
/// @brief Installed runner for NANO_OS_KERNEL_TEST bodies, or NULL.
static NanoOsTestKernelRunner _kernelRunner = NULL;

/// @var _abortBuffer
///
/// @brief longjmp target for the currently executing test body.
static jmp_buf _abortBuffer;

/// @var _currentFailed
///
/// @brief Whether the currently executing test has recorded a failure.
static bool _currentFailed = false;

/// @var _currentDiag
///
/// @brief Diagnostic text for the current test's first failure.
static char _currentDiag[512];

int nanoOsTestStrcmp(const char *a, const char *b) {
  return strcmp(a, b);
}

void nanoOsTestRegister(NanoOsTestCase *testCase) {
  testCase->next = NULL;
  if (_head == NULL) {
    _head = testCase;
    _tail = testCase;
  } else {
    _tail->next = testCase;
    _tail = testCase;
  }
}

void nanoOsTestSetKernelRunner(NanoOsTestKernelRunner runner) {
  _kernelRunner = runner;
}

jmp_buf* nanoOsTestAbortBuffer(void) {
  return &_abortBuffer;
}

void nanoOsTestRecordFailure(const char *file, int line, const char *fmt, ...) {
  const char *base = strrchr(file, '/');
  base = (base != NULL) ? (base + 1) : file;

  int prefix = snprintf(_currentDiag, sizeof(_currentDiag),
    "%s:%d: ", base, line);
  if ((prefix > 0) && ((size_t) prefix < sizeof(_currentDiag))) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(_currentDiag + prefix, sizeof(_currentDiag) - (size_t) prefix,
      fmt, args);
    va_end(args);
  }

  _currentFailed = true;
  NANO_OS_TEST_ABORT();
}

void nanoOsTestForceFail(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(_currentDiag, sizeof(_currentDiag), fmt, args);
  va_end(args);
  _currentFailed = true;
}

void nanoOsTestParentBegin(void) {
  _currentFailed = false;
  _currentDiag[0] = '\0';
}

void nanoOsTestChildEnd(int fd) {
  // Wire format: 1 status byte ('F' or '.') followed by the NUL-terminated
  // diagnostic string.
  unsigned char statusByte = _currentFailed ? (unsigned char) 'F'
                                            : (unsigned char) '.';
  if (write(fd, &statusByte, 1) != 1) {
    return;
  }
  size_t len = strnlen(_currentDiag, sizeof(_currentDiag) - 1) + 1;
  ssize_t written = write(fd, _currentDiag, len);
  (void) written;
}

void nanoOsTestParentEnd(int fd) {
  unsigned char statusByte = 0;
  ssize_t got = read(fd, &statusByte, 1);
  if (got != 1) {
    // Child produced nothing (crashed before reporting).  Leave the state as
    // set by nanoOsTestParentBegin; the harness return code will flag it.
    return;
  }
  _currentFailed = (statusByte == (unsigned char) 'F');

  size_t offset = 0;
  while (offset < sizeof(_currentDiag) - 1) {
    ssize_t n = read(fd, _currentDiag + offset, sizeof(_currentDiag) - 1 - offset);
    if (n <= 0) {
      break;
    }
    offset += (size_t) n;
    if (memchr(_currentDiag, '\0', offset) != NULL) {
      break;
    }
  }
  _currentDiag[sizeof(_currentDiag) - 1] = '\0';
}

/// @fn static void runOneBody(NanoOsTestFn body)
///
/// @brief Execute a test body under the abort buffer.  On return
/// _currentFailed / _currentDiag reflect the outcome.  Used directly for
/// plain tests, and as the body callback for the kernel runner.
static void runOneBody(NanoOsTestFn body) {
  if (setjmp(_abortBuffer) == 0) {
    body();
  }
}

/// @var _pendingBody
///
/// @brief Body handed to the kernel runner via runKernelBodyTrampoline.
static NanoOsTestFn _pendingBody = NULL;

/// @fn static void runKernelBodyTrampoline(void)
///
/// @brief Adapter passed to the kernel runner: runs _pendingBody under the
/// abort buffer from inside the booted kernel process.
static void runKernelBodyTrampoline(void) {
  runOneBody(_pendingBody);
}

int nanoOsTestRunAll(const char *filter) {
  int total = 0;
  for (NanoOsTestCase *tc = _head; tc != NULL; tc = tc->next) {
    total++;
  }
  printf("TAP version 13\n");
  printf("1..%d\n", total);

  int index = 0;
  int failures = 0;
  char label[256];

  for (NanoOsTestCase *tc = _head; tc != NULL; tc = tc->next) {
    index++;
    snprintf(label, sizeof(label), "%s/%s", tc->suite, tc->name);

    if ((filter != NULL) && (strstr(label, filter) == NULL)) {
      printf("ok %d - %s # SKIP filtered out\n", index, label);
      continue;
    }

    nanoOsTestParentBegin();

    if (tc->isKernelTest) {
      if (_kernelRunner == NULL) {
        printf("ok %d - %s # SKIP no kernel runner installed\n",
          index, label);
        continue;
      }
      _pendingBody = tc->fn;
      // The runner forks; runKernelBodyTrampoline executes in the child and
      // the runner folds the child's result back into _currentFailed /
      // _currentDiag before returning here.  A negative status means the
      // runner declined to run (e.g. incompatible build) - report SKIP.
      int harnessStatus = _kernelRunner(runKernelBodyTrampoline);
      _pendingBody = NULL;
      if (harnessStatus < 0) {
        printf("ok %d - %s # SKIP kernel harness unavailable\n", index, label);
        continue;
      }
      if ((harnessStatus != 0) && (!_currentFailed)) {
        _currentFailed = true;
        snprintf(_currentDiag, sizeof(_currentDiag),
          "kernel harness returned status %d (child crashed or failed to "
          "boot)", harnessStatus);
      }
    } else {
      runOneBody(tc->fn);
    }

    if (_currentFailed) {
      failures++;
      printf("not ok %d - %s\n", index, label);
      printf("# %s\n", _currentDiag[0] ? _currentDiag : "(no diagnostic)");
    } else {
      printf("ok %d - %s\n", index, label);
    }
    fflush(stdout);
  }

  printf("# %d test(s), %d failure(s)\n", index, failures);
  return failures;
}
