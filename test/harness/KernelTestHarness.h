///////////////////////////////////////////////////////////////////////////////
///
/// @file              KernelTestHarness.h
///
/// @brief             Boots a real NanoOs kernel under the mock HAL and runs a
///                    test body inside it as a process.
///
/// Each kernel test runs in its own forked child process: the child boots the
/// kernel, the test body executes as a NanoOs process (the "driver" that would
/// otherwise be the login shell), the body's pass/fail is streamed back to the
/// parent over a pipe, and the child exits.  A fresh fork per test means every
/// kernel test starts from a pristine address space - no teardown ordering to
/// get right, and a test that calls shutdown()/power-off is harmless.
///
///////////////////////////////////////////////////////////////////////////////

#ifndef KERNEL_TEST_HARNESS_H
#define KERNEL_TEST_HARNESS_H

#include "NanoOsTest.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @fn int kernelTestRun(NanoOsTestFn body)
///
/// @brief NanoOsTestKernelRunner implementation.  Forks, boots a kernel in the
/// child under the mock HAL, runs body as the driver process, and collects the
/// result.
///
/// @param body The test body to execute inside the booted kernel.
///
/// @return 0 if the child booted and ran to completion (the body's own
/// pass/fail is reported separately, through the framework's failure state);
/// non-zero if the child crashed or the kernel failed to boot.
int kernelTestRun(NanoOsTestFn body);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KERNEL_TEST_HARNESS_H
