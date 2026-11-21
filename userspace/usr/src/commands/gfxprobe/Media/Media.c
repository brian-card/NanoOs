///////////////////////////////////////////////////////////////////////////////
///
/// @file              Media.c
///
/// @brief             The gfxprobe steps that upload, size, destroy and
///                    capture images, discard preuploads, and wait for the
///                    display driver to finish.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "GfxProbe.h"

CGA_COLOR_PALETTE

/// @fn static GfxError uploadImage(GfxDisplay *disp, char **words,
///   GfxHandle *handle)
///
/// @brief Upload an image whose format, width, height and pixels are given
/// as "rgba8888", "rgba2222" or "rgb888", two numbers, and a file of pixels,
/// "-" for none, or "pattern" for RGBA8888 pixels whose colour at (x, y) is
/// CGA color (x + y) % 16.
///
/// @param disp The display.
/// @param words The format, width, height and pixels.
/// @param handle Where to store the image's handle.
///
/// @return Returns the status of the upload.
static GfxError uploadImage(GfxDisplay *disp, char **words,
  GfxHandle *handle
) {
  GfxImageDesc desc = { 0 };
  desc.format = GFX_FMT_RGB888;
  if (strcmp(words[0], "rgba8888") == 0) {
    desc.format = GFX_FMT_RGBA8888;
  } else if (strcmp(words[0], "rgba2222") == 0) {
    desc.format = GFX_FMT_RGBA2222;
  }
  desc.width = (uint16_t) strtol(words[1], NULL, 0);
  desc.height = (uint16_t) strtol(words[2], NULL, 0);
  uint8_t *pixels = NULL;
  if (strcmp(words[3], "pattern") == 0) {
    desc.format = GFX_FMT_RGBA8888;
    desc.size = (size_t) desc.width * desc.height * 4;
    pixels = (uint8_t*) malloc(desc.size);
    if (pixels == NULL) {
      return GFX_ERR_OUT_OF_MEMORY;
    }
    for (size_t ii = 0; ii < (size_t) desc.width * desc.height; ii++) {
      GfxColor color = cgaColorPalette[
        ((ii % desc.width) + (ii / desc.width)) % 16];
      pixels[(ii * 4) + 0] = (uint8_t) (color >> 24);
      pixels[(ii * 4) + 1] = (uint8_t) (color >> 16);
      pixels[(ii * 4) + 2] = (uint8_t) (color >> 8);
      pixels[(ii * 4) + 3] = (uint8_t) color;
    }
  } else if (strcmp(words[3], "-") != 0) {
    pixels = (uint8_t*) readFile(words[3], &desc.size);
    if (pixels == NULL) {
      return GFX_ERR_OUT_OF_MEMORY;
    }
  }
  desc.pixels = pixels;
  GfxError status = gfxUploadImage(disp, &desc, handle);
  free(pixels);
  return status;
}

/// @fn static long elapsedMs(const struct timespec *start)
///
/// @brief Get the time since a moment.
///
/// @param start The moment.
///
/// @return Returns the milliseconds since start.
static long elapsedMs(const struct timespec *start) {
  struct timespec now;
  timespec_get(&now, TIME_UTC);
  return ((long) (now.tv_sec - start->tv_sec) * 1000L)
    + ((now.tv_nsec - start->tv_nsec) / 1000000L);
}

/// @fn void* gfxprobeMedia(void *args)
///
/// @brief Carry out one step, printing its result:
/// "image <format> <width> <height> <pixels>",
/// "size <handle>", "destroy <handle>", "discard <handle>",
/// "capture <x> <y> <width> <height>", "fill" (upload 1x1 images until one
/// fails), "time" (print the time since the last "time" step), and
/// "finish", which prints how long it waited.
///
/// @param args A pointer to the PaintArgs, whose consumed stays 0 for any
///   other step.
///
/// @return Always returns the provided pointer.
void* gfxprobeMedia(void *args) {
  PaintArgs *paintArgs = (PaintArgs*) args;
  GfxDisplay *disp = paintArgs->disp;
  char **words = paintArgs->words;
  int numWords = paintArgs->numWords;
  const char *step = words[0];
  GfxHandle handle = GFX_INVALID_HANDLE;

  if ((strcmp(step, "image") == 0) && (numWords >= 5)) {
    GfxError status = uploadImage(disp, &words[1], &handle);
    printf("gfxprobe: image %s 0x%x\n", gfxErrorString(status),
      (unsigned int) handle);
    paintArgs->consumed = 5;
  } else if ((strcmp(step, "size") == 0) && (numWords >= 2)) {
    uint16_t width = 0, height = 0;
    GfxError status = gfxGetImageSize(disp,
      (GfxHandle) strtol(words[1], NULL, 0), &width, &height);
    printf("gfxprobe: size %s %ux%u\n", gfxErrorString(status),
      (unsigned int) width, (unsigned int) height);
    paintArgs->consumed = 2;
  } else if ((strcmp(step, "destroy") == 0) && (numWords >= 2)) {
    report("destroy", gfxDestroyImage(disp,
      (GfxHandle) strtol(words[1], NULL, 0)));
    paintArgs->consumed = 2;
  } else if ((strcmp(step, "discard") == 0) && (numWords >= 2)) {
    report("discard", gfxDiscardPreupload(disp,
      (GfxHandle) strtol(words[1], NULL, 0)));
    paintArgs->consumed = 2;
  } else if ((strcmp(step, "capture") == 0) && (numWords >= 5)) {
    GfxRect rect = {
      (int16_t) strtol(words[1], NULL, 0), (int16_t) strtol(words[2], NULL, 0),
      (int16_t) strtol(words[3], NULL, 0), (int16_t) strtol(words[4], NULL, 0)
    };
    GfxError status = gfxCaptureRect(disp, &rect, &handle);
    printf("gfxprobe: capture %s 0x%x\n", gfxErrorString(status),
      (unsigned int) handle);
    paintArgs->consumed = 5;
  } else if (strcmp(step, "fill") == 0) {
    GfxImageDesc desc = { 1, 1, GFX_FMT_RGBA2222, NULL, 1 };
    GfxError status = GFX_OK;
    int count = 0;
    while ((status = gfxUploadImage(disp, &desc, &handle)) == GFX_OK) {
      count++;
    }
    printf("gfxprobe: fill %d %s\n", count, gfxErrorString(status));
    paintArgs->consumed = 1;
  } else if (strcmp(step, "time") == 0) {
    printf("gfxprobe: time %ld ms\n", elapsedMs(paintArgs->mark));
    timespec_get(paintArgs->mark, TIME_UTC);
    paintArgs->consumed = 1;
  } else if (strcmp(step, "finish") == 0) {
    struct timespec start;
    timespec_get(&start, TIME_UTC);
    GfxError status = gfxFinish(disp);
    printf("gfxprobe: finish %s %ld ms\n", gfxErrorString(status),
      elapsedMs(&start));
    paintArgs->consumed = 1;
  }
  return args;
}
