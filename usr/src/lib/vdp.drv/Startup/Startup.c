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

/// @def VDP_WORD
///
/// @brief The two bytes of a 16-bit VDU argument, low byte first.
#define VDP_WORD(value) ((value) & 0xff), (((value) >> 8) & 0xff)

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
/// its hotspot at the top left (VDU 23,27,&40,0,0), enable the mouse
/// (VDU 23,0,&89,0), and select the cursor (VDU 23,0,&89,3,id;).  The cursor
/// is selected after the mouse is enabled because the VDP ignores the
/// selection while the mouse is disabled.
static const uint8_t _vdpCursorUploadEnd[] = {
  23, 27, 0x20, VDP_WORD(VDP_CURSOR_ID),
  23, 27, 0x21, VDP_WORD(MOUSE_CURSOR_WIDTH), VDP_WORD(MOUSE_CURSOR_HEIGHT), 1,
  23, 27, 0x40, 0, 0,
  23, 0, 0x89, 0,
  23, 0, 0x89, 3, VDP_WORD(VDP_CURSOR_ID)
};

/// @fn static void vdpDefineCursor(void)
///
/// @brief Upload the mouse cursor to the VDP, enable the mouse, and make the
/// cursor its pointer.
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

/// @fn void* vdpSetupScreen(void *args)
///
/// @brief Set up the screen and the mouse pointer once the VDP is listening
/// and draw the output buffered until then.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpSetupScreen(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;

  vdpOutput->listening = true;
  vdpWrite(vdpScreenSetup, vdpScreenSetupSize);
  vdpDefineCursor();
  if (vdpOutput->buffer != NULL) {
    vdpWrite(vdpOutput->buffer->data, vdpOutput->buffer->length);
    free(vdpOutput->buffer);
    vdpOutput->buffer = NULL;
  }

  return args;
}
