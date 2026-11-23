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

#include "AgonKeycodes.h"
#include "ConsoleOutput.h"
#include "VdpDriver.h"

_Static_assert((KEYCODE_NUMLOCK == KEYCODE_SCROLLLOCK + 1)
  && (KEYCODE_CAPSLOCK == KEYCODE_SCROLLLOCK + 2),
  "vdpReadKeyboard takes the lock keys to be three keycodes in a row");

/// @def VDP_POLL_NS
///
/// @brief How long to wait for a message, in nanoseconds, while a message the
/// driver sent hasn't been marked done.  Its receiver doesn't notify the
/// driver when it marks one done.
#define VDP_POLL_NS 10000000L

/// @fn static void vdpWriteText(VdpOutput *vdpOutput, const char *text)
///
/// @brief Draw a string of console text on the VDP, or have the Console
/// overlay hold it if the VDP isn't listening yet or another process owns the
/// display.  Holding it there keeps the buffering, which happens only then,
/// out of the main overlay.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param text The NUL-terminated string to draw.
///
/// @return This function returns no value.
static void vdpWriteText(VdpOutput *vdpOutput, const char *text) {
  if (vdpOutput->heldText != NULL) {
    vdpOutput->textToHold = text;
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Console", "vdpHoldText",
      vdpOutput);
    return;
  }
  uint8_t output[4 * VDP_TEXT_MAX_EXPANSION];
  while ((*text != '\0') || (vdpOutput->textState.erasePending > 0)
    || (vdpOutput->textState.scrollDirection != 0)
  ) {
    size_t length = vdpTranslateText(
      &vdpOutput->textState, &text, output, sizeof(output));
    if (vdpOutput->listening == true) {
      vdpWrite(output, length);
    }
  }
}

/// @fn static void vdpGraphics(VdpOutput *vdpOutput, ProcessMessage *message)
///
/// @brief Have the Draw overlay carry out a message whose type's low byte is
/// that of GFX_MSG_SUBMIT or GFX_MSG_PREUPLOAD, and the Graphics overlay any
/// other, or check on a held GFX_MSG_FINISH if there is no message.  The
/// overlays check that the message is a GraphicsProtocol.h one, which keeps
/// the check out of the main overlay.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param message The message to carry out, or NULL.
///
/// @return This function returns no value.
static void vdpGraphics(VdpOutput *vdpOutput, ProcessMessage *message) {
  const char *overlay = "Graphics";
  if (message != NULL) {
    uint8_t type = (uint8_t) processMessageType(message);
    if ((type == GFX_MSG_SUBMIT) || (type == GFX_MSG_PREUPLOAD)) {
      overlay = "Draw";
    }
  }
  vdpOutput->graphicsMessage = message;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, overlay, "vdpGraphics",
    vdpOutput);
}

/// @fn static bool vdpReadKeyboard(VdpInput *input)
///
/// @brief Read everything the VDP has sent, adding the translation of each
/// key that went down to the keys waiting to be sent to the console, and
/// setting the keyboard's LEDs after each lock key.  A key that doesn't fit
/// is dropped.  While another process owns the display, keys and the mouse
/// are queued for it instead, except Ctrl+Alt+Backspace, which takes the
/// display back, and mouse packets are ignored while nobody does.
///
/// @param input A pointer to the driver's VdpInput.
///
/// @return Returns false if the UART reported that the VDP is gone, true
/// otherwise.
static bool vdpReadKeyboard(VdpInput *input) {
  int status = 0;
  while (((status = vdpReceive(input)) == 0)
    && (input->receivedLength > 0)
  ) {
    for (size_t ii = 0; ii < input->receivedLength; ii++) {
      if (vdpParseByte(&input->parser, input->received[ii]) == false) {
        continue;
      }
      if (vdpIsPollReply(&input->parser, input->pollCookie) == true) {
        input->pollAnswered = true;
        continue;
      }
      bool keyboard = (input->parser.command == VDP_PACKET_KEYBOARD);
      if ((keyboard == true) && (input->parser.data[3] == 0)
        && ((uint8_t) (input->parser.data[2] - KEYCODE_SCROLLLOCK) < 3)
      ) {
        callOverlayFunction(OVERLAY_SAME_NAMESPACE,
          "Console", "vdpLockKey", input);
      }
      if (input->events != NULL) {
        if ((keyboard == true) && (vdpIsReleaseKey(input->parser.data))) {
          callOverlayFunction(OVERLAY_SAME_NAMESPACE,
            "Graphics", "vdpRevoke", input->output);
        } else {
          vdpQueueEvent(input->events, &input->eventState, &input->parser,
            input->numLock);
        }
        continue;
      } else if (keyboard == false) {
        continue;
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
    input->receivedLength = 0;
  }

  return status != -EIO;
}

/// @fn static bool vdpCollect(ProcessMessage **message)
///
/// @brief Release a message the driver sent once its receiver has marked it
/// done.
///
/// @param message A pointer to the message, which is set to NULL when it's
///   released.
///
/// @return Returns true if the message was released, false otherwise.
static bool vdpCollect(ProcessMessage **message) {
  if ((*message == NULL) || (processMessageDone(*message) == false)) {
    return false;
  }
  processMessageRelease(*message);
  *message = NULL;
  return true;
}

/// @fn static bool vdpSend(ProcessId pid, int64_t type, void *args,
///   size_t size, ProcessMessage **sent)
///
/// @brief Send a message whose receiver marks it done when it has finished
/// with the arguments.
///
/// @param pid The receiver.
/// @param type The message's type.
/// @param args The message's arguments.
/// @param size The size of the arguments.
/// @param sent Set to the message if it was sent.
///
/// @return Returns true if the message was sent, false otherwise.
static bool vdpSend(ProcessId pid, int64_t type, void *args, size_t size,
  ProcessMessage **sent
) {
  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return false;
  }
  processMessageInit(message, type, args, size, /* waiting= */ true);
  if (sendProcessMessageToPid(pid, message) != processSuccess) {
    processMessageRelease(message);
    return false;
  }
  *sent = message;
  return true;
}

/// @fn static void vdpSendEvents(VdpInput *input)
///
/// @brief Send the display's owner the events waiting for it once it has
/// finished with the last ones, swapping the queue with the spare arguments.
///
/// @param input A pointer to the driver's VdpInput.
///
/// @return This function returns no value.
static void vdpSendEvents(VdpInput *input) {
  vdpCollect(&input->eventsMessage);
  GfxInputEventsArgs *events = input->events;
  if ((input->eventsMessage != NULL) || (events == NULL)
    || (events->count == 0)
  ) {
    return;
  }

  if (vdpSend(input->output->owner,
      GFX_COMMAND_SIGNATURE | GFX_MSG_INPUT_EVENTS, events, sizeof(*events),
      &input->eventsMessage) == false
  ) {
    return;
  }
  input->events = input->sentEvents;
  input->events->count = 0;
  input->sentEvents = events;
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
  if ((vdpCollect(&input->portMessage) == true)
    && (input->portArgs->returnValue == 0)
  ) {
    input->portId = input->portArgs->portId;
  }
  vdpCollect(&input->inputMessage);
  vdpCollect(&input->sizeMessage);
  if ((input->portId < 0) || (input->inputMessage != NULL)
    || (input->pendingLength == 0)
  ) {
    return;
  }

  uint8_t consumed = 0;
  input->inputArgs->portId = (unsigned char) input->portId;
  input->inputArgs->numBytes = 0;
  while ((consumed < input->pendingLength)
    && (input->inputArgs->numBytes + input->pending[consumed]
      <= CONSOLE_INPUT_READY_SIZE)
  ) {
    uint8_t keyLength = input->pending[consumed];
    memcpy(&input->inputArgs->bytes[input->inputArgs->numBytes],
      &input->pending[consumed + 1], keyLength);
    input->inputArgs->numBytes += keyLength;
    consumed += 1 + keyLength;
  }

  if (vdpSend(input->consolePid,
      CONSOLE_COMMAND_SIGNATURE | CONSOLE_INPUT_READY, input->inputArgs,
      sizeof(*input->inputArgs), &input->inputMessage) == false
  ) {
    return;
  }
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
    goto exit;
  }

  while (1) {
    if (vdpOutput.finishMessage != NULL) {
      vdpGraphics(&vdpOutput, NULL);
    }
    if (((vdpOutput.listening == false)
        || ((input.portId < 0) && (input.portMessage == NULL)))
      && (callOverlayFunction(OVERLAY_SAME_NAMESPACE,
        "Startup", "vdpSetUp", &startupArgs) != &startupArgs)
    ) {
      goto exit;
    }
    if (vdpOutput.replayText != NULL) {
      vdpWriteText(&vdpOutput, vdpOutput.replayText);
      free(vdpOutput.replayText);
      vdpOutput.replayText = NULL;
    }
    if ((input.portId >= 0)
      && ((vdpOutput.textState.resizeReportWanted == true)
        || ((vdpOutput.sizeChanged == true) && (input.sizeMessage == NULL)))
    ) {
      callOverlayFunction(OVERLAY_SAME_NAMESPACE,
        "Console", "vdpReportSize", &startupArgs);
    }
    vdpSendInput(&input);
    vdpSendEvents(&input);

    struct timespec *timeout = NULL;
    if (vdpOutput.nextPoll.tv_nsec != VDP_NEVER) {
      timeout = &vdpOutput.nextPoll;
    }
    struct timespec soon;
    if ((input.portMessage != NULL) || (input.inputMessage != NULL)
      || (input.pendingLength > 0) || (input.sizeMessage != NULL)
      || (input.eventsMessage != NULL)
    ) {
      vdpTimeFromNow(&soon, 0, VDP_POLL_NS);
      if ((timeout == NULL) || (vdpTimeBefore(&soon, timeout) == true)) {
        timeout = &soon;
      }
    }

    ProcessMessage *processMessage = processMessageQueueWait(timeout);
    if (processMessage == NULL) {
      continue;
    }
    const char *text = consoleOutputText(processMessage);
    bool vdpPresent = true;
    if (text != NULL) {
      vdpWriteText(&vdpOutput, text);
      // A burst of output can outlast the UART's receive ring, so keys are
      // read here as well as when the UART reports them.
      vdpPresent = vdpReadKeyboard(&input);
    } else if (processMessageType(processMessage)
      == (VDP_DRIVER_SIGNATURE | VDP_UART_INPUT)
    ) {
      vdpPresent = vdpReadKeyboard(&input);
    } else {
      vdpGraphics(&vdpOutput, processMessage);
      processMessage = vdpOutput.graphicsMessage;
    }
    if (processMessage != NULL) {
      consoleOutputDone(processMessage);
    }
    if (vdpPresent == false) {
      // Exiting lets the scheduler restart the driver from scratch.
      goto exit;
    }
  }

exit:
  callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    "Startup", "vdpShutdown", &startupArgs);
  return 1;
}
