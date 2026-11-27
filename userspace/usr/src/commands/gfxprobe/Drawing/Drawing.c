///////////////////////////////////////////////////////////////////////////////
///
/// @file              Drawing.c
///
/// @brief             The second half of gfxprobe's "all" mode:  the command
///                    buffer, synchronization, error and cursor messages,
///                    then one whose size is wrong.
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
#include "GfxProbe.h"

/// @fn void* gfxprobeDrawing(void *args)
///
/// @brief Send the display driver each command buffer, synchronization,
/// error and cursor message type once, in protocol order, then a
/// GFX_MSG_GET_ERROR whose size is wrong, then close the display, and print
/// each result.
///
/// @param args A pointer to the ProbeArgs, whose status is set to 0 if
///   there was memory for the command buffer and 1 otherwise.
///
/// @return Always returns the provided pointer.
void* gfxprobeDrawing(void *args) {
  ProbeArgs *probeArgs = (ProbeArgs*) args;
  GfxDisplay *disp = probeArgs->disp;
  GfxCmdBuf *buf = gfxCmdBufCreate(disp, 64);

  if (buf == NULL) {
    printf("gfxprobe: no memory\n");
    probeArgs->status = 1;
    return args;
  }

  GfxHandle handle = GFX_INVALID_HANDLE;
  GfxRect rect = { 0, 0, 1, 1 };

  gfxCmdBufBegin(buf, 0);
  gfxCmdDrawRect(buf, &rect, true);
  GfxError status = gfxSubmit(buf);

  gfxCmdDrawRect(buf, &rect, true);

  if (status == GFX_OK) {
    status = gfxSubmit(buf);
  }

  report("SUBMIT", status);
  gfxCmdBufBegin(buf, GFX_CMDBUF_NO_FLUSH);
  gfxCmdDrawRect(buf, &rect, true);
  report("PREUPLOAD", gfxPreupload(buf, &handle));
  report("DISCARD_PREUPLOAD", gfxDiscardPreupload(disp, 1));
  report("FINISH", gfxFinish(disp));
  report("CAPTURE_RECT", gfxCaptureRect(disp, &rect, &handle));
  report("GET_ERROR", gfxGetError(disp));
  report("SET_CURSOR", gfxSetCursor(disp, GFX_INVALID_HANDLE, 0, 0));
  report("SHOW_CURSOR", gfxShowCursor(disp, true));
  report("WARP_CURSOR", gfxWarpCursor(disp, 1, 1));
  MESSAGE_ARGS_ALLOCATE(GfxGetErrorArgs, wrongSize);
  report("WRONG_SIZE", gfxCall_(disp, GFX_MSG_GET_ERROR, wrongSize,
    sizeof(*wrongSize) + 1));
  MESSAGE_ARGS_FREE(wrongSize);
  report("CLOSE_DISPLAY", gfxCloseDisplay(disp));

  gfxCmdBufDestroy(buf);
  probeArgs->status = 0;

  return probeArgs;
}
