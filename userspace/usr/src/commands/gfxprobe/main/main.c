///////////////////////////////////////////////////////////////////////////////
///
/// @file              main.c
///
/// @brief             A test command that exercises the display driver from
///                    a graphics client through Graphics.h.
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
#include "GfxProbe.h"

/// @fn static int sendToDriver(void)
///
/// @brief Ask the display driver for its error and print the answer.
///
/// @return Returns 0 if the driver answered, nonzero otherwise.
static int sendToDriver(void) {
  GfxDisplay disp;
  if (connectDriver(&disp) != 0) {
    return 1;
  }
  GfxError status = gfxGetError(&disp);
  report("GET_ERROR", status);
  return (status == GFX_ERR_DEVICE_LOST) ? 1 : 0;
}

/// @fn static int cycleFonts(void)
///
/// @brief Open the display, upload and destroy a font four times, printing
/// each handle, try to make the destroyed font the default, and close the
/// display.
///
/// @return Returns 0 if there was memory for the font, nonzero otherwise.
static int cycleFonts(void) {
  GfxDisplay disp;
  if (connectDriver(&disp) != 0) {
    return 1;
  }
  GfxFontDesc font;
  void *bitmap = probeFont(&font);
  if (bitmap == NULL) {
    printf("gfxprobe: no memory\n");
    return 1;
  }

  report("open", gfxOpenDisplay(&disp, GFX_DRIVER_PROCESS_NAME, 640, 480));
  GfxHandle handle = GFX_INVALID_HANDLE;
  for (int ii = 1; ii <= 4; ii++) {
    GfxError status = gfxUploadFont(&disp, &font, &handle);
    printf("gfxprobe: upload %d %s 0x%x\n", ii, gfxErrorString(status),
      (unsigned int) handle);
    report("destroy", gfxDestroyFont(&disp, handle));
  }
  report("default destroyed", gfxSetDefaultFont(&disp, handle));
  report("close", gfxCloseDisplay(&disp));

  free(bitmap);
  return 0;
}

/// @fn static int sendEveryType(void)
///
/// @brief Send the display driver every client message type once, in
/// protocol order, then one whose size is wrong, printing each result.  The
/// two halves are in separate overlays because the whole doesn't fit in one.
///
/// @return Returns 0 if every message was sent, nonzero otherwise.
static int sendEveryType(void) {
  GfxDisplay disp;
  if (connectDriver(&disp) != 0) {
    return 1;
  }
  ProbeArgs probeArgs = { .disp = &disp, .status = 1 };
  if ((callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Resources",
      "gfxprobeResources", &probeArgs) != &probeArgs)
    || (probeArgs.status != 0)
  ) {
    return 1;
  }
  probeArgs.status = 1;
  if (callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Drawing",
      "gfxprobeDrawing", &probeArgs) != &probeArgs
  ) {
    return 1;
  }
  return probeArgs.status;
}

int main(int argc, char **argv) {
  if ((argc == 2) && (strcmp(argv[1], "--help") == 0)) {
    printf("Usage: gfxprobe find|send|all|fonts|do <step>...\n");
    return 0;
  } else if ((argc == 2) && (strcmp(argv[1], "find") == 0)) {
    ProcessId pid = 0;
    return findDriver(&pid);
  } else if ((argc == 2) && (strcmp(argv[1], "send") == 0)) {
    return sendToDriver();
  } else if ((argc == 2) && (strcmp(argv[1], "fonts") == 0)) {
    return cycleFonts();
  } else if ((argc == 2) && (strcmp(argv[1], "all") == 0)) {
    return sendEveryType();
  } else if ((argc > 2) && (strcmp(argv[1], "do") == 0)) {
    StepsArgs stepsArgs = {
      .numSteps = argc - 2, .steps = &argv[2], .status = 1,
    };
    if (callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Steps", "gfxprobeSteps",
      &stepsArgs) != &stepsArgs
    ) {
      return 1;
    }
    return stepsArgs.status;
  }

  fprintf(stderr, "Usage: gfxprobe find|send|all|fonts|do <step>...\n");
  return 2;
}
