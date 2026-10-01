///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_ipccap.c
///
/// @brief             Tests for sorted insertion into a process's IPC
///                    capability array.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "kernel/Console.h"

#define TEST_CAPABILITY_SLOTS 8

static bool capabilityPresent(IpcCapability *capabilities,
  size_t numCapabilities, ProcessId destinationPid, uint32_t messageType
) {
  return (findIpcCapability(capabilities, numCapabilities, destinationPid,
    CONSOLE_COMMAND_SIGNATURE, messageType) != NULL);
}

static bool capabilitiesAreSorted(IpcCapability *capabilities,
  size_t numCapabilities
) {
  for (size_t ii = 1; ii < numCapabilities; ii++) {
    if (capabilities[ii - 1].destinationPid > capabilities[ii].destinationPid) {
      return false;
    }
  }

  return true;
}

NANO_OS_KERNEL_TEST(ipccap, insert_keeps_every_ascending_destination) {
  IpcCapability capabilities[TEST_CAPABILITY_SLOTS];
  memset(capabilities, 0, sizeof(capabilities));

  // This is the order the scheduler grants the console its capability for each
  // shell, one per console port.
  for (size_t ii = 0; ii < 4; ii++) {
    ipcCapabilityInsert(capabilities, ii, (ProcessId) (5 + ii),
      CONSOLE_COMMAND_SIGNATURE, CONSOLE_RETURNING_INPUT);
  }

  NANO_OS_ASSERT_TRUE(capabilitiesAreSorted(capabilities, 4));
  for (size_t ii = 0; ii < 4; ii++) {
    NANO_OS_ASSERT_TRUE(capabilityPresent(capabilities, 4,
      (ProcessId) (5 + ii), CONSOLE_RETURNING_INPUT));
  }
}

NANO_OS_KERNEL_TEST(ipccap, insert_keeps_every_descending_destination) {
  IpcCapability capabilities[TEST_CAPABILITY_SLOTS];
  memset(capabilities, 0, sizeof(capabilities));

  for (size_t ii = 0; ii < 4; ii++) {
    ipcCapabilityInsert(capabilities, ii, (ProcessId) (8 - ii),
      CONSOLE_COMMAND_SIGNATURE, CONSOLE_RETURNING_INPUT);
  }

  NANO_OS_ASSERT_TRUE(capabilitiesAreSorted(capabilities, 4));
  for (size_t ii = 0; ii < 4; ii++) {
    NANO_OS_ASSERT_TRUE(capabilityPresent(capabilities, 4,
      (ProcessId) (8 - ii), CONSOLE_RETURNING_INPUT));
  }
}

NANO_OS_KERNEL_TEST(ipccap, insert_into_the_middle_keeps_the_neighbors) {
  IpcCapability capabilities[TEST_CAPABILITY_SLOTS];
  memset(capabilities, 0, sizeof(capabilities));

  ipcCapabilityInsert(capabilities, 0, 2,
    CONSOLE_COMMAND_SIGNATURE, CONSOLE_RETURNING_INPUT);
  ipcCapabilityInsert(capabilities, 1, 4,
    CONSOLE_COMMAND_SIGNATURE, CONSOLE_RETURNING_INPUT);
  ipcCapabilityInsert(capabilities, 2, 6,
    CONSOLE_COMMAND_SIGNATURE, CONSOLE_RETURNING_INPUT);
  ipcCapabilityInsert(capabilities, 3, 8,
    CONSOLE_COMMAND_SIGNATURE, CONSOLE_RETURNING_INPUT);
  ipcCapabilityInsert(capabilities, 4, 5,
    CONSOLE_COMMAND_SIGNATURE, CONSOLE_RETURNING_INPUT);

  NANO_OS_ASSERT_TRUE(capabilitiesAreSorted(capabilities, 5));
  ProcessId expected[] = {2, 4, 5, 6, 8};
  for (size_t ii = 0; ii < 5; ii++) {
    NANO_OS_ASSERT_EQ_INT((long long) expected[ii],
      (long long) capabilities[ii].destinationPid);
    NANO_OS_ASSERT_TRUE(capabilityPresent(capabilities, 5, expected[ii],
      CONSOLE_RETURNING_INPUT));
  }
}
