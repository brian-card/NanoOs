///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_smoke.c
///
/// @brief             Smoke tests: the framework runs, and a real kernel boots
///                    under the mock HAL with a test body executing as a
///                    process inside it.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

// NanoOs kernel headers - safe to include in a suite TU (the simulator does
// the same in NanoOsSim.c).
#include "kernel/Hal.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"

// -------------------------------------------------------------------------
// Framework self-check (no kernel).
// -------------------------------------------------------------------------

NANO_OS_TEST(framework, assertions_pass_on_truth) {
  NANO_OS_ASSERT_TRUE(1 + 1 == 2);
  NANO_OS_ASSERT_FALSE(0);
  NANO_OS_ASSERT_EQ_INT(42, 40 + 2);
  NANO_OS_ASSERT_NE_INT(1, 2);
  int local = 0;
  NANO_OS_ASSERT_NOT_NULL(&local);
  NANO_OS_ASSERT_NULL(NULL);
  NANO_OS_ASSERT_STR_EQ("nano", "nano");
}

// -------------------------------------------------------------------------
// Kernel bring-up: this body runs as the driver process inside a freshly
// booted kernel.
// -------------------------------------------------------------------------

NANO_OS_KERNEL_TEST(boot, scheduler_state_is_live) {
  NANO_OS_ASSERT_NOT_NULL(SCHEDULER_STATE);
  NANO_OS_ASSERT_EQ_INT(1, SCHEDULER_STATE->schedulerPid);
  NANO_OS_ASSERT_EQ_INT(2, SCHEDULER_STATE->consolePid);
  NANO_OS_ASSERT_EQ_INT(3, SCHEDULER_STATE->memoryManagerPid);
}

NANO_OS_KERNEL_TEST(boot, body_runs_as_a_real_process) {
  // The driver occupies the shell slot, so it must have a valid, non-kernel
  // PID and a live process descriptor.
  ProcessDescriptor *self = getRunningProcess();
  NANO_OS_ASSERT_NOT_NULL(self);
  NANO_OS_ASSERT_TRUE(getRunningPid() >= SCHEDULER_STATE->firstShellPid);
  NANO_OS_ASSERT_TRUE(getRunningPid() <= NANO_OS_NUM_PROCESSES);
}

NANO_OS_KERNEL_TEST(boot, hal_clock_is_the_virtual_mock) {
  // The deterministic clock starts at zero and only moves when asked.
  int64_t elapsedA = 0;
  int64_t elapsedB = 0;
  HAL->clock.getElapsedMilliseconds(0, &elapsedA);
  HAL->clock.getElapsedMilliseconds(0, &elapsedB);
  NANO_OS_ASSERT_EQ_INT(elapsedA, elapsedB);
}
