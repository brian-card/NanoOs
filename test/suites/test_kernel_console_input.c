///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_kernel_console_input.c
///
/// @brief             Kernel tests for the console's forwarding ports:
///                    CONSOLE_ADD_FORWARDING_PORT, CONSOLE_INPUT_READY, the
///                    forwarding-port byte reader, and output forwarding.
///
///////////////////////////////////////////////////////////////////////////////

#include "NanoOsTest.h"
#include "HalMock.h"

#include <string.h>

#include "kernel/Console.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsSignal.h"
#include "user/NanoOsStdio.h"

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
  consolePort->modes = CONSOLE_MODE_DEFAULT;
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

static char forwardedText[16];
static int64_t forwardedType;
static ProcessId forwardedFrom;
static int numForwarded;

/// @fn static void* fakeDriver(void *args)
///
/// @brief Stand in for a forwarding port's process: record each string the
/// console forwards and mark its message done.
///
/// @param args Unused.
///
/// @return This function never returns.
static void* fakeDriver(void *args) {
  (void) args;
  while (1) {
    ProcessMessage *processMessage = processMessageQueueWait(NULL);
    forwardedType = processMessageType(processMessage);
    forwardedFrom = processPid(processMessageFrom(processMessage));
    strncpy(forwardedText, (const char*) processMessageData(processMessage),
      sizeof(forwardedText) - 1);
    numForwarded++;
    processMessageSetDone(processMessage);
  }

  return NULL;
}

/// @fn static ConsolePort* forwardFixture(void)
///
/// @brief Start fakeDriver in the first user process slot, let this process
/// send it CONSOLE_FORWARD_STRING, and set up a forwarding port it services.
///
/// @return Returns a pointer to the port, or NULL if fakeDriver couldn't be
/// started.
static ConsolePort* forwardFixture(void) {
  static IpcCapability forwardCapability;
  memset(forwardedText, 0, sizeof(forwardedText));
  forwardedType = 0;
  forwardedFrom = PROCESS_ID_NOT_SET;
  numForwarded = 0;

  ProcessId driverPid = schedulerGetFirstShellPid() + schedulerGetNumShells();
  ProcessDescriptor *driver = &allProcesses[driverPid - 1];
  if (processCreate(driver, fakeDriver, NULL) != processSuccess) {
    return NULL;
  }
  threadSetContext(driver->mainThread, driver);
  processQueuePush(driver->readyQueue, driver);

  forwardCapability.destinationPid = driverPid;
  forwardCapability.signature = CONSOLE_COMMAND_SIGNATURE;
  forwardCapability.messageTypes = ((uint32_t) 1) << CONSOLE_FORWARD_STRING;
  getRunningProcess()->ipcCapabilities = &forwardCapability;
  getRunningProcess()->numIpcCapabilities = 1;
  getRunningProcess()->ipcCapabilitiesDynamic = false;

  ConsolePort *consolePort = inputFixture(driverPid);
  consolePort->consolePrintString = consoleForwardString;
  consolePort->modes = CONSOLE_MODE_DEFAULT;
  return consolePort;
}

NANO_OS_KERNEL_TEST(console_forward, string_reaches_the_driver_before_return) {
  ConsolePort *consolePort = forwardFixture();
  NANO_OS_ASSERT_NOT_NULL(consolePort);

  char text[8];
  strcpy(text, "hello");
  NANO_OS_ASSERT_EQ_INT(5, consolePort->consolePrintString(consolePort, text));
  memset(text, 0, sizeof(text));

  NANO_OS_ASSERT_EQ_INT(1, numForwarded);
  NANO_OS_ASSERT_STR_EQ("hello", forwardedText);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_COMMAND_SIGNATURE | CONSOLE_FORWARD_STRING,
    forwardedType);
  NANO_OS_ASSERT_EQ_INT(getRunningPid(), forwardedFrom);
}

NANO_OS_KERNEL_TEST(console_forward, echo_goes_to_the_driver) {
  ConsolePort *consolePort = forwardFixture();
  NANO_OS_ASSERT_NOT_NULL(consolePort);

  consoleEchoInput(consolePort, CONSOLE_ECHO_CHAR, 'a');
  NANO_OS_ASSERT_EQ_INT(1, numForwarded);
  NANO_OS_ASSERT_STR_EQ("a", forwardedText);

  consoleEchoInput(consolePort, CONSOLE_ECHO_ERASE, ASCII_BACKSPACE);
  NANO_OS_ASSERT_EQ_INT(2, numForwarded);
  NANO_OS_ASSERT_STR_EQ("\b \b", forwardedText);

  memset(forwardedText, 0, sizeof(forwardedText));
  consoleEchoInput(consolePort, CONSOLE_ECHO_NEWLINE, '\r');
  NANO_OS_ASSERT_EQ_INT(3, numForwarded);
  NANO_OS_ASSERT_STR_EQ("\n", forwardedText);

  consoleEchoInput(consolePort, CONSOLE_ECHO_NONE, 'b');
  consolePort->modes = CONSOLE_MODE_DEFAULT & ~CONSOLE_MODE_ECHO;
  consoleEchoInput(consolePort, CONSOLE_ECHO_CHAR, 'c');
  NANO_OS_ASSERT_EQ_INT(3, numForwarded);
}

NANO_OS_KERNEL_TEST(console_forward, unreachable_driver_prints_nothing) {
  ConsolePort *consolePort = forwardFixture();
  NANO_OS_ASSERT_NOT_NULL(consolePort);
  getRunningProcess()->numIpcCapabilities = 0;

  NANO_OS_ASSERT_EQ_INT(0, consolePort->consolePrintString(consolePort, "x"));
  NANO_OS_ASSERT_EQ_INT(0, numForwarded);
}

/// @fn static void releasePid(ProcessId pid)
///
/// @brief Hand the console's CONSOLE_RELEASE_PID_PORT handler a message for a
/// process ID that appears to come from the scheduler.
///
/// @param pid The ProcessId to release.
///
/// @return This function returns no value.
static void releasePid(ProcessId pid) {
  static ConsoleReleasePidPortArgs releaseArgs;
  releaseArgs.processId = pid;
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    return;
  }
  processMessageInit(sent,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_PID_PORT,
    &releaseArgs, sizeof(releaseArgs), /* waiting= */ false);
  if (processMessageQueuePush(getRunningProcess(), sent) != processSuccess) {
    processMessageRelease(sent);
    return;
  }
  ProcessMessage *received = processMessageQueuePop();
  msg_from(received).coro = allProcesses[schedulerPid - 1].mainThread;
  consoleReleasePidPortCommandHandler(&inputState, received);
}

NANO_OS_KERNEL_TEST(console_release, forwarder_exit_drops_its_held_input) {
  ConsolePort *consolePort = inputFixture(getRunningPid());
  ProcessMessage *held = sendInput(&inputArgs[0], "\x1b[", 2);
  NANO_OS_ASSERT_NOT_NULL(held);
  NANO_OS_ASSERT_EQ_INT('\x1b', consolePort->readByte(consolePort));
  consolePort->inputState = CONSOLE_INPUT_STATE_ESCAPE;

  releasePid(getRunningPid());

  NANO_OS_ASSERT_NULL(consolePort->pendingInput);
  NANO_OS_ASSERT_EQ_INT(0, consolePort->pendingInputIndex);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_STATE_NORMAL, consolePort->inputState);
  NANO_OS_ASSERT_NULL(processMessageInUse(held));
  NANO_OS_ASSERT_EQ_INT(getRunningPid(), consolePort->forwardingPid);
  NANO_OS_ASSERT_EQ_INT(-1, consolePort->readByte(consolePort));
}

NANO_OS_KERNEL_TEST(console_release, other_exit_keeps_held_input) {
  ConsolePort *consolePort = inputFixture(getRunningPid());
  ProcessMessage *held = sendInput(&inputArgs[0], "z", 1);
  NANO_OS_ASSERT_NOT_NULL(held);

  releasePid(consolePid);

  NANO_OS_ASSERT_EQ_PTR(held, consolePort->pendingInput);
  NANO_OS_ASSERT_EQ_INT('z', consolePort->readByte(consolePort));
  processMessageRelease(held);
}

/// @fn static int getUartPort(unsigned char uartId)
///
/// @brief Hand the console's CONSOLE_GET_UART_PORT handler a request.
///
/// @param uartId The ID of the HAL UART to look up.
///
/// @return Returns the port index the handler reported, or -2 if the request
/// couldn't be made.
static int getUartPort(unsigned char uartId) {
  static ConsoleGetUartPortArgs uartPortArgs;
  uartPortArgs.uartId = uartId;
  uartPortArgs.portId = -2;
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    return -2;
  }
  processMessageInit(sent, CONSOLE_COMMAND_SIGNATURE | CONSOLE_GET_UART_PORT,
    &uartPortArgs, sizeof(uartPortArgs), /* waiting= */ false);
  if (processMessageQueuePush(getRunningProcess(), sent) != processSuccess) {
    processMessageRelease(sent);
    return -2;
  }
  consoleGetUartPortCommandHandler(&inputState, processMessageQueuePop());
  return uartPortArgs.portId;
}

NANO_OS_KERNEL_TEST(console_uart_port, finds_the_port_a_uart_services) {
  addFixture();
  inputState.consolePorts[0].uartId = 1;
  ConsolePort *forwardingPort = &inputState.consolePorts[1];
  forwardingPort->forwardingPid = getRunningPid();
  forwardingPort->uartId = 0;
  inputState.numConsolePorts = 2;

  NANO_OS_ASSERT_EQ_INT(0, getUartPort(1));
  NANO_OS_ASSERT_EQ_INT(-1, getUartPort(0));
  NANO_OS_ASSERT_EQ_INT(-1, getUartPort(3));
}

static const char *uartLikeBytes;
static size_t uartLikeAvailable;
static size_t uartLikeRead;
static char deliveredLines[4][CONSOLE_BUFFER_SIZE];
static int numDeliveredLines;

/// @fn static int uartLikeReadByte(ConsolePort *consolePort)
///
/// @brief Stand in for a UART's readByte: return the next byte that has
/// "arrived", or -1 if the test hasn't let another one arrive yet.
///
/// @param consolePort Unused.
///
/// @return Returns the byte, cast to an int, or -1.
static int uartLikeReadByte(ConsolePort *consolePort) {
  (void) consolePort;
  if (uartLikeRead < uartLikeAvailable) {
    return (uint8_t) uartLikeBytes[uartLikeRead++];
  }
  return -1;
}

/// @fn static void readerFixture(ConsolePort *consolePort)
///
/// @brief Make this process the reader of a port, waiting for input, with the
/// IPC capability the console needs to hand it lines.
///
/// @param consolePort A pointer to the ConsolePort to read from.
///
/// @return This function returns no value.
static void readerFixture(ConsolePort *consolePort) {
  static IpcCapability readerCapability;
  readerCapability.destinationPid = getRunningPid();
  readerCapability.signature = CONSOLE_COMMAND_SIGNATURE;
  readerCapability.messageTypes = ((uint32_t) 1) << CONSOLE_RETURNING_INPUT;
  getRunningProcess()->ipcCapabilities = &readerCapability;
  getRunningProcess()->numIpcCapabilities = 1;
  getRunningProcess()->ipcCapabilitiesDynamic = false;

  consolePort->inputOwner = getRunningPid();
  consolePort->waitingForInput = true;
  consolePort->modes = CONSOLE_MODE_DEFAULT & ~CONSOLE_MODE_ECHO;
  memset(deliveredLines, 0, sizeof(deliveredLines));
  numDeliveredLines = 0;
}

/// @var deliveredRaw
///
/// @brief Whether each line serviceAndRead read was marked as raw input.
static bool deliveredRaw[4];

/// @fn static bool serviceAndRead(ConsolePort *consolePort)
///
/// @brief Run a service pass of a port, then read every line it delivered and
/// wait for the next one, as a reader would.
///
/// @param consolePort A pointer to the ConsolePort to service.
///
/// @return Returns true if the port is waiting out the escape delay.
static bool serviceAndRead(ConsolePort *consolePort) {
  bool escapePending = consoleServicePort(&inputState, consolePort);
  ProcessMessage *processMessage = NULL;
  while ((processMessage = processMessageQueuePop()) != NULL) {
    if ((processMessageType(processMessage)
        == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_RETURNING_INPUT))
      && (numDeliveredLines < 4)
    ) {
      ConsoleBuffer *consoleBuffer
        = (ConsoleBuffer*) processMessageData(processMessage);
      deliveredRaw[numDeliveredLines] = consoleBuffer->rawInput;
      strcpy(deliveredLines[numDeliveredLines++], consoleBuffer->buffer);
    }
    processMessageRelease(processMessage);
  }
  consolePort->consoleBuffer->bufferAwaitingRelease = false;
  consolePort->waitingForInput = true;
  return escapePending;
}

/// @fn static void deliverByteByByte(const char *bytes)
///
/// @brief Let a UART-like port's bytes arrive one at a time, servicing the port
/// after each one, then let the escape delay pass.
///
/// @param bytes The bytes to deliver.
///
/// @return This function returns no value.
static void deliverByteByByte(const char *bytes) {
  ConsolePort *consolePort = inputFixture(PROCESS_ID_NOT_SET);
  consolePort->readByte = uartLikeReadByte;
  readerFixture(consolePort);
  uartLikeBytes = bytes;
  uartLikeAvailable = 0;
  uartLikeRead = 0;

  for (size_t ii = 0; bytes[ii] != '\0'; ii++) {
    uartLikeAvailable = ii + 1;
    serviceAndRead(consolePort);
  }
  mockClockAdvanceNs((CONSOLE_ESCAPE_DELAY_MS + 1) * 1000000ULL);
  serviceAndRead(consolePort);
}

/// @fn static void deliverInBatches(const char **batches)
///
/// @brief Send a forwarding port's bytes in CONSOLE_INPUT_READY batches, as
/// the VDP driver does, servicing the port until each batch is read.
///
/// @param batches The NULL-terminated array of batches to send.
///
/// @return This function returns no value.
static void deliverInBatches(const char **batches) {
  ConsolePort *consolePort = inputFixture(getRunningPid());
  readerFixture(consolePort);

  for (int ii = 0; batches[ii] != NULL; ii++) {
    ProcessMessage *sent = sendInput(&inputArgs[0], batches[ii],
      (uint8_t) strlen(batches[ii]));
    for (int pass = 0; (pass < 8) && (processMessageDone(sent) == false);
      pass++
    ) {
      serviceAndRead(consolePort);
    }
    processMessageRelease(sent);
  }
  mockClockAdvanceNs((CONSOLE_ESCAPE_DELAY_MS + 1) * 1000000ULL);
  serviceAndRead(consolePort);
}

NANO_OS_KERNEL_TEST(console_escape, uart_arrow_arriving_in_pieces_is_one_key) {
  deliverByteByByte("\x1b[Aecho ok\r");

  NANO_OS_ASSERT_EQ_INT(2, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("\x1b[A", deliveredLines[0]);
  NANO_OS_ASSERT_STR_EQ("echo ok\n", deliveredLines[1]);
}

NANO_OS_KERNEL_TEST(console_escape, forwarded_arrow_is_the_same_key) {
  const char *batches[] = { "\x1b[Aecho", " ok\r", NULL };
  deliverInBatches(batches);

  NANO_OS_ASSERT_EQ_INT(2, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("\x1b[A", deliveredLines[0]);
  NANO_OS_ASSERT_STR_EQ("echo ok\n", deliveredLines[1]);
}

NANO_OS_KERNEL_TEST(console_escape, lone_escape_waits_for_the_delay) {
  ConsolePort *consolePort = inputFixture(PROCESS_ID_NOT_SET);
  consolePort->readByte = uartLikeReadByte;
  readerFixture(consolePort);
  uartLikeBytes = "\x1bx";
  uartLikeAvailable = 1;
  uartLikeRead = 0;
  mockClockAdvanceNs(1000000000ULL);

  NANO_OS_ASSERT_TRUE(serviceAndRead(consolePort));
  mockClockAdvanceNs((CONSOLE_ESCAPE_DELAY_MS - 1) * 1000000ULL);
  NANO_OS_ASSERT_TRUE(serviceAndRead(consolePort));
  NANO_OS_ASSERT_EQ_INT(0, numDeliveredLines);

  mockClockAdvanceNs(2 * 1000000ULL);
  NANO_OS_ASSERT_FALSE(serviceAndRead(consolePort));
  NANO_OS_ASSERT_EQ_INT(1, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("\x1b", deliveredLines[0]);

  uartLikeAvailable = 2;
  serviceAndRead(consolePort);
  NANO_OS_ASSERT_EQ_INT(CONSOLE_INPUT_STATE_NORMAL, consolePort->inputState);
}

NANO_OS_KERNEL_TEST(console_escape, forwarded_lone_escape_matches_uart) {
  const char *batches[] = { "\x1b", NULL };
  deliverInBatches(batches);

  NANO_OS_ASSERT_EQ_INT(1, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("\x1b", deliveredLines[0]);
}

/// @fn static ConsolePort* rawFixture(const char *bytes)
///
/// @brief Make this process the reader of a UART-like port in raw mode that
/// has all of a string's bytes waiting to be read.
///
/// @param bytes The bytes waiting on the port.
///
/// @return Returns a pointer to the port.
static ConsolePort* rawFixture(const char *bytes) {
  ConsolePort *consolePort = inputFixture(PROCESS_ID_NOT_SET);
  consolePort->readByte = uartLikeReadByte;
  readerFixture(consolePort);
  consolePort->modes = 0;
  uartLikeBytes = bytes;
  uartLikeAvailable = strlen(bytes);
  uartLikeRead = 0;
  return consolePort;
}

NANO_OS_KERNEL_TEST(console_raw, each_key_is_delivered_by_itself) {
  ConsolePort *consolePort = rawFixture("ab\r");

  serviceAndRead(consolePort);
  NANO_OS_ASSERT_EQ_INT(1, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("a", deliveredLines[0]);
  NANO_OS_ASSERT_TRUE(deliveredRaw[0]);
  NANO_OS_ASSERT_EQ_INT(1, (long long) uartLikeRead);

  serviceAndRead(consolePort);
  serviceAndRead(consolePort);
  NANO_OS_ASSERT_EQ_INT(3, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("b", deliveredLines[1]);
  NANO_OS_ASSERT_STR_EQ("\r", deliveredLines[2]);
}

NANO_OS_KERNEL_TEST(console_raw, typed_ahead_keys_wait_for_the_reader) {
  ConsolePort *consolePort = rawFixture("xy");
  consolePort->waitingForInput = false;

  consoleServicePort(&inputState, consolePort);
  NANO_OS_ASSERT_TRUE(consolePort->consoleBuffer->holdsCompletedLine);
  NANO_OS_ASSERT_EQ_INT(1, (long long) uartLikeRead);

  consolePort->waitingForInput = true;
  serviceAndRead(consolePort);
  serviceAndRead(consolePort);
  NANO_OS_ASSERT_EQ_INT(2, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("x", deliveredLines[0]);
  NANO_OS_ASSERT_STR_EQ("y", deliveredLines[1]);
}

NANO_OS_KERNEL_TEST(console_raw, canonical_lines_are_not_marked_raw) {
  deliverByteByByte("ok\r");

  NANO_OS_ASSERT_EQ_INT(1, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("ok\n", deliveredLines[0]);
  NANO_OS_ASSERT_FALSE(deliveredRaw[0]);
}

NANO_OS_KERNEL_TEST(console_raw, releasing_the_port_restores_its_modes) {
  ConsolePort *consolePort = rawFixture("");
  consolePort->shell = consolePid;

  releasePid(getRunningPid());

  NANO_OS_ASSERT_EQ_INT(CONSOLE_MODE_DEFAULT, consolePort->modes);
}

NANO_OS_KERNEL_TEST(console_raw, keys_wait_while_the_buffer_is_lent_out) {
  ConsolePort *consolePort = rawFixture("x");
  consolePort->consoleBuffer->owner = getRunningPid();

  ConsoleBuffer *lent = getAvailableConsoleBuffer(&inputState, getRunningPid());
  NANO_OS_ASSERT_TRUE(lent == consolePort->consoleBuffer);
  strcpy(lent->buffer, "echo");
  consoleServicePort(&inputState, consolePort);
  NANO_OS_ASSERT_EQ_INT(0, (long long) uartLikeRead);
  NANO_OS_ASSERT_STR_EQ("echo", lent->buffer);

  releaseConsoleBuffer(&inputState, lent);
  serviceAndRead(consolePort);
  NANO_OS_ASSERT_EQ_INT(1, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("x", deliveredLines[0]);
}

/// @fn static void setModes(uint8_t modes)
///
/// @brief Hand the console's CONSOLE_SET_MODES handler a message from this
/// process.
///
/// @param modes The CONSOLE_MODE_* flags to set.
///
/// @return This function returns no value.
static void setModes(uint8_t modes) {
  static ConsoleModesArgs modesArgs;
  modesArgs.modes = modes;
  ProcessMessage *sent = getAvailableMessage();
  if (sent == NULL) {
    return;
  }
  processMessageInit(sent, CONSOLE_COMMAND_SIGNATURE | CONSOLE_SET_MODES,
    &modesArgs, sizeof(modesArgs), /* waiting= */ false);
  if (processMessageQueuePush(getRunningProcess(), sent) != processSuccess) {
    processMessageRelease(sent);
    return;
  }
  consoleSetModesCommandHandler(&inputState, processMessageQueuePop());
}

NANO_OS_KERNEL_TEST(console_raw, a_held_key_starts_the_next_canonical_line) {
  ConsolePort *consolePort = rawFixture("echo two\r");
  consolePort->outputOwner = getRunningPid();
  consolePort->waitingForInput = false;
  consoleServicePort(&inputState, consolePort);
  NANO_OS_ASSERT_EQ_INT(1, (long long) uartLikeRead);

  setModes(CONSOLE_MODE_DEFAULT & ~CONSOLE_MODE_ECHO);
  consolePort->waitingForInput = true;
  serviceAndRead(consolePort);
  NANO_OS_ASSERT_EQ_INT(1, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("echo two\n", deliveredLines[0]);
  NANO_OS_ASSERT_FALSE(deliveredRaw[0]);
}

NANO_OS_KERNEL_TEST(console_raw, a_held_line_feed_after_return_is_dropped) {
  ConsolePort *consolePort = rawFixture("\r\necho two\r");
  consolePort->outputOwner = getRunningPid();
  serviceAndRead(consolePort);
  NANO_OS_ASSERT_STR_EQ("\r", deliveredLines[0]);
  consolePort->waitingForInput = false;
  consoleServicePort(&inputState, consolePort);
  NANO_OS_ASSERT_EQ_INT(2, (long long) uartLikeRead);

  setModes(CONSOLE_MODE_DEFAULT & ~CONSOLE_MODE_ECHO);
  consolePort->waitingForInput = true;
  serviceAndRead(consolePort);
  NANO_OS_ASSERT_EQ_INT(2, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("echo two\n", deliveredLines[1]);
}

NANO_OS_KERNEL_TEST(console_raw, a_partial_line_is_delivered_on_going_raw) {
  ConsolePort *consolePort = rawFixture("ec");
  consolePort->modes = CONSOLE_MODE_DEFAULT & ~CONSOLE_MODE_ECHO;
  consolePort->outputOwner = getRunningPid();
  serviceAndRead(consolePort);
  NANO_OS_ASSERT_EQ_INT(0, numDeliveredLines);

  setModes(0);
  serviceAndRead(consolePort);
  NANO_OS_ASSERT_EQ_INT(1, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("ec", deliveredLines[0]);
  NANO_OS_ASSERT_TRUE(deliveredRaw[0]);
}

NANO_OS_KERNEL_TEST(console_read, a_process_keeps_its_own_storage) {
  static int value;
  NANO_OS_ASSERT_EQ_INT(processSuccess,
    setProcessStorage(FGETS_CONSOLE_BUFFER_KEY, &value));
  NANO_OS_ASSERT_TRUE(getProcessStorage(FGETS_CONSOLE_BUFFER_KEY) == &value);
  setProcessStorage(FGETS_CONSOLE_BUFFER_KEY, NULL);
}

/// @fn static ConsoleBuffer* heldInput(const char *text, bool rawInput)
///
/// @brief Make a ConsoleBuffer holding input as if the console had sent it to
/// this process and a read had left it unfinished.
///
/// @param text The input in the buffer.
/// @param rawInput Whether the input came from a port in raw mode.
///
/// @return Returns a pointer to the buffer.
static ConsoleBuffer* heldInput(const char *text, bool rawInput) {
  static ConsoleBuffer consoleBuffer;
  memset(&consoleBuffer, 0, sizeof(consoleBuffer));
  strcpy(consoleBuffer.buffer, text);
  consoleBuffer.rawInput = rawInput;
  setProcessStorage(FGETS_CONSOLE_BUFFER_KEY, &consoleBuffer);
  return &consoleBuffer;
}

NANO_OS_KERNEL_TEST(console_read, raw_input_that_does_not_fit_is_kept) {
  ConsoleBuffer *consoleBuffer = heldInput("echo two\n", true);
  char chunk[8];

  NANO_OS_ASSERT_EQ_INT(4, (long long) nanoOsFread(chunk, 1, 4, stdin));
  NANO_OS_ASSERT_TRUE(memcmp(chunk, "echo", 4) == 0);
  NANO_OS_ASSERT_TRUE(getProcessStorage(FGETS_CONSOLE_BUFFER_KEY)
    == consoleBuffer);

  memset(chunk, 'x', sizeof(chunk));
  NANO_OS_ASSERT_EQ_INT(5, (long long) nanoOsFread(chunk, 1, 4 + 4, stdin));
  NANO_OS_ASSERT_TRUE(memcmp(chunk, " two\nxxx", 8) == 0);
  NANO_OS_ASSERT_NULL(getProcessStorage(FGETS_CONSOLE_BUFFER_KEY));
}

NANO_OS_KERNEL_TEST(console_read, canonical_input_that_does_not_fit_is_dropped) {
  heldInput("echo two\n", false);
  char chunk[4];

  NANO_OS_ASSERT_EQ_INT(4, (long long) nanoOsFread(chunk, 1, 4, stdin));
  NANO_OS_ASSERT_TRUE(memcmp(chunk, "echo", 4) == 0);
  NANO_OS_ASSERT_NULL(getProcessStorage(FGETS_CONSOLE_BUFFER_KEY));
}

/// @fn static ProcessMessage* queueMessage(
///   ConsoleCommand command, void *data, size_t size)
///
/// @brief Push a waiting console message onto this process's own queue, as if
/// another process had sent it to the console.
///
/// @param command The ConsoleCommand of the message.
/// @param data The message's data.
/// @param size The size of the data.
///
/// @return Returns the message, which the caller must release.
static ProcessMessage* queueMessage(
  ConsoleCommand command, void *data, size_t size
) {
  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return NULL;
  }
  processMessageInit(message, CONSOLE_COMMAND_SIGNATURE | command,
    data, size, /* waiting= */ true);
  if (processMessageQueuePush(getRunningProcess(), message) != processSuccess) {
    processMessageRelease(message);
    return NULL;
  }
  return message;
}

NANO_OS_KERNEL_TEST(console_input, held_input_is_not_handled_twice) {
  ConsolePort *consolePort = inputFixture(getRunningPid());
  inputArgs[0].portId = TEST_FORWARDING_PORT_INDEX;
  inputArgs[0].numBytes = 2;
  memcpy(inputArgs[0].bytes, "ab", 2);
  static ConsoleGetNumPortsArgs numPortsArgs;
  ProcessMessage *input = queueMessage(
    CONSOLE_INPUT_READY, &inputArgs[0], sizeof(inputArgs[0]));
  ProcessMessage *other = queueMessage(
    CONSOLE_GET_NUM_PORTS, &numPortsArgs, sizeof(numPortsArgs));
  NANO_OS_ASSERT_NOT_NULL(input);
  NANO_OS_ASSERT_NOT_NULL(other);

  handleConsoleMessages(&inputState, false, NULL);

  NANO_OS_ASSERT_TRUE(processMessageDone(other));
  NANO_OS_ASSERT_EQ_PTR(input, consolePort->pendingInput);
  NANO_OS_ASSERT_FALSE(processMessageDone(input));
  NANO_OS_ASSERT_NULL(processMessageQueuePop());
  NANO_OS_ASSERT_EQ_INT('a', consolePort->readByte(consolePort));
  NANO_OS_ASSERT_EQ_INT('b', consolePort->readByte(consolePort));
  NANO_OS_ASSERT_TRUE(processMessageDone(input));
  processMessageRelease(input);
  processMessageRelease(other);
}

/// @def TEST_UNUSED_PID
///
/// @brief A process ID past the end of the process table, which the
/// scheduler refuses to signal, for input owners that must not be signaled.
#define TEST_UNUSED_PID ((ProcessId) 250)

static SchedulerSendSignalArgs signalArgs[CONSOLE_NUM_PORTS];
static int numInterruptEchoes;

/// @fn static int recordEcho(ConsolePort *consolePort, const char *string)
///
/// @brief Stand in for a port's consolePrintString, counting the "^C" echoes.
///
/// @param consolePort Unused.
/// @param string The string the console printed.
///
/// @return Always returns 0.
static int recordEcho(ConsolePort *consolePort, const char *string) {
  (void) consolePort;
  if (strcmp(string, "^C\n") == 0) {
    numInterruptEchoes++;
  }
  return 0;
}

/// @fn static ConsolePort* signalFixture(const char *bytes, uint8_t modes)
///
/// @brief Make a UART-like port, owned by a process that can't be signaled,
/// that has all of a string's bytes waiting to be read.
///
/// @param bytes The bytes waiting on the port.
/// @param modes The CONSOLE_MODE_* flags of the port.
///
/// @return Returns a pointer to the port.
static ConsolePort* signalFixture(const char *bytes, uint8_t modes) {
  ConsolePort *consolePort = inputFixture(PROCESS_ID_NOT_SET);
  consolePort->readByte = uartLikeReadByte;
  consolePort->consolePrintString = recordEcho;
  consolePort->modes = modes;
  consolePort->inputOwner = TEST_UNUSED_PID;
  memset(signalArgs, 0, sizeof(signalArgs));
  inputState.sendSignalArgs = signalArgs;
  numInterruptEchoes = 0;
  uartLikeBytes = bytes;
  uartLikeAvailable = strlen(bytes);
  uartLikeRead = 0;
  return consolePort;
}

NANO_OS_KERNEL_TEST(console_signal, ctrl_c_interrupts_while_a_line_is_held) {
  ConsolePort *consolePort = signalFixture("ls\r\x03",
    CONSOLE_MODE_DEFAULT & ~CONSOLE_MODE_ECHO);

  consoleServicePort(&inputState, consolePort);

  NANO_OS_ASSERT_EQ_INT(4, (long long) uartLikeRead);
  NANO_OS_ASSERT_EQ_INT(TEST_UNUSED_PID, signalArgs[0].pid);
  NANO_OS_ASSERT_EQ_INT(SIGINT, signalArgs[0].signal);
  NANO_OS_ASSERT_EQ_INT(1, numInterruptEchoes);
  NANO_OS_ASSERT_FALSE(consolePort->consoleBuffer->holdsCompletedLine);
}

NANO_OS_KERNEL_TEST(console_signal, ctrl_c_waits_for_a_lent_buffer_only) {
  ConsolePort *consolePort = signalFixture("\x03",
    CONSOLE_MODE_DEFAULT & ~CONSOLE_MODE_ECHO);
  consolePort->consoleBuffer->bufferAwaitingRelease = true;

  consoleServicePort(&inputState, consolePort);
  NANO_OS_ASSERT_EQ_INT(0, (long long) uartLikeRead);
  NANO_OS_ASSERT_EQ_INT(0, numInterruptEchoes);

  consolePort->consoleBuffer->bufferAwaitingRelease = false;
  consoleServicePort(&inputState, consolePort);
  NANO_OS_ASSERT_EQ_INT(1, (long long) uartLikeRead);
  NANO_OS_ASSERT_EQ_INT(SIGINT, signalArgs[0].signal);
  NANO_OS_ASSERT_EQ_INT(1, numInterruptEchoes);
}

NANO_OS_KERNEL_TEST(console_signal, raw_input_without_signals_keeps_ctrl_c) {
  ConsolePort *consolePort = signalFixture("\x03", 0);
  readerFixture(consolePort);
  consolePort->modes = 0;

  serviceAndRead(consolePort);

  NANO_OS_ASSERT_EQ_INT(0, numInterruptEchoes);
  NANO_OS_ASSERT_EQ_INT(0, signalArgs[0].signal);
  NANO_OS_ASSERT_EQ_INT(1, numDeliveredLines);
  NANO_OS_ASSERT_STR_EQ("\x03", deliveredLines[0]);
}

NANO_OS_KERNEL_TEST(console_signal, each_port_keeps_its_own_signal_args) {
  ConsolePort *first = signalFixture("\x03",
    CONSOLE_MODE_DEFAULT & ~CONSOLE_MODE_ECHO);
  inputState.numConsolePorts = 2;
  ConsolePort *second = &inputState.consolePorts[1];
  *second = *first;
  second->consoleBuffer = &inputState.consoleBuffers[1];
  second->inputOwner = TEST_UNUSED_PID + 1;

  consoleServicePort(&inputState, first);
  uartLikeAvailable = 1;
  uartLikeRead = 0;
  consoleServicePort(&inputState, second);

  // Both SIGINT messages are still unhandled, so neither's args may change.
  NANO_OS_ASSERT_EQ_INT(2, numInterruptEchoes);
  NANO_OS_ASSERT_EQ_INT(TEST_UNUSED_PID, signalArgs[0].pid);
  NANO_OS_ASSERT_EQ_INT(TEST_UNUSED_PID + 1, signalArgs[1].pid);
}
