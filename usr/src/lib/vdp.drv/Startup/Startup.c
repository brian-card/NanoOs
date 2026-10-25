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

/// @def VDP_CURSOR_ID
///
/// @brief The ID of the VDP buffer, bitmap, and mouse cursor that hold the
/// driver's mouse cursor.  IDs up to 255 name the VDP's built-in cursors.
#define VDP_CURSOR_ID 0xF000

/// @def VDP_SETUP_TIMEOUT_NS
///
/// @brief How long, in nanoseconds, to wait for the VDP to answer the poll
/// that follows the screen setup before writing to it anyway.  Resetting the
/// mouse can take the VDP about 5 seconds when no mouse is plugged in.
#define VDP_SETUP_TIMEOUT_NS 8000000000LL

/// @def VDP_WORD
///
/// @brief The two bytes of a 16-bit VDU argument, low byte first.
#define VDP_WORD(value) ((value) & 0xff), (((value) >> 8) & 0xff)

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
/// (VDU 23,0,&85,255,13,16384;) and reset all 32 audio channels; and restore
/// the default keyboard repeat delay and rate with the LEDs off
/// (VDU 23,0,&88,500;100;0).  The mouse is reset with the cursor.
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
  VDP_AUDIO_RESET_8(16), VDP_AUDIO_RESET_8(24),
  23, 0, 0x88, VDP_WORD(500), VDP_WORD(100), 0
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
/// select the cursor (VDU 23,0,&89,3,id;).  The VDP only enables a mouse its
/// last reset detected, but an enable while the mouse is already enabled
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
  23, 0, 0x89, 3, VDP_WORD(VDP_CURSOR_ID)
};

/// @fn static void vdpDefineCursor(void)
///
/// @brief Upload the mouse cursor to the VDP, reset and enable the mouse, and
/// make the cursor its pointer.
///
/// @return This function returns no value.
static void vdpDefineCursor(void) {
  vdpWrite(_vdpCursorUploadStart, sizeof(_vdpCursorUploadStart));
  vdpWrite(cursorPixels, sizeof(cursorPixels));
  vdpWrite(_vdpCursorUploadEnd, sizeof(_vdpCursorUploadEnd));
}

/// @fn void* vdpStartup(void *args)
///
/// @brief Initialize the driver's state and register for the bytes the VDP
/// sends.
///
/// @param args A pointer to the driver's VdpStartupArgs.
///
/// @return Always returns the provided pointer.
void* vdpStartup(void *args) {
  VdpStartupArgs *startupArgs = (VdpStartupArgs*) args;
  VdpInput *input = startupArgs->input;
  VdpOutput *vdpOutput = startupArgs->output;

  memset(input, 0, sizeof(*input));
  input->portId = -1;
  input->pollCookie = VDP_GENERAL_POLL_COOKIE;
  memset(vdpOutput, 0, sizeof(*vdpOutput));
  vdpOutput->giveUpNs = vdpNowNs() + VDP_GENERAL_POLL_TIMEOUT_NS;
  vdpOutput->buffer = (VdpOutputBuffer*) malloc(sizeof(VdpOutputBuffer));
  if (vdpOutput->buffer != NULL) {
    vdpOutput->buffer->length = 0;
  }

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
  vdpWrite(vdpScreenSetup, vdpScreenSetupSize);
  vdpDefineCursor();

  input->pollAnswered = false;
  input->pollCookie = VDP_SETUP_POLL_COOKIE;
  vdpOutput->screenSetUp = true;
  vdpOutput->nextPollNs = vdpNowNs();
  vdpOutput->giveUpNs = vdpOutput->nextPollNs + VDP_SETUP_TIMEOUT_NS;
}

/// @fn static void vdpFinishSetup(VdpOutput *vdpOutput)
///
/// @brief Start drawing on the VDP once it has finished the screen setup, and
/// draw the output buffered until then.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
///
/// @return This function returns no value.
static void vdpFinishSetup(VdpOutput *vdpOutput) {
  vdpOutput->listening = true;
  if (vdpOutput->buffer != NULL) {
    vdpWrite(vdpOutput->buffer->data, vdpOutput->buffer->length);
    free(vdpOutput->buffer);
    vdpOutput->buffer = NULL;
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
