///////////////////////////////////////////////////////////////////////////////
///
/// @file              GfxProbe.h
///
/// @brief             What gfxprobe's overlays share:  finding the display
///                    driver, printing results, the probe's font, and the
///                    arguments of the "all" mode's overlays.
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

#ifndef GFX_PROBE_H
#define GFX_PROBE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ExecutiveProcesses.h"
#include "../../../src/kernel/Graphics.h"

/// @def PROBE_FONT_SIZE
///
/// @brief The size of the one-glyph 8x8 font the probe uploads.
#define PROBE_FONT_SIZE 8

/// @struct ProbeArgs
///
/// @brief The arguments of the Resources and Drawing overlays, which run
/// the "all" mode in two halves.
///
/// @param disp The display, connected to the driver.
/// @param status The half's exit status.
typedef struct ProbeArgs {
  GfxDisplay *disp;
  int         status;
} ProbeArgs;

/// @fn static inline int findDriver(ProcessId *pid)
///
/// @brief Look up the display driver and print what was found.
///
/// @param pid Where to store the driver's ProcessId.
///
/// @return Returns 0 if the driver was found, nonzero otherwise.
static inline int findDriver(ProcessId *pid) {
  int status = findProcessByName(GFX_DRIVER_PROCESS_NAME, pid);
  if (status != 0) {
    printf("gfxprobe: no graphics driver (%d)\n", status);
    return 1;
  }
  printf("gfxprobe: graphics driver is pid %d\n", (int) *pid);
  return 0;
}

/// @fn static inline int connectDriver(GfxDisplay *disp)
///
/// @brief Point a GfxDisplay at the display driver without opening the
/// display, so that every message the probe sends is one it chose.
///
/// @param disp The display to set up.
///
/// @return Returns 0 if the driver was found, nonzero otherwise.
static inline int connectDriver(GfxDisplay *disp) {
  memset(disp, 0, sizeof(*disp));
  return findDriver(&disp->driverPid);
}

/// @fn static inline void report(const char *what, GfxError status)
///
/// @brief Print the result of one call.
///
/// @param what The call.
/// @param status Its result.
///
/// @return This function returns no value.
static inline void report(const char *what, GfxError status) {
  printf("gfxprobe: %s %s\n", what, gfxErrorString(status));
}

/// @fn static inline void* probeFont(GfxFontDesc *desc)
///
/// @brief Describe a one-glyph 8x8 font for 'A', with its bitmap on the heap
/// rather than in the probe's overlay.
///
/// @param desc The description to fill in.
///
/// @return Returns the bitmap, which the caller frees, or NULL on failure.
static inline void* probeFont(GfxFontDesc *desc) {
  uint8_t *bitmap = (uint8_t*) malloc(PROBE_FONT_SIZE);
  if (bitmap != NULL) {
    memset(bitmap, 0x18, PROBE_FONT_SIZE);
  }
  desc->glyphW = 8;
  desc->glyphH = 8;
  desc->firstChar = 'A';
  desc->numChars = 1;
  desc->bitmap = bitmap;
  desc->size = PROBE_FONT_SIZE;
  return bitmap;
}

#endif // GFX_PROBE_H
