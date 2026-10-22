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
#include "kernel/Hal.h"
#include "user/NanoOsApi.h"

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

#define TEST_UART_WRITE ((((uint16_t) HAL_UART) << 8) | HAL_UART_WRITE)
#define TEST_UART_READ ((((uint16_t) HAL_UART) << 8) | HAL_UART_READ)
#define TEST_CLOCK_MS ((((uint16_t) HAL_CLOCK) << 8) \
  | HAL_CLOCK_GET_ELAPSED_MILLISECONDS)

NANO_OS_TEST(halcap, insert_keeps_the_array_sorted) {
  HalCapability capabilities[4];
  size_t numCapabilities = 0;
  numCapabilities = halCapabilityInsert(capabilities, numCapabilities,
    TEST_CLOCK_MS, 0x01);
  numCapabilities = halCapabilityInsert(capabilities, numCapabilities,
    TEST_UART_WRITE, 0x02);
  numCapabilities = halCapabilityInsert(capabilities, numCapabilities,
    TEST_UART_READ, 0x01);

  NANO_OS_ASSERT_EQ_INT(3, numCapabilities);
  for (size_t ii = 1; ii < numCapabilities; ii++) {
    NANO_OS_ASSERT_TRUE(capabilities[ii - 1].subsystemFunction
      < capabilities[ii].subsystemFunction);
  }
  NANO_OS_ASSERT_EQ_INT(TEST_UART_READ, capabilities[0].subsystemFunction);
  NANO_OS_ASSERT_EQ_INT(0x01, capabilities[0].deviceIds);
  NANO_OS_ASSERT_EQ_INT(TEST_UART_WRITE, capabilities[1].subsystemFunction);
  NANO_OS_ASSERT_EQ_INT(0x02, capabilities[1].deviceIds);
  NANO_OS_ASSERT_EQ_INT(TEST_CLOCK_MS, capabilities[2].subsystemFunction);
}

NANO_OS_TEST(halcap, insert_of_a_granted_function_adds_its_devices) {
  HalCapability capabilities[2];
  size_t numCapabilities = 0;
  numCapabilities = halCapabilityInsert(capabilities, numCapabilities,
    TEST_UART_WRITE, 0x02);
  numCapabilities = halCapabilityInsert(capabilities, numCapabilities,
    TEST_UART_WRITE, 0x01);

  NANO_OS_ASSERT_EQ_INT(1, numCapabilities);
  NANO_OS_ASSERT_EQ_INT(0x03, capabilities[0].deviceIds);
}

/// @fn static void* idleProcess(void *args)
///
/// @brief Yield forever without ever reading a message.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* idleProcess(void *args) {
  (void) args;
  while (1) {
    processYield();
  }

  return NULL;
}

/// @fn static ProcessDescriptor* startIdleProcess(void)
///
/// @brief Run idleProcess in the first user process slot.
///
/// @return Returns a pointer to the process's ProcessDescriptor, or NULL if it
/// couldn't be started.
static ProcessDescriptor* startIdleProcess(void) {
  ProcessId pid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *processDescriptor = &allProcesses[pid - 1];
  if (processCreate(processDescriptor, idleProcess, NULL) != processSuccess) {
    return NULL;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processQueuePush(processDescriptor->readyQueue, processDescriptor);
  return processDescriptor;
}

/// @fn static int executiveSend(ProcessDescriptor *target,
///   IpcCapability *capabilities, size_t numCapabilities,
///   ProcessMessage **sent)
///
/// @brief Send a CONSOLE_WRITE_BUFFER message to a process through the
/// executive API while this process holds the given IPC capabilities.
///
/// @param target A pointer to the ProcessDescriptor to send to.
/// @param capabilities The IPC capabilities to hold during the send.
/// @param numCapabilities The number of entries in capabilities.
/// @param sent Where to store the message that was sent.
///
/// @return Returns the executive API's return value.
static int executiveSend(ProcessDescriptor *target,
  IpcCapability *capabilities, size_t numCapabilities, ProcessMessage **sent
) {
  ProcessDescriptor *self = getRunningProcess();
  IpcCapability *savedCapabilities = self->ipcCapabilities;
  size_t savedNumCapabilities = self->numIpcCapabilities;
  self->ipcCapabilities = capabilities;
  self->numIpcCapabilities = numCapabilities;

  *sent = getAvailableMessage();
  processMessageInit(*sent, CONSOLE_COMMAND_SIGNATURE | CONSOLE_WRITE_BUFFER,
    NULL, 0, /* waiting= */ true);
  int returnValue = nanoOsExecutiveApi.sendProcessMessageToPid(
    target->processId, *sent);

  self->ipcCapabilities = savedCapabilities;
  self->numIpcCapabilities = savedNumCapabilities;
  return returnValue;
}

NANO_OS_KERNEL_TEST(executive_api, send_with_the_capability_is_queued) {
  ProcessDescriptor *target = startIdleProcess();
  NANO_OS_ASSERT_NOT_NULL(target);
  IpcCapability capability = {
    .destinationPid = target->processId,
    .signature = CONSOLE_COMMAND_SIGNATURE,
    .messageTypes = ((uint32_t) 1) << CONSOLE_WRITE_BUFFER,
  };
  ProcessMessage *sent = NULL;

  NANO_OS_ASSERT_EQ_INT(processSuccess,
    executiveSend(target, &capability, 1, &sent));

  NANO_OS_ASSERT_EQ_PTR(sent, target->mainThread->messageQueue.head);
  comessageQueueRemove(target->mainThread, sent);
  processMessageRelease(sent);
}

NANO_OS_KERNEL_TEST(executive_api, send_without_the_capability_is_refused) {
  ProcessDescriptor *target = startIdleProcess();
  NANO_OS_ASSERT_NOT_NULL(target);
  IpcCapability capability = {
    .destinationPid = target->processId,
    .signature = CONSOLE_COMMAND_SIGNATURE,
    .messageTypes = ((uint32_t) 1) << CONSOLE_WRITE_VALUE,
  };
  ProcessMessage *sent = NULL;

  NANO_OS_ASSERT_EQ_INT(processError,
    executiveSend(target, &capability, 1, &sent));

  NANO_OS_ASSERT_NULL(target->mainThread->messageQueue.head);
  processMessageRelease(sent);
}

NANO_OS_KERNEL_TEST(executive_api, available_message_is_marked_in_use) {
  ProcessMessage *processMessage = nanoOsExecutiveApi.getAvailableMessage();
  NANO_OS_ASSERT_NOT_NULL(processMessage);
  NANO_OS_ASSERT_TRUE(processMessageInUse(processMessage));

  ProcessMessage *second = nanoOsExecutiveApi.getAvailableMessage();
  NANO_OS_ASSERT_NOT_NULL(second);
  NANO_OS_ASSERT_TRUE(second != processMessage);
  processMessageRelease(second);
  processMessageRelease(processMessage);
}
