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

#include "VdpDriver.h"

/// @var _vdpScreenSetup
///
/// @brief VDU commands sent once the VDP is listening: select the 640x480,
/// 16-color screen mode (VDU 22,0), select the US keyboard layout
/// (VDU 23,0,&81,1), clear the screen (VDU 12), and turn the text cursor on
/// (VDU 23,1,1).
static const uint8_t _vdpScreenSetup[] = {
  22, 0, 23, 0, 0x81, 1, 12, 23, 1, 1
};

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
/// @brief Set up the screen once the VDP is listening and draw the output
/// buffered until then.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpSetupScreen(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;

  vdpOutput->listening = true;
  vdpWrite(_vdpScreenSetup, sizeof(_vdpScreenSetup));
  if (vdpOutput->buffer != NULL) {
    vdpWrite(vdpOutput->buffer->data, vdpOutput->buffer->length);
    free(vdpOutput->buffer);
    vdpOutput->buffer = NULL;
  }

  return args;
}
