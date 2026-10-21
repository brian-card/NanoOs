///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_console_input.c
///
/// @brief             Kernel tests for the console's forwarding ports:
///                    CONSOLE_ADD_FORWARDING_PORT, CONSOLE_INPUT_READY, and
///                    the forwarding-port byte reader.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Console.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"

#define TEST_FORWARDING_PORT_INDEX 0

static ConsoleState inputState;
static ConsoleInputReadyArgs inputArgs[2];

/// @fn static ConsolePort* inputFixture(ProcessId forwardingPid)
///
/// @brief Set up a ConsoleState with one forwarding port serviced by
/// forwardingPid.
///
/// @param forwardingPid The ProcessId to service the port.
///
/// @return Returns a pointer to the port.
static ConsolePort* inputFixture(ProcessId forwardingPid) {
  memset(&inputState, 0, sizeof(inputState));
  memset(&inputArgs, 0, sizeof(inputArgs));
  inputState.numConsolePorts = 1;
  ConsolePort *consolePort
    = &inputState.consolePorts[TEST_FORWARDING_PORT_INDEX];
  consolePort->consoleBuffer = &inputState.consoleBuffers[0];
  consolePort->inputState = CONSOLE_INPUT_STATE_NORMAL;
  consolePort->forwardingPid = forwardingPid;
  consolePort->readByte = consoleReadForwardedByte;
  return consolePort;
}

/// @fn static ProcessMessage* sendInput(ConsoleInputReadyArgs *args,
///   const char *bytes, uint8_t numBytes)
///
/// @brief Push CONSOLE_INPUT_READY for the forwarding port onto this
/// process's own queue, pop it, and hand it to the handler as the console
/// would.  Pushing directly skips the IPC capability check a send would make.
///
/// @param args A pointer to the ConsoleInputReadyArgs to send.
/// @param bytes The input bytes to send.
/// @param numBytes The value to send as the number of bytes.
///
/// @return Returns the sent message, which the caller must release.
static ProcessMessage* sendInput(
  ConsoleInputReadyArgs *args, const char *bytes, uint8_t numBytes
) {
  args->portId = TEST_FORWARDING_PORT_INDEX;
  args->numBytes = numBytes;
  memcpy(args->bytes, bytes, strlen(bytes));
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    return NULL;
  }
  processMessageInit(sent, CONSOLE_COMMAND_SIGNATURE | CONSOLE_INPUT_READY,
    args, sizeof(*args), /* waiting= */ true);
  if (processMessageQueuePush(getRunningProcess(), sent) != processSuccess) {
    processMessageRelease(sent);
    return NULL;
  }

  ProcessMessage *received = processMessageQueuePop();
  if (received != sent) {
    return NULL;
  }
  consoleInputReadyCommandHandler(&inputState, received);
  return sent;
}

NANO_OS_KERNEL_TEST(console_input, forwarded_bytes_form_a_line_then_done) {
  ConsolePort *consolePort = inputFixture(getRunningPid());
  ProcessMessage *sent = sendInput(&inputArgs[0], "hi\r", 3);
  NANO_OS_ASSERT_NOT_NULL(sent);
  NANO_OS_ASSERT_EQ_PTR(sent, consolePort->pendingInput);

  ConsoleEcho echo = CONSOLE_ECHO_NONE;
  NANO_OS_ASSERT_EQ_INT('h', consolePort->readByte(consolePort));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(consolePort, 'h', &echo));
  NANO_OS_ASSERT_EQ_INT('i', consolePort->readByte(consolePort));
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_NONE,
    consoleProcessByte(consolePort, 'i', &echo));
  NANO_OS_ASSERT_FALSE(processMessageDone(sent));

  NANO_OS_ASSERT_EQ_INT('\r', consolePort->readByte(consolePort));
  NANO_OS_ASSERT_TRUE(processMessageDone(sent));
  NANO_OS_ASSERT_NULL(consolePort->pendingInput);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_LINE,
    consoleProcessByte(consolePort, '\r', &echo));
  consolePort->consoleBuffer->buffer[
    consolePort->consoleBuffer->consoleBufferIndex] = '\0';
  NANO_OS_ASSERT_STR_EQ("hi\n", consolePort->consoleBuffer->buffer);

  NANO_OS_ASSERT_EQ_INT(-1, consolePort->readByte(consolePort));
  processMessageRelease(sent);
}

NANO_OS_KERNEL_TEST(console_input, input_from_another_process_is_rejected) {
  ConsolePort *consolePort = inputFixture(consolePid);
  ProcessMessage *sent = sendInput(&inputArgs[0], "x", 1);
  NANO_OS_ASSERT_NOT_NULL(sent);

  NANO_OS_ASSERT_NULL(consolePort->pendingInput);
  NANO_OS_ASSERT_TRUE(processMessageDone(sent));
  NANO_OS_ASSERT_EQ_INT(-1, consolePort->readByte(consolePort));
  processMessageRelease(sent);
}

NANO_OS_KERNEL_TEST(console_input, second_message_while_one_is_held_fails) {
  ConsolePort *consolePort = inputFixture(getRunningPid());
  ProcessMessage *first = sendInput(&inputArgs[0], "a", 1);
  NANO_OS_ASSERT_NOT_NULL(first);
  ProcessMessage *second = sendInput(&inputArgs[1], "b", 1);
  NANO_OS_ASSERT_NOT_NULL(second);

  NANO_OS_ASSERT_EQ_PTR(first, consolePort->pendingInput);
  NANO_OS_ASSERT_FALSE(processMessageDone(first));
  NANO_OS_ASSERT_TRUE(processMessageDone(second));
  processMessageRelease(second);

  NANO_OS_ASSERT_EQ_INT('a', consolePort->readByte(consolePort));
  NANO_OS_ASSERT_TRUE(processMessageDone(first));
  processMessageRelease(first);
}

NANO_OS_KERNEL_TEST(console_input, index_past_the_last_port_is_rejected) {
  ConsolePort *consolePort = inputFixture(getRunningPid());
  ConsolePort *unusedPort = &inputState.consolePorts[1];
  unusedPort->forwardingPid = getRunningPid();
  unusedPort->consoleBuffer = &inputState.consoleBuffers[1];
  inputArgs[0].portId = 1;
  inputArgs[0].numBytes = 1;
  inputArgs[0].bytes[0] = 'x';
  ProcessMessage *sent = getAvailableMessage();
  NANO_OS_ASSERT_NOT_NULL(sent);
  processMessageInit(sent, CONSOLE_COMMAND_SIGNATURE | CONSOLE_INPUT_READY,
    &inputArgs[0], sizeof(inputArgs[0]), /* waiting= */ true);
  NANO_OS_ASSERT_EQ_INT(processSuccess,
    processMessageQueuePush(getRunningProcess(), sent));
  NANO_OS_ASSERT_EQ_PTR(sent, processMessageQueuePop());
  consoleInputReadyCommandHandler(&inputState, sent);

  NANO_OS_ASSERT_NULL(consolePort->pendingInput);
  NANO_OS_ASSERT_NULL(unusedPort->pendingInput);
  NANO_OS_ASSERT_TRUE(processMessageDone(sent));
  processMessageRelease(sent);
}

NANO_OS_KERNEL_TEST(console_input, oversized_byte_count_is_rejected) {
  ConsolePort *consolePort = inputFixture(getRunningPid());
  ProcessMessage *sent
    = sendInput(&inputArgs[0], "x", CONSOLE_INPUT_READY_SIZE + 1);
  NANO_OS_ASSERT_NOT_NULL(sent);

  NANO_OS_ASSERT_NULL(consolePort->pendingInput);
  NANO_OS_ASSERT_TRUE(processMessageDone(sent));
  processMessageRelease(sent);
}

NANO_OS_KERNEL_TEST(console_input, hal_port_notice_is_not_held) {
  ConsolePort *consolePort = inputFixture(PROCESS_ID_NOT_SET);
  ProcessMessage *sent = sendInput(&inputArgs[0], "", 0);
  NANO_OS_ASSERT_NOT_NULL(sent);

  NANO_OS_ASSERT_NULL(consolePort->pendingInput);
  NANO_OS_ASSERT_TRUE(processMessageDone(sent));
  processMessageRelease(sent);
}

static ConsoleAddForwardingPortArgs addArgs;

/// @fn static ConsolePort* addFixture(void)
///
/// @brief Set up a ConsoleState as runConsole leaves it with one HAL port on
/// UART 1 using the first ConsoleBuffer.
///
/// @return Returns a pointer to the HAL port.
static ConsolePort* addFixture(void) {
  memset(&inputState, 0, sizeof(inputState));
  memset(&addArgs, 0, sizeof(addArgs));
  for (int ii = 0; ii < CONSOLE_NUM_BUFFERS; ii++) {
    inputState.consoleBuffers[ii].owner = PROCESS_ID_NOT_SET;
  }
  inputState.numConsolePorts = 1;
  ConsolePort *consolePort = &inputState.consolePorts[0];
  consolePort->uartId = 1;
  consolePort->consoleBuffer = &inputState.consoleBuffers[0];
  consolePort->forwardingPid = PROCESS_ID_NOT_SET;
  return consolePort;
}

/// @fn static bool addPort(ProcessId forwardingPid)
///
/// @brief Push CONSOLE_ADD_FORWARDING_PORT onto this process's own queue, pop
/// it, and hand it to the handler as the console would.
///
/// @param forwardingPid The ProcessId to ask for a port for.
///
/// @return Returns true if the handler ran and marked the message done.
static bool addPort(ProcessId forwardingPid) {
  addArgs.forwardingPid = forwardingPid;
  addArgs.portId = 0;
  addArgs.returnValue = 1;
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    return false;
  }
  processMessageInit(sent,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_ADD_FORWARDING_PORT,
    &addArgs, sizeof(addArgs), /* waiting= */ true);
  if (processMessageQueuePush(getRunningProcess(), sent) != processSuccess) {
    processMessageRelease(sent);
    return false;
  }

  ProcessMessage *received = processMessageQueuePop();
  if (received != sent) {
    processMessageRelease(sent);
    return false;
  }
  consoleAddForwardingPort(&inputState, received);
  bool done = processMessageDone(sent);
  processMessageRelease(sent);
  return done;
}

NANO_OS_KERNEL_TEST(console_add_port, adds_a_port_at_the_next_index) {
  ConsolePort *halPort = addFixture();
  NANO_OS_ASSERT_TRUE(addPort(getRunningPid()));
  NANO_OS_ASSERT_EQ_INT(0, addArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(1, addArgs.portId);
  NANO_OS_ASSERT_EQ_INT(2, inputState.numConsolePorts);

  ConsolePort *consolePort = &inputState.consolePorts[1];
  NANO_OS_ASSERT_EQ_INT(1, consolePort->inputReadyArgs.portId);
  NANO_OS_ASSERT_EQ_INT(getRunningPid(), consolePort->forwardingPid);
  NANO_OS_ASSERT_EQ_PTR(&inputState.consoleBuffers[1],
    consolePort->consoleBuffer);
  NANO_OS_ASSERT_TRUE(halPort->consoleBuffer != consolePort->consoleBuffer);
  NANO_OS_ASSERT_EQ_PTR(consoleReadForwardedByte, consolePort->readByte);
  NANO_OS_ASSERT_EQ_INT(PROCESS_ID_NOT_SET, consolePort->inputOwner);
  NANO_OS_ASSERT_EQ_INT(PROCESS_ID_NOT_SET, consolePort->shell);

  ProcessMessage *sent = getAvailableMessage();
  NANO_OS_ASSERT_NOT_NULL(sent);
  inputArgs[0].portId = addArgs.portId;
  inputArgs[0].numBytes = 1;
  inputArgs[0].bytes[0] = 'q';
  processMessageInit(sent, CONSOLE_COMMAND_SIGNATURE | CONSOLE_INPUT_READY,
    &inputArgs[0], sizeof(inputArgs[0]), /* waiting= */ true);
  NANO_OS_ASSERT_EQ_INT(processSuccess,
    processMessageQueuePush(getRunningProcess(), sent));
  NANO_OS_ASSERT_EQ_PTR(sent, processMessageQueuePop());
  consoleInputReadyCommandHandler(&inputState, sent);
  NANO_OS_ASSERT_EQ_INT('q', consolePort->readByte(consolePort));
  NANO_OS_ASSERT_TRUE(processMessageDone(sent));
  processMessageRelease(sent);
}

NANO_OS_KERNEL_TEST(console_add_port, same_process_gets_its_port_back) {
  addFixture();
  NANO_OS_ASSERT_TRUE(addPort(getRunningPid()));
  unsigned char firstPortId = addArgs.portId;
  NANO_OS_ASSERT_TRUE(addPort(getRunningPid()));
  NANO_OS_ASSERT_EQ_INT(0, addArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(firstPortId, addArgs.portId);
  NANO_OS_ASSERT_EQ_INT(2, inputState.numConsolePorts);
}

NANO_OS_KERNEL_TEST(console_add_port, full_port_table_fails) {
  addFixture();
  inputState.numConsolePorts = CONSOLE_NUM_PORTS;
  NANO_OS_ASSERT_TRUE(addPort(getRunningPid()));
  NANO_OS_ASSERT_EQ_INT(-ENOSPC, addArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_NUM_PORTS, inputState.numConsolePorts);
}

NANO_OS_KERNEL_TEST(console_add_port, checked_out_buffers_are_not_taken) {
  addFixture();
  for (int ii = 1; ii < CONSOLE_NUM_BUFFERS - 1; ii++) {
    inputState.consoleBuffers[ii].owner = consolePid;
  }
  NANO_OS_ASSERT_TRUE(addPort(getRunningPid()));
  NANO_OS_ASSERT_EQ_INT(0, addArgs.returnValue);
  NANO_OS_ASSERT_EQ_PTR(&inputState.consoleBuffers[CONSOLE_NUM_BUFFERS - 1],
    inputState.consolePorts[1].consoleBuffer);

  addFixture();
  for (int ii = 1; ii < CONSOLE_NUM_BUFFERS; ii++) {
    inputState.consoleBuffers[ii].owner = consolePid;
  }
  NANO_OS_ASSERT_TRUE(addPort(getRunningPid()));
  NANO_OS_ASSERT_EQ_INT(-EBUSY, addArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(1, inputState.numConsolePorts);
}

NANO_OS_KERNEL_TEST(console_add_port, unset_process_id_is_rejected) {
  addFixture();
  NANO_OS_ASSERT_TRUE(addPort(PROCESS_ID_NOT_SET));
  NANO_OS_ASSERT_EQ_INT(-EINVAL, addArgs.returnValue);
  NANO_OS_ASSERT_EQ_INT(1, inputState.numConsolePorts);
}
