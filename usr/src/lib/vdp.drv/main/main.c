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

#include <stdlib.h>
#include <string.h>

#include "ExecutiveProcesses.h"
#include "Console.h"
#include "ConsoleOutput.h"
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

/// @def VDP_GENERAL_POLL_RETRY_NS
///
/// @brief How often, in nanoseconds, the general poll is resent until the VDP
/// answers.  A VDP that has just been powered on ignores everything for
/// several seconds.
#define VDP_GENERAL_POLL_RETRY_NS 500000000LL

/// @def VDP_GENERAL_POLL_TIMEOUT_NS
///
/// @brief How long, in nanoseconds, to wait for the VDP to answer the general
/// poll before writing to it anyway.
#define VDP_GENERAL_POLL_TIMEOUT_NS 15000000000LL

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
/// @param pollAnswered Whether the VDP has replied to the general poll.
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
  bool                         pollAnswered;
} VdpInput;

/// @struct VdpOutput
///
/// @brief State of the output the driver draws on the VDP.  Kept on main's
/// stack for the same reason as VdpInput.
///
/// @param textState The state of the translation of the console's text.
/// @param listening Whether the VDP is drawing what the driver writes.  The
///   VDP ignores everything until it has answered a general poll.
/// @param nextPollNs When to send the next general poll, in nanoseconds.
/// @param giveUpNs When to stop waiting for the VDP to answer, in nanoseconds.
/// @param buffer The output written while the VDP wasn't listening, or NULL.
typedef struct VdpOutput {
  VdpTextState     textState;
  bool             listening;
  int64_t          nextPollNs;
  int64_t          giveUpNs;
  VdpOutputBuffer *buffer;
} VdpOutput;

/// @var _vdpScreenSetup
///
/// @brief VDU commands sent once the VDP is listening: select the 640x480,
/// 16-color screen mode (VDU 22,0), select the US keyboard layout
/// (VDU 23,0,&81,1), clear the screen (VDU 12), and turn the text cursor on
/// (VDU 23,1,1).
static const uint8_t _vdpScreenSetup[] = {
  22, 0, 23, 0, 0x81, 1, 12, 23, 1, 1
};

/// @var _vdpGeneralPoll
///
/// @brief The general poll (VDU 23,0,&80,cookie), which the VDP answers with
/// a packet carrying the cookie.
static const uint8_t _vdpGeneralPoll[] = {
  23, 0, 0x80, VDP_GENERAL_POLL_COOKIE
};

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

/// @fn static int64_t vdpNowNs(void)
///
/// @brief Get the current time in nanoseconds.
///
/// @return Returns the current time in nanoseconds.
static int64_t vdpNowNs(void) {
  struct timespec now;
  overlayMap.header.osApi->userApi->nanoOsTimespec_get(&now, TIME_UTC);
  return (((int64_t) now.tv_sec) * 1000000000LL) + now.tv_nsec;
}

/// @fn static void vdpWriteText(VdpOutput *vdpOutput, const char *text)
///
/// @brief Draw a string of console text on the VDP, or buffer it if the VDP
/// isn't listening yet.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param text The NUL-terminated string to draw.
///
/// @return This function returns no value.
static void vdpWriteText(VdpOutput *vdpOutput, const char *text) {
  uint8_t output[4 * VDP_TEXT_MAX_EXPANSION];
  while (*text != '\0') {
    size_t length = vdpTranslateText(
      &vdpOutput->textState, &text, output, sizeof(output));
    if (vdpOutput->listening == true) {
      vdpWrite(output, length);
    } else if (vdpOutput->buffer != NULL) {
      vdpBufferOutput(vdpOutput->buffer, output, length);
    }
  }
}

/// @fn static void vdpHandshake(VdpOutput *vdpOutput, VdpInput *input)
///
/// @brief Send the general poll until the VDP answers it or the wait times
/// out, then set up the screen and draw the buffered output.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param input A pointer to the driver's VdpInput.
///
/// @return This function returns no value.
static void vdpHandshake(VdpOutput *vdpOutput, VdpInput *input) {
  if (vdpOutput->listening == true) {
    return;
  }

  int64_t now = vdpNowNs();
  if ((input->pollAnswered == false) && (now < vdpOutput->giveUpNs)) {
    if (now >= vdpOutput->nextPollNs) {
      vdpWrite(_vdpGeneralPoll, sizeof(_vdpGeneralPoll));
      vdpOutput->nextPollNs = now + VDP_GENERAL_POLL_RETRY_NS;
    }
    return;
  }

  vdpOutput->listening = true;
  vdpWrite(_vdpScreenSetup, sizeof(_vdpScreenSetup));
  if (vdpOutput->buffer != NULL) {
    vdpWrite(vdpOutput->buffer->data, vdpOutput->buffer->length);
    free(vdpOutput->buffer);
    vdpOutput->buffer = NULL;
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

/// @fn static bool vdpReadKeyboard(VdpInput *input)
///
/// @brief Read everything the VDP has sent, adding the translation of each
/// key that went down to the keys waiting to be sent to the console.  A key
/// that doesn't fit is dropped.
///
/// @param input A pointer to the driver's VdpInput.
///
/// @return Returns false if the UART reported that the VDP is gone, true
/// otherwise.
static bool vdpReadKeyboard(VdpInput *input) {
  uint8_t bytes[16];
  ssize_t numBytes = 0;
  int status = 0;
  while (((status = getHal()->uart->read(VDP_UART, bytes, sizeof(bytes),
        &numBytes)) == 0)
    && (numBytes > 0)
  ) {
    for (ssize_t ii = 0; ii < numBytes; ii++) {
      if (vdpParseByte(&input->parser, bytes[ii]) == false) {
        continue;
      }
      if (vdpIsPollReply(&input->parser) == true) {
        input->pollAnswered = true;
        continue;
      }
      if ((input->parser.command != VDP_PACKET_KEYBOARD)
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

  return status != -EIO;
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
  VdpInput input;
  memset(&input, 0, sizeof(input));
  input.portId = -1;
  VdpOutput vdpOutput;
  memset(&vdpOutput, 0, sizeof(vdpOutput));
  vdpOutput.giveUpNs = vdpNowNs() + VDP_GENERAL_POLL_TIMEOUT_NS;
  vdpOutput.buffer = (VdpOutputBuffer*) malloc(sizeof(VdpOutputBuffer));
  if (vdpOutput.buffer != NULL) {
    vdpOutput.buffer->length = 0;
  }

  getHal()->uart->registerCallback(VDP_UART, getRunningProcess(),
    VDP_DRIVER_SIGNATURE | VDP_UART_INPUT, NULL);
  // Bytes that arrived before the callback was registered notified no one.
  if (vdpReadKeyboard(&input) == false) {
    return 1;
  }

  while (1) {
    vdpHandshake(&vdpOutput, &input);
    vdpRequestPort(&input);
    vdpSendInput(&input);

    int64_t deadlineNs = INT64_MAX;
    if ((input.portMessage != NULL) || (input.inputMessage != NULL)
      || (input.pendingLength > 0)
    ) {
      deadlineNs = vdpNowNs() + VDP_POLL_NS;
    }
    if ((vdpOutput.listening == false)
      && (vdpOutput.nextPollNs < deadlineNs)
    ) {
      deadlineNs = vdpOutput.nextPollNs;
    }
    struct timespec deadline;
    struct timespec *timeout = NULL;
    if (deadlineNs != INT64_MAX) {
      deadline.tv_sec = (time_t) (deadlineNs / 1000000000LL);
      deadline.tv_nsec = (long) (deadlineNs % 1000000000LL);
      timeout = &deadline;
    }

    ProcessMessage *processMessage = processMessageQueueWait(timeout);
    if (processMessage == NULL) {
      continue;
    }
    const char *text = consoleOutputText(processMessage);
    bool vdpPresent = true;
    if (text != NULL) {
      vdpWriteText(&vdpOutput, text);
    } else if (processMessageType(processMessage)
      == (VDP_DRIVER_SIGNATURE | VDP_UART_INPUT)
    ) {
      vdpPresent = vdpReadKeyboard(&input);
    }
    consoleOutputDone(processMessage);
    if (vdpPresent == false) {
      // Exiting lets the scheduler restart the driver from scratch.
      return 1;
    }
  }

  return 0;
}
