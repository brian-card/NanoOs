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

/// @file Console.c
///
/// @brief The VDP driver's console work that doesn't belong in the main
/// overlay:  holding the console's text while the driver can't draw it, and
/// telling the console the screen's size.

#include <stdlib.h>
#include <string.h>

#include "VdpDriver.h"

/// @fn void* vdpHoldText(void *args)
///
/// @brief Add the main overlay's textToHold to the console text held while
/// the driver can't draw it.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpHoldText(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;
  vdpBufferOutput(vdpOutput->heldText, (const uint8_t*) vdpOutput->textToHold,
    strlen(vdpOutput->textToHold));
  return args;
}

/// @fn void* vdpHoldReplay(void *args)
///
/// @brief Turn the held console text into the string the main overlay draws
/// once it can, when the console has the display back or the VDP has started
/// listening.  If the held text was cut short, the replay starts after its
/// first newline, so that a cut escape sequence can't garble the screen.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpHoldReplay(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;
  VdpOutputBuffer *heldText = vdpOutput->heldText;
  uint16_t start = 0;
  if (heldText->length == VDP_OUTPUT_BUFFER_SIZE) {
    while ((start < heldText->length) && (heldText->data[start] != '\n')) {
      start++;
    }
    start++;
  }
  if (start < heldText->length) {
    vdpOutput->replayText = (char*) malloc(heldText->length - start + 1);
    if (vdpOutput->replayText != NULL) {
      memcpy(vdpOutput->replayText, &heldText->data[start],
        heldText->length - start);
      vdpOutput->replayText[heldText->length - start] = '\0';
    }
  }
  free(heldText);
  vdpOutput->heldText = NULL;
  return args;
}

/// @fn static char* vdpAppendNumber(char *text, unsigned int number)
///
/// @brief Write a number in decimal.
///
/// @param text Where to write the number.
/// @param number The number to write.
///
/// @return Returns a pointer just past the last digit written.
static char* vdpAppendNumber(char *text, unsigned int number) {
  char digits[5];
  int numDigits = 0;
  do {
    digits[numDigits++] = (char) ('0' + (number % 10));
    number /= 10;
  } while ((number > 0) && (numDigits < (int) sizeof(digits)));
  while (numDigits > 0) {
    *text++ = digits[--numDigits];
  }
  return text;
}

/// @fn void* vdpReportSize(void *args)
///
/// @brief Tell the console the screen's rows and columns once the font sets
/// them, and queue a resize report for the console as keys of up to
/// CONSOLE_INPUT_READY_SIZE bytes when one is wanted and there's room for it.
/// The main overlay calls this only when there is something to send, which
/// keeps it out of the main overlay.
///
/// @param args A pointer to the driver's VdpStartupArgs.
///
/// @return Always returns the provided pointer.
void* vdpReportSize(void *args) {
  VdpInput *input = ((VdpStartupArgs*) args)->input;
  VdpOutput *vdpOutput = ((VdpStartupArgs*) args)->output;
  unsigned int rows = vdpOutput->textState.numRows;
  unsigned int columns = vdpOutput->textState.numColumns;

  if ((vdpOutput->sizeChanged == true) && (input->sizeMessage == NULL)) {
    ProcessMessage *message = getAvailableMessage();
    if (message != NULL) {
      input->sizeArgs->rows = (uint16_t) rows;
      input->sizeArgs->columns = (uint16_t) columns;
      input->sizeArgs->returnValue = 0;
      processMessageInit(message,
        CONSOLE_COMMAND_SIGNATURE | CONSOLE_SET_WINSIZE,
        input->sizeArgs, sizeof(*input->sizeArgs), /* waiting= */ true);
      if (sendProcessMessageToPid(input->consolePid, message)
        == processSuccess
      ) {
        input->sizeMessage = message;
        vdpOutput->sizeChanged = false;
      } else {
        processMessageRelease(message);
      }
    }
  }

  if (vdpOutput->textState.resizeReportWanted == false) {
    return args;
  }
  char report[32] = "\033[48;";
  char *end = vdpAppendNumber(&report[5], rows);
  *end++ = ';';
  end = vdpAppendNumber(end, columns);
  *end++ = ';';
  end = vdpAppendNumber(end, VDP_SCREEN_HEIGHT);
  *end++ = ';';
  end = vdpAppendNumber(end, VDP_SCREEN_WIDTH);
  *end++ = 't';
  uint8_t length = (uint8_t) (end - report);
  uint8_t numChunks = (uint8_t) ((length + CONSOLE_INPUT_READY_SIZE - 1)
    / CONSOLE_INPUT_READY_SIZE);
  if (input->pendingLength + numChunks + length > VDP_PENDING_SIZE) {
    return args;
  }
  for (uint8_t start = 0; start < length; start += CONSOLE_INPUT_READY_SIZE) {
    uint8_t chunk = length - start;
    if (chunk > CONSOLE_INPUT_READY_SIZE) {
      chunk = CONSOLE_INPUT_READY_SIZE;
    }
    input->pending[input->pendingLength++] = chunk;
    memcpy(&input->pending[input->pendingLength], &report[start], chunk);
    input->pendingLength += chunk;
  }
  vdpOutput->textState.resizeReportWanted = false;
  return args;
}
