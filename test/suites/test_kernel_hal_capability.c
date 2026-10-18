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
    granted
      += (processDescriptor->halCapabilityBitmap[ii >> 3] >> (ii & 7)) & 1;
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
    HAL_CLOCK_GET_ELAPSED_MILLISECONDS, 0, (int64_t) 0, &elapsed));
  NANO_OS_ASSERT_TRUE(elapsed >= 0);
}

NANO_OS_KERNEL_TEST(hal_capability, call_hal_denies_an_ungranted_function) {
  ProcessDescriptor *runningProcess = getRunningProcess();
  NANO_OS_ASSERT_NULL(findHalCapability(runningProcess->halCapabilities,
    runningProcess->numHalCapabilities, HAL_UART, HAL_UART_CONFIGURE));

  NANO_OS_ASSERT_EQ_INT(-EACCES,
    callHal(HAL_UART, HAL_UART_CONFIGURE, 0, (uint32_t) 9600));
}

/// @fn static int deviceMaskTestMismatches(
///   const ProcessDescriptor *processDescriptor)
///
/// @brief Count the subsystems whose device mask in a process disagrees with
/// the intersection of the deviceIds of its grants in that subsystem.
///
/// @param processDescriptor The ProcessDescriptor to check.
///
/// @return Returns the number of disagreements.
static int deviceMaskTestMismatches(
  const ProcessDescriptor *processDescriptor
) {
  int mismatches = 0;
  for (uint8_t subsystem = 0; subsystem < HAL_NUM_SUBSYSTEMS; subsystem++) {
    bool seen = false;
    uint8_t expected = 0;
    for (size_t ii = 0; ii < processDescriptor->numHalCapabilities; ii++) {
      HalCapability *capability = &processDescriptor->halCapabilities[ii];
      if ((capability->subsystemFunction >> 8) == subsystem) {
        expected = seen ? (expected & capability->deviceIds)
          : capability->deviceIds;
        seen = true;
      }
    }
    mismatches += (processDescriptor->halDeviceMasks[subsystem] != expected);
  }
  return mismatches;
}

NANO_OS_KERNEL_TEST(hal_capability, every_process_device_mask_matches) {
  for (size_t pid = 1; pid <= numProcesses; pid++) {
    NANO_OS_ASSERT_EQ_INT(0, deviceMaskTestMismatches(&allProcesses[pid - 1]));
  }
}

NANO_OS_KERNEL_TEST(hal_capability, user_timer_mask_has_the_patched_device) {
  int usersChecked = 0;
  for (size_t pid = 1; pid <= numProcesses; pid++) {
    ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
    HalCapability *cancel = findHalCapability(
      processDescriptor->halCapabilities,
      processDescriptor->numHalCapabilities, HAL_TIMER, HAL_TIMER_CANCEL);
    if ((processDescriptor->privilegeLevel != PRIVILEGE_LEVEL_USER)
      || (cancel == NULL)
    ) {
      continue;
    }
    // The scheduler patches this grant's device in before building any mask.
    NANO_OS_ASSERT_NE_INT(0, cancel->deviceIds);
    NANO_OS_ASSERT_EQ_INT(cancel->deviceIds,
      processDescriptor->halDeviceMasks[HAL_TIMER]);
    usersChecked++;
  }
  NANO_OS_ASSERT_TRUE(usersChecked > 0);
}

NANO_OS_KERNEL_TEST(hal_capability, mixed_device_masks_intersect) {
  HalCapability capabilities[] = {
    {
      .subsystemFunction = (((uint16_t) HAL_UART) << 8) | HAL_UART_POLL,
      .deviceIds =         0x03,
    },
    {
      .subsystemFunction = (((uint16_t) HAL_UART) << 8) | HAL_UART_WRITE,
      .deviceIds =         0x06,
    },
    {
      .subsystemFunction = (((uint16_t) HAL_SPI) << 8) | HAL_SPI_TRANSFER8,
      .deviceIds =         0x01,
    },
  };
  ProcessDescriptor processDescriptor;
  memset(&processDescriptor, 0xff, sizeof(processDescriptor));

  setProcessHalCapabilities(&processDescriptor, capabilities, 3);

  NANO_OS_ASSERT_EQ_INT(0x02, processDescriptor.halDeviceMasks[HAL_UART]);
  NANO_OS_ASSERT_EQ_INT(0x01, processDescriptor.halDeviceMasks[HAL_SPI]);
  NANO_OS_ASSERT_EQ_INT(0x00, processDescriptor.halDeviceMasks[HAL_TIMER]);
}

NANO_OS_KERNEL_TEST(hal_capability, call_hal_checks_the_device) {
  ProcessDescriptor *runningProcess = getRunningProcess();
  NANO_OS_ASSERT_EQ_INT(0x03, runningProcess->halDeviceMasks[HAL_UART]);

  bool isConsole0 = false;
  bool isConsole1 = false;
  NANO_OS_ASSERT_EQ_INT(0,
    callHal(HAL_UART, HAL_UART_IS_CONSOLE, 0, &isConsole0));
  NANO_OS_ASSERT_EQ_INT(0,
    callHal(HAL_UART, HAL_UART_IS_CONSOLE, 1, &isConsole1));
  NANO_OS_ASSERT_TRUE(isConsole0 != isConsole1);

  bool untouched = false;
  NANO_OS_ASSERT_EQ_INT(-EACCES,
    callHal(HAL_UART, HAL_UART_IS_CONSOLE, 2, &untouched));
  NANO_OS_ASSERT_EQ_INT(-EACCES,
    callHal(HAL_UART, HAL_UART_IS_CONSOLE, 8, &untouched));
  NANO_OS_ASSERT_EQ_INT(-EACCES,
    callHal(HAL_UART, HAL_UART_IS_CONSOLE, -1, &untouched));
  NANO_OS_ASSERT_FALSE(untouched);
}
