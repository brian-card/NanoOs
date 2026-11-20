///////////////////////////////////////////////////////////////////////////////
///
/// @file              Resources.c
///
/// @brief             The first half of gfxprobe's "all" mode:  the display,
///                    image and font messages.
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
#include "GfxProbe.h"

/// @fn void* gfxprobeResources(void *args)
///
/// @brief Send the display driver each display, image and font message type
/// once, in protocol order, and print each result.
///
/// @param args A pointer to the ProbeArgs, whose status is set to 0 if
///   there was memory for the font and 1 otherwise.
///
/// @return Always returns the provided pointer.
void* gfxprobeResources(void *args) {
  ProbeArgs *probeArgs = (ProbeArgs*) args;
  GfxDisplay *disp = probeArgs->disp;
  GfxFontDesc font;
  void *bitmap = probeFont(&font);
  if (bitmap == NULL) {
    printf("gfxprobe: no memory\n");
    probeArgs->status = 1;
    return args;
  }

  GfxDisplay opened;
  GfxHandle handle = GFX_INVALID_HANDLE;
  GfxImageDesc image = { 1, 1, GFX_FMT_RGBA2222, NULL, 1 };
  uint16_t width = 0, height = 0;
  report("OPEN_DISPLAY", gfxOpenDisplay(&opened, GFX_DRIVER_PROCESS_NAME,
    640, 480));
  report("CLOSE_DISPLAY", gfxCloseDisplay(disp));
  report("GET_SURFACE_SIZE", gfxGetSurfaceSize(disp, &width, &height));
  report("UPLOAD_IMAGE", gfxUploadImage(disp, &image, &handle));
  report("DESTROY_IMAGE", gfxDestroyImage(disp, 1));
  report("GET_IMAGE_SIZE", gfxGetImageSize(disp, 1, &width, &height));
  report("UPLOAD_FONT", gfxUploadFont(disp, &font, &handle));
  report("DESTROY_FONT", gfxDestroyFont(disp, handle));
  report("SET_DEFAULT_FONT", gfxSetDefaultFont(disp, GFX_INVALID_HANDLE));

  free(bitmap);
  probeArgs->status = 0;
  return args;
}
