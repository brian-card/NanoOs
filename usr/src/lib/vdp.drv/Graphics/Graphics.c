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
#include "Graphics.h"
#include "VdpFont.h"

/// @def VDP_FONT_ID_BASE
///
/// @brief The ID of the VDP buffer and font that hold the first font uploaded.
/// Each upload after it takes the next ID.
#define VDP_FONT_ID_BASE 0xF100

/// @def VDP_FONT_NUM_GLYPHS
///
/// @brief The number of glyphs a VDP font buffer must hold.
#define VDP_FONT_NUM_GLYPHS 256

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

/// @fn static GfxHandle vdpUploadFont(VdpOutput *vdpOutput,
///   const GfxFontDesc *desc)
///
/// @brief Upload a font to the next font ID with VDU 23,0,&A0,id;2 to clear
/// the buffer, VDU 23,0,&A0,id;0,length; to write all 256 glyphs to it, blank
/// for the characters the font doesn't cover, and VDU 23,0,&95,1,id;width,
/// height,ascent,flags to make a font of it.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param desc The font to upload.
///
/// @return Returns the font's handle on success, GFX_INVALID_HANDLE if the
/// font isn't one the VDP can hold or VDP_MAX_FONTS have been uploaded.
static GfxHandle vdpUploadFont(VdpOutput *vdpOutput,
  const GfxFontDesc *desc
) {
  size_t glyphSize = ((desc->glyphW + 7) / 8) * desc->glyphH;
  size_t fontSize = glyphSize * desc->numChars;
  size_t bufferSize = glyphSize * VDP_FONT_NUM_GLYPHS;
  if ((desc->bitmap == NULL) || (glyphSize == 0) || (desc->numChars == 0)
    || (desc->firstChar + desc->numChars > VDP_FONT_NUM_GLYPHS)
    || (desc->size < fontSize) || (bufferSize > 0xFFFF)
    || (vdpOutput->numFonts >= VDP_MAX_FONTS)
  ) {
    return GFX_INVALID_HANDLE;
  }

  vdpOutput->fontWidths[vdpOutput->numFonts] = desc->glyphW;
  vdpOutput->fontHeights[vdpOutput->numFonts] = desc->glyphH;
  uint16_t id = VDP_FONT_ID_BASE + vdpOutput->numFonts++;
  const uint8_t upload[] = {
    23, 0, 0xA0, VDP_WORD(id), 2,
    23, 0, 0xA0, VDP_WORD(id), 0, VDP_WORD(bufferSize)
  };
  vdpWrite(upload, sizeof(upload));
  vdpWriteZeros(glyphSize * desc->firstChar);
  vdpWrite((const uint8_t*) desc->bitmap, fontSize);
  vdpWriteZeros(bufferSize - fontSize - (glyphSize * desc->firstChar));
  const uint8_t define[] = {
    23, 0, 0x95, 1, VDP_WORD(id), desc->glyphW, desc->glyphH,
    desc->glyphH, 0
  };
  vdpWrite(define, sizeof(define));

  return (GfxHandle) (uintptr_t) id;
}

/// @fn static uint16_t vdpFontId(VdpOutput *vdpOutput, GfxHandle font)
///
/// @brief Get the ID of an uploaded font from its handle.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param font The handle vdpUploadFont returned for the font.
///
/// @return Returns the font's ID, or 0 if the handle isn't one vdpUploadFont
/// returned.
static uint16_t vdpFontId(VdpOutput *vdpOutput, GfxHandle font) {
  uintptr_t id = (uintptr_t) font;
  if ((id < VDP_FONT_ID_BASE)
    || (id - VDP_FONT_ID_BASE >= vdpOutput->numFonts)
  ) {
    return 0;
  }

  return (uint16_t) id;
}

/// @fn static GfxError vdpSetSystemFont(VdpOutput *vdpOutput, GfxHandle font)
///
/// @brief Select an uploaded font for console text with
/// VDU 23,0,&95,0,id;flags, and note that the screen's rows and columns, and
/// a resize report if they're on, are to be sent.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param font The handle vdpUploadFont returned for the font.
///
/// @return Returns GFX_OK on success, GFX_ERR_INVALID_HANDLE if the font
/// wasn't uploaded.
static GfxError vdpSetSystemFont(VdpOutput *vdpOutput, GfxHandle font) {
  uint16_t id = vdpFontId(vdpOutput, font);
  if (id == 0) {
    return GFX_ERR_INVALID_HANDLE;
  }

  const uint8_t select[] = { 23, 0, 0x95, 0, VDP_WORD(id), 0 };
  vdpWrite(select, sizeof(select));
  vdpOutput->textState.numColumns
    = VDP_SCREEN_WIDTH / vdpOutput->fontWidths[id - VDP_FONT_ID_BASE];
  vdpOutput->textState.numRows
    = VDP_SCREEN_HEIGHT / vdpOutput->fontHeights[id - VDP_FONT_ID_BASE];
  vdpOutput->sizeChanged = true;
  vdpOutput->textState.resizeReportWanted = vdpOutput->textState.resizeReports;
  return GFX_OK;
}

/// @fn static void vdpDestroyFont(VdpOutput *vdpOutput, GfxHandle font)
///
/// @brief Delete an uploaded font with VDU 23,0,&95,4,id; and its buffer with
/// VDU 23,0,&A0,id;2.  The VDP keeps drawing in a selected font until another
/// is selected.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param font The handle vdpUploadFont returned for the font.
///
/// @return This function returns no value.
static void vdpDestroyFont(VdpOutput *vdpOutput, GfxHandle font) {
  uint16_t id = vdpFontId(vdpOutput, font);
  if (id == 0) {
    return;
  }

  const uint8_t destroy[] = {
    23, 0, 0x95, 4, VDP_WORD(id),
    23, 0, 0xA0, VDP_WORD(id), 2
  };
  vdpWrite(destroy, sizeof(destroy));
}

/// @fn static void vdpHandleGraphics(VdpOutput *vdpOutput, uint8_t type,
///   void *payload)
///
/// @brief Carry out a Graphics.h message, writing its response over its
/// payload.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param type The GfxMsgType of the message.
/// @param payload The message's payload.
///
/// @return This function returns no value.
static void vdpHandleGraphics(VdpOutput *vdpOutput, uint8_t type,
  void *payload
) {
  if (type == GFX_MSG_UPLOAD_FONT) {
    GfxHandle handle
      = vdpUploadFont(vdpOutput, &((GfxMsgUploadFont*) payload)->desc);
    ((GfxMsgHandleResp*) payload)->handle = handle;
  } else if (type == GFX_MSG_SET_SYSTEM_FONT) {
    GfxError err = vdpSetSystemFont(vdpOutput,
      ((GfxMsgSetSystemFont*) payload)->handle);
    ((GfxMsgErrorResp*) payload)->err = err;
  } else if (type == GFX_MSG_DESTROY_FONT) {
    vdpDestroyFont(vdpOutput, ((GfxMsgDestroyFont*) payload)->handle);
    free(payload);
  }
}

/// @fn static void vdpLoadBuiltInFont(VdpOutput *vdpOutput)
///
/// @brief Upload the driver's built-in font and make it the system font with
/// the same messages a client would send.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
///
/// @return This function returns no value.
static void vdpLoadBuiltInFont(VdpOutput *vdpOutput) {
  union {
    GfxMsgUploadFont    upload;
    GfxMsgHandleResp    handle;
    GfxMsgSetSystemFont select;
  } message;
  message.upload.displayId = 0;
  message.upload.desc.glyphW = VDP_FONT_WIDTH;
  message.upload.desc.glyphH = VDP_FONT_HEIGHT;
  message.upload.desc.firstChar = VDP_FONT_FIRST_CHAR;
  message.upload.desc.numChars = VDP_FONT_NUM_CHARS;
  message.upload.desc.bitmap = vdpFont;
  message.upload.desc.size = sizeof(vdpFont);
  vdpHandleGraphics(vdpOutput, GFX_MSG_UPLOAD_FONT, &message);

  GfxHandle font = message.handle.handle;
  message.select.displayId = 0;
  message.select.handle = font;
  vdpHandleGraphics(vdpOutput, GFX_MSG_SET_SYSTEM_FONT, &message);
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
      input->sizeArgs.rows = (uint16_t) rows;
      input->sizeArgs.columns = (uint16_t) columns;
      input->sizeArgs.returnValue = 0;
      processMessageInit(message,
        CONSOLE_COMMAND_SIGNATURE | CONSOLE_SET_WINSIZE,
        &input->sizeArgs, sizeof(input->sizeArgs), /* waiting= */ true);
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
/// @brief Carry out the Graphics.h message in the driver's graphicsMessage,
/// ignoring any other message, or load the built-in font if there is none.
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
    vdpHandleGraphics(vdpOutput, (uint8_t) processMessageType(message),
      processMessageData(message));
  }

  return args;
}
