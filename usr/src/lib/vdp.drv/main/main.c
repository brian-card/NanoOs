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

#include "ExecutiveProcesses.h"
#include "Console.h"
#include "VdpText.h"

/// @def VDP_UART
///
/// @brief The ID of the UART connected to the VDP.
#define VDP_UART 0

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

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;
  VdpTextState textState = { .column = 0 };

  vdpWrite(_vdpScreenSetup, sizeof(_vdpScreenSetup));
  while (1) {
    ProcessMessage *processMessage = processMessageQueueWait(NULL);
    if (processMessageType(processMessage)
      == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_WRITE_BUFFER)
    ) {
      vdpWriteText(&textState,
        (const char*) processMessageData(processMessage));
    }
    processMessageSetDone(processMessage);
    if (processMessageWaiting(processMessage) == false) {
      processMessageRelease(processMessage);
    }
  }

  return 0;
}
