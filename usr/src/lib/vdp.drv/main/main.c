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

/// @file main.c
///
/// @brief Entrypoint into the Agon Light 2 VDP driver process.

#include <string.h>

#include "ExecutiveProcesses.h"
#include "Console.h"
#include "VdpKeyboard.h"
#include "VdpText.h"

/// @def VDP_UART
///
/// @brief The ID of the UART connected to the VDP.
#define VDP_UART 0

/// @def VDP_DRIVER_SIGNATURE
///
/// @brief The 64-bit signature of the messages the VDP driver receives from
/// the HAL.  "\0\0VDPDRV" as a little-endian value.
#define VDP_DRIVER_SIGNATURE ((int64_t) 0x5652445044560000)

/// @def VDP_UART_INPUT
///
/// @brief The message the HAL sends when the VDP has sent bytes.
#define VDP_UART_INPUT 1

/// @def VDP_PENDING_SIZE
///
/// @brief The size of the buffer of keys waiting to be sent to the console.
#define VDP_PENDING_SIZE 32

/// @def VDP_POLL_NS
///
/// @brief How long to wait for a message, in nanoseconds, while a message the
/// driver sent hasn't been marked done.  The console doesn't notify the
/// driver when it marks one done.
#define VDP_POLL_NS 10000000L

/// @struct VdpInput
///
/// @brief State of the keyboard input the driver forwards to the console.
/// Kept on main's stack because the overlay's static data doesn't survive the
/// overlay window being given to another program.
///
/// @param consolePid The ProcessId of the console.
/// @param portId The index of the driver's console port, or -1 until the
///   console has said which it is.
/// @param portMessage The CONSOLE_ADD_FORWARDING_PORT message waiting on the
///   console, or NULL.
/// @param portArgs The arguments of portMessage.
/// @param inputMessage The CONSOLE_INPUT_READY message waiting on the
///   console, or NULL.
/// @param inputArgs The arguments of inputMessage.
/// @param parser The parser of the bytes the VDP sends.
/// @param pendingLength The number of bytes in pending.
/// @param pending The translations of the keys not yet sent to the console,
///   each preceded by its length so that no key is split between messages.
typedef struct VdpInput {
  ProcessId                    consolePid;
  int                          portId;
  ProcessMessage              *portMessage;
  ConsoleAddForwardingPortArgs portArgs;
  ProcessMessage              *inputMessage;
  ConsoleInputReadyArgs        inputArgs;
  VdpPacketParser              parser;
  uint8_t                      pendingLength;
  uint8_t                      pending[VDP_PENDING_SIZE];
} VdpInput;

/// @var _vdpScreenSetup
///
/// @brief VDU commands sent once at startup: clear the screen (VDU 12) and
/// turn the text cursor on (VDU 23,1,1).
static const uint8_t _vdpScreenSetup[] = { 12, 23, 1, 1 };

/// @fn static void vdpWrite(const uint8_t *data, size_t length)
///
/// @brief Write raw VDU bytes to the VDP.
///
/// @param data The bytes to write.
/// @param length The number of bytes in data.
///
/// @return This function returns no value.
static void vdpWrite(const uint8_t *data, size_t length) {
  getHal()->uart->write(VDP_UART, data, (ssize_t) length, NULL);
}

/// @fn static void vdpWriteText(VdpTextState *state, const char *text)
///
/// @brief Draw a string of console text on the VDP.
///
/// @param state A pointer to the VdpTextState of the console's text.
/// @param text The NUL-terminated string to draw.
///
/// @return This function returns no value.
static void vdpWriteText(VdpTextState *state, const char *text) {
  uint8_t output[4 * VDP_TEXT_MAX_EXPANSION];
  while (*text != '\0') {
    size_t length = vdpTranslateText(state, &text, output, sizeof(output));
    vdpWrite(output, length);
  }
}

/// @fn static void vdpRequestPort(VdpInput *input)
///
/// @brief Ask the console which port it forwards to this process, without
/// waiting for the answer: the console may be waiting on this process to draw
/// its output.
///
/// @param input A pointer to the driver's VdpInput.
///
/// @return This function returns no value.
static void vdpRequestPort(VdpInput *input) {
  if ((input->portId >= 0) || (input->portMessage != NULL)
    || (findProcessByName(CONSOLE_PROCESS_NAME, &input->consolePid) != 0)
  ) {
    return;
  }

  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return;
  }
  input->portArgs.forwardingPid = getRunningPid();
  input->portArgs.portId = 0;
  input->portArgs.returnValue = 0;
  processMessageInit(message,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_ADD_FORWARDING_PORT,
    &input->portArgs, sizeof(input->portArgs), /* waiting= */ true);
  if (sendProcessMessageToPid(input->consolePid, message) != processSuccess) {
    processMessageRelease(message);
    return;
  }
  input->portMessage = message;
}

/// @fn static void vdpReadKeyboard(VdpInput *input)
///
/// @brief Read everything the VDP has sent, adding the translation of each
/// key that went down to the keys waiting to be sent to the console.  A key
/// that doesn't fit is dropped.
///
/// @param input A pointer to the driver's VdpInput.
///
/// @return This function returns no value.
static void vdpReadKeyboard(VdpInput *input) {
  uint8_t bytes[16];
  ssize_t numBytes = 0;
  while ((getHal()->uart->read(VDP_UART, bytes, sizeof(bytes), &numBytes)
      == 0)
    && (numBytes > 0)
  ) {
    for (ssize_t ii = 0; ii < numBytes; ii++) {
      if ((vdpParseByte(&input->parser, bytes[ii]) == false)
        || (input->parser.command != VDP_PACKET_KEYBOARD)
        || (input->parser.length != 4)
      ) {
        continue;
      }

      uint8_t key[VDP_KEY_MAX_BYTES];
      size_t keyLength = vdpTranslateKey(input->parser.data, key);
      if ((keyLength == 0)
        || (input->pendingLength + 1 + keyLength > VDP_PENDING_SIZE)
      ) {
        continue;
      }
      input->pending[input->pendingLength++] = (uint8_t) keyLength;
      memcpy(&input->pending[input->pendingLength], key, keyLength);
      input->pendingLength += (uint8_t) keyLength;
    }
  }
}

/// @fn static void vdpSendInput(VdpInput *input)
///
/// @brief Collect the answers to the messages sent to the console, and send
/// it as many of the waiting keys as one CONSOLE_INPUT_READY holds once the
/// previous one is done.
///
/// @param input A pointer to the driver's VdpInput.
///
/// @return This function returns no value.
static void vdpSendInput(VdpInput *input) {
  if ((input->portMessage != NULL)
    && (processMessageDone(input->portMessage) == true)
  ) {
    if (input->portArgs.returnValue == 0) {
      input->portId = input->portArgs.portId;
    }
    processMessageRelease(input->portMessage);
    input->portMessage = NULL;
  }
  if ((input->inputMessage != NULL)
    && (processMessageDone(input->inputMessage) == true)
  ) {
    processMessageRelease(input->inputMessage);
    input->inputMessage = NULL;
  }
  if ((input->portId < 0) || (input->inputMessage != NULL)
    || (input->pendingLength == 0)
  ) {
    return;
  }

  uint8_t consumed = 0;
  input->inputArgs.portId = (unsigned char) input->portId;
  input->inputArgs.numBytes = 0;
  while ((consumed < input->pendingLength)
    && (input->inputArgs.numBytes + input->pending[consumed]
      <= CONSOLE_INPUT_READY_SIZE)
  ) {
    uint8_t keyLength = input->pending[consumed];
    memcpy(&input->inputArgs.bytes[input->inputArgs.numBytes],
      &input->pending[consumed + 1], keyLength);
    input->inputArgs.numBytes += keyLength;
    consumed += 1 + keyLength;
  }

  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return;
  }
  processMessageInit(message, CONSOLE_COMMAND_SIGNATURE | CONSOLE_INPUT_READY,
    &input->inputArgs, sizeof(input->inputArgs), /* waiting= */ true);
  if (sendProcessMessageToPid(input->consolePid, message) != processSuccess) {
    processMessageRelease(message);
    return;
  }
  input->inputMessage = message;
  input->pendingLength -= consumed;
  memmove(input->pending, &input->pending[consumed], input->pendingLength);
}

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;
  VdpTextState textState = { .column = 0 };
  VdpInput input;
  memset(&input, 0, sizeof(input));
  input.portId = -1;

  vdpWrite(_vdpScreenSetup, sizeof(_vdpScreenSetup));
  getHal()->uart->registerCallback(VDP_UART, getRunningProcess(),
    VDP_DRIVER_SIGNATURE | VDP_UART_INPUT, NULL);
  // Bytes that arrived before the callback was registered notified no one.
  vdpReadKeyboard(&input);

  while (1) {
    vdpRequestPort(&input);
    vdpSendInput(&input);

    struct timespec deadline;
    struct timespec *timeout = NULL;
    if ((input.portMessage != NULL) || (input.inputMessage != NULL)
      || (input.pendingLength > 0)
    ) {
      overlayMap.header.osApi->userApi->nanoOsTimespec_get(
        &deadline, TIME_UTC);
      deadline.tv_nsec += VDP_POLL_NS;
      if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
      }
      timeout = &deadline;
    }

    ProcessMessage *processMessage = processMessageQueueWait(timeout);
    if (processMessage == NULL) {
      continue;
    }
    int64_t type = processMessageType(processMessage);
    if (type == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_WRITE_BUFFER)) {
      vdpWriteText(&textState,
        (const char*) processMessageData(processMessage));
    } else if (type == (VDP_DRIVER_SIGNATURE | VDP_UART_INPUT)) {
      vdpReadKeyboard(&input);
    }
    processMessageSetDone(processMessage);
    if (processMessageWaiting(processMessage) == false) {
      processMessageRelease(processMessage);
    }
  }

  return 0;
}
