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

/// @file Startup.c
///
/// @brief Startup of the Agon Light 2 VDP driver process, kept out of the
/// main overlay.

#include <stdlib.h>
#include <string.h>

#include "MouseCursor.h"
#include "ScreenSetup.h"
#include "VdpDriver.h"
#include "VdpFont.h"

/// @def VDP_SETUP_TIMEOUT_SECONDS
///
/// @brief How long, in seconds, to wait for the VDP to answer the poll that
/// follows the screen setup before writing to it anyway.  Resetting the mouse
/// can take the VDP about 5 seconds when no mouse is plugged in.
#define VDP_SETUP_TIMEOUT_SECONDS 8

/// @def VDP_AUDIO_RESET
///
/// @brief The bytes of VDU 23,0,&85,channel,10, which resets an audio channel
/// if it's enabled.
#define VDP_AUDIO_RESET(channel) 23, 0, 0x85, (channel), 10

/// @def VDP_AUDIO_RESET_8
///
/// @brief The bytes of VDP_AUDIO_RESET for eight channels starting at base.
#define VDP_AUDIO_RESET_8(base) \
  VDP_AUDIO_RESET((base) + 0), VDP_AUDIO_RESET((base) + 1), \
  VDP_AUDIO_RESET((base) + 2), VDP_AUDIO_RESET((base) + 3), \
  VDP_AUDIO_RESET((base) + 4), VDP_AUDIO_RESET((base) + 5), \
  VDP_AUDIO_RESET((base) + 6), VDP_AUDIO_RESET((base) + 7)

/// @var _vdpReset
///
/// @brief VDU commands that return the state an eZ80 reset leaves on the VDP
/// to its power-on defaults, sent before the screen setup: turn legacy modes
/// off (VDU 23,0,&C1,0) so that mode 0 is 640x480, control keys on
/// (VDU 23,0,&98,1), and console mode off (VDU 23,0,&FE,0); reset sprites
/// (VDU 23,27,16); clear every buffer, bitmap, mouse cursor, font, and sample
/// (VDU 23,0,&A0,65535;2); restore the system font (VDU 23,0,&91); reset the
/// copper's signal list (VDU 23,0,&C4,4); restore the default sample rate
/// (VDU 23,0,&85,255,13,16384;) and reset all 32 audio channels.  The
/// keyboard is reset by vdpSetKeyboardLeds and the mouse with the cursor.
static const uint8_t _vdpReset[] = {
  23, 0, 0xC1, 0,
  23, 0, 0x98, 1,
  23, 0, 0xFE, 0,
  23, 27, 16,
  23, 0, 0xA0, VDP_WORD(0xFFFF), 2,
  23, 0, 0x91,
  23, 0, 0xC4, 4,
  23, 0, 0x85, 255, 13, VDP_WORD(16384),
  VDP_AUDIO_RESET_8(0), VDP_AUDIO_RESET_8(8),
  VDP_AUDIO_RESET_8(16), VDP_AUDIO_RESET_8(24)
};

/// @var _vdpCursorUploadStart
///
/// @brief VDU commands that start the upload of the mouse cursor: clear the
/// buffer (VDU 23,0,&A0,id;2) so that a restarted driver's upload isn't
/// appended to the last one, then write the pixels to it
/// (VDU 23,0,&A0,id;0,length;), which follow as one RGBA2222 byte each.
static const uint8_t _vdpCursorUploadStart[] = {
  23, 0, 0xA0, VDP_WORD(VDP_CURSOR_ID), 2,
  23, 0, 0xA0, VDP_WORD(VDP_CURSOR_ID), 0,
  VDP_WORD(sizeof(cursorPixels))
};

/// @var _vdpCursorUploadEnd
///
/// @brief VDU commands sent after the mouse cursor's pixels: select the
/// buffer as the bitmap (VDU 23,27,&20,id;), make an RGBA2222 bitmap of it
/// (VDU 23,27,&21,width;height;1), make a mouse cursor of the bitmap with
/// its hotspot at the top left (VDU 23,27,&40,0,0), reset the mouse
/// (VDU 23,0,&89,2) so that one plugged in or unplugged since the last reset
/// is noticed, disable it (VDU 23,0,&89,1), enable it (VDU 23,0,&89,0), and
/// select the transparent cursor (VDU 23,0,&89,3,id;), because the pointer
/// shows only while a program owns the display.  The VDP only enables a mouse
/// its last reset detected, but an enable while the mouse is already enabled
/// shows the pointer without checking, so the disable comes between; it comes
/// after the reset because it suspends the mouse's port, which the enable
/// resumes.  The VDP ignores the cursor selection while the mouse is disabled.
static const uint8_t _vdpCursorUploadEnd[] = {
  23, 27, 0x20, VDP_WORD(VDP_CURSOR_ID),
  23, 27, 0x21, VDP_WORD(MOUSE_CURSOR_WIDTH), VDP_WORD(MOUSE_CURSOR_HEIGHT), 1,
  23, 27, 0x40, 0, 0,
  23, 0, 0x89, 2,
  23, 0, 0x89, 1,
  23, 0, 0x89, 0,
  23, 0, 0x89, 3, VDP_WORD(VDP_HIDDEN_CURSOR_ID)
};

/// @var _vdpHiddenCursor
///
/// @brief VDU commands that make the transparent cursor, which hides the
/// pointer while nobody owns the display and when an owner hides it:  clear
/// its buffer (VDU 23,0,&A0,id;2), write one transparent RGBA2222 pixel to it
/// (VDU 23,0,&A0,id;0,1;0), select it as the bitmap (VDU 23,27,&20,id;), make
/// a 1x1 RGBA2222 bitmap of it (VDU 23,27,&21,1;1;1), and make a mouse cursor
/// of the bitmap (VDU 23,27,&40,0,0).
static const uint8_t _vdpHiddenCursor[] = {
  23, 0, 0xA0, VDP_WORD(VDP_HIDDEN_CURSOR_ID), 2,
  23, 0, 0xA0, VDP_WORD(VDP_HIDDEN_CURSOR_ID), 0, VDP_WORD(1), 0,
  23, 27, 0x20, VDP_WORD(VDP_HIDDEN_CURSOR_ID),
  23, 27, 0x21, VDP_WORD(1), VDP_WORD(1), 1,
  23, 27, 0x40, 0, 0
};

/// @fn static void vdpDefineCursor(void)
///
/// @brief Upload the mouse cursor and the transparent one to the VDP, reset
/// and enable the mouse, and make the transparent cursor its pointer.
///
/// @return This function returns no value.
static void vdpDefineCursor(void) {
  vdpWrite(_vdpHiddenCursor, sizeof(_vdpHiddenCursor));
  vdpWrite(_vdpCursorUploadStart, sizeof(_vdpCursorUploadStart));
  vdpWrite(cursorPixels, sizeof(cursorPixels));
  vdpWrite(_vdpCursorUploadEnd, sizeof(_vdpCursorUploadEnd));
}

/// @var _vdpNumLockOn
///
/// @brief The argument that turns Num Lock on when the driver starts.
static const char _vdpNumLockOn[] = "--numlock=on";

/// @var _vdpNumLockOff
///
/// @brief The argument that leaves Num Lock off when the driver starts.
static const char _vdpNumLockOff[] = "--numlock=off";

/// @fn static void vdpBufferString(VdpOutputBuffer *buffer,
///   const char *string)
///
/// @brief Append a string to the console text drawn once the VDP is
/// listening, with any byte that isn't printable ASCII replaced so that it
/// can't start an escape sequence.
///
/// @param buffer A pointer to the output buffer, or NULL to drop the string.
/// @param string The NUL-terminated string to append.
///
/// @return This function returns no value.
static void vdpBufferString(VdpOutputBuffer *buffer, const char *string) {
  if (buffer == NULL) {
    return;
  }

  for (; (*string != '\0') && (buffer->length < VDP_OUTPUT_BUFFER_SIZE);
    string++
  ) {
    char c = *string;
    if (((c < ' ') || (c > '~')) && (c != '\r') && (c != '\n')) {
      c = '?';
    }
    buffer->data[buffer->length++] = (uint8_t) c;
  }
}

/// @fn static void vdpParseArguments(VdpInput *input, VdpOutput *vdpOutput,
///   char **argv)
///
/// @brief Apply the driver's arguments, warning on the driver's own screen
/// about any it doesn't recognize and ignoring them, so that a mistake in
/// config.sys can't take the console away.  The screen is the only place
/// the driver can report to:  its hardware grant keeps it off the other UARTs.
///
/// @param input A pointer to the driver's VdpInput.
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param argv The driver's NULL-terminated arguments.
///
/// @return This function returns no value.
static void vdpParseArguments(VdpInput *input, VdpOutput *vdpOutput,
  char **argv
) {
  if ((argv == NULL) || (argv[0] == NULL)) {
    return;
  }

  for (char **arg = &argv[1]; *arg != NULL; arg++) {
    if (strcmp(*arg, _vdpNumLockOn) == 0) {
      input->numLock = true;
    } else if (strcmp(*arg, _vdpNumLockOff) == 0) {
      input->numLock = false;
    } else {
      vdpBufferString(vdpOutput->heldText, argv[0]);
      vdpBufferString(vdpOutput->heldText,
        ": Ignoring unrecognized argument \"");
      vdpBufferString(vdpOutput->heldText, *arg);
      vdpBufferString(vdpOutput->heldText, "\"\n");
    }
  }
}

/// @fn static void* vdpCreateArgs(size_t size)
///
/// @brief Create the arguments of one of the messages to the console.  Kept
/// out of line because each inlined MESSAGE_ARGS_CREATE costs more than the
/// call.
///
/// @param size The size of the arguments.
///
/// @return Returns a pointer to the zeroed arguments on success, NULL on
/// failure.
static __attribute__((noinline)) void* vdpCreateArgs(size_t size) {
  return MESSAGE_ARGS_CREATE_BYTES(size, MESSAGE_ARGS_ROUND_TRIP);
}

/// @fn static void vdpDestroyArgs(ProcessMessage *message, void *args)
///
/// @brief Free the arguments of a message unless its receiver hasn't finished
/// with them, in which case the kernel reclaims them once it has.
///
/// @param message The message the arguments were sent with, or NULL.
/// @param args The arguments.
///
/// @return This function returns no value.
static void vdpDestroyArgs(ProcessMessage *message, void *args) {
  if ((message == NULL) || (processMessageDone(message) == true)) {
    MESSAGE_ARGS_DESTROY(args);
  }
}

/// @fn void* vdpShutdown(void *args)
///
/// @brief Free the arguments of the messages to the console and the display's
/// owner, and the buffer of received bytes, before the driver exits.
///
/// @param args A pointer to the driver's VdpStartupArgs.
///
/// @return Always returns the provided pointer.
void* vdpShutdown(void *args) {
  VdpInput *input = ((VdpStartupArgs*) args)->input;
  vdpDestroyArgs(input->portMessage, input->portArgs);
  vdpDestroyArgs(input->inputMessage, input->inputArgs);
  vdpDestroyArgs(input->sizeMessage, input->sizeArgs);
  vdpDestroyArgs(NULL, input->events);
  vdpDestroyArgs(input->eventsMessage, input->sentEvents);
  vdpDestroyArgs(input->orphanMessage, input->orphanEvents);
  free(input->received);
  input->received = NULL;
  return args;
}

/// @fn void* vdpStartup(void *args)
///
/// @brief Initialize the driver's state from its arguments and register for
/// the bytes the VDP sends.
///
/// @param args A pointer to the driver's VdpStartupArgs.
///
/// @return Returns the provided pointer on success, NULL if there was no
/// memory for the arguments of the messages to the console or the buffer of
/// received bytes.
void* vdpStartup(void *args) {
  VdpStartupArgs *startupArgs = (VdpStartupArgs*) args;
  VdpInput *input = startupArgs->input;
  VdpOutput *vdpOutput = startupArgs->output;

  memset(input, 0, sizeof(*input));
  input->portArgs = (ConsoleAddForwardingPortArgs*) vdpCreateArgs(
    sizeof(*input->portArgs));
  input->inputArgs = (ConsoleInputReadyArgs*) vdpCreateArgs(
    sizeof(*input->inputArgs));
  input->sizeArgs = (ConsoleWinsizeArgs*) vdpCreateArgs(
    sizeof(*input->sizeArgs));
  input->received = (uint8_t*) malloc(VDP_RECEIVED_SIZE);
  if ((input->portArgs == NULL) || (input->inputArgs == NULL)
    || (input->sizeArgs == NULL) || (input->received == NULL)
  ) {
    vdpShutdown(args);
    return NULL;
  }
  input->portId = -1;
  input->pollCookie = VDP_GENERAL_POLL_COOKIE;
  memset(vdpOutput, 0, sizeof(*vdpOutput));
  vdpOutput->owner = PROCESS_ID_NOT_SET;
  input->output = vdpOutput;
  vdpOutput->input = input;
  vdpOutput->textState.numColumns = VDP_SCREEN_WIDTH / VDP_SYSTEM_GLYPH_SIZE;
  vdpOutput->textState.numRows = VDP_SCREEN_HEIGHT / VDP_SYSTEM_GLYPH_SIZE;
  vdpOutput->sizeChanged = true;
  vdpTimeFromNow(&vdpOutput->giveUp, VDP_GENERAL_POLL_TIMEOUT_SECONDS, 0);
  vdpOutput->heldText = (VdpOutputBuffer*) malloc(sizeof(VdpOutputBuffer));
  if (vdpOutput->heldText != NULL) {
    vdpOutput->heldText->length = 0;
  }
  vdpParseArguments(input, vdpOutput, startupArgs->argv);

  getHal()->uart->registerCallback(VDP_UART, getRunningProcess(),
    VDP_DRIVER_SIGNATURE | VDP_UART_INPUT, NULL);

  return args;
}

/// @fn static void vdpSendScreenSetup(VdpInput *input, VdpOutput *vdpOutput)
///
/// @brief Reset the VDP and set up the screen and the mouse pointer, then have
/// the main overlay poll the VDP, holding back output until the VDP answers,
/// which it does only once it has finished the setup.  The main overlay sends
/// the poll because the replies to the setup overrun UART0's receive buffer
/// before the driver reads them, which a reply sent with the setup would be
/// lost in.
///
/// @param input A pointer to the driver's VdpInput.
/// @param vdpOutput A pointer to the driver's VdpOutput.
///
/// @return This function returns no value.
static void vdpSendScreenSetup(VdpInput *input, VdpOutput *vdpOutput) {
  vdpWrite(_vdpReset, sizeof(_vdpReset));
  vdpSetKeyboardLeds(input->numLock ? VDP_LED_NUM_LOCK : 0);
  vdpWrite(vdpScreenSetup, vdpScreenSetupSize);
  vdpDefineCursor();

  input->pollAnswered = false;
  input->pollCookie = VDP_SETUP_POLL_COOKIE;
  vdpOutput->screenSetUp = true;
  vdpTimeFromNow(&vdpOutput->nextPoll, 0, 0);
  vdpTimeFromNow(&vdpOutput->giveUp, VDP_SETUP_TIMEOUT_SECONDS, 0);
}

/// @fn static void vdpFinishSetup(VdpOutput *vdpOutput)
///
/// @brief Start drawing on the VDP once it has finished the screen setup, and
/// have the Console overlay turn the text held until then into the main
/// overlay's replay.  No poll is due until a
/// GFX_MSG_FINISH sends one.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
///
/// @return This function returns no value.
static void vdpFinishSetup(VdpOutput *vdpOutput) {
  vdpOutput->listening = true;
  vdpOutput->nextPoll.tv_nsec = VDP_NEVER;

  if (vdpOutput->heldText != NULL) {
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Console", "vdpHoldReplay",
      vdpOutput);
  }
}

/// @fn void* vdpSetupScreen(void *args)
///
/// @brief Called each time a poll the driver waits on is answered or times
/// out: the first time, send the screen setup, and the second, start drawing.
///
/// @param args A pointer to the driver's VdpStartupArgs.
///
/// @return Always returns the provided pointer.
void* vdpSetupScreen(void *args) {
  VdpStartupArgs *startupArgs = (VdpStartupArgs*) args;

  if (startupArgs->output->screenSetUp == false) {
    vdpSendScreenSetup(startupArgs->input, startupArgs->output);
  } else {
    vdpFinishSetup(startupArgs->output);
  }

  return args;
}

/// @def VDP_GENERAL_POLL_RETRY_NS
///
/// @brief How often, in nanoseconds, the general poll is resent until the VDP
/// answers.  A VDP that has just been powered on ignores everything for
/// several seconds.
#define VDP_GENERAL_POLL_RETRY_NS 500000000L

/// @fn static void vdpLoadBuiltInFont(VdpOutput *vdpOutput)
///
/// @brief Upload the driver's built-in font to slot 0 and select it for
/// console text with VDU 23,0,&95,0,id;flags, noting that the screen's rows
/// and columns, and a resize report if they're on, are to be sent.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
///
/// @return This function returns no value.
static void vdpLoadBuiltInFont(VdpOutput *vdpOutput) {
  GfxUploadFontArgs font = {
    .glyphW = VDP_FONT_WIDTH,
    .glyphH = VDP_FONT_HEIGHT,
    .firstChar = VDP_FONT_FIRST_CHAR,
    .numChars = VDP_FONT_NUM_CHARS,
    .size = sizeof(vdpFont),
    .bitmap = vdpFont,
  };
  if (vdpUploadFontSlot(vdpOutput, 0, &font) != GFX_OK) {
    return;
  }

  const uint8_t select[] = { 23, 0, 0x95, 0, VDP_WORD(VDP_FONT_ID_BASE), 0 };
  vdpWrite(select, sizeof(select));
  vdpOutput->textState.numColumns = VDP_SCREEN_WIDTH / VDP_FONT_WIDTH;
  vdpOutput->textState.numRows = VDP_SCREEN_HEIGHT / VDP_FONT_HEIGHT;
  vdpOutput->sizeChanged = true;
  vdpOutput->textState.resizeReportWanted = vdpOutput->textState.resizeReports;
}

/// @fn static void vdpClearOwnerBuffers(void)
///
/// @brief Delete the images and preuploads that a display owner of an
/// earlier run of the driver left on the VDP, with VDU 23,0,&A0,id;2.
///
/// @return This function returns no value.
static void vdpClearOwnerBuffers(void) {
  for (uint8_t slot = 0; slot < VDP_MAX_IMAGES; slot++) {
    const uint8_t clear[] = {
      23, 0, 0xA0, VDP_WORD(VDP_IMAGE_ID_BASE + slot), 2,
      23, 0, 0xA0, VDP_WORD(VDP_PRELOAD_ID_BASE + slot), 2
    };
    vdpWrite(clear, sizeof(clear));
  }
}

/// @fn static void vdpHandshake(VdpStartupArgs *startupArgs)
///
/// @brief Send the general poll until the VDP answers it or the wait times
/// out, then set up the screen, and again once the poll that follows the
/// setup, which is sent the same way, is answered or times out.  If it was
/// answered, an earlier owner's buffers are deleted and the built-in font is
/// loaded first:  until then the VDP may be busy resetting the mouse, and the
/// font overflows its receive buffer.
///
/// @param startupArgs A pointer to the driver's VdpStartupArgs.
///
/// @return This function returns no value.
static void vdpHandshake(VdpStartupArgs *startupArgs) {
  VdpOutput *vdpOutput = startupArgs->output;
  VdpInput *input = startupArgs->input;
  if (vdpOutput->listening == true) {
    return;
  }

  if ((input->pollAnswered == false)
    && (vdpTimeReached(&vdpOutput->giveUp) == false)
  ) {
    if (vdpTimeReached(&vdpOutput->nextPoll) == true) {
      // VDU 23,0,&80,cookie, which the VDP answers with the cookie.
      uint8_t poll[] = { 23, 0, 0x80, input->pollCookie };
      vdpWrite(poll, sizeof(poll));
      vdpTimeFromNow(&vdpOutput->nextPoll, 0, VDP_GENERAL_POLL_RETRY_NS);
    }
    return;
  }

  if ((vdpOutput->screenSetUp == true) && (input->pollAnswered == true)) {
    vdpClearOwnerBuffers();
    vdpLoadBuiltInFont(vdpOutput);
  }
  vdpSetupScreen(startupArgs);
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
  input->portArgs->forwardingPid = getRunningPid();
  input->portArgs->outputPid = PROCESS_ID_NOT_SET;
  input->portArgs->rows = 0;
  input->portArgs->columns = 0;
  input->portArgs->portId = 0;
  input->portArgs->returnValue = 0;
  processMessageInit(message,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_ADD_FORWARDING_PORT,
    input->portArgs, sizeof(*input->portArgs), /* waiting= */ true);
  if (sendProcessMessageToPid(input->consolePid, message) != processSuccess) {
    processMessageRelease(message);
    return;
  }
  input->portMessage = message;
}

/// @fn void* vdpSetUp(void *args)
///
/// @brief Advance the handshake with the VDP and ask the console for the
/// driver's port.  The main overlay calls this only until the VDP is drawing
/// and the port is known, which keeps both out of the main overlay.
///
/// @param args A pointer to the driver's VdpStartupArgs.
///
/// @return Always returns the provided pointer.
void* vdpSetUp(void *args) {
  vdpHandshake((VdpStartupArgs*) args);
  vdpRequestPort(((VdpStartupArgs*) args)->input);
  return args;
}
