///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_console_input.c
///
/// @brief             Kernel tests for the console's CONSOLE_INPUT_READY
///                    handler and the forwarding-port byte reader.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"

#include <string.h>

#include "kernel/Console.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"

#define TEST_FORWARDING_PORT_ID 7

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
  ConsolePort *consolePort = &inputState.consolePorts[0];
  consolePort->portId = TEST_FORWARDING_PORT_ID;
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
  args->portId = TEST_FORWARDING_PORT_ID;
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
