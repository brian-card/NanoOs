///////////////////////////////////////////////////////////////////////////////
///
/// @file              Paint.c
///
/// @brief             The gfxprobe steps that submit and preupload files of
///                    command records.
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

/// @fn static GfxCmdBuf* recordFile(GfxDisplay *disp, const char *path,
///   uint8_t flags)
///
/// @brief Make a command buffer whose recording is a file's bytes, as they
/// are, so that a test can send any records, broken ones included.
///
/// @param disp The display.
/// @param path The file of records.
/// @param flags The gfxCmdBufBegin flags.
///
/// @return Returns the command buffer, which the caller destroys, or NULL
/// on failure.
static GfxCmdBuf* recordFile(GfxDisplay *disp, const char *path,
  uint8_t flags
) {
  size_t size = 0;
  uint8_t *data = (uint8_t*) readFile(path, &size);

  if (data == NULL) {
    return NULL;
  }

  GfxCmdBuf *buf = gfxCmdBufCreate(disp, (size > 0) ? size : 1);
  if (buf != NULL) {
    gfxCmdBufBegin(buf, flags);
    uint8_t *record = gfxCmdBufReserve_(buf, size);
    if (record != NULL) {
      memcpy(record, data, size);
    }
  }

  free(data);

  return buf;
}

/// @fn static GfxError submitFile(GfxDisplay *disp, const char *path)
///
/// @brief Submit a file of records as one buffer and wait for the driver's
/// status for it.
///
/// @param disp The display.
/// @param path The file of records.
///
/// @return Returns the driver's status, or GFX_ERR_OUT_OF_MEMORY if the file
/// couldn't be read.
static GfxError submitFile(GfxDisplay *disp, const char *path) {
  GfxCmdBuf *buf = recordFile(disp, path, 0);
  if (buf == NULL) {
    return GFX_ERR_OUT_OF_MEMORY;
  }

  GfxError status = gfxSubmit(buf);

  // gfxSubmit switched buffers; settling the one it sent folds in its status.
  gfxCmdBufSlot_(buf, buf->submitArgs[buf->writeIdx ^ 1], buf->writeIdx ^ 1,
    false);

  if (status == GFX_OK) {
    status = (GfxError) buf->error;
  }

  gfxCmdBufDestroy(buf);

  return status;
}

/// @fn static GfxError preloadFile(GfxDisplay *disp, const char *path,
///   GfxHandle *handle)
///
/// @brief Preupload a file of records.
///
/// @param disp The display.
/// @param path The file of records.
/// @param handle Where to store the preupload's handle.
///
/// @return Returns the status of the preupload.
static GfxError preloadFile(GfxDisplay *disp, const char *path,
  GfxHandle *handle
) {
  *handle = GFX_INVALID_HANDLE;
  GfxCmdBuf *buf = recordFile(disp, path, GFX_CMDBUF_NO_FLUSH);
  if (buf == NULL) {
    return GFX_ERR_OUT_OF_MEMORY;
  }

  GfxError status = gfxPreupload(buf, handle);

  gfxCmdBufDestroy(buf);

  return status;
}

/// @fn void* gfxprobePaint(void *args)
///
/// @brief Carry out "submit <file>" or "preload <file>", printing the
/// result.
///
/// @param args A pointer to the PaintArgs, whose consumed stays 0 for any
///   other step.
///
/// @return Always returns the provided pointer.
void* gfxprobePaint(void *args) {
  PaintArgs *paintArgs = (PaintArgs*) args;
  GfxDisplay *disp = paintArgs->disp;
  char **words = paintArgs->words;
  GfxHandle handle = GFX_INVALID_HANDLE;

  if (paintArgs->numWords < 2) {
    return args;
  } else if (strcmp(words[0], "submit") == 0) {
    report("submit", submitFile(disp, words[1]));
    paintArgs->consumed = 2;
  } else if (strcmp(words[0], "preload") == 0) {
    GfxError status = preloadFile(disp, words[1], &handle);
    printf("gfxprobe: preload %s 0x%x\n", gfxErrorString(status),
      (unsigned int) handle);
    paintArgs->consumed = 2;
  }

  return args;
}
