///////////////////////////////////////////////////////////////////////////////
///
/// @file              KernelTestHarness.c
///
/// @brief             Boot-a-kernel-per-test runner.  See KernelTestHarness.h.
///
/// Each kernel test runs in its own forked child.  The child boots a kernel
/// under the mock HAL, the test body runs as the "driver" process (the slot a
/// login shell would occupy), the result is streamed to the parent over a
/// pipe, and the child _exit()s.  A fresh address space per test means there
/// is no teardown ordering to get right and a test that calls shutdown() is
/// harmless.
///
///////////////////////////////////////////////////////////////////////////////

// System headers first, before the NanoOs headers remap stdio names.
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "NanoOsTest.h"
#include "KernelTestHarness.h"
#include "HalMock.h"

#ifdef __SANITIZE_ADDRESS__

// halPosixImplInit()'s heap-sizing trick recurses ~96 KB down the stack and
// longjmps back; AddressSanitizer's frame instrumentation turns that into a
// stack overflow.  Kernel tests are skipped under ASan - build without ASAN=1
// to run them (plain unit suites still get ASan coverage).
int kernelTestRun(NanoOsTestFn body) {
  (void) body;
  return -1;
}

#else // __SANITIZE_ADDRESS__

// NanoOs kernel headers (resolved through the sim/ symlink dir).
#include "kernel/Hal.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"

/// @def PRIVILEGE_LEVEL_KERNEL_VALUE
///
/// @brief Privilege level the driver process runs at.  The driver is the test
/// harness' stand-in for init, so it runs unrestricted - kernel tests can call
/// HAL functions and kernel services directly.  Kept as a literal so this file
/// does not have to include the enum's header.
#define DRIVER_PRIVILEGE_LEVEL 0 /* PRIVILEGE_LEVEL_KERNEL */

// --- child-side state (only meaningful in the forked child) -------------

static int         _reportFd   = -1;
static NanoOsTestFn _driverBody = NULL;

/// @fn static void driverReportAndExit(int code)
static void driverReportAndExit(int code) {
  if (_reportFd >= 0) {
    nanoOsTestChildEnd(_reportFd);
    _reportFd = -1;
  }
  _exit(code);
}

/// @fn static void* driverProcessMain(void *args)
///
/// @brief Entry point of the process that occupies the shell slot: run the
/// test body, report, exit the child.
static void* driverProcessMain(void *args) {
  (void) args;
  if (_driverBody != NULL) {
    _driverBody();
  }
  driverReportAndExit(0);
  return NULL; // not reached
}

/// @fn static int32_t kernelTestRestartShell(void *processDescriptorRaw)
///
/// @brief Installed as HAL->platform.restartShell.  Populates the shell slot
/// with the driver process instead of a login shell.
static int32_t kernelTestRestartShell(void *processDescriptorRaw) {
  ProcessDescriptor *processDescriptor
    = (ProcessDescriptor*) processDescriptorRaw;

  if ((SCHEDULER_STATE == NULL)
    || (SCHEDULER_STATE->hostname == NULL)
    || (*SCHEDULER_STATE->hostname == '\0')
  ) {
    return -11; // -EAGAIN: scheduler not up yet; retried next sweep.
  }

  if (processCreate(processDescriptor, driverProcessMain, NULL)
    != processSuccess
  ) {
    return -12; // -ENOMEM
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->userId         = ROOT_USER_ID;
  processDescriptor->privilegeLevel = DRIVER_PRIVILEGE_LEVEL;

  return 0;
}

// --- power: any mode just ends the child cleanly ---------------------

/// @fn static void kernelTestOnPowerMode(void)
///
/// @brief Invoked by the mock HAL when a test drives HAL->power.enterMode().
/// The child dies; its result (whatever the body asserted before calling
/// shutdown) has already been captured or will be reported here.
static void kernelTestOnPowerMode(void) {
  driverReportAndExit(0);
}

// --- runner --------------------------------------------------------

static int runChild(NanoOsTestFn body, int reportFd) {
  // Keep kernel bring-up chatter off the parent's TAP stream (stdout).
  dup2(STDERR_FILENO, STDOUT_FILENO);

  _reportFd   = reportFd;
  _driverBody = body;

  halMockSetRestartShell(kernelTestRestartShell);
  halMockSetPowerHook(kernelTestOnPowerMode);

  HalMockConfig config = halMockConfigDefault();
  if (halMockInit(&config, NULL) != 0) {
    nanoOsTestForceFail("halMockInit() failed - kernel HAL did not come up");
    driverReportAndExit(2);
  }

  nanoOsStart(); // does not return

  nanoOsTestForceFail("nanoOsStart() returned - scheduler exited unexpectedly");
  driverReportAndExit(2);
  return 2; // not reached
}

int kernelTestRun(NanoOsTestFn body) {
  int pipeFds[2];
  if (pipe(pipeFds) != 0) {
    nanoOsTestForceFail("pipe() failed in kernel test harness");
    return 3;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    nanoOsTestForceFail("fork() failed in kernel test harness");
    return 3;
  }

  if (pid == 0) {
    close(pipeFds[0]);
    int childStatus = runChild(body, pipeFds[1]);
    _exit(childStatus);
  }

  // Parent.
  close(pipeFds[1]);
  nanoOsTestParentEnd(pipeFds[0]);
  close(pipeFds[0]);

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return 4;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status); // 0 == clean, 2 == boot failure
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status); // crashed
  }
  return 5;
}

#endif // __SANITIZE_ADDRESS__
