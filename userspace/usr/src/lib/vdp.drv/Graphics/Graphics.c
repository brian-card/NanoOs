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

/// @file Graphics.c
///
/// @brief Handling of the Graphics.h messages the Agon Light 2 VDP driver
/// supports, kept out of the main overlay.

#include <stdlib.h>
#include <string.h>

#include "VdpDriver.h"
#include "VdpFont.h"

/// @def VDP_ONE_WAY_MESSAGES
///
/// @brief The client message types whose arguments the driver destroys.
#define VDP_ONE_WAY_MESSAGES ( \
    (1UL << GFX_MSG_CLOSE_DISPLAY) | (1UL << GFX_MSG_DESTROY_IMAGE) \
  | (1UL << GFX_MSG_DESTROY_FONT) | (1UL << GFX_MSG_DISCARD_PREUPLOAD) \
  | (1UL << GFX_MSG_SHOW_CURSOR) | (1UL << GFX_MSG_WARP_CURSOR))

/// @var _vdpArgsSizes
///
/// @brief The size of each client message type's arguments, 0 for a type
/// that has none.
static const uint8_t _vdpArgsSizes[GFX_MSG_COUNT] = {
  [GFX_MSG_OPEN_DISPLAY]      = sizeof(GfxOpenDisplayArgs),
  [GFX_MSG_CLOSE_DISPLAY]     = 0,
  [GFX_MSG_GET_SURFACE_SIZE]  = sizeof(GfxSurfaceSizeArgs),
  [GFX_MSG_UPLOAD_IMAGE]      = sizeof(GfxUploadImageArgs),
  [GFX_MSG_DESTROY_IMAGE]     = sizeof(GfxHandleArgs),
  [GFX_MSG_GET_IMAGE_SIZE]    = sizeof(GfxImageSizeArgs),
  [GFX_MSG_UPLOAD_FONT]       = sizeof(GfxUploadFontArgs),
  [GFX_MSG_DESTROY_FONT]      = sizeof(GfxHandleArgs),
  [GFX_MSG_SET_DEFAULT_FONT]  = sizeof(GfxSetDefaultFontArgs),
  [GFX_MSG_SUBMIT]            = sizeof(GfxSubmitArgs),
  [GFX_MSG_PREUPLOAD]         = sizeof(GfxPreuploadArgs),
  [GFX_MSG_DISCARD_PREUPLOAD] = sizeof(GfxHandleArgs),
  [GFX_MSG_FINISH]            = sizeof(GfxFinishArgs),
  [GFX_MSG_CAPTURE_RECT]      = sizeof(GfxCaptureRectArgs),
  [GFX_MSG_GET_ERROR]         = sizeof(GfxGetErrorArgs),
  [GFX_MSG_SET_CURSOR]        = sizeof(GfxSetCursorArgs),
  [GFX_MSG_SHOW_CURSOR]       = sizeof(GfxShowCursorArgs),
  [GFX_MSG_WARP_CURSOR]       = sizeof(GfxWarpCursorArgs),
};

/// @fn static uint8_t vdpFontSlot(VdpOutput *vdpOutput, GfxHandle font)
///
/// @brief Get the slot of a client's font from its handle.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param font The handle vdpUploadFont returned for the font.
///
/// @return Returns the font's slot, or 0 if the handle isn't that of a client
/// font that is still uploaded.
static uint8_t vdpFontSlot(VdpOutput *vdpOutput, GfxHandle font) {
  uint16_t slot = (uint16_t) (font - VDP_FONT_ID_BASE);
  if ((font < VDP_FONT_ID_BASE) || (slot == 0) || (slot >= VDP_MAX_FONTS)
    || ((vdpOutput->fontsUsed & (1 << slot)) == 0)
  ) {
    return 0;
  }

  return (uint8_t) slot;
}

/// @fn static uint8_t vdpUploadFont(VdpOutput *vdpOutput,
///   GfxUploadFontArgs *args)
///
/// @brief Upload a client's font to the first free client font slot.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param args The message's arguments, whose handle is set on success.
///
/// @return Returns GFX_OK on success, GFX_ERR_OUT_OF_HANDLES if every client
/// slot is used, or the result of vdpUploadFontSlot.
static uint8_t vdpUploadFont(VdpOutput *vdpOutput, GfxUploadFontArgs *args) {
  uint8_t slot = 1;
  while ((slot < VDP_MAX_FONTS)
    && ((vdpOutput->fontsUsed & (1 << slot)) != 0)
  ) {
    slot++;
  }
  if (slot >= VDP_MAX_FONTS) {
    return GFX_ERR_OUT_OF_HANDLES;
  }

  uint8_t status = vdpUploadFontSlot(vdpOutput, slot, args);
  if (status == GFX_OK) {
    args->handle = (GfxHandle) (VDP_FONT_ID_BASE + slot);
  }
  return status;
}

/// @fn static uint8_t vdpSetDefaultFont(VdpOutput *vdpOutput,
///   GfxHandle font)
///
/// @brief Make a client's font, or the built-in one, the font that records
/// draw in unless they select another.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param font The font's handle, or GFX_INVALID_HANDLE for the built-in one.
///
/// @return Returns GFX_OK on success, GFX_ERR_INVALID_HANDLE if the font isn't
/// uploaded.
static uint8_t vdpSetDefaultFont(VdpOutput *vdpOutput, GfxHandle font) {
  uint8_t slot = 0;
  if (font != GFX_INVALID_HANDLE) {
    slot = vdpFontSlot(vdpOutput, font);
    if (slot == 0) {
      return GFX_ERR_INVALID_HANDLE;
    }
  }

  vdpOutput->ownerDefaultFont = slot;
  return GFX_OK;
}

/// @fn static void vdpDestroyFont(VdpOutput *vdpOutput, GfxHandle font)
///
/// @brief Delete a client's font with VDU 23,0,&95,4,id; and its buffer with
/// VDU 23,0,&A0,id;2, and free its slot.  The VDP keeps drawing in a selected
/// font until another is selected.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param font The handle vdpUploadFont returned for the font.
///
/// @return This function returns no value.
static void vdpDestroyFont(VdpOutput *vdpOutput, GfxHandle font) {
  uint8_t slot = vdpFontSlot(vdpOutput, font);
  if (slot == 0) {
    return;
  }

  const uint8_t destroy[] = {
    23, 0, 0x95, 4, VDP_WORD(font),
    23, 0, 0xA0, VDP_WORD(font), 2
  };
  vdpWrite(destroy, sizeof(destroy));
  vdpOutput->fontsUsed &= (uint8_t) ~(1 << slot);
  if (vdpOutput->ownerDefaultFont == slot) {
    vdpOutput->ownerDefaultFont = 0;
  }
}

/// @var _vdpEnterGraphics
///
/// @brief VDU commands that give the display to its owner:  select context 1,
/// which the VDP makes as a copy of the console's context 0
/// (VDU 23,0,&C8,0,1), use pixel coordinates (VDU 23,0,&C0,0), turn the text
/// cursor off (VDU 23,1,0), reset the viewports (VDU 26) and the graphics
/// origin (VDU 29,0;0;), and clear the screen (VDU 16).
static const uint8_t _vdpEnterGraphics[] = {
  23, 0, 0xC8, 0, 1,
  23, 0, 0xC0, 0,
  23, 1, 0,
  26,
  29, 0, 0, 0, 0,
  16
};

/// @var _vdpLeaveGraphics
///
/// @brief VDU commands that give the display back to the console:  select
/// its context 0 (VDU 23,0,&C8,0,0), delete the owner's context 1
/// (VDU 23,0,&C8,1,1), and clear the screen (VDU 12).
static const uint8_t _vdpLeaveGraphics[] = {
  23, 0, 0xC8, 0, 0,
  23, 0, 0xC8, 1, 1,
  12
};

/// @fn static void vdpFreeFonts(VdpOutput *vdpOutput)
///
/// @brief Destroy every font a display owner uploaded.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
///
/// @return This function returns no value.
static void vdpFreeFonts(VdpOutput *vdpOutput) {
  for (uint8_t slot = 1; slot < VDP_MAX_FONTS; slot++) {
    vdpDestroyFont(vdpOutput, (GfxHandle) (VDP_FONT_ID_BASE + slot));
  }
}

/// @var _vdpOwnerFrees
///
/// @brief The functions that free each kind of resource a display owner can
/// create, all of which run when ownership ends.
static void (*const _vdpOwnerFrees[])(VdpOutput *vdpOutput) = {
  vdpFreeFonts,
};

/// @fn static void vdpHoldReplay(VdpOutput *vdpOutput)
///
/// @brief Turn the console text held while the display was owned into the
/// string the main overlay draws once the console has the display back.  If
/// the held text was cut short, the replay starts after its first newline, so
/// that a cut escape sequence can't garble the screen.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
///
/// @return This function returns no value.
static void vdpHoldReplay(VdpOutput *vdpOutput) {
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
}

/// @fn static void vdpRelease(VdpOutput *vdpOutput, bool revoke)
///
/// @brief End the display's ownership:  free everything the owner created,
/// give the screen back to the console's context, and have the console's
/// held text redrawn from the top of a cleared screen.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param revoke Whether to tell the owner with GFX_MSG_DISPLAY_REVOKED,
///   which it gets only when it didn't ask to give the display up.
///
/// @return This function returns no value.
static void vdpRelease(VdpOutput *vdpOutput, bool revoke) {
  for (uint8_t ii = 0;
    ii < sizeof(_vdpOwnerFrees) / sizeof(_vdpOwnerFrees[0]); ii++
  ) {
    _vdpOwnerFrees[ii](vdpOutput);
  }
  vdpOutput->ownerDefaultFont = 0;
  vdpWrite(_vdpLeaveGraphics, sizeof(_vdpLeaveGraphics));
  vdpOutput->textState.row = 0;
  vdpOutput->textState.column = 0;
  vdpOutput->textState.escapeState = VDP_ESCAPE_NONE;
  vdpOutput->textState.erasePending = 0;
  vdpOutput->textState.scrollDirection = 0;
  vdpHoldReplay(vdpOutput);

  if (revoke == true) {
    ProcessMessage *message = getAvailableMessage();
    if (message != NULL) {
      processMessageInit(message,
        GFX_COMMAND_SIGNATURE | GFX_MSG_DISPLAY_REVOKED,
        NULL, 0, /* waiting= */ false);
      if (sendProcessMessageToPid(vdpOutput->owner, message)
        != processSuccess
      ) {
        processMessageRelease(message);
      }
    }
  }
  vdpOutput->owner = PROCESS_ID_NOT_SET;
  vdpOutput->ownerDescriptor = NULL;
  vdpOutput->error = GFX_OK;
}

/// @fn static uint8_t vdpOpenDisplay(VdpOutput *vdpOutput,
///   ProcessDescriptor *sender, GfxOpenDisplayArgs *args)
///
/// @brief Give the display to a process if nobody owns it, and answer with
/// the display's size and the built-in font's metrics.  Opening a display the
/// sender already owns changes nothing.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param sender The ProcessDescriptor of the process asking.
/// @param args The message's arguments.
///
/// @return Returns GFX_OK on success, GFX_ERR_DISPLAY_BUSY if another process
/// owns the display, or GFX_ERR_OUT_OF_MEMORY if there was no memory to hold
/// the console's text.
static uint8_t vdpOpenDisplay(VdpOutput *vdpOutput,
  ProcessDescriptor *sender, GfxOpenDisplayArgs *args
) {
  if (vdpOutput->owner == PROCESS_ID_NOT_SET) {
    vdpOutput->heldText = (VdpOutputBuffer*) malloc(sizeof(VdpOutputBuffer));
    if (vdpOutput->heldText == NULL) {
      return GFX_ERR_OUT_OF_MEMORY;
    }
    vdpOutput->heldText->length = 0;
    vdpOutput->owner = processPid(sender);
    vdpOutput->ownerDescriptor = sender;
    vdpWrite(_vdpEnterGraphics, sizeof(_vdpEnterGraphics));
  } else if (vdpOutput->owner != processPid(sender)) {
    return GFX_ERR_DISPLAY_BUSY;
  }

  args->width = VDP_SCREEN_WIDTH;
  args->height = VDP_SCREEN_HEIGHT;
  args->defaultFontW = VDP_FONT_WIDTH;
  args->defaultFontH = VDP_FONT_HEIGHT;
  args->defaultFontFirst = VDP_FONT_FIRST_CHAR;
  args->defaultFontCount = VDP_FONT_NUM_CHARS;
  return GFX_OK;
}

/// @fn void* vdpRevoke(void *args)
///
/// @brief Take the display back from its owner for the console, telling the
/// owner, as Ctrl+Alt+Backspace does.  The main overlay calls this so that
/// the release stays out of it.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpRevoke(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;
  if (vdpOutput->owner != PROCESS_ID_NOT_SET) {
    vdpRelease(vdpOutput, true);
  }
  return args;
}

/// @fn static void vdpHandleGraphics(VdpOutput *vdpOutput,
///   ProcessMessage *message, uint8_t type)
///
/// @brief Answer a client's GraphicsProtocol.h message:  a synchronous one
/// gets a status, GFX_ERR_INVALID_STATE if its arguments are the wrong size,
/// GFX_ERR_NOT_OWNER if its sender doesn't own the display, and
/// GFX_ERR_NOT_SUPPORTED if the driver doesn't implement it, and a one-way
/// one has its arguments destroyed.  A message whose waiting flag doesn't
/// match its type is left untouched.  An owner that is no longer a running
/// graphics client gives the display up, in case its GFX_MSG_CLIENT_ENDED
/// was lost.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param message The message.
/// @param type Its GfxMsgType, below GFX_MSG_COUNT.
///
/// @return This function returns no value.
static void vdpHandleGraphics(VdpOutput *vdpOutput, ProcessMessage *message,
  uint8_t type
) {
  void *payload = processMessageData(message);
  bool oneWay = ((VDP_ONE_WAY_MESSAGES >> type) & 1) != 0;
  if (oneWay == processMessageWaiting(message)) {
    return;
  }
  ProcessDescriptor *sender = processMessageFrom(message);
  if ((vdpOutput->owner != PROCESS_ID_NOT_SET)
    && (vdpOutput->owner != processPid(sender))
    && ((processRunning(vdpOutput->ownerDescriptor) == false)
      || (vdpOutput->ownerDescriptor->graphicsClient == false))
  ) {
    vdpRelease(vdpOutput, false);
  }

  uint8_t status = GFX_ERR_NOT_SUPPORTED;
  if ((processMessageSize(message) != _vdpArgsSizes[type])
    || ((_vdpArgsSizes[type] > 0) && (payload == NULL))
  ) {
    status = GFX_ERR_INVALID_STATE;
  } else if (type == GFX_MSG_OPEN_DISPLAY) {
    status = vdpOpenDisplay(vdpOutput, sender, (GfxOpenDisplayArgs*) payload);
  } else if (type == GFX_MSG_GET_SURFACE_SIZE) {
    ((GfxSurfaceSizeArgs*) payload)->width = VDP_SCREEN_WIDTH;
    ((GfxSurfaceSizeArgs*) payload)->height = VDP_SCREEN_HEIGHT;
    status = GFX_OK;
  } else if (vdpOutput->owner != processPid(sender)) {
    status = GFX_ERR_NOT_OWNER;
  } else if (type == GFX_MSG_CLOSE_DISPLAY) {
    vdpRelease(vdpOutput, false);
  } else if (type == GFX_MSG_GET_ERROR) {
    ((GfxGetErrorArgs*) payload)->error = vdpOutput->error;
    vdpOutput->error = GFX_OK;
    status = GFX_OK;
  } else if (type == GFX_MSG_UPLOAD_FONT) {
    status = vdpUploadFont(vdpOutput, (GfxUploadFontArgs*) payload);
  } else if (type == GFX_MSG_SET_DEFAULT_FONT) {
    status = vdpSetDefaultFont(vdpOutput,
      ((GfxSetDefaultFontArgs*) payload)->handle);
  } else if (type == GFX_MSG_DESTROY_FONT) {
    vdpDestroyFont(vdpOutput, ((GfxHandleArgs*) payload)->handle);
  }

  if (oneWay == true) {
    if (payload != NULL) {
      MESSAGE_ARGS_DESTROY(payload);
    }
  } else if ((payload != NULL) && (processMessageSize(message) > 0)) {
    *((uint8_t*) payload) = status;
  }
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

/// @fn void* vdpGraphics(void *args)
///
/// @brief Answer the GraphicsProtocol.h message in the driver's
/// graphicsMessage, ignoring any other message.  GFX_MSG_CLIENT_ENDED carries
/// a ProcessId, not storage, and ends the display's ownership if that process
/// owns it.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpGraphics(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;
  ProcessMessage *message = vdpOutput->graphicsMessage;

  if ((processMessageType(message) & ~((int64_t) 0xff))
    == GFX_COMMAND_SIGNATURE
  ) {
    uint8_t type = (uint8_t) processMessageType(message);
    if (type < GFX_MSG_COUNT) {
      vdpHandleGraphics(vdpOutput, message, type);
    } else if ((type == GFX_MSG_CLIENT_ENDED)
      && ((ProcessId) (uintptr_t) processMessageData(message)
        == vdpOutput->owner)
    ) {
      vdpRelease(vdpOutput, false);
    }
  }

  return args;
}
