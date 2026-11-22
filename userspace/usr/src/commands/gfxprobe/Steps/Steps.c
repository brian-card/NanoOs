///////////////////////////////////////////////////////////////////////////////
///
/// @file              Steps.c
///
/// @brief             gfxprobe's "do" mode:  a sequence of steps against the
///                    display driver, kept out of the main overlay.
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
#include <unistd.h>
#include "GfxProbe.h"

/// @fn static void holdUntilRevoked(void)
///
/// @brief Wait until the display driver says the display was taken back,
/// finishing every other message that arrives meanwhile.
///
/// @return This function returns no value.
static void holdUntilRevoked(void) {
  while (1) {
    ProcessMessage *message = processMessageQueueWait(NULL);
    if (message == NULL) {
      continue;
    }
    int64_t type = processMessageType(message);
    processMessageSetDone(message);
    if (processMessageWaiting(message) == false) {
      processMessageRelease(message);
    }
    if (type == (GFX_COMMAND_SIGNATURE | GFX_MSG_DISPLAY_REVOKED)) {
      printf("gfxprobe: revoked\n");
      return;
    }
  }
}

/// @fn static int runSteps(int numSteps, char **steps)
///
/// @brief Carry out a sequence of steps against the display driver, printing
/// the result of each:  "open", "close", "error", "font" (upload the probe's
/// font), "hold" (wait until the display is taken back), "print <text>",
/// "burst <count>" (print that many numbered lines), "sleep <seconds>", and
/// the Paint, Media, and Input overlays' steps.
///
/// @param numSteps The number of words in steps.
/// @param steps The steps and their arguments.
///
/// @return Returns 0 if every step was understood, nonzero otherwise.
static int runSteps(int numSteps, char **steps) {
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

  int returnValue = 0;
  struct timespec mark;
  timespec_get(&mark, TIME_UTC);
  for (int ii = 0; ii < numSteps; ii++) {
    const char *step = steps[ii];
    const char *argument = (ii + 1 < numSteps) ? steps[ii + 1] : "";
    if (strcmp(step, "open") == 0) {
      GfxError status = gfxOpenDisplay(&disp, GFX_DRIVER_PROCESS_NAME,
        640, 480);
      if (status == GFX_OK) {
        printf("gfxprobe: open no error %ux%u\n",
          (unsigned int) disp.width, (unsigned int) disp.height);
      } else {
        report("open", status);
      }
    } else if (strcmp(step, "close") == 0) {
      report("close", gfxCloseDisplay(&disp));
    } else if (strcmp(step, "error") == 0) {
      report("error", gfxGetError(&disp));
    } else if (strcmp(step, "font") == 0) {
      GfxHandle handle = GFX_INVALID_HANDLE;
      GfxError status = gfxUploadFont(&disp, &font, &handle);
      printf("gfxprobe: font %s 0x%x\n", gfxErrorString(status),
        (unsigned int) handle);
    } else if (strcmp(step, "hold") == 0) {
      holdUntilRevoked();
    } else if (strcmp(step, "print") == 0) {
      printf("gfxprobe: %s\n", argument);
      ii++;
    } else if (strcmp(step, "burst") == 0) {
      int count = (int) strtol(argument, NULL, 10);
      for (int line = 0; line < count; line++) {
        printf("gfxprobe: burst %02d ................\n", line);
      }
      ii++;
    } else if (strcmp(step, "sleep") == 0) {
      sleep((unsigned int) strtol(argument, NULL, 10));
      ii++;
    } else {
      PaintArgs paintArgs = {
        .disp = &disp, .words = &steps[ii], .numWords = numSteps - ii,
        .consumed = 0, .mark = &mark,
      };
      callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Paint", "gfxprobePaint",
        &paintArgs);
      if (paintArgs.consumed == 0) {
        callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Media", "gfxprobeMedia",
          &paintArgs);
      }
      if (paintArgs.consumed == 0) {
        callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Input", "gfxprobeInput",
          &paintArgs);
      }
      if (paintArgs.consumed == 0) {
        printf("gfxprobe: unknown step %s\n", step);
        returnValue = 2;
        break;
      }
      ii += paintArgs.consumed - 1;
    }
  }

  free(bitmap);
  return returnValue;
}

/// @fn void* gfxprobeSteps(void *args)
///
/// @brief Run the steps a StepsArgs holds, storing the exit status.
///
/// @param args A pointer to the StepsArgs.
///
/// @return Always returns the provided pointer.
void* gfxprobeSteps(void *args) {
  StepsArgs *stepsArgs = (StepsArgs*) args;
  stepsArgs->status = runSteps(stepsArgs->numSteps, stepsArgs->steps);
  return args;
}
