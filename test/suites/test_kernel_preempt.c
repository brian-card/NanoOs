///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_preempt.c
///
/// @brief             Tests for deferral of forced preemption while a process
///                    is inside a critical section.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include "kernel/Coroutines.h"
#include "kernel/Hal.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "HalMock.h"

NANO_OS_KERNEL_TEST(preempt, critical_section_is_not_inhibited_by_default) {
  NANO_OS_ASSERT_FALSE(schedulerPreemptionInhibited());
  NANO_OS_ASSERT_FALSE(schedulerPreemptionPending());
}

NANO_OS_KERNEL_TEST(preempt, inhibit_and_allow_are_balanced) {
  schedulerInhibitPreemption();
  NANO_OS_ASSERT_TRUE(schedulerPreemptionInhibited());

  schedulerAllowPreemption();
  NANO_OS_ASSERT_FALSE(schedulerPreemptionInhibited());
}

NANO_OS_KERNEL_TEST(preempt, nested_sections_stay_inhibited_until_the_last) {
  schedulerInhibitPreemption();
  schedulerInhibitPreemption();
  schedulerInhibitPreemption();

  schedulerAllowPreemption();
  NANO_OS_ASSERT_TRUE(schedulerPreemptionInhibited());
  schedulerAllowPreemption();
  NANO_OS_ASSERT_TRUE(schedulerPreemptionInhibited());

  schedulerAllowPreemption();
  NANO_OS_ASSERT_FALSE(schedulerPreemptionInhibited());
}

NANO_OS_KERNEL_TEST(preempt, a_preemption_inside_a_section_is_deferred) {
  schedulerInhibitPreemption();

  // forceYield is what the preemption timer invokes.  Inside a critical
  // section it must record the preemption rather than switch away.
  forceYield();
  NANO_OS_ASSERT_TRUE(schedulerPreemptionPending());
  NANO_OS_ASSERT_TRUE(schedulerPreemptionInhibited());

  schedulerAllowPreemption();
  // Leaving the section takes the preemption, which clears it.
  NANO_OS_ASSERT_FALSE(schedulerPreemptionPending());
  NANO_OS_ASSERT_FALSE(schedulerPreemptionInhibited());
}

NANO_OS_KERNEL_TEST(preempt, a_deferred_preemption_survives_an_inner_section) {
  schedulerInhibitPreemption();
  forceYield();
  NANO_OS_ASSERT_TRUE(schedulerPreemptionPending());

  // An inner section coming and going must not release the preemption early.
  schedulerInhibitPreemption();
  schedulerAllowPreemption();
  NANO_OS_ASSERT_TRUE(schedulerPreemptionPending());
  NANO_OS_ASSERT_TRUE(schedulerPreemptionInhibited());

  schedulerAllowPreemption();
  NANO_OS_ASSERT_FALSE(schedulerPreemptionPending());
}

NANO_OS_KERNEL_TEST(preempt, the_depth_is_per_coroutine) {
  Coroutine *running = getRunningCoroutine();
  NANO_OS_ASSERT_NOT_NULL(running);

  schedulerInhibitPreemption();
  NANO_OS_ASSERT_EQ_INT(1, (long long) running->criticalSectionDepth);
  schedulerAllowPreemption();
  NANO_OS_ASSERT_EQ_INT(0, (long long) running->criticalSectionDepth);
}

NANO_OS_KERNEL_TEST(preempt, switching_to_unpreemptible_processes_cancels_nothing) {
  // Every process running here is a kernel process, so nothing arms the timer.
  unsigned int cancelCount = mockTimerCancelCount();
  processYield();
  NANO_OS_ASSERT_EQ_INT(cancelCount, mockTimerCancelCount());
}

NANO_OS_KERNEL_TEST(preempt, an_armed_timer_does_not_outlive_a_yield) {
  int preemptionTimer = schedulerGetPreemptionTimer();
  NANO_OS_ASSERT_TRUE(preemptionTimer >= 0);

  schedulerPreemptionTimerArmed = true;
  HAL->timer->configOneShot(preemptionTimer, 10000000, forceYield);
  NANO_OS_ASSERT_TRUE(mockTimerArmed(preemptionTimer));

  processYield();
  NANO_OS_ASSERT_FALSE(mockTimerArmed(preemptionTimer));
  NANO_OS_ASSERT_FALSE(schedulerPreemptionTimerArmed);
}
