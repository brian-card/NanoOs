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

#include "ConsoleOutput.h"
#include "VdpDriver.h"

/// @def VDP_POLL_NS
///
/// @brief How long to wait for a message, in nanoseconds, while a message the
/// driver sent hasn't been marked done.  The console doesn't notify the
/// driver when it marks one done.
#define VDP_POLL_NS 10000000L

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
  while ((*text != '\0') || (vdpOutput->textState.erasePending > 0)) {
    size_t length = vdpTranslateText(
      &vdpOutput->textState, &text, output, sizeof(output));
    if (vdpOutput->listening == true) {
      vdpWrite(output, length);
    } else if (vdpOutput->buffer != NULL) {
      vdpBufferOutput(vdpOutput->buffer, output, length);
    }
  }
}

/// @fn static void vdpGraphics(VdpOutput *vdpOutput, ProcessMessage *message)
///
/// @brief Have the Graphics overlay carry out a message, or load the built-in
/// font if message is NULL.  The overlay checks that the message is a
/// Graphics.h one, which keeps the check out of the main overlay.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param message The message to carry out, or NULL.
///
/// @return This function returns no value.
static void vdpGraphics(VdpOutput *vdpOutput, ProcessMessage *message) {
  vdpOutput->graphicsMessage = message;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    "Graphics", "vdpGraphics", vdpOutput);
}

/// @fn static bool vdpReadKeyboard(VdpInput *input)
///
/// @brief Read everything the VDP has sent, adding the translation of each
/// key that went down to the keys waiting to be sent to the console, and
/// setting the keyboard's LEDs after each lock key.  A key that doesn't fit
/// is dropped.
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
      if (vdpIsPollReply(&input->parser, input->pollCookie) == true) {
        input->pollAnswered = true;
        continue;
      }
      if ((input->parser.command != VDP_PACKET_KEYBOARD)
        || (input->parser.length != 4)
      ) {
        continue;
      }

      uint8_t leds = 0;
      if (vdpUpdateLocks(input->parser.data, &input->numLock, &leds)) {
        vdpSetKeyboardLeds(leds);
      }
      uint8_t key[VDP_KEY_MAX_BYTES];
      size_t keyLength
        = vdpTranslateKey(input->parser.data, input->numLock, key);
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
  if ((input->sizeMessage != NULL)
    && (processMessageDone(input->sizeMessage) == true)
  ) {
    processMessageRelease(input->sizeMessage);
    input->sizeMessage = NULL;
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
  VdpInput input;
  VdpOutput vdpOutput;
  VdpStartupArgs startupArgs = {
    .input = &input,
    .output = &vdpOutput,
    .argv = argv,
  };
  if (callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    "Startup", "vdpStartup", &startupArgs) != &startupArgs
  ) {
    return 1;
  }

  // Bytes that arrived before the callback was registered notified no one.
  if (vdpReadKeyboard(&input) == false) {
    return 1;
  }

  while (1) {
    if (((vdpOutput.listening == false)
        || ((input.portId < 0) && (input.portMessage == NULL)))
      && (callOverlayFunction(OVERLAY_SAME_NAMESPACE,
        "Startup", "vdpSetUp", &startupArgs) != &startupArgs)
    ) {
      return 1;
    }
    if ((input.portId >= 0)
      && ((vdpOutput.textState.resizeReportWanted == true)
        || ((vdpOutput.sizeChanged == true) && (input.sizeMessage == NULL)))
    ) {
      callOverlayFunction(OVERLAY_SAME_NAMESPACE,
        "Graphics", "vdpReportSize", &startupArgs);
    }
    vdpSendInput(&input);

    int64_t deadlineNs = INT64_MAX;
    if ((input.portMessage != NULL) || (input.inputMessage != NULL)
      || (input.pendingLength > 0) || (input.sizeMessage != NULL)
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
    } else {
      vdpGraphics(&vdpOutput, processMessage);
    }
    consoleOutputDone(processMessage);
    if (vdpPresent == false) {
      // Exiting lets the scheduler restart the driver from scratch.
      return 1;
    }
  }

  return 0;
}
