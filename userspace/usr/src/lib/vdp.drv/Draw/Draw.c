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

/// @file Draw.c
///
/// @brief Carrying out a display owner's command records on the Agon Light 2
/// VDP, for GFX_MSG_SUBMIT and GFX_MSG_PREUPLOAD.

#include <string.h>

#include "VdpDriver.h"

RGB222_TO_CGA_MAP

/// @def VDP_SINK_SIZE
///
/// @brief The number of VDU bytes collected before they're written.
#define VDP_SINK_SIZE 64

/// @def VDP_PLOT_MOVE
///
/// @brief The PLOT command that moves to an absolute point.
#define VDP_PLOT_MOVE 0x04

/// @def VDP_PLOT_LINE
///
/// @brief The PLOT command that draws a line in the foreground to an absolute
/// point.
#define VDP_PLOT_LINE 0x05

/// @def VDP_PLOT_LINE_OPEN
///
/// @brief The PLOT command that draws a line in the foreground to an absolute
/// point, leaving that point out, so that a closed outline draws each corner
/// once.
#define VDP_PLOT_LINE_OPEN 0x0D

/// @def VDP_PLOT_TRIANGLE
///
/// @brief The PLOT command that fills the triangle of the last two points and
/// an absolute point in the foreground.
#define VDP_PLOT_TRIANGLE 0x55

/// @def VDP_PLOT_RECTANGLE
///
/// @brief The PLOT command that fills the rectangle from the last point to an
/// absolute point in the foreground.
#define VDP_PLOT_RECTANGLE 0x65

/// @def VDP_PLOT_RECTANGLE_BACKGROUND
///
/// @brief The PLOT command that fills the rectangle from the last point to an
/// absolute point in the background.
#define VDP_PLOT_RECTANGLE_BACKGROUND 0x67

/// @def VDP_PLOT_CIRCLE
///
/// @brief The PLOT command that draws a circle around the last point through
/// a relative point.
#define VDP_PLOT_CIRCLE 0x91

/// @def VDP_PLOT_DISC
///
/// @brief The PLOT command that fills a circle around the last point through
/// a relative point.
#define VDP_PLOT_DISC 0x99

/// @def VDP_PLOT_COPY
///
/// @brief The PLOT command that copies the rectangle between the last two
/// points so that its bottom-left corner is at an absolute point.
#define VDP_PLOT_COPY 0xBE

/// @def VDP_PLOT_BITMAP
///
/// @brief The PLOT command that draws the selected bitmap with its top-left
/// corner at an absolute point.
#define VDP_PLOT_BITMAP 0xED

/// @struct VdpSink
///
/// @brief Where a walk of command records puts its VDU bytes:  written to the
/// VDP whenever the buffer fills, or only counted.
///
/// @param total The number of bytes put in the sink.
/// @param length The number of bytes in data.
/// @param counting Whether the bytes are only counted.
/// @param input A pointer to the driver's VdpInput, which takes what the VDP
///   sends after each write.
/// @param data The bytes not yet written.
typedef struct VdpSink {
  uint32_t  total;
  uint8_t   length;
  bool      counting;
  VdpInput *input;
  uint8_t   data[VDP_SINK_SIZE];
} VdpSink;

/// @struct VdpWalk
///
/// @brief The state of a walk of a buffer of command records.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param draw The owner's drawing state.
/// @param pen The pen the records change and draw with.
/// @param origin The screen position of the records' (0,0):  the owner's for
///   a submit, (0,0) for a preupload, which is offset when it is called.
/// @param preload Whether the records are being preuploaded.
/// @param sink Where the VDU bytes go.
typedef struct VdpWalk {
  VdpOutput    *vdpOutput;
  VdpDrawState *draw;
  VdpPen       *pen;
  GfxPoint      origin;
  bool          preload;
  VdpSink       sink;
} VdpWalk;

/// @var _vdpRecordSizes
///
/// @brief The size of each GfxCmdType's record, its tag included, and of a
/// GFX_CMD_DRAW_TEXT record without its text.
static const uint8_t _vdpRecordSizes[GFX_CMD_COUNT] = {
  [GFX_CMD_SET_VIEWPORT]   = GFX_CMD_SET_VIEWPORT_SIZE,
  [GFX_CMD_SET_ORIGIN]     = GFX_CMD_SET_ORIGIN_SIZE,
  [GFX_CMD_PUSH_SCISSOR]   = GFX_CMD_PUSH_SCISSOR_SIZE,
  [GFX_CMD_POP_SCISSOR]    = GFX_CMD_POP_SCISSOR_SIZE,
  [GFX_CMD_SET_FG_COLOR]   = GFX_CMD_SET_FG_COLOR_SIZE,
  [GFX_CMD_SET_BG_COLOR]   = GFX_CMD_SET_BG_COLOR_SIZE,
  [GFX_CMD_SET_BLEND]      = GFX_CMD_SET_BLEND_SIZE,
  [GFX_CMD_SET_FONT]       = GFX_CMD_SET_FONT_SIZE,
  [GFX_CMD_CLEAR]          = GFX_CMD_CLEAR_SIZE,
  [GFX_CMD_DRAW_RECT]      = GFX_CMD_DRAW_RECT_SIZE,
  [GFX_CMD_DRAW_LINE]      = GFX_CMD_DRAW_LINE_SIZE,
  [GFX_CMD_DRAW_TRIANGLE]  = GFX_CMD_DRAW_TRIANGLE_SIZE,
  [GFX_CMD_DRAW_CIRCLE]    = GFX_CMD_DRAW_CIRCLE_SIZE,
  [GFX_CMD_DRAW_IMAGE]     = GFX_CMD_DRAW_IMAGE_SIZE,
  [GFX_CMD_DRAW_SUB_IMAGE] = GFX_CMD_DRAW_SUB_IMAGE_SIZE,
  [GFX_CMD_DRAW_TEXT]      = GFX_CMD_DRAW_TEXT_SIZE(0),
  [GFX_CMD_CALL]           = GFX_CMD_CALL_SIZE,
  [GFX_CMD_COPY_RECT]      = GFX_CMD_COPY_RECT_SIZE,
  [GFX_CMD_SCROLL_RECT]    = GFX_CMD_SCROLL_RECT_SIZE,
};

/// @var _vdpGcolModes
///
/// @brief The VDU 18 mode of each GfxBlendMode.
static const uint8_t _vdpGcolModes[] = { 0, 2, 1, 3, 4 };

/// @var _vdpScreen
///
/// @brief The whole screen, which every clip is inside.
static const GfxRect _vdpScreen = { 0, 0, VDP_SCREEN_WIDTH, VDP_SCREEN_HEIGHT };

/// @fn static void vdpEmit(VdpSink *sink, const uint8_t *bytes,
///   uint8_t length)
///
/// @brief Put VDU bytes in a sink, writing what it holds first if they don't
/// fit, and then taking what the VDP has sent meanwhile.
///
/// @param sink The sink.
/// @param bytes The bytes.
/// @param length The number of bytes, at most VDP_SINK_SIZE.
///
/// @return This function returns no value.
static void vdpEmit(VdpSink *sink, const uint8_t *bytes, uint8_t length) {
  sink->total += length;

  if (sink->counting == true) {
    return;
  }

  if ((sink->length + length) > VDP_SINK_SIZE) {
    vdpWrite(sink->data, sink->length);
    sink->length = 0;
    vdpReceive(sink->input);
  }

  memcpy(&sink->data[sink->length], bytes, length);
  sink->length += length;
}

/// @fn static void vdpPlot(VdpWalk *walk, uint8_t command,
///   int16_t pointX, int16_t pointY)
///
/// @brief Send VDU 25,command,pointX;pointY;.
///
/// @param walk The walk.
/// @param command The PLOT command.
/// @param pointX The X coordinate.
/// @param pointY The Y coordinate.
///
/// @return This function returns no value.
static void vdpPlot(VdpWalk *walk, uint8_t command, int16_t pointX,
  int16_t pointY
) {
  const uint8_t plot[] = {
    25, command, VDP_WORD(pointX), VDP_WORD(pointY)
  };
  vdpEmit(&walk->sink, plot, sizeof(plot));
}

/// @fn static void vdpIntersect(GfxRect *result, const GfxRect *rect,
///   const GfxRect *other)
///
/// @brief Intersect two rectangles.
///
/// @param result Where to store the intersection, whose width and height are
///   0 if it's empty.
/// @param rect One rectangle.
/// @param other The other.
///
/// @return This function returns no value.
static void vdpIntersect(GfxRect *result, const GfxRect *rect,
  const GfxRect *other
) {
  int left = (rect->x > other->x) ? rect->x : other->x;
  int top = (rect->y > other->y) ? rect->y : other->y;
  int right = rect->x + rect->w;
  int bottom = rect->y + rect->h;

  if ((other->x + other->w) < right) {
    right = other->x + other->w;
  }

  if ((other->y + other->h) < bottom) {
    bottom = other->y + other->h;
  }

  result->x = (int16_t) left;
  result->y = (int16_t) top;
  result->w = (right > left) ? (int16_t) (right - left) : 0;
  result->h = (bottom > top) ? (int16_t) (bottom - top) : 0;
}

/// @fn static void vdpViewport(VdpWalk *walk, const GfxRect *rect)
///
/// @brief Set the VDP's graphics viewport with VDU 24, whose corners in
/// pixel coordinates are left;bottom;right;top;, bottom being the larger Y.
///
/// @param walk The walk.
/// @param rect The viewport, which isn't empty and is on the screen.
///
/// @return This function returns no value.
static void vdpViewport(VdpWalk *walk, const GfxRect *rect) {
  const uint8_t viewport[] = {
    24, VDP_WORD(rect->x), VDP_WORD(rect->y + rect->h - 1),
    VDP_WORD(rect->x + rect->w - 1), VDP_WORD(rect->y)
  };
  vdpEmit(&walk->sink, viewport, sizeof(viewport));
  walk->draw->vdpClip = *rect;
}

/// @fn static bool vdpClipReady(VdpWalk *walk)
///
/// @brief Give the VDP the current clip as its graphics viewport if it
/// doesn't have it.  A preupload leaves the clip to whatever it is when the
/// preupload is called.
///
/// @param walk The walk.
///
/// @return Returns false if the clip is empty, so nothing can be drawn, true
/// otherwise.
static bool vdpClipReady(VdpWalk *walk) {
  if (walk->preload == true) {
    return true;
  }

  VdpDrawState *draw = walk->draw;
  const GfxRect *clip = &draw->clip[draw->clipDepth];

  if ((clip->w <= 0) || (clip->h <= 0)) {
    return false;
  }

  if (memcmp(clip, &draw->vdpClip, sizeof(*clip)) != 0) {
    vdpViewport(walk, clip);
  }

  return true;
}

/// @fn static void vdpForgetPen(VdpPen *pen)
///
/// @brief Mark everything the VDP has of a pen unknown, so that it is all
/// sent again.
///
/// @param pen The pen.
///
/// @return This function returns no value.
static void vdpForgetPen(VdpPen *pen) {
  pen->vdpFg = VDP_UNKNOWN;
  pen->vdpBg = VDP_UNKNOWN;
  pen->vdpBlend = VDP_UNKNOWN;
  pen->vdpFont = VDP_UNKNOWN;
}

/// @fn static void vdpSyncPen(VdpWalk *walk)
///
/// @brief Give the VDP the pen's colors and blend mode with VDU 18 where it
/// doesn't have them.  The blend mode applies to the foreground only, so that
/// clearing to the background always overwrites.
///
/// @param walk The walk.
///
/// @return This function returns no value.
static void vdpSyncPen(VdpWalk *walk) {
  VdpPen *pen = walk->pen;
  if ((pen->fg != pen->vdpFg) || (pen->blend != pen->vdpBlend)) {
    const uint8_t gcol[] = { 18, _vdpGcolModes[pen->blend], pen->fg };
    vdpEmit(&walk->sink, gcol, sizeof(gcol));
    pen->vdpFg = pen->fg;
    pen->vdpBlend = pen->blend;
  }

  if (pen->bg != pen->vdpBg) {
    const uint8_t gcol[] = { 18, 0, (uint8_t) (128 + pen->bg) };
    vdpEmit(&walk->sink, gcol, sizeof(gcol));
    pen->vdpBg = pen->bg;
  }
}

/// @fn static void vdpDrawText(VdpWalk *walk, int16_t left, int16_t top,
///   const uint8_t *text, uint8_t length)
///
/// @brief Draw text at the graphics cursor, which the owner's context draws
/// text at, in the pen's font, selected with VDU 23,0,&95,0,id;0 if the VDP
/// doesn't have it.  Bytes the VDP would take as control codes are sent as
/// VDU 27,byte, which draws them as glyphs.
///
/// @param walk The walk.
/// @param left The screen X coordinate of the text's top-left corner.
/// @param top The screen Y coordinate of the text's top-left corner.
/// @param text The text.
/// @param length The number of bytes of text.
///
/// @return This function returns no value.
static void vdpDrawText(VdpWalk *walk, int16_t left, int16_t top,
  const uint8_t *text, uint8_t length
) {
  VdpPen *pen = walk->pen;
  if (pen->font != pen->vdpFont) {
    const uint8_t select[] = {
      23, 0, 0x95, 0, VDP_WORD(VDP_FONT_ID_BASE + pen->font), 0
    };
    vdpEmit(&walk->sink, select, sizeof(select));
    pen->vdpFont = pen->font;
  }

  vdpPlot(walk, VDP_PLOT_MOVE, left, top);

  for (uint8_t ii = 0; ii < length; ii++) {
    uint8_t glyph[] = { 27, text[ii] };

    if ((text[ii] < 32) || (text[ii] == 127)) {
      vdpEmit(&walk->sink, glyph, sizeof(glyph));
    } else {
      vdpEmit(&walk->sink, &glyph[1], 1);
    }
  }
}

/// @fn static void vdpFillBackground(VdpWalk *walk, int16_t left,
///   int16_t top, int16_t right, int16_t bottom)
///
/// @brief Fill a rectangle with the background.
///
/// @param walk The walk.
/// @param left The screen X coordinate of its left column.
/// @param top The screen Y coordinate of its top row.
/// @param right The screen X coordinate of its right column.
/// @param bottom The screen Y coordinate of its bottom row.
///
/// @return This function returns no value.
static void vdpFillBackground(VdpWalk *walk, int16_t left, int16_t top,
  int16_t right, int16_t bottom
) {
  vdpPlot(walk, VDP_PLOT_MOVE, left, top);
  vdpPlot(walk, VDP_PLOT_RECTANGLE_BACKGROUND, right, bottom);
}

/// @fn static void vdpCopy(VdpWalk *walk, int16_t left, int16_t top,
///   int16_t width, int16_t height, int16_t toLeft, int16_t toTop)
///
/// @brief Copy a rectangle with PLOT &BE, which the VDP clips to its graphics
/// viewport.  The VDP cuts a source that is off the screen down without
/// moving the destination, so a submit cuts the source itself and moves the
/// destination with it.
///
/// @param walk The walk.
/// @param left The screen X coordinate of the source's left column.
/// @param top The screen Y coordinate of the source's top row.
/// @param width The source's width.
/// @param height The source's height.
/// @param toLeft The screen X coordinate of the destination's left column.
/// @param toTop The screen Y coordinate of the destination's top row.
///
/// @return This function returns no value.
static void vdpCopy(VdpWalk *walk, int16_t left, int16_t top, int16_t width,
  int16_t height, int16_t toLeft, int16_t toTop
) {
  if (walk->preload == false) {
    if (left < 0) {
      toLeft -= left;
      width += left;
      left = 0;
    }

    if (top < 0) {
      toTop -= top;
      height += top;
      top = 0;
    }

    if ((left + width) > VDP_SCREEN_WIDTH) {
      width = VDP_SCREEN_WIDTH - left;
    }

    if ((top + height) > VDP_SCREEN_HEIGHT) {
      height = VDP_SCREEN_HEIGHT - top;
    }
  }

  if ((width <= 0) || (height <= 0)) {
    return;
  }

  vdpPlot(walk, VDP_PLOT_MOVE, left, top);
  vdpPlot(walk, VDP_PLOT_MOVE, left + width - 1, top + height - 1);
  vdpPlot(walk, VDP_PLOT_COPY, toLeft, toTop + height - 1);
}

/// @fn static void vdpScroll(VdpWalk *walk, int16_t left, int16_t top,
///   int16_t width, int16_t height, int16_t deltaX, int16_t deltaY)
///
/// @brief Move the contents of a rectangle, and fill the part of it nothing
/// moved into with the background.
///
/// @param walk The walk.
/// @param left The screen X coordinate of the rectangle's left column.
/// @param top The screen Y coordinate of the rectangle's top row.
/// @param width The rectangle's width.
/// @param height The rectangle's height.
/// @param deltaX The distance to move the contents right.
/// @param deltaY The distance to move the contents down.
///
/// @return This function returns no value.
static void vdpScroll(VdpWalk *walk, int16_t left, int16_t top, int16_t width,
  int16_t height, int16_t deltaX, int16_t deltaY
) {
  int16_t right = left + width - 1;
  int16_t bottom = top + height - 1;
  int16_t distanceX = (deltaX < 0) ? -deltaX : deltaX;
  int16_t distanceY = (deltaY < 0) ? -deltaY : deltaY;

  if ((width <= 0) || (height <= 0)) {
    return;
  }
  if ((distanceX >= width) || (distanceY >= height)) {
    vdpFillBackground(walk, left, top, right, bottom);
    return;
  }

  int16_t fromX = (deltaX < 0) ? (left - deltaX) : left;
  int16_t fromY = (deltaY < 0) ? (top - deltaY) : top;

  vdpCopy(walk, fromX, fromY, width - distanceX, height - distanceY,
    fromX + deltaX, fromY + deltaY);

  if (deltaY > 0) {
    vdpFillBackground(walk, left, top, right, top + deltaY - 1);
  } else if (deltaY < 0) {
    vdpFillBackground(walk, left, bottom + deltaY + 1, right, bottom);
  }

  if (deltaX > 0) {
    vdpFillBackground(walk, left, top, left + deltaX - 1, bottom);
  } else if (deltaX < 0) {
    vdpFillBackground(walk, right + deltaX + 1, top, right, bottom);
  }
}

/// @fn static void vdpScissor(VdpWalk *walk, uint8_t tag,
///   const int16_t *words)
///
/// @brief Carry out GFX_CMD_SET_VIEWPORT, GFX_CMD_SET_ORIGIN,
/// GFX_CMD_PUSH_SCISSOR or GFX_CMD_POP_SCISSOR.  Pushing onto a full scissor
/// stack or popping an empty one sets the sticky error.
///
/// @param walk The walk.
/// @param tag The record's GfxCmdType.
/// @param words The record's parameters.
///
/// @return This function returns no value.
static void vdpScissor(VdpWalk *walk, uint8_t tag, const int16_t *words) {
  VdpDrawState *draw = walk->draw;
  GfxRect rect = { words[0], words[1], words[2], words[3] };

  if (tag == GFX_CMD_SET_ORIGIN) {
    draw->origin.x = words[0];
    draw->origin.y = words[1];
    walk->origin = draw->origin;
  } else if (tag == GFX_CMD_SET_VIEWPORT) {
    draw->clipDepth = 0;
    vdpIntersect(&draw->clip[0], &rect, &_vdpScreen);
  } else if (tag == GFX_CMD_PUSH_SCISSOR) {
    if (draw->clipDepth == GFX_MAX_SCISSOR_DEPTH) {
      vdpSetError(walk->vdpOutput, GFX_ERR_INVALID_STATE);
      return;
    }

    rect.x += draw->origin.x;
    rect.y += draw->origin.y;
    vdpIntersect(&draw->clip[draw->clipDepth + 1], &rect,
      &draw->clip[draw->clipDepth]);
    draw->clipDepth++;
  } else if (draw->clipDepth == 0) {
    vdpSetError(walk->vdpOutput, GFX_ERR_INVALID_STATE);
  } else {
    draw->clipDepth--;
  }
}

/// @fn static uint8_t vdpSetPen(VdpWalk *walk, uint8_t tag,
///   const uint8_t *params)
///
/// @brief Carry out GFX_CMD_SET_FG_COLOR, GFX_CMD_SET_BG_COLOR,
/// GFX_CMD_SET_BLEND or GFX_CMD_SET_FONT, which change the pen and send
/// nothing.  Colors become the nearest of the screen's 16.
///
/// @param walk The walk.
/// @param tag The record's GfxCmdType.
/// @param params The record's parameters.
///
/// @return Returns GFX_OK on success, GFX_ERR_INVALID_STATE for a blend mode
/// that doesn't exist, or GFX_ERR_INVALID_HANDLE for a font that isn't
/// uploaded.
static uint8_t vdpSetPen(VdpWalk *walk, uint8_t tag, const uint8_t *params) {
  VdpPen *pen = walk->pen;

  if (tag == GFX_CMD_SET_BLEND) {
    if (params[0] > GFX_BLEND_INVERT) {
      return GFX_ERR_INVALID_STATE;
    }

    pen->blend = params[0];
  } else if (tag == GFX_CMD_SET_FONT) {
    VdpOutput *vdpOutput = walk->vdpOutput;
    GfxHandle font = (GfxHandle) (params[0] | (params[1] << 8));
    uint16_t slot = (uint16_t) (font - VDP_FONT_ID_BASE);

    if (font == GFX_INVALID_HANDLE) {
      slot = vdpOutput->ownerDefaultFont;
    } else if ((font < VDP_FONT_ID_BASE) || (slot == 0)
      || (slot >= VDP_MAX_FONTS)
      || ((vdpOutput->fontsUsed & (1 << slot)) == 0)
    ) {
      return GFX_ERR_INVALID_HANDLE;
    }

    pen->font = (uint8_t) slot;
  } else {
    // A GfxColor is 0xRRGGBBAA, so red is its last byte.
    uint8_t index = rgb222ToCgaMap[(vdpChannelBits(params[3]) << 4)
      | (vdpChannelBits(params[2]) << 2) | vdpChannelBits(params[1])];

    if (tag == GFX_CMD_SET_FG_COLOR) {
      pen->fg = index;
    } else {
      pen->bg = index;
    }
  }

  return GFX_OK;
}

/// @fn static uint8_t vdpRecord(VdpWalk *walk, uint8_t tag,
///   const int16_t *words, const uint8_t *params)
///
/// @brief Carry out one command record.  A preupload may hold only pen and
/// drawing records, and no GFX_CMD_DRAW_SUB_IMAGE, whose clip it couldn't put
/// back.  Handles are checked before the clip, so that a bad one stops the
/// buffer even where nothing would be drawn.
///
/// @param walk The walk.
/// @param tag The record's GfxCmdType.
/// @param words The record's parameters as 16-bit values.
/// @param params The record's parameters.
///
/// @return Returns GFX_OK if the walk can go on, the GfxError that stops it
/// otherwise.
static uint8_t vdpRecord(VdpWalk *walk, uint8_t tag, const int16_t *words,
  const uint8_t *params
) {
  VdpDrawState *draw = walk->draw;
  bool forbidden = false;

  if (tag <= GFX_CMD_POP_SCISSOR) {
    if (walk->preload == true) {
      return GFX_ERR_INVALID_STATE;
    }

    vdpScissor(walk, tag, words);
    return GFX_OK;
  } else if (tag <= GFX_CMD_SET_FONT) {
    return vdpSetPen(walk, tag, params);
  } else if ((tag == GFX_CMD_DRAW_IMAGE) || (tag == GFX_CMD_DRAW_SUB_IMAGE)) {
    if (vdpSlot(draw->imagesUsed, (GfxHandle) words[0], VDP_IMAGE_ID_BASE,
      VDP_MAX_IMAGES) == VDP_MAX_IMAGES
    ) {
      return GFX_ERR_INVALID_HANDLE;
    }

    forbidden = (tag == GFX_CMD_DRAW_SUB_IMAGE);
  } else if (tag == GFX_CMD_CALL) {
    if (vdpSlot(draw->preloadsUsed, (GfxHandle) words[0],
      VDP_PRELOAD_ID_BASE, VDP_MAX_PRELOADS) == VDP_MAX_PRELOADS
    ) {
      return GFX_ERR_INVALID_HANDLE;
    }

    forbidden = true;
  }

  if ((forbidden == true) && (walk->preload == true)) {
    return GFX_ERR_INVALID_STATE;
  }
  if (vdpClipReady(walk) == false) {
    return GFX_OK;
  }

  vdpSyncPen(walk);

  int16_t firstX = words[0] + walk->origin.x;
  int16_t firstY = words[1] + walk->origin.y;

  if (tag == GFX_CMD_CLEAR) {
    const uint8_t clear[] = { 16 };
    vdpEmit(&walk->sink, clear, sizeof(clear));
  } else if (tag == GFX_CMD_DRAW_RECT) {
    int16_t right = firstX + words[2] - 1;
    int16_t bottom = firstY + words[3] - 1;

    if ((words[2] <= 0) || (words[3] <= 0)) {
      return GFX_OK;
    }

    vdpPlot(walk, VDP_PLOT_MOVE, firstX, firstY);

    if ((params[8] != 0) || (words[2] == 1) || (words[3] == 1)) {
      vdpPlot(walk, VDP_PLOT_RECTANGLE, right, bottom);
    } else {
      vdpPlot(walk, VDP_PLOT_LINE_OPEN, right, firstY);
      vdpPlot(walk, VDP_PLOT_LINE_OPEN, right, bottom);
      vdpPlot(walk, VDP_PLOT_LINE_OPEN, firstX, bottom);
      vdpPlot(walk, VDP_PLOT_LINE_OPEN, firstX, firstY);
    }
  } else if (tag == GFX_CMD_DRAW_LINE) {
    vdpPlot(walk, VDP_PLOT_MOVE, firstX, firstY);
    vdpPlot(walk, VDP_PLOT_LINE,
      words[2] + walk->origin.x, words[3] + walk->origin.y);
  } else if (tag == GFX_CMD_DRAW_TRIANGLE) {
    int16_t secondX = words[2] + walk->origin.x;
    int16_t secondY = words[3] + walk->origin.y;
    int16_t thirdX = words[4] + walk->origin.x;
    int16_t thirdY = words[5] + walk->origin.y;

    vdpPlot(walk, VDP_PLOT_MOVE, firstX, firstY);

    if (params[12] != 0) {
      vdpPlot(walk, VDP_PLOT_MOVE, secondX, secondY);
      vdpPlot(walk, VDP_PLOT_TRIANGLE, thirdX, thirdY);
    } else {
      vdpPlot(walk, VDP_PLOT_LINE_OPEN, secondX, secondY);
      vdpPlot(walk, VDP_PLOT_LINE_OPEN, thirdX, thirdY);
      vdpPlot(walk, VDP_PLOT_LINE_OPEN, firstX, firstY);
    }
  } else if (tag == GFX_CMD_DRAW_CIRCLE) {
    if (words[2] <= 0) {
      return GFX_OK;
    }

    vdpPlot(walk, VDP_PLOT_MOVE, firstX, firstY);
    // The radius is relative to the center, so the origin isn't added.
    vdpPlot(walk, (params[6] != 0) ? VDP_PLOT_DISC : VDP_PLOT_CIRCLE,
      words[2], 0);
  } else if (tag == GFX_CMD_DRAW_IMAGE) {
    const uint8_t select[] = { 23, 27, 0x20, VDP_WORD(words[0]) };
    vdpEmit(&walk->sink, select, sizeof(select));
    vdpPlot(walk, VDP_PLOT_BITMAP,
      words[1] + walk->origin.x, words[2] + walk->origin.y);
  } else if (tag == GFX_CMD_DRAW_SUB_IMAGE) {
    GfxRect target = {
      words[5] + walk->origin.x, words[6] + walk->origin.y, words[3], words[4]
    };
    vdpIntersect(&target, &target, &draw->clip[draw->clipDepth]);

    if ((target.w == 0) || (target.h == 0)) {
      return GFX_OK;
    }

    vdpViewport(walk, &target);
    const uint8_t select[] = { 23, 27, 0x20, VDP_WORD(words[0]) };
    vdpEmit(&walk->sink, select, sizeof(select));
    vdpPlot(walk, VDP_PLOT_BITMAP, words[5] + walk->origin.x - words[1],
      words[6] + walk->origin.y - words[2]);
  } else if (tag == GFX_CMD_DRAW_TEXT) {
    vdpDrawText(walk, firstX, firstY, &params[5], params[4]);
  } else if (tag == GFX_CMD_CALL) {
    const uint8_t call[] = {
      29, VDP_WORD(walk->origin.x), VDP_WORD(walk->origin.y),
      23, 0, 0xA0, VDP_WORD(words[0]), 1,
      29, 0, 0, 0, 0
    };
    vdpEmit(&walk->sink, call, sizeof(call));
    vdpForgetPen(walk->pen);
  } else if (tag == GFX_CMD_COPY_RECT) {
    vdpCopy(walk, firstX, firstY, words[2], words[3],
      words[4] + walk->origin.x, words[5] + walk->origin.y);
  } else {
    vdpScroll(walk, firstX, firstY, words[2], words[3], words[4], words[5]);
  }

  return GFX_OK;
}

/// @fn static void vdpWalkInit(VdpWalk *walk, VdpOutput *vdpOutput,
///   VdpPen *pen, bool preload)
///
/// @brief Start a walk.
///
/// @param walk The walk.
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param pen The pen the records change and draw with.
/// @param preload Whether the records are being preuploaded.
///
/// @return This function returns no value.
static void vdpWalkInit(VdpWalk *walk, VdpOutput *vdpOutput, VdpPen *pen,
  bool preload
) {
  memset(walk, 0, sizeof(*walk));
  walk->vdpOutput = vdpOutput;
  walk->sink.input = vdpOutput->input;
  walk->draw = vdpOutput->draw;
  walk->pen = pen;
  walk->preload = preload;

  if (preload == false) {
    walk->origin = walk->draw->origin;
  }
}

/// @fn static uint8_t vdpWalkRecords(VdpWalk *walk, const uint8_t *data,
///   uint32_t size)
///
/// @brief Carry out a buffer of command records, stopping at the first that
/// is unknown, cut short, or fails.
///
/// @param walk The walk.
/// @param data The records.
/// @param size The number of bytes at data.
///
/// @return Returns GFX_OK if every record was carried out,
/// GFX_ERR_INVALID_STATE for an unknown or short record, or the GfxError of
/// the record that failed.
static uint8_t vdpWalkRecords(VdpWalk *walk, const uint8_t *data,
  uint32_t size
) {
  uint32_t position = 0;
  while (position < size) {
    uint8_t tag = data[position];
    if (tag >= GFX_CMD_COUNT) {
      return GFX_ERR_INVALID_STATE;
    }

    uint32_t left = size - position;
    uint16_t length = _vdpRecordSizes[tag];

    if ((tag == GFX_CMD_DRAW_TEXT) && (length <= left)) {
      length += data[position + length - 1];
    }
    if (length > left) {
      return GFX_ERR_INVALID_STATE;
    }

    int16_t words[7];
    uint8_t numBytes = sizeof(words);

    if ((length - 1) < numBytes) {
      numBytes = (uint8_t) (length - 1);
    }

    memcpy(words, &data[position + 1], numBytes);
    uint8_t status = vdpRecord(walk, tag, words, &data[position + 1]);
    if (status != GFX_OK) {
      return status;
    }

    position += length;
  }

  return GFX_OK;
}

/// @fn static uint8_t vdpSubmit(VdpOutput *vdpOutput,
///   const GfxSubmitArgs *args)
///
/// @brief Carry out a submitted buffer of command records.  A failure stops
/// the buffer and sets the sticky error.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param args The message's arguments.
///
/// @return Returns the result of vdpWalkRecords.
static uint8_t vdpSubmit(VdpOutput *vdpOutput, const GfxSubmitArgs *args) {
  VdpWalk walk;
  vdpWalkInit(&walk, vdpOutput, &vdpOutput->draw->pen, false);
  uint8_t status = vdpWalkRecords(&walk, args->cmdData, args->cmdSize);

  vdpWrite(walk.sink.data, walk.sink.length);

  if (status != GFX_OK) {
    vdpSetError(vdpOutput, status);
  }

  return status;
}

/// @fn static uint8_t vdpPreupload(VdpOutput *vdpOutput,
///   GfxPreuploadArgs *args)
///
/// @brief Translate a buffer of command records into a VDP buffer that
/// GFX_CMD_CALL runs:  once to count the VDU bytes, which the buffer's write
/// needs first, then to write them.  The preupload starts with the pen the
/// owner has now, and none of it on the VDP.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param args The message's arguments, whose handle is set on success.
///
/// @return Returns GFX_OK on success, GFX_ERR_OUT_OF_HANDLES if every
/// preload slot is used, GFX_ERR_OUT_OF_MEMORY if the VDU bytes don't fit
/// one VDP buffer write, or the result of vdpWalkRecords.
static uint8_t vdpPreupload(VdpOutput *vdpOutput, GfxPreuploadArgs *args) {
  VdpDrawState *draw = vdpOutput->draw;
  uint8_t slot = vdpFreeSlot(draw->preloadsUsed, VDP_MAX_PRELOADS);

  if (slot == VDP_MAX_PRELOADS) {
    return GFX_ERR_OUT_OF_HANDLES;
  }

  GfxHandle id = (GfxHandle) (VDP_PRELOAD_ID_BASE + slot);
  uint32_t length = 0;
  VdpPen pen;
  VdpWalk walk;

  for (uint8_t pass = 0; pass < 2; pass++) {
    pen = draw->pen;
    vdpForgetPen(&pen);
    vdpWalkInit(&walk, vdpOutput, &pen, true);
    walk.sink.counting = (pass == 0);

    if (pass == 1) {
      const uint8_t write[] = {
        23, 0, 0xA0, VDP_WORD(id), 2,
        23, 0, 0xA0, VDP_WORD(id), 0, VDP_WORD(length)
      };
      vdpWrite(write, sizeof(write));
    }

    uint8_t status = vdpWalkRecords(&walk, args->cmdData, args->cmdSize);
    if (status != GFX_OK) {
      return status;
    }

    length = walk.sink.total;
    if (length > 0xFFFF) {
      return GFX_ERR_OUT_OF_MEMORY;
    }
  }

  vdpWrite(walk.sink.data, walk.sink.length);
  draw->preloadsUsed |= (uint16_t) (1 << slot);
  args->handle = id;

  return GFX_OK;
}

/// @fn void* vdpGraphics(void *args)
///
/// @brief Answer the GFX_MSG_SUBMIT or GFX_MSG_PREUPLOAD in the driver's
/// graphicsMessage, which the main overlay sends here instead of to the
/// Graphics overlay so that each costs one overlay call.  Anything else is
/// left untouched, as is a message whose waiting flag is wrong.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpGraphics(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;
  ProcessMessage *message = vdpOutput->graphicsMessage;
  int64_t type = processMessageType(message);
  GfxPreuploadArgs *payload = (GfxPreuploadArgs*) processMessageData(message);

  if (((type & (~((int64_t) 0xff))) != GFX_COMMAND_SIGNATURE)
    || (processMessageWaiting(message) == false) || (payload == NULL)
  ) {
    return args;
  }

  size_t size = sizeof(GfxSubmitArgs);

  if (((uint8_t) type) == GFX_MSG_PREUPLOAD) {
    size = sizeof(GfxPreuploadArgs);
  }

  uint8_t status = GFX_OK;

  if ((processMessageSize(message) != size)
    || ((payload->cmdData == NULL) && (payload->cmdSize > 0))
  ) {
    status = GFX_ERR_INVALID_STATE;
  } else if (vdpOutput->owner != processPid(processMessageFrom(message))) {
    status = GFX_ERR_NOT_OWNER;
  } else if (((uint8_t) type) == GFX_MSG_SUBMIT) {
    status = vdpSubmit(vdpOutput, (GfxSubmitArgs*) payload);
  } else {
    status = vdpPreupload(vdpOutput, payload);
  }

  payload->status = status;

  return args;
}
