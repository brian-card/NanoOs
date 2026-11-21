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

/// @file Images.c
///
/// @brief Uploading, capturing and sizing a display owner's images on the
/// Agon Light 2 VDP, kept out of the Graphics overlay, which calls this one
/// for those messages only.

#include "VdpDriver.h"

/// @fn static uint8_t vdpImagePixel(uint8_t format, const uint8_t *pixel)
///
/// @brief Convert an RGBA2222 or RGB888 pixel to the VDP's RGBA2222, which
/// packs red in the bottom two bits and alpha in the top two.
///
/// @param format The pixel's GfxPixelFmt.
/// @param pixel The pixel.
///
/// @return Returns the VDP's pixel, opaque for RGB888.
static uint8_t vdpImagePixel(uint8_t format, const uint8_t *pixel) {
  if (format == GFX_FMT_RGBA2222) {
    uint8_t value = *pixel;
    return (uint8_t) (((value >> 6) & 0x03) | ((value >> 2) & 0x0c)
      | ((value << 2) & 0x30) | ((value << 6) & 0xc0));
  }
  return (uint8_t) (vdpChannelBits(pixel[0]) | (vdpChannelBits(pixel[1]) << 2)
    | (vdpChannelBits(pixel[2]) << 4) | 0xc0);
}

/// @fn static void vdpWritePixels(uint8_t format, const uint8_t *pixels,
///   uint32_t numPixels)
///
/// @brief Write an image's pixels in the VDP's format:  RGBA8888 as they
/// are, the others converted to the VDP's RGBA2222, and none as zeros, which
/// are transparent.
///
/// @param format The pixels' GfxPixelFmt.
/// @param pixels The pixels, or NULL.
/// @param numPixels The number of pixels to write.
///
/// @return This function returns no value.
static void vdpWritePixels(uint8_t format, const uint8_t *pixels,
  uint32_t numPixels
) {
  if (format == GFX_FMT_RGBA8888) {
    if (pixels == NULL) {
      vdpWriteZeros(numPixels * 4);
    } else {
      vdpWrite(pixels, numPixels * 4);
    }
    return;
  } else if (pixels == NULL) {
    vdpWriteZeros(numPixels);
    return;
  }

  uint8_t bytesPerPixel = (format == GFX_FMT_RGB888) ? 3 : 1;
  uint8_t chunk[32];
  while (numPixels > 0) {
    uint8_t length = sizeof(chunk);
    if (numPixels < length) {
      length = (uint8_t) numPixels;
    }
    for (uint8_t ii = 0; ii < length; ii++) {
      chunk[ii] = vdpImagePixel(format, pixels);
      pixels += bytesPerPixel;
    }
    vdpWrite(chunk, length);
    numPixels -= length;
  }
}

/// @fn static uint8_t vdpUploadImage(VdpOutput *vdpOutput,
///   GfxUploadImageArgs *args)
///
/// @brief Upload an image to the first free image slot:  clear its buffer
/// with VDU 23,0,&A0,id;2, write the pixels with VDU 23,0,&A0,id;0,length;
/// in blocks of at most 0xFFFF bytes, consolidate several blocks with
/// VDU 23,0,&A0,id;&0E, select the bitmap with VDU 23,27,&20,id; and make it
/// with VDU 23,27,&21,width;height;format.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param args The message's arguments, whose handle is set on success.
///
/// @return Returns GFX_OK on success, GFX_ERR_INVALID_STATE if the format,
/// size or pixels are wrong, or GFX_ERR_OUT_OF_HANDLES if every image slot is
/// used.
static uint8_t vdpUploadImage(VdpOutput *vdpOutput, GfxUploadImageArgs *args) {
  static const uint8_t bytesPerPixel[] = { 4, 1, 3 };
  VdpDrawState *draw = vdpOutput->draw;
  if ((args->format > GFX_FMT_RGB888) || (args->width == 0)
    || (args->height == 0)
  ) {
    return GFX_ERR_INVALID_STATE;
  }
  uint32_t numPixels = (uint32_t) args->width * args->height;
  if ((args->pixels != NULL)
    && (args->size != numPixels * bytesPerPixel[args->format])
  ) {
    return GFX_ERR_INVALID_STATE;
  }
  uint8_t slot = vdpFreeSlot(draw->imagesUsed, VDP_MAX_IMAGES);
  if (slot == VDP_MAX_IMAGES) {
    return GFX_ERR_OUT_OF_HANDLES;
  }

  GfxHandle id = (GfxHandle) (VDP_IMAGE_ID_BASE + slot);
  uint8_t vdpBytesPerPixel = (args->format == GFX_FMT_RGBA8888) ? 4 : 1;
  uint32_t pixelsPerBlock = 0xFFFF / vdpBytesPerPixel;
  const uint8_t *pixels = (const uint8_t*) args->pixels;
  const uint8_t clear[] = { 23, 0, 0xA0, VDP_WORD(id), 2 };
  vdpWrite(clear, sizeof(clear));
  uint32_t left = numPixels;
  while (left > 0) {
    uint32_t blockPixels = (left < pixelsPerBlock) ? left : pixelsPerBlock;
    uint32_t blockSize = blockPixels * vdpBytesPerPixel;
    const uint8_t write[] = {
      23, 0, 0xA0, VDP_WORD(id), 0, VDP_WORD(blockSize)
    };
    vdpWrite(write, sizeof(write));
    vdpWritePixels(args->format, pixels, blockPixels);
    if (pixels != NULL) {
      pixels += blockPixels * bytesPerPixel[args->format];
    }
    left -= blockPixels;
  }
  if (numPixels > pixelsPerBlock) {
    const uint8_t consolidate[] = { 23, 0, 0xA0, VDP_WORD(id), 0x0E };
    vdpWrite(consolidate, sizeof(consolidate));
  }
  const uint8_t make[] = {
    23, 27, 0x20, VDP_WORD(id),
    23, 27, 0x21, VDP_WORD(args->width), VDP_WORD(args->height),
    (uint8_t) ((args->format == GFX_FMT_RGBA8888) ? 0 : 1)
  };
  vdpWrite(make, sizeof(make));
  draw->imagesUsed |= (uint16_t) (1 << slot);
  draw->imageWidths[slot] = args->width;
  draw->imageHeights[slot] = args->height;
  args->handle = id;
  return GFX_OK;
}

/// @fn static uint8_t vdpImageSize(VdpOutput *vdpOutput,
///   GfxImageSizeArgs *args)
///
/// @brief Answer with an image's size.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param args The message's arguments.
///
/// @return Returns GFX_OK on success, GFX_ERR_INVALID_HANDLE if the image
/// doesn't exist.
static uint8_t vdpImageSize(VdpOutput *vdpOutput, GfxImageSizeArgs *args) {
  VdpDrawState *draw = vdpOutput->draw;
  uint8_t slot = vdpSlot(draw->imagesUsed, args->handle, VDP_IMAGE_ID_BASE,
    VDP_MAX_IMAGES);
  if (slot == VDP_MAX_IMAGES) {
    return GFX_ERR_INVALID_HANDLE;
  }
  args->width = draw->imageWidths[slot];
  args->height = draw->imageHeights[slot];
  return GFX_OK;
}

/// @fn static uint8_t vdpCaptureRect(VdpOutput *vdpOutput,
///   GfxCaptureRectArgs *args)
///
/// @brief Make an image of the part of a rectangle of the screen that is on
/// the screen:  move to its corners with PLOT 4, then capture between them
/// with VDU 23,27,&21,id;0;.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param args The message's arguments, whose handle is set on success.
///
/// @return Returns GFX_OK on success, GFX_ERR_INVALID_STATE if none of the
/// rectangle is on the screen, or GFX_ERR_OUT_OF_HANDLES if every image slot
/// is used.
static uint8_t vdpCaptureRect(VdpOutput *vdpOutput,
  GfxCaptureRectArgs *args
) {
  VdpDrawState *draw = vdpOutput->draw;
  GfxRect *rect = &args->rect;
  int left = (rect->x > 0) ? rect->x : 0;
  int top = (rect->y > 0) ? rect->y : 0;
  int right = rect->x + rect->w;
  int bottom = rect->y + rect->h;
  if (right > VDP_SCREEN_WIDTH) {
    right = VDP_SCREEN_WIDTH;
  }
  if (bottom > VDP_SCREEN_HEIGHT) {
    bottom = VDP_SCREEN_HEIGHT;
  }
  if ((right <= left) || (bottom <= top)) {
    return GFX_ERR_INVALID_STATE;
  }
  uint8_t slot = vdpFreeSlot(draw->imagesUsed, VDP_MAX_IMAGES);
  if (slot == VDP_MAX_IMAGES) {
    return GFX_ERR_OUT_OF_HANDLES;
  }

  GfxHandle id = (GfxHandle) (VDP_IMAGE_ID_BASE + slot);
  const uint8_t capture[] = {
    25, 4, VDP_WORD(left), VDP_WORD(top),
    25, 4, VDP_WORD(right - 1), VDP_WORD(bottom - 1),
    23, 27, 0x21, VDP_WORD(id), 0, 0
  };
  vdpWrite(capture, sizeof(capture));
  draw->imagesUsed |= (uint16_t) (1 << slot);
  draw->imageWidths[slot] = (uint16_t) (right - left);
  draw->imageHeights[slot] = (uint16_t) (bottom - top);
  args->handle = id;
  return GFX_OK;
}

/// @fn void* vdpImages(void *args)
///
/// @brief Answer the GFX_MSG_UPLOAD_IMAGE, GFX_MSG_GET_IMAGE_SIZE or
/// GFX_MSG_CAPTURE_RECT in the driver's graphicsMessage, which the Graphics
/// overlay has checked comes from the display's owner and has the right
/// arguments.
///
/// @param args A pointer to the driver's VdpOutput.
///
/// @return Always returns the provided pointer.
void* vdpImages(void *args) {
  VdpOutput *vdpOutput = (VdpOutput*) args;
  ProcessMessage *message = vdpOutput->graphicsMessage;
  uint8_t type = (uint8_t) processMessageType(message);
  void *payload = processMessageData(message);
  uint8_t status = GFX_OK;
  if (type == GFX_MSG_UPLOAD_IMAGE) {
    status = vdpUploadImage(vdpOutput, (GfxUploadImageArgs*) payload);
  } else if (type == GFX_MSG_GET_IMAGE_SIZE) {
    status = vdpImageSize(vdpOutput, (GfxImageSizeArgs*) payload);
  } else {
    status = vdpCaptureRect(vdpOutput, (GfxCaptureRectArgs*) payload);
  }
  *((uint8_t*) payload) = status;
  return args;
}
