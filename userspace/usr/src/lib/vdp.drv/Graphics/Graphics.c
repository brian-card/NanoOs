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
  if ((vdpOutput->draw != NULL) && (vdpOutput->draw->pen.font == slot)) {
    vdpOutput->draw->pen.font = vdpOutput->ownerDefaultFont;
  }
}

/// @fn static void vdpClearBuffer(GfxHandle id)
///
/// @brief Delete a VDP buffer, and the bitmap made from it, with
/// VDU 23,0,&A0,id;2.
///
/// @param id The buffer's ID.
///
/// @return This function returns no value.
static void vdpClearBuffer(GfxHandle id) {
  const uint8_t clear[] = { 23, 0, 0xA0, VDP_WORD(id), 2 };
  vdpWrite(clear, sizeof(clear));
}

/// @fn static void vdpSelectCursor(GfxHandle id)
///
/// @brief Make a mouse cursor the pointer with VDU 23,0,&89,3,id;.
///
/// @param id The cursor's ID.
///
/// @return This function returns no value.
static void vdpSelectCursor(GfxHandle id) {
  const uint8_t select[] = { 23, 0, 0x89, 3, VDP_WORD(id) };
  vdpWrite(select, sizeof(select));
}

/// @fn static void vdpShowCursor(VdpDrawState *draw)
///
/// @brief Make the owner's cursor the pointer, or the transparent one if the
/// owner has hidden it.
///
/// @param draw The owner's drawing state.
///
/// @return This function returns no value.
static void vdpShowCursor(VdpDrawState *draw) {
  if (draw->cursorHidden == true) {
    vdpSelectCursor(VDP_HIDDEN_CURSOR_ID);
  } else {
    vdpSelectCursor(draw->cursor);
  }
}

/// @fn static void vdpDestroyImage(VdpOutput *vdpOutput, GfxHandle image)
///
/// @brief Delete an image and free its slot.  The VDP deletes a cursor made
/// from the image along with it, so an image that was the cursor gives way to
/// the driver's.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param image The image's handle.
///
/// @return This function returns no value.
static void vdpDestroyImage(VdpOutput *vdpOutput, GfxHandle image) {
  VdpDrawState *draw = vdpOutput->draw;
  uint8_t slot = vdpSlot(draw->imagesUsed, image, VDP_IMAGE_ID_BASE,
    VDP_MAX_IMAGES);
  if (slot < VDP_MAX_IMAGES) {
    vdpClearBuffer(image);
    draw->imagesUsed ^= (uint16_t) (1 << slot);
    if (draw->cursor == image) {
      draw->cursor = VDP_CURSOR_ID;
      vdpShowCursor(draw);
    }
  }
}

/// @fn static void vdpDiscardPreload(VdpOutput *vdpOutput, GfxHandle preload)
///
/// @brief Delete a preuploaded command buffer and free its slot.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param preload The preload's handle.
///
/// @return This function returns no value.
static void vdpDiscardPreload(VdpOutput *vdpOutput, GfxHandle preload) {
  VdpDrawState *draw = vdpOutput->draw;
  uint8_t slot = vdpSlot(draw->preloadsUsed, preload, VDP_PRELOAD_ID_BASE,
    VDP_MAX_PRELOADS);
  if (slot < VDP_MAX_PRELOADS) {
    vdpClearBuffer(preload);
    draw->preloadsUsed ^= (uint16_t) (1 << slot);
  }
}

/// @fn static uint8_t vdpSetCursor(VdpDrawState *draw,
///   const GfxSetCursorArgs *args)
///
/// @brief Make one of the owner's images, or the driver's cursor, the mouse
/// cursor:  select the image's bitmap (VDU 23,27,&20,id;), make a cursor of it
/// with the hotspot (VDU 23,27,&40,hotX,hotY), and make that the pointer
/// unless the owner has hidden it.
///
/// @param draw The owner's drawing state.
/// @param args The message's arguments.
///
/// @return Returns GFX_OK on success, GFX_ERR_INVALID_HANDLE if the image
/// isn't one of the owner's.
static uint8_t vdpSetCursor(VdpDrawState *draw, const GfxSetCursorArgs *args) {
  GfxHandle cursor = args->image;
  if (cursor == GFX_INVALID_HANDLE) {
    cursor = VDP_CURSOR_ID;
  } else if (vdpSlot(draw->imagesUsed, cursor, VDP_IMAGE_ID_BASE,
      VDP_MAX_IMAGES) == VDP_MAX_IMAGES
  ) {
    return GFX_ERR_INVALID_HANDLE;
  } else {
    const uint8_t make[] = {
      23, 27, 0x20, VDP_WORD(cursor),
      23, 27, 0x40, args->hotX, args->hotY
    };
    vdpWrite(make, sizeof(make));
  }
  draw->cursor = cursor;
  vdpShowCursor(draw);
  return GFX_OK;
}

/// @fn static void vdpWarpCursor(const GfxWarpCursorArgs *args)
///
/// @brief Move the pointer with VDU 23,0,&89,4,x;y;.
///
/// @param args The message's arguments.
///
/// @return This function returns no value.
static void vdpWarpCursor(const GfxWarpCursorArgs *args) {
  const uint8_t warp[] = {
    23, 0, 0x89, 4, VDP_WORD(args->x), VDP_WORD(args->y)
  };
  vdpWrite(warp, sizeof(warp));
}

/// @fn static uint8_t vdpFinish(VdpOutput *vdpOutput, ProcessMessage *message)
///
/// @brief Hold a GFX_MSG_FINISH until the VDP has drawn everything sent
/// before it:  flush its drawing queue with VDU 23,0,&CA, then send a general
/// poll, whose reply the main overlay waits for.  Each poll takes the next
/// cookie, so that the late reply to one whose GFX_MSG_FINISH was revoked
/// can't end the next.  Clearing graphicsMessage tells the main overlay not
/// to mark the message done.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param message The message.
///
/// @return Returns GFX_OK, which stays the status if the VDP answers, or
/// GFX_ERR_INVALID_STATE if a GFX_MSG_FINISH is already held.
static uint8_t vdpFinish(VdpOutput *vdpOutput, ProcessMessage *message) {
  if (vdpOutput->finishMessage != NULL) {
    return GFX_ERR_INVALID_STATE;
  }
  uint8_t cookie = (uint8_t) (vdpOutput->input->pollCookie + 1);
  if ((cookie < VDP_FINISH_POLL_COOKIE) || (cookie > VDP_LAST_POLL_COOKIE)) {
    cookie = VDP_FINISH_POLL_COOKIE;
  }
  const uint8_t finish[] = {
    23, 0, 0xCA,
    23, 0, 0x80, cookie
  };
  vdpOutput->input->pollCookie = cookie;
  vdpOutput->input->pollAnswered = false;
  vdpWrite(finish, sizeof(finish));
  vdpTimeFromNow(&vdpOutput->nextPoll, VDP_GENERAL_POLL_TIMEOUT_SECONDS, 0);
  vdpOutput->finishMessage = message;
  vdpOutput->graphicsMessage = NULL;
  return GFX_OK;
}

/// @fn static void vdpFinishDone(VdpOutput *vdpOutput, uint8_t status)
///
/// @brief Mark the held GFX_MSG_FINISH done with a status, or, if the status
/// is VDP_UNKNOWN, release it untouched, since its sender has ended and its
/// arguments are gone with it.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param status The GfxError to answer with, or VDP_UNKNOWN.
///
/// @return This function returns no value.
static void vdpFinishDone(VdpOutput *vdpOutput, uint8_t status) {
  ProcessMessage *message = vdpOutput->finishMessage;
  if (message == NULL) {
    return;
  }
  vdpOutput->finishMessage = NULL;
  vdpOutput->nextPoll.tv_nsec = VDP_NEVER;
  if (status == VDP_UNKNOWN) {
    processMessageRelease(message);
    return;
  }
  ((GfxFinishArgs*) processMessageData(message))->status = status;
  processMessageSetDone(message);
}

/// @var _vdpEnterGraphics
///
/// @brief VDU commands that give the display to its owner:  select context 1,
/// which the VDP makes as a copy of the console's context 0
/// (VDU 23,0,&C8,0,1), use pixel coordinates (VDU 23,0,&C0,0), turn the text
/// cursor off (VDU 23,1,0), draw text at the graphics cursor (VDU 5) without
/// wrapping it at the viewport's edge (VDU 23,16,&40,&BF), reset the
/// viewports (VDU 26) and the graphics origin (VDU 29,0;0;), clear the
/// screen (VDU 16), and put the pointer in the middle of the screen
/// (VDU 23,0,&89,4,x;y;).  The VDP turns the pointer's position into the
/// context's coordinates only when it moves, so without the move the owner's
/// first mouse packets would carry it in the console's.
static const uint8_t _vdpEnterGraphics[] = {
  23, 0, 0xC8, 0, 1,
  23, 0, 0xC0, 0,
  23, 1, 0,
  5,
  23, 16, 0x40, 0xBF,
  26,
  29, 0, 0, 0, 0,
  16,
  23, 0, 0x89, 4,
  VDP_WORD(VDP_SCREEN_WIDTH / 2), VDP_WORD(VDP_SCREEN_HEIGHT / 2)
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

/// @fn static void vdpFreeImages(VdpOutput *vdpOutput)
///
/// @brief Delete every image a display owner uploaded or captured.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
///
/// @return This function returns no value.
static void vdpFreeImages(VdpOutput *vdpOutput) {
  for (uint8_t slot = 0; slot < VDP_MAX_IMAGES; slot++) {
    vdpDestroyImage(vdpOutput, (GfxHandle) (VDP_IMAGE_ID_BASE + slot));
  }
}

/// @fn static void vdpFreePreloads(VdpOutput *vdpOutput)
///
/// @brief Delete every command buffer a display owner preuploaded.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
///
/// @return This function returns no value.
static void vdpFreePreloads(VdpOutput *vdpOutput) {
  for (uint8_t slot = 0; slot < VDP_MAX_PRELOADS; slot++) {
    vdpDiscardPreload(vdpOutput, (GfxHandle) (VDP_PRELOAD_ID_BASE + slot));
  }
}

/// @var _vdpOwnerFrees
///
/// @brief The functions that free each kind of resource a display owner can
/// create, all of which run when ownership ends.
static void (*const _vdpOwnerFrees[])(VdpOutput *vdpOutput) = {
  vdpFreeFonts,
  vdpFreeImages,
  vdpFreePreloads,
};

/// @fn static void vdpReleaseEvents(VdpInput *input)
///
/// @brief Free the events queue of an owner that is giving up the display.
/// Arguments the owner hasn't finished with are kept until it has:  as the
/// orphan, so that the next owner's events aren't held up, or, if there
/// already is one, as the next owner's spare.
///
/// @param input A pointer to the driver's VdpInput.
///
/// @return This function returns no value.
static void vdpReleaseEvents(VdpInput *input) {
  MESSAGE_ARGS_DESTROY(input->events);
  input->events = NULL;
  if (input->eventsMessage == NULL) {
    MESSAGE_ARGS_DESTROY(input->sentEvents);
    input->sentEvents = NULL;
  } else if (input->orphanMessage == NULL) {
    input->orphanMessage = input->eventsMessage;
    input->orphanEvents = input->sentEvents;
    input->eventsMessage = NULL;
    input->sentEvents = NULL;
  }
}

/// @fn static void vdpRelease(VdpOutput *vdpOutput, bool revoke)
///
/// @brief End the display's ownership:  free everything the owner created,
/// give the screen and the driver's mouse cursor back to the console's
/// context, and have the console's held text redrawn from the top of a
/// cleared screen.  A GFX_MSG_FINISH the
/// owner is waiting on is answered with GFX_ERR_NOT_OWNER if the owner is
/// still running, which only a revoke leaves it.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param revoke Whether to tell the owner with GFX_MSG_DISPLAY_REVOKED,
///   which it gets only when it didn't ask to give the display up.
///
/// @return This function returns no value.
static void vdpRelease(VdpOutput *vdpOutput, bool revoke) {
  vdpFinishDone(vdpOutput, (revoke == true) ? GFX_ERR_NOT_OWNER : VDP_UNKNOWN);
  for (uint8_t ii = 0;
    ii < sizeof(_vdpOwnerFrees) / sizeof(_vdpOwnerFrees[0]); ii++
  ) {
    _vdpOwnerFrees[ii](vdpOutput);
  }
  free(vdpOutput->draw);
  vdpOutput->draw = NULL;
  vdpReleaseEvents(vdpOutput->input);
  vdpOutput->ownerDefaultFont = 0;
  vdpSelectCursor(VDP_CURSOR_ID);
  vdpWrite(_vdpLeaveGraphics, sizeof(_vdpLeaveGraphics));
  vdpOutput->textState.row = 0;
  vdpOutput->textState.column = 0;
  vdpOutput->textState.escapeState = VDP_ESCAPE_NONE;
  vdpOutput->textState.erasePending = 0;
  vdpOutput->textState.scrollDirection = 0;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Console", "vdpHoldReplay",
    vdpOutput);

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
/// owns the display or the driver hasn't finished setting up the screen, whose
/// setup would undo the owner's drawing, or GFX_ERR_OUT_OF_MEMORY if there was
/// no memory to hold the console's text or the owner's input.
static uint8_t vdpOpenDisplay(VdpOutput *vdpOutput,
  ProcessDescriptor *sender, GfxOpenDisplayArgs *args
) {
  if (vdpOutput->listening == false) {
    return GFX_ERR_DISPLAY_BUSY;
  } else if (vdpOutput->owner == PROCESS_ID_NOT_SET) {
    VdpInput *input = vdpOutput->input;
    VdpDrawState *draw = (VdpDrawState*) calloc(1, sizeof(VdpDrawState));
    vdpOutput->heldText = (VdpOutputBuffer*) malloc(sizeof(VdpOutputBuffer));
    input->events = MESSAGE_ARGS_CREATE(GfxInputEventsArgs,
      MESSAGE_ARGS_ROUND_TRIP);
    if (input->sentEvents == NULL) {
      input->sentEvents = MESSAGE_ARGS_CREATE(GfxInputEventsArgs,
        MESSAGE_ARGS_ROUND_TRIP);
    }
    if ((draw == NULL) || (vdpOutput->heldText == NULL)
      || (input->events == NULL) || (input->sentEvents == NULL)
    ) {
      free(draw);
      free(vdpOutput->heldText);
      vdpOutput->heldText = NULL;
      vdpReleaseEvents(input);
      return GFX_ERR_OUT_OF_MEMORY;
    }
    memset(&input->eventState, 0, sizeof(input->eventState));
    vdpOutput->heldText->length = 0;
    draw->clip[0].w = VDP_SCREEN_WIDTH;
    draw->clip[0].h = VDP_SCREEN_HEIGHT;
    draw->vdpClip = draw->clip[0];
    draw->pen.fg = CGA_INDEX_WHITE;
    draw->pen.vdpFg = VDP_UNKNOWN;
    draw->pen.vdpBg = VDP_UNKNOWN;
    draw->pen.vdpBlend = VDP_UNKNOWN;
    draw->pen.vdpFont = VDP_UNKNOWN;
    draw->cursor = VDP_CURSOR_ID;
    vdpOutput->draw = draw;
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
  } else if ((type == GFX_MSG_UPLOAD_IMAGE) || (type == GFX_MSG_GET_IMAGE_SIZE)
    || (type == GFX_MSG_CAPTURE_RECT)
  ) {
    // The Images overlay answers in the arguments, which keep the client's
    // GFX_ERR_DEVICE_LOST if the overlay can't be called.
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Images", "vdpImages",
      vdpOutput);
    status = *((uint8_t*) payload);
  } else if (type == GFX_MSG_DESTROY_IMAGE) {
    vdpDestroyImage(vdpOutput, ((GfxHandleArgs*) payload)->handle);
  } else if (type == GFX_MSG_DISCARD_PREUPLOAD) {
    vdpDiscardPreload(vdpOutput, ((GfxHandleArgs*) payload)->handle);
  } else if (type == GFX_MSG_FINISH) {
    status = vdpFinish(vdpOutput, message);
  } else if (type == GFX_MSG_SET_CURSOR) {
    status = vdpSetCursor(vdpOutput->draw, (GfxSetCursorArgs*) payload);
  } else if (type == GFX_MSG_SHOW_CURSOR) {
    vdpOutput->draw->cursorHidden
      = (((GfxShowCursorArgs*) payload)->show == 0);
    vdpShowCursor(vdpOutput->draw);
  } else if (type == GFX_MSG_WARP_CURSOR) {
    vdpWarpCursor((GfxWarpCursorArgs*) payload);
  }

  if (oneWay == true) {
    if (payload != NULL) {
      MESSAGE_ARGS_DESTROY(payload);
    }
  } else if ((payload != NULL) && (processMessageSize(message) > 0)) {
    *((uint8_t*) payload) = status;
  }
}

/// @fn void* vdpGraphics(void *args)
///
/// @brief Answer the GraphicsProtocol.h message in the driver's
/// graphicsMessage, ignoring any other message.  GFX_MSG_CLIENT_ENDED carries
/// a ProcessId, not storage, and ends the display's ownership if that process
/// owns it.  With no message, finish the held GFX_MSG_FINISH once the VDP has
/// answered its poll, or with GFX_ERR_DEVICE_LOST once it is too late to.
/// Either way, free the orphaned events once their receiver has finished with
/// them.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpGraphics(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;
  ProcessMessage *message = vdpOutput->graphicsMessage;
  VdpInput *input = vdpOutput->input;

  if ((input->orphanMessage != NULL)
    && (processMessageDone(input->orphanMessage) == true)
  ) {
    processMessageRelease(input->orphanMessage);
    input->orphanMessage = NULL;
    MESSAGE_ARGS_DESTROY(input->orphanEvents);
    input->orphanEvents = NULL;
  }

  if (message == NULL) {
    if (vdpOutput->input->pollAnswered == true) {
      vdpFinishDone(vdpOutput, GFX_OK);
    } else if (vdpTimeReached(&vdpOutput->nextPoll) == true) {
      vdpFinishDone(vdpOutput, GFX_ERR_DEVICE_LOST);
    }
  } else if ((processMessageType(message) & ~((int64_t) 0xff))
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
