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




/// @file Canvas.c
///
/// @brief Canvases:  turning an application's records into preuploads of
/// the driver's, and discarding them, entered from the Template and
/// Property overlays only for canvases.

#include "Wm.h"

/// @def WM_TEXT_RECORD
///
/// @brief What _wmRecordSizes holds for a text record.
#define WM_TEXT_RECORD UINT8_MAX

/// @var _wmRecordSizes
///
/// @brief The size of each Graphics.h record a canvas may hold, 0 for those
/// it may not:  the ones that move the origin or the clip, name an image or
/// a font, which belong to the window server's own connection, or call or
/// copy, and WM_TEXT_RECORD for text, whose size is in the record.
static const uint8_t _wmRecordSizes[] = {
  0,                          // GFX_CMD_SET_VIEWPORT
  0,                          // GFX_CMD_SET_ORIGIN
  0,                          // GFX_CMD_PUSH_SCISSOR
  0,                          // GFX_CMD_POP_SCISSOR
  GFX_CMD_SET_FG_COLOR_SIZE,  // GFX_CMD_SET_FG_COLOR
  GFX_CMD_SET_BG_COLOR_SIZE,  // GFX_CMD_SET_BG_COLOR
  GFX_CMD_SET_BLEND_SIZE,     // GFX_CMD_SET_BLEND
  0,                          // GFX_CMD_SET_FONT
  GFX_CMD_CLEAR_SIZE,         // GFX_CMD_CLEAR
  GFX_CMD_DRAW_RECT_SIZE,     // GFX_CMD_DRAW_RECT
  GFX_CMD_DRAW_LINE_SIZE,     // GFX_CMD_DRAW_LINE
  GFX_CMD_DRAW_TRIANGLE_SIZE, // GFX_CMD_DRAW_TRIANGLE
  GFX_CMD_DRAW_CIRCLE_SIZE,   // GFX_CMD_DRAW_CIRCLE
  0,                          // GFX_CMD_DRAW_IMAGE
  0,                          // GFX_CMD_DRAW_SUB_IMAGE
  WM_TEXT_RECORD,             // GFX_CMD_DRAW_TEXT
};
GFX_STATIC_ASSERT(sizeof(_wmRecordSizes) == GFX_CMD_DRAW_TEXT + 1,
  "_wmRecordSizes needs an entry for each tag up to GFX_CMD_DRAW_TEXT");
GFX_STATIC_ASSERT((GFX_CMD_SET_FG_COLOR == 4) && (GFX_CMD_CLEAR == 8)
  && (GFX_CMD_DRAW_IMAGE == 13),
  "_wmRecordSizes' entries are in GfxCmdType's order");

/// @def WM_PEN_RECORDS
///
/// @brief The number of kinds of record that set the pen:  the foreground
/// and background colors and the blend.
#define WM_PEN_RECORDS 3

/// @var _wmPenTags
///
/// @brief The tags of the records that set the pen.
static const uint8_t _wmPenTags[WM_PEN_RECORDS] = {
  GFX_CMD_SET_FG_COLOR, GFX_CMD_SET_BG_COLOR, GFX_CMD_SET_BLEND,
};

/// @fn static uint16_t wmRecordLength(const uint8_t *record, uint16_t left)
///
/// @brief Find the size of a Graphics.h record a canvas may hold.
///
/// @param record The record.
/// @param left The bytes from the record to the end of the records.
///
/// @return Returns its size, or 0 if a canvas may not hold it or it is cut
/// short.
static uint16_t wmRecordLength(const uint8_t *record, uint16_t left) {
  uint8_t tag = record[GFX_CMD_TAG];
  uint16_t size = 0;
  if (tag < sizeof(_wmRecordSizes)) {
    size = _wmRecordSizes[tag];
  }
  if ((size == WM_TEXT_RECORD) && (left > GFX_CMD_DRAW_TEXT_LENGTH)) {
    size = GFX_CMD_DRAW_TEXT_SIZE(record[GFX_CMD_DRAW_TEXT_LENGTH]);
  } else if (size == WM_TEXT_RECORD) {
    size = 0;
  }
  return (size > left) ? 0 : size;
}

/// @fn static uint8_t wmSetCanvas(WmState *state, const WmWindow *window,
///   WmWidgets *widgets, WmWidget *canvas, const WinBufferArgs *args)
///
/// @brief Replace a canvas's records:  split them at record boundaries into
/// preuploads of the driver's of at most WM_CANVAS_CHUNK bytes each, and
/// discard the old ones.  Each preupload after the first starts with the
/// color and blend records in effect where it begins, since the driver
/// starts every preupload from the pen it has when the preupload is made.
///
/// @param state The window manager's state.
/// @param window The canvas's window.
/// @param widgets The window's widgets.
/// @param canvas The canvas.
/// @param args The request's arguments.
///
/// @return Returns WIN_OK, WIN_ERR_INVALID for records past WIN_CANVAS_MAX
/// bytes, cut short or that a canvas may not hold, or WIN_ERR_NO_MEMORY if
/// they need more than WM_CANVAS_PRELOADS preuploads or there was no
/// memory, in which case the canvas is left empty.
static uint8_t wmSetCanvas(WmState *state, const WmWindow *window,
  WmWidgets *widgets, WmWidget *canvas, const WinBufferArgs *args
) {
  const uint8_t *data = args->data;
  uint16_t size = args->size;
  if ((size > WIN_CANVAS_MAX) || ((size > 0) && (data == NULL))) {
    return WIN_ERR_INVALID;
  }
  for (uint16_t at = 0; at < size; ) {
    uint16_t length = wmRecordLength(&data[at], size - at);
    if (length == 0) {
      return WIN_ERR_INVALID;
    }
    at += length;
  }
  WmCanvas *preloads = (WmCanvas*) canvas->text;
  if (preloads == NULL) {
    if (((uint32_t) widgets->bytes + sizeof(WmCanvas) > UINT16_MAX)
      || !wmCharge(state, window->owner, sizeof(WmCanvas))
    ) {
      return WIN_ERR_NO_MEMORY;
    }
    preloads = (WmCanvas*) calloc(1, sizeof(WmCanvas));
    if (preloads == NULL) {
      wmCharge(state, window->owner, -(int32_t) sizeof(WmCanvas));
      return WIN_ERR_NO_MEMORY;
    }
    canvas->text = (char*) preloads;
    canvas->itemsSize = sizeof(WmCanvas);
    widgets->bytes = (uint16_t) (widgets->bytes + sizeof(WmCanvas));
  }
  wmDiscardCanvas(state, canvas);
  uint8_t status = WIN_OK;
  const uint8_t *pens[WM_PEN_RECORDS] = { NULL, NULL, NULL };
  for (uint16_t at = 0; (at < size) && (status == WIN_OK); ) {
    const uint8_t *starting[WM_PEN_RECORDS];
    uint16_t prefix = 0;
    for (uint8_t ii = 0; ii < WM_PEN_RECORDS; ii++) {
      starting[ii] = pens[ii];
      if (pens[ii] != NULL) {
        prefix += wmRecordLength(pens[ii], WM_CANVAS_CHUNK);
      }
    }
    uint16_t chunk = 0;
    while ((at + chunk < size)
      && (prefix + chunk
        + wmRecordLength(&data[at + chunk], size - at - chunk)
        <= WM_CANVAS_CHUNK)
    ) {
      const uint8_t *record = &data[at + chunk];
      for (uint8_t ii = 0; ii < WM_PEN_RECORDS; ii++) {
        if (record[GFX_CMD_TAG] == _wmPenTags[ii]) {
          pens[ii] = record;
        }
      }
      chunk += wmRecordLength(record, size - at - chunk);
    }
    GfxHandle handle = GFX_INVALID_HANDLE;
    gfxCmdBufBegin(state->cmd, GFX_CMDBUF_NO_FLUSH);
    uint8_t *copy = gfxCmdBufReserve_(state->cmd, prefix + chunk);
    if ((preloads->count == WM_CANVAS_PRELOADS) || (copy == NULL)) {
      status = WIN_ERR_NO_MEMORY;
    } else {
      for (uint8_t ii = 0; ii < WM_PEN_RECORDS; ii++) {
        if (starting[ii] != NULL) {
          uint16_t length = wmRecordLength(starting[ii], WM_CANVAS_CHUNK);
          memcpy(copy, starting[ii], length);
          copy += length;
        }
      }
      memcpy(copy, &data[at], chunk);
      if (gfxPreupload(state->cmd, &handle) != GFX_OK) {
        status = WIN_ERR_NO_MEMORY;
      } else {
        preloads->preloads[preloads->count++] = handle;
      }
    }
    at += chunk;
  }
  gfxCmdBufBegin(state->cmd, 0);
  if (status != WIN_OK) {
    wmDiscardCanvas(state, canvas);
  }
  return status;
}

/// @fn void* wmCanvasSet(void *args)
///
/// @brief Carry out a WIN_MSG_SET_CANVAS on the job's window, which the
/// Template overlay has found and checked the sender owns.
///
/// @param args A pointer to the WmTemplateJob.
///
/// @return Always returns the provided pointer.
void* wmCanvasSet(void *args) {
  WmTemplateJob *job = (WmTemplateJob*) args;
  WinBufferArgs *set = (WinBufferArgs*) job->data;
  WmWidgets *widgets = (WmWidgets*) job->window->widgets;
  WmWidget *canvas = (set->widget != WIN_NONE)
    ? wmWidgetById(widgets, set->widget) : NULL;
  set->status = WIN_ERR_NOT_FOUND;
  if ((canvas != NULL) && (canvas->kind == WIN_WIDGET_CANVAS)) {
    set->status = wmSetCanvas(job->state, job->window, widgets, canvas, set);
    wmDamageWidget(job->state, job->window, canvas);
  }
  return args;
}

/// @fn void* wmCanvasDiscard(void *args)
///
/// @brief Discard the preuploads of the canvases among some of a window's
/// widgets.
///
/// @param args A pointer to the WmCanvasJob.
///
/// @return Always returns the provided pointer.
void* wmCanvasDiscard(void *args) {
  WmCanvasJob *job = (WmCanvasJob*) args;
  for (uint8_t ii = job->from; ii < job->to; ii++) {
    wmDiscardCanvas(job->state, &job->widgets->widget[ii]);
  }
  return args;
}
