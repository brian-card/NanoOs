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

/// @def VDP_FONT_ID_BASE
///
/// @brief The ID of the VDP buffer and font of font slot 0, the built-in
/// font.  Each slot after it takes the next ID, which is also the slot's
/// GfxHandle.
#define VDP_FONT_ID_BASE 0xF100

/// @def VDP_FONT_NUM_GLYPHS
///
/// @brief The number of glyphs a VDP font buffer must hold.
#define VDP_FONT_NUM_GLYPHS 256

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

/// @var _vdpZeros
///
/// @brief Blank glyph rows, written for the characters a font has no glyphs
/// for.
static const uint8_t _vdpZeros[32] = { 0 };

/// @fn static void vdpWriteZeros(size_t length)
///
/// @brief Write zero bytes to the VDP.
///
/// @param length The number of zero bytes to write.
///
/// @return This function returns no value.
static void vdpWriteZeros(size_t length) {
  while (length > 0) {
    size_t chunk
      = (length < sizeof(_vdpZeros)) ? length : sizeof(_vdpZeros);
    vdpWrite(_vdpZeros, chunk);
    length -= chunk;
  }
}

/// @fn static uint8_t vdpUploadFontSlot(VdpOutput *vdpOutput, uint8_t slot,
///   const GfxUploadFontArgs *font)
///
/// @brief Upload a font to a font slot with VDU 23,0,&A0,id;2 to clear the
/// buffer, VDU 23,0,&A0,id;0,length; to write all 256 glyphs to it, blank for
/// the characters the font doesn't cover, and VDU 23,0,&95,1,id;width,height,
/// ascent,flags to make a font of it, then mark the slot used.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param slot The font slot.
/// @param font The font, whose bitmap is readable by the driver.
///
/// @return Returns GFX_OK on success, GFX_ERR_NOT_SUPPORTED if the font isn't
/// one the VDP can hold.
static uint8_t vdpUploadFontSlot(VdpOutput *vdpOutput, uint8_t slot,
  const GfxUploadFontArgs *font
) {
  size_t glyphSize = ((font->glyphW + 7) / 8) * font->glyphH;
  size_t fontSize = glyphSize * font->numChars;
  size_t bufferSize = glyphSize * VDP_FONT_NUM_GLYPHS;
  if ((font->bitmap == NULL) || (glyphSize == 0) || (font->numChars == 0)
    || (font->firstChar + font->numChars > VDP_FONT_NUM_GLYPHS)
    || (font->size < fontSize) || (bufferSize > 0xFFFF)
  ) {
    return GFX_ERR_NOT_SUPPORTED;
  }

  vdpOutput->fontWidths[slot] = font->glyphW;
  vdpOutput->fontHeights[slot] = font->glyphH;
  vdpOutput->fontsUsed |= (uint8_t) (1 << slot);
  uint16_t fontId = VDP_FONT_ID_BASE + slot;
  const uint8_t upload[] = {
    23, 0, 0xA0, VDP_WORD(fontId), 2,
    23, 0, 0xA0, VDP_WORD(fontId), 0, VDP_WORD(bufferSize)
  };
  vdpWrite(upload, sizeof(upload));
  vdpWriteZeros(glyphSize * font->firstChar);
  vdpWrite((const uint8_t*) font->bitmap, fontSize);
  vdpWriteZeros(bufferSize - fontSize - (glyphSize * font->firstChar));
  const uint8_t define[] = {
    23, 0, 0x95, 1, VDP_WORD(fontId), font->glyphW, font->glyphH,
    font->glyphH, 0
  };
  vdpWrite(define, sizeof(define));
  return GFX_OK;
}

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

/// @fn static void vdpHandleGraphics(VdpOutput *vdpOutput,
///   ProcessMessage *message, uint8_t type)
///
/// @brief Answer a client's GraphicsProtocol.h message:  a synchronous one
/// gets a status, GFX_ERR_INVALID_STATE if its arguments are the wrong size
/// and GFX_ERR_NOT_SUPPORTED if the driver doesn't implement it, and a
/// one-way one has its arguments destroyed.  A message whose waiting flag
/// doesn't match its type is left untouched.
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

  uint8_t status = GFX_ERR_NOT_SUPPORTED;
  if ((processMessageSize(message) != _vdpArgsSizes[type])
    || ((_vdpArgsSizes[type] > 0) && (payload == NULL))
  ) {
    status = GFX_ERR_INVALID_STATE;
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
/// graphicsMessage, ignoring any other message, or load the built-in font if
/// there is none.  GFX_MSG_CLIENT_ENDED carries a ProcessId, not storage, and
/// needs only to be marked done.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpGraphics(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;
  ProcessMessage *message = vdpOutput->graphicsMessage;

  if (message == NULL) {
    vdpLoadBuiltInFont(vdpOutput);
  } else if ((processMessageType(message) & ~((int64_t) 0xff))
    == GFX_COMMAND_SIGNATURE
  ) {
    uint8_t type = (uint8_t) processMessageType(message);
    if (type < GFX_MSG_COUNT) {
      vdpHandleGraphics(vdpOutput, message, type);
    }
  }

  return args;
}
