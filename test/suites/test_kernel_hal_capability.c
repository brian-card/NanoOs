////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file test_kernel_hal_capability.c
///
/// @brief Kernel tests for the per-process HAL capability bitmap.

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Hal.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "hal/HalCommon.h"
#include "user/NanoOsErrno.h"

/// @fn static int capabilityTestMismatches(
///   const ProcessDescriptor *processDescriptor)
///
/// @brief Count the HAL functions whose bit in a process's capability bitmap
/// disagrees with a search of its capability array.
///
/// @param processDescriptor The ProcessDescriptor to check.
///
/// @return Returns the number of disagreements.
static int capabilityTestMismatches(
  const ProcessDescriptor *processDescriptor
) {
  int mismatches = 0;
  for (uint8_t subsystem = 0; subsystem < HAL_NUM_SUBSYSTEMS; subsystem++) {
    uint8_t numFunctions = halCapabilityBitBase[subsystem + 1]
      - halCapabilityBitBase[subsystem];
    for (uint8_t function = 0; function < numFunctions; function++) {
      bool searched = findHalCapability(processDescriptor->halCapabilities,
        processDescriptor->numHalCapabilities,
        (HalSubsystem) subsystem, function) != NULL;
      uint8_t bit = halCapabilityBitBase[subsystem] + function;
      bool mapped
        = (processDescriptor->halCapabilityBitmap[bit >> 3] >> (bit & 7)) & 1;
      mismatches += (searched != mapped);
    }
  }
  return mismatches;
}

/// @fn static int capabilityTestGranted(
///   const ProcessDescriptor *processDescriptor)
///
/// @brief Count the bits set in a process's capability bitmap.
///
/// @param processDescriptor The ProcessDescriptor to count.
///
/// @return Returns the number of set bits.
static int capabilityTestGranted(const ProcessDescriptor *processDescriptor) {
  int granted = 0;
  for (int ii = 0; ii < HAL_CAPABILITY_BITMAP_SIZE * 8; ii++) {
    granted += (processDescriptor->halCapabilityBitmap[ii >> 3] >> (ii & 7)) & 1;
  }
  return granted;
}

NANO_OS_KERNEL_TEST(hal_capability, bit_bases_follow_function_counts) {
  NANO_OS_ASSERT_EQ_INT(0, halCapabilityBitBase[HAL_PLATFORM]);
  NANO_OS_ASSERT_EQ_INT(HAL_PLATFORM_NUM_FNS, halCapabilityBitBase[HAL_MEMORY]);
  NANO_OS_ASSERT_EQ_INT(HAL_TIMER_NUM_FNS,
    halCapabilityBitBase[HAL_BLOCK_DEVICE] - halCapabilityBitBase[HAL_TIMER]);
  NANO_OS_ASSERT_EQ_INT(HAL_BLOCK_DEVICE_NUM_FNS,
    halCapabilityBitBase[HAL_NUM_SUBSYSTEMS]
    - halCapabilityBitBase[HAL_BLOCK_DEVICE]);
  NANO_OS_ASSERT_TRUE(halCapabilityBitBase[HAL_NUM_SUBSYSTEMS]
    <= HAL_CAPABILITY_BITMAP_SIZE * 8);
}

NANO_OS_KERNEL_TEST(hal_capability, every_process_bitmap_matches_its_array) {
  int processesWithGrants = 0;
  for (size_t pid = 1; pid <= numProcesses; pid++) {
    ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
    NANO_OS_ASSERT_EQ_INT(0, capabilityTestMismatches(processDescriptor));
    processesWithGrants += (capabilityTestGranted(processDescriptor) > 0);
  }
  NANO_OS_ASSERT_TRUE(processesWithGrants >= 4);
}

NANO_OS_KERNEL_TEST(hal_capability, sd_card_bitmap_matches_its_array) {
  static HalCapability sdCardCapabilities[]
    = SD_CARD_HAL_CAPABILITIES_INITIALIZER;
  size_t numSdCardCapabilities
    = sizeof(sdCardCapabilities) / sizeof(sdCardCapabilities[0]);
  ProcessDescriptor processDescriptor;
  memset(&processDescriptor, 0xff, sizeof(processDescriptor));

  setProcessHalCapabilities(&processDescriptor,
    sdCardCapabilities, numSdCardCapabilities);

  NANO_OS_ASSERT_EQ_PTR(sdCardCapabilities, processDescriptor.halCapabilities);
  NANO_OS_ASSERT_EQ_INT((int) numSdCardCapabilities,
    (int) processDescriptor.numHalCapabilities);
  NANO_OS_ASSERT_EQ_INT(0, capabilityTestMismatches(&processDescriptor));
  NANO_OS_ASSERT_EQ_INT((int) numSdCardCapabilities,
    capabilityTestGranted(&processDescriptor));
}

NANO_OS_KERNEL_TEST(hal_capability, no_capabilities_clears_the_bitmap) {
  ProcessDescriptor processDescriptor;
  memset(&processDescriptor, 0xff, sizeof(processDescriptor));

  setProcessHalCapabilities(&processDescriptor, NULL, 0);

  NANO_OS_ASSERT_NULL(processDescriptor.halCapabilities);
  NANO_OS_ASSERT_EQ_INT(0, capabilityTestGranted(&processDescriptor));
}

NANO_OS_KERNEL_TEST(hal_capability, call_hal_runs_a_granted_function) {
  int64_t elapsed = -1;
  NANO_OS_ASSERT_EQ_INT(0, callHal(HAL_CLOCK,
    HAL_CLOCK_GET_ELAPSED_MILLISECONDS, (int64_t) 0, &elapsed));
  NANO_OS_ASSERT_TRUE(elapsed >= 0);
}

NANO_OS_KERNEL_TEST(hal_capability, call_hal_denies_an_ungranted_function) {
  ProcessDescriptor *runningProcess = getRunningProcess();
  NANO_OS_ASSERT_NULL(findHalCapability(runningProcess->halCapabilities,
    runningProcess->numHalCapabilities, HAL_UART, HAL_UART_CONFIGURE));

  NANO_OS_ASSERT_EQ_INT(-EACCES,
    callHal(HAL_UART, HAL_UART_CONFIGURE, (int32_t) 0, (uint32_t) 9600));
}
