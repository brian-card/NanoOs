///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              05.22.2026
///
/// @file              Graphics.h
///
/// @brief             The client side of the graphics driver protocol in
///                    GraphicsProtocol.h:  display ownership, resources,
///                    command recording and submission, and text
///                    measurement from cached font metrics.
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

#ifndef GRAPHICS_H
#define GRAPHICS_H

// Standard C includes
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// NanoOs includes
#include "GraphicsProtocol.h"
#ifdef NANO_OS_OVERLAY_BUILD
#include "ExecutiveProcesses.h"
#else
#include "Processes.h"
#include "../hal/HalCommon.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// @def GFX_OUT_OF_LINE
///
/// @brief Declare a helper that every wrapper calls out of line, so that each
/// wrapper costs a call rather than a copy of the helper.  `unused` keeps a
/// translation unit that calls none of a helper's wrappers warning-free.
#define GFX_OUT_OF_LINE static __attribute__((noinline, unused))

/// @fn static inline const char *gfxErrorString(GfxError err)
///
/// @brief Describe a GfxError.
///
/// @param err The error to describe.
///
/// @return Returns a constant string.
static inline const char *gfxErrorString(GfxError err) {
    switch (err) {
    case GFX_OK:                  return "no error";
    case GFX_ERR_OUT_OF_MEMORY:   return "out of memory";
    case GFX_ERR_OUT_OF_HANDLES:  return "out of handles";
    case GFX_ERR_CMDBUF_OVERFLOW: return "command buffer overflow";
    case GFX_ERR_INVALID_HANDLE:  return "invalid handle";
    case GFX_ERR_INVALID_STATE:   return "invalid state";
    case GFX_ERR_DEVICE_LOST:     return "device lost";
    case GFX_ERR_NOT_SUPPORTED:   return "not supported";
    case GFX_ERR_DISPLAY_BUSY:    return "display busy";
    case GFX_ERR_NOT_OWNER:       return "not the display owner";
    default:                      return "unknown error";
    }
}


// =========================================================================
// CLIENT-SIDE STRUCTURES
// =========================================================================

typedef struct GfxImageDesc {
    uint16_t       width, height;
    GfxPixelFmt    format;
    const void    *pixels;    // may be NULL to allocate without initializing;
                              // not in the caller's overlay
    size_t         size;      // byte count of pixels buffer
} GfxImageDesc;

typedef struct GfxFontDesc {
    uint8_t      glyphW, glyphH;  // pixel dimensions of each glyph cell
    uint8_t      firstChar;       // first codepoint in the bitmap
    uint16_t     numChars;        // number of glyphs
    const void  *bitmap;          // 1-bit glyph rows, MSB leftmost, each row
                                  // padded to a whole byte; not in the
                                  // caller's overlay
    size_t       size;
} GfxFontDesc;

typedef struct GfxFontCacheEntry {
    GfxHandle handle;
    uint8_t   glyphW, glyphH;
    uint8_t   firstChar;
    uint16_t  numChars;
} GfxFontCacheEntry;

/// @struct GfxDisplay
///
/// @brief A client's connection to the display driver.
///
/// @param driverPid The ProcessId of the driver.
/// @param width The width of the display, in pixels.
/// @param height The height of the display, in pixels.
/// @param builtInFont The metrics of the driver's built-in font.
/// @param defaultFont The font GFX_INVALID_HANDLE stands for, or
///   GFX_INVALID_HANDLE for the built-in font.
/// @param fontCount The number of entries in fontCache.
/// @param fontCache The metrics of each font the client has uploaded.
typedef struct GfxDisplay {
    ProcessId         driverPid;
    uint16_t          width, height;
    GfxFontCacheEntry builtInFont;
    GfxHandle         defaultFont;
    uint8_t           fontCount;
    GfxFontCacheEntry fontCache[GFX_MAX_FONTS];
} GfxDisplay;


// =========================================================================
// TRANSPORT
// =========================================================================

/// @fn static inline GfxError gfxCall_(GfxDisplay *disp, uint8_t type,
///   void *args, size_t size)
///
/// @brief Send the driver a synchronous message and wait for it to be done,
/// with the status at the start of its arguments preset to
/// GFX_ERR_DEVICE_LOST.  The wait has no timeout because the kernel marks
/// done every message to a driver that ends.
///
/// @param disp The display.
/// @param type The GfxMsgType.
/// @param args The arguments, from MESSAGE_ARGS_ALLOCATE, or NULL if there
///   was no storage for them.
/// @param size The size of the arguments.
///
/// @return Returns the status the driver set, GFX_ERR_OUT_OF_MEMORY if there
/// was no storage or message, or GFX_ERR_DEVICE_LOST if the send failed.
GFX_OUT_OF_LINE GfxError gfxCall_(GfxDisplay *disp, uint8_t type,
    void *args, size_t size
) {
    if (args == NULL) {
        return GFX_ERR_OUT_OF_MEMORY;
    }
    *((uint8_t*) args) = GFX_ERR_DEVICE_LOST;
    ProcessMessage *message = getAvailableMessage();
    if (message == NULL) {
        return GFX_ERR_OUT_OF_MEMORY;
    }
    processMessageInit(message, GFX_COMMAND_SIGNATURE | type,
        args, size, /* waiting= */ true);
    if (sendProcessMessageToPid(disp->driverPid, message) != processSuccess) {
        processMessageRelease(message);
        return GFX_ERR_DEVICE_LOST;
    }
    processMessageWaitForDone(message, NULL);
    processMessageRelease(message);
    return (GfxError) *((uint8_t*) args);
}

/// @fn static inline GfxError gfxPost_(GfxDisplay *disp, uint8_t type,
///   void *args, size_t size)
///
/// @brief Send the driver a one-way message, which then owns its arguments,
/// or destroy the arguments if it couldn't be sent.
///
/// @param disp The display.
/// @param type The GfxMsgType.
/// @param args The arguments, from MESSAGE_ARGS_CREATE with
///   MESSAGE_ARGS_ONE_WAY, or NULL for a message that has none.
/// @param size The size of the arguments.
///
/// @return Returns GFX_OK if the message was sent, GFX_ERR_OUT_OF_MEMORY if
/// there was no message, or GFX_ERR_DEVICE_LOST if the send failed.
GFX_OUT_OF_LINE GfxError gfxPost_(GfxDisplay *disp, uint8_t type,
    void *args, size_t size
) {
    GfxError status = GFX_ERR_OUT_OF_MEMORY;
    ProcessMessage *message = getAvailableMessage();
    if (message != NULL) {
        processMessageInit(message, GFX_COMMAND_SIGNATURE | type,
            args, size, /* waiting= */ false);
        if (sendProcessMessageToPid(disp->driverPid, message)
            == processSuccess
        ) {
            return GFX_OK;
        }
        processMessageRelease(message);
        status = GFX_ERR_DEVICE_LOST;
    }
    if (args != NULL) {
        MESSAGE_ARGS_DESTROY(args);
    }
    return status;
}

/// @fn static inline GfxError gfxPostHandle_(GfxDisplay *disp, uint8_t type,
///   GfxHandle handle)
///
/// @brief Send the driver a one-way message whose only argument is a handle.
///
/// @param disp The display.
/// @param type The GfxMsgType.
/// @param handle The handle.
///
/// @return Returns the result of gfxPost_, or GFX_ERR_OUT_OF_MEMORY if there
/// was no storage for the arguments.
GFX_OUT_OF_LINE GfxError gfxPostHandle_(GfxDisplay *disp,
    uint8_t type, GfxHandle handle
) {
    GfxHandleArgs *args = MESSAGE_ARGS_CREATE(GfxHandleArgs,
        MESSAGE_ARGS_ONE_WAY);
    if (args == NULL) {
        return GFX_ERR_OUT_OF_MEMORY;
    }
    args->handle = handle;
    return gfxPost_(disp, type, args, sizeof(*args));
}


// =========================================================================
// DISPLAY
// =========================================================================

/// @fn static inline GfxError gfxOpenDisplay(GfxDisplay *disp,
///   const char *driverName, uint16_t width, uint16_t height)
///
/// @brief Find the driver registered under a name and take ownership of its
/// display, caching the display's size and the built-in font's metrics.
///
/// @param disp The display to initialize.
/// @param driverName The name the driver is registered under, normally
///   GFX_DRIVER_PROCESS_NAME.
/// @param width The requested width, in pixels.
/// @param height The requested height, in pixels.
///
/// @return Returns GFX_OK on success, GFX_ERR_DEVICE_LOST if no driver has
/// the name, or the driver's status otherwise.
static inline GfxError gfxOpenDisplay(GfxDisplay *disp,
    const char *driverName, uint16_t width, uint16_t height
) {
    memset(disp, 0, sizeof(*disp));
    ProcessId driverPid = PROCESS_ID_NOT_SET;
    if (findProcessByName(driverName, &driverPid) != 0) {
        disp->driverPid = PROCESS_ID_NOT_SET;
        return GFX_ERR_DEVICE_LOST;
    }
    disp->driverPid = driverPid;

    MESSAGE_ARGS_ALLOCATE(GfxOpenDisplayArgs, args);
    if (args != NULL) {
        args->width  = width;
        args->height = height;
    }
    GfxError status = gfxCall_(disp, GFX_MSG_OPEN_DISPLAY,
        args, sizeof(*args));
    if (status == GFX_OK) {
        disp->width                 = args->width;
        disp->height                = args->height;
        disp->builtInFont.glyphW    = args->defaultFontW;
        disp->builtInFont.glyphH    = args->defaultFontH;
        disp->builtInFont.firstChar = args->defaultFontFirst;
        disp->builtInFont.numChars  = args->defaultFontCount;
    }
    MESSAGE_ARGS_FREE(args);
    return status;
}

/// @fn static inline GfxError gfxCloseDisplay(GfxDisplay *disp)
///
/// @brief Give up ownership of the display, which frees everything the
/// client created on it.
///
/// @param disp The display.
///
/// @return Returns the result of gfxPost_.
static inline GfxError gfxCloseDisplay(GfxDisplay *disp) {
    GfxError status = gfxPost_(disp, GFX_MSG_CLOSE_DISPLAY, NULL, 0);
    disp->fontCount   = 0;
    disp->defaultFont = GFX_INVALID_HANDLE;
    return status;
}

/// @fn static inline GfxError gfxGetSurfaceSize(GfxDisplay *disp,
///   uint16_t *outWidth, uint16_t *outHeight)
///
/// @brief Ask the driver for the display's size, and update the cached size
/// with the answer.
///
/// @param disp The display.
/// @param outWidth Where to store the width, or NULL.
/// @param outHeight Where to store the height, or NULL.
///
/// @return Returns the status of the query.  The outputs are written only on
/// GFX_OK.
static inline GfxError gfxGetSurfaceSize(GfxDisplay *disp,
    uint16_t *outWidth, uint16_t *outHeight
) {
    MESSAGE_ARGS_ALLOCATE(GfxSurfaceSizeArgs, args);
    GfxError status = gfxCall_(disp, GFX_MSG_GET_SURFACE_SIZE,
        args, sizeof(*args));
    if (status == GFX_OK) {
        disp->width  = args->width;
        disp->height = args->height;
        if (outWidth)  *outWidth  = args->width;
        if (outHeight) *outHeight = args->height;
    }
    MESSAGE_ARGS_FREE(args);
    return status;
}


// =========================================================================
// IMAGES
// =========================================================================

/// @fn static inline GfxError gfxUploadImage(GfxDisplay *disp,
///   const GfxImageDesc *desc, GfxHandle *outImage)
///
/// @brief Upload an image to the driver.
///
/// @param disp The display.
/// @param desc The image.
/// @param outImage Where to store the new image's handle, which is
///   GFX_INVALID_HANDLE on failure.
///
/// @return Returns the status of the upload.
static inline GfxError gfxUploadImage(GfxDisplay *disp,
    const GfxImageDesc *desc, GfxHandle *outImage
) {
    *outImage = GFX_INVALID_HANDLE;
    MESSAGE_ARGS_ALLOCATE(GfxUploadImageArgs, args);
    if (args == NULL) {
        return GFX_ERR_OUT_OF_MEMORY;
    }
    args->format = (uint8_t) desc->format;
    args->width  = desc->width;
    args->height = desc->height;
    args->size   = (uint32_t) desc->size;
    args->pixels = MESSAGE_BUFFER_SHARE(desc->pixels, desc->size,
        MESSAGE_BUFFER_IN, disp->driverPid);
    GfxError status = GFX_ERR_OUT_OF_MEMORY;
    if ((args->pixels != NULL) || (desc->pixels == NULL)) {
        status = gfxCall_(disp, GFX_MSG_UPLOAD_IMAGE, args, sizeof(*args));
        MESSAGE_BUFFER_UNSHARE(args->pixels, desc->pixels,
            MESSAGE_BUFFER_IN);
    }
    if (status == GFX_OK) {
        *outImage = args->handle;
    }
    MESSAGE_ARGS_FREE(args);
    return status;
}

/// @fn static inline GfxError gfxDestroyImage(GfxDisplay *disp,
///   GfxHandle image)
///
/// @brief Free an uploaded or captured image.  One-way.
///
/// @param disp The display.
/// @param image The image, or GFX_INVALID_HANDLE to do nothing.
///
/// @return Returns the result of sending the message.
static inline GfxError gfxDestroyImage(GfxDisplay *disp, GfxHandle image) {
    if (image == GFX_INVALID_HANDLE) {
        return GFX_OK;
    }
    return gfxPostHandle_(disp, GFX_MSG_DESTROY_IMAGE, image);
}

/// @fn static inline GfxError gfxGetImageSize(GfxDisplay *disp,
///   GfxHandle image, uint16_t *outW, uint16_t *outH)
///
/// @brief Ask the driver for an image's size.
///
/// @param disp The display.
/// @param image The image.
/// @param outW Where to store the width, or NULL.
/// @param outH Where to store the height, or NULL.
///
/// @return Returns the status of the query.  The outputs are written only on
/// GFX_OK.
static inline GfxError gfxGetImageSize(GfxDisplay *disp, GfxHandle image,
    uint16_t *outW, uint16_t *outH
) {
    MESSAGE_ARGS_ALLOCATE(GfxImageSizeArgs, args);
    if (args != NULL) {
        args->handle = image;
    }
    GfxError status = gfxCall_(disp, GFX_MSG_GET_IMAGE_SIZE,
        args, sizeof(*args));
    if (status == GFX_OK) {
        if (outW) *outW = args->width;
        if (outH) *outH = args->height;
    }
    MESSAGE_ARGS_FREE(args);
    return status;
}

/// @fn static inline GfxError gfxCaptureRect(GfxDisplay *disp,
///   const GfxRect *rect, GfxHandle *outImage)
///
/// @brief Copy a rectangle of the screen into a new image, which the caller
/// frees with gfxDestroyImage.
///
/// @param disp The display.
/// @param rect The rectangle, in screen coordinates.
/// @param outImage Where to store the image's handle, which is
///   GFX_INVALID_HANDLE on failure.
///
/// @return Returns the status of the capture.
static inline GfxError gfxCaptureRect(GfxDisplay *disp, const GfxRect *rect,
    GfxHandle *outImage
) {
    *outImage = GFX_INVALID_HANDLE;
    MESSAGE_ARGS_ALLOCATE(GfxCaptureRectArgs, args);
    if (args != NULL) {
        args->rect = *rect;
    }
    GfxError status = gfxCall_(disp, GFX_MSG_CAPTURE_RECT,
        args, sizeof(*args));
    if (status == GFX_OK) {
        *outImage = args->handle;
    }
    MESSAGE_ARGS_FREE(args);
    return status;
}


// =========================================================================
// FONTS
// =========================================================================

/// @fn static inline GfxFontCacheEntry* gfxFontEntry_(GfxDisplay *disp,
///   GfxHandle font)
///
/// @brief Find the cached metrics of a font, with GFX_INVALID_HANDLE standing
/// for the default font.
///
/// @param disp The display.
/// @param font The font.
///
/// @return Returns the font's metrics, or NULL if the client didn't upload
/// it.
GFX_OUT_OF_LINE GfxFontCacheEntry* gfxFontEntry_(
    GfxDisplay *disp, GfxHandle font
) {
    if (font == GFX_INVALID_HANDLE) {
        font = disp->defaultFont;
        if (font == GFX_INVALID_HANDLE) {
            return &disp->builtInFont;
        }
    }
    for (uint8_t ii = 0; ii < disp->fontCount; ii++) {
        if (disp->fontCache[ii].handle == font) {
            return &disp->fontCache[ii];
        }
    }
    return NULL;
}

/// @fn static inline GfxError gfxUploadFont(GfxDisplay *disp,
///   const GfxFontDesc *desc, GfxHandle *outFont)
///
/// @brief Upload a font and cache its metrics for text measurement.
///
/// @param disp The display.
/// @param desc The font.
/// @param outFont Where to store the font's handle, which is
///   GFX_INVALID_HANDLE on failure.
///
/// @return Returns GFX_ERR_OUT_OF_HANDLES without sending anything if
/// GFX_MAX_FONTS fonts are already uploaded, the status of the upload
/// otherwise.
static inline GfxError gfxUploadFont(GfxDisplay *disp,
    const GfxFontDesc *desc, GfxHandle *outFont
) {
    *outFont = GFX_INVALID_HANDLE;
    if (disp->fontCount >= GFX_MAX_FONTS) {
        return GFX_ERR_OUT_OF_HANDLES;
    }
    MESSAGE_ARGS_ALLOCATE(GfxUploadFontArgs, args);
    if (args == NULL) {
        return GFX_ERR_OUT_OF_MEMORY;
    }
    args->glyphW    = desc->glyphW;
    args->glyphH    = desc->glyphH;
    args->firstChar = desc->firstChar;
    args->numChars  = desc->numChars;
    args->size      = (uint32_t) desc->size;
    args->bitmap    = MESSAGE_BUFFER_SHARE(desc->bitmap, desc->size,
        MESSAGE_BUFFER_IN, disp->driverPid);
    GfxError status = GFX_ERR_OUT_OF_MEMORY;
    if ((args->bitmap != NULL) || (desc->bitmap == NULL)) {
        status = gfxCall_(disp, GFX_MSG_UPLOAD_FONT, args, sizeof(*args));
        MESSAGE_BUFFER_UNSHARE(args->bitmap, desc->bitmap,
            MESSAGE_BUFFER_IN);
    }
    if (status == GFX_OK) {
        GfxFontCacheEntry *entry = &disp->fontCache[disp->fontCount++];
        entry->handle    = args->handle;
        entry->glyphW    = desc->glyphW;
        entry->glyphH    = desc->glyphH;
        entry->firstChar = desc->firstChar;
        entry->numChars  = desc->numChars;
        *outFont     = args->handle;
    }
    MESSAGE_ARGS_FREE(args);
    return status;
}

/// @fn static inline GfxError gfxDestroyFont(GfxDisplay *disp,
///   GfxHandle font)
///
/// @brief Free an uploaded font and forget its metrics.  One-way.
///
/// @param disp The display.
/// @param font The font, or GFX_INVALID_HANDLE to do nothing.
///
/// @return Returns GFX_ERR_INVALID_STATE without sending anything if the font
/// is the default font, which gfxSetDefaultFont(disp, GFX_INVALID_HANDLE)
/// changes back to the built-in one, or the result of sending the message
/// otherwise.
static inline GfxError gfxDestroyFont(GfxDisplay *disp, GfxHandle font) {
    if (font == GFX_INVALID_HANDLE) {
        return GFX_OK;
    }
    if (font == disp->defaultFont) {
        return GFX_ERR_INVALID_STATE;
    }

    for (uint8_t ii = 0; ii < disp->fontCount; ii++) {
        if (disp->fontCache[ii].handle == font) {
            for (uint8_t jj = ii; jj + 1 < disp->fontCount; jj++)
                disp->fontCache[jj] = disp->fontCache[jj + 1];
            disp->fontCount--;
            break;
        }
    }
    return gfxPostHandle_(disp, GFX_MSG_DESTROY_FONT, font);
}

/// @fn static inline GfxError gfxSetDefaultFont(GfxDisplay *disp,
///   GfxHandle font)
///
/// @brief Make an uploaded font, or the built-in one, the font that
/// GFX_INVALID_HANDLE stands for in records and measurements.
///
/// @param disp The display.
/// @param font The font, or GFX_INVALID_HANDLE for the built-in font.
///
/// @return Returns the driver's status.
static inline GfxError gfxSetDefaultFont(GfxDisplay *disp, GfxHandle font) {
    MESSAGE_ARGS_ALLOCATE(GfxSetDefaultFontArgs, args);
    if (args != NULL) {
        args->handle = font;
    }
    GfxError status = gfxCall_(disp, GFX_MSG_SET_DEFAULT_FONT,
        args, sizeof(*args));
    if (status == GFX_OK) {
        disp->defaultFont = font;
    }
    MESSAGE_ARGS_FREE(args);
    return status;
}


// =========================================================================
// TEXT MEASUREMENT  (client-side, no IPC)
//
// Every measurement uses cached metrics of fixed-width fonts, works in
// uint32_t, and saturates at UINT16_MAX.
// =========================================================================

/// @fn static inline uint16_t gfxSaturate_(uint32_t value)
///
/// @brief Clamp a measurement to what a uint16_t holds.
///
/// @param value The measurement.
///
/// @return Returns value, or UINT16_MAX if it's larger.
static inline uint16_t gfxSaturate_(uint32_t value) {
    return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t) value;
}

/// @fn static inline void gfxGetGlyphSize(GfxDisplay *disp, GfxHandle font,
///   uint8_t *outW, uint8_t *outH)
///
/// @brief Get a font's glyph cell size.
///
/// @param disp The display.
/// @param font The font.
/// @param outW Where to store the width, or NULL; 0 for an unknown font.
/// @param outH Where to store the height, or NULL; 0 for an unknown font.
///
/// @return This function returns no value.
static inline void gfxGetGlyphSize(GfxDisplay *disp, GfxHandle font,
    uint8_t *outW, uint8_t *outH
) {
    GfxFontCacheEntry *entry = gfxFontEntry_(disp, font);
    if (outW) *outW = (entry != NULL) ? entry->glyphW : 0;
    if (outH) *outH = (entry != NULL) ? entry->glyphH : 0;
}

/// @fn static inline uint16_t gfxMeasureText(GfxDisplay *disp,
///   GfxHandle font, const char *str)
///
/// @brief Measure the width of a single-line string.
///
/// @param disp The display.
/// @param font The font.
/// @param str The string, or NULL.
///
/// @return Returns the width in pixels, 0 for an unknown font.
static inline uint16_t gfxMeasureText(GfxDisplay *disp, GfxHandle font,
    const char *str
) {
    GfxFontCacheEntry *entry = gfxFontEntry_(disp, font);
    if ((str == NULL) || (entry == NULL)) return 0;
    size_t len = strlen(str);
    if (len > UINT16_MAX) len = UINT16_MAX;
    return gfxSaturate_(((uint32_t) len) * entry->glyphW);
}

/// @fn static inline bool gfxMeasureTextSize(GfxDisplay *disp,
///   GfxHandle font, const char *str, uint16_t *outW, uint16_t *outH)
///
/// @brief Measure the width of the longest line and the height of a string
/// whose lines are separated by '\\n'.
///
/// @param disp The display.
/// @param font The font.
/// @param str The string, or NULL for an empty one.
/// @param outW Where to store the width, or NULL.
/// @param outH Where to store the height, or NULL.
///
/// @return Returns false for an unknown font, true otherwise.
static inline bool gfxMeasureTextSize(GfxDisplay *disp, GfxHandle font,
    const char *str, uint16_t *outW, uint16_t *outH
) {
    GfxFontCacheEntry *entry = gfxFontEntry_(disp, font);
    if (entry == NULL) return false;

    uint32_t maxLineLen = 0;
    uint32_t curLineLen = 0;
    uint32_t lines = 1;
    for (const char *character = str;
        (character != NULL) && (*character != '\0'); character++
    ) {
        if (*character == '\n') {
            if (curLineLen > maxLineLen) maxLineLen = curLineLen;
            curLineLen = 0;
            if (lines < UINT16_MAX) lines++;
        } else if (curLineLen < UINT16_MAX) {
            curLineLen++;
        }
    }
    if (curLineLen > maxLineLen) maxLineLen = curLineLen;

    if (outW) *outW = gfxSaturate_(maxLineLen * entry->glyphW);
    if (outH) *outH = gfxSaturate_(lines * entry->glyphH);
    return true;
}

/// @fn static inline uint16_t gfxFitText(GfxDisplay *disp, GfxHandle font,
///   const char *str, uint16_t maxWidth)
///
/// @brief Count how many leading characters of a string fit a width.
///
/// @param disp The display.
/// @param font The font.
/// @param str The string, or NULL.
/// @param maxWidth The width in pixels.
///
/// @return Returns the count, 0 for an unknown font.
static inline uint16_t gfxFitText(GfxDisplay *disp, GfxHandle font,
    const char *str, uint16_t maxWidth
) {
    GfxFontCacheEntry *entry = gfxFontEntry_(disp, font);
    if ((str == NULL) || (entry == NULL) || (entry->glyphW == 0)) return 0;
    uint32_t fits = maxWidth / entry->glyphW;
    size_t len = strlen(str);
    return (fits < len) ? (uint16_t) fits : gfxSaturate_((uint32_t) len);
}

/// @fn static inline uint16_t gfxHitTestText(GfxDisplay *disp,
///   GfxHandle font, const char *str, uint16_t pixelX)
///
/// @brief Map a pixel offset into a string to the index of the nearest
/// character boundary.
///
/// @param disp The display.
/// @param font The font.
/// @param str The string, or NULL.
/// @param pixelX The offset in pixels.
///
/// @return Returns the index, at most the string's length, 0 for an unknown
/// font.
static inline uint16_t gfxHitTestText(GfxDisplay *disp, GfxHandle font,
    const char *str, uint16_t pixelX
) {
    GfxFontCacheEntry *entry = gfxFontEntry_(disp, font);
    if ((str == NULL) || (entry == NULL) || (entry->glyphW == 0)) return 0;
    uint32_t idx = (((uint32_t) pixelX) + (entry->glyphW / 2)) / entry->glyphW;
    size_t len = strlen(str);
    return (idx < len) ? (uint16_t) idx : gfxSaturate_((uint32_t) len);
}

/// @fn static inline bool gfxGetFontRange(GfxDisplay *disp, GfxHandle font,
///   uint8_t *outFirst, uint16_t *outCount)
///
/// @brief Get the codepoints a font has glyphs for.
///
/// @param disp The display.
/// @param font The font.
/// @param outFirst Where to store the first codepoint, or NULL.
/// @param outCount Where to store the number of glyphs, or NULL.
///
/// @return Returns false for an unknown font, true otherwise.
static inline bool gfxGetFontRange(GfxDisplay *disp, GfxHandle font,
    uint8_t *outFirst, uint16_t *outCount
) {
    GfxFontCacheEntry *entry = gfxFontEntry_(disp, font);
    if (entry == NULL) return false;
    if (outFirst) *outFirst = entry->firstChar;
    if (outCount) *outCount = entry->numChars;
    return true;
}


// =========================================================================
// COMMAND BUFFER
//
// Two equal buffers.  Records go into the write buffer; a submit shares it
// with the driver, sends it as a persistent round trip, and switches to the
// other buffer, first waiting for the driver to be done with that one.
// =========================================================================

/// @def GFX_CMDBUF_NO_FLUSH
///
/// @brief A gfxCmdBufBegin flag:  a record that doesn't fit sets
/// GFX_ERR_CMDBUF_OVERFLOW instead of submitting what has been recorded.
#define GFX_CMDBUF_NO_FLUSH 0x01

/// @struct GfxCmdBuf
///
/// @brief A double-buffered command recording.
///
/// @param disp The display it submits to.
/// @param data The two buffers.
/// @param capacity The size of each buffer.
/// @param used The bytes recorded in data[writeIdx].
/// @param writeIdx The buffer being recorded into.
/// @param flags The GFX_CMDBUF_* flags of the recording.
/// @param error The first GfxError since gfxCmdBufBegin or gfxSubmit.
/// @param inFlight The GFX_MSG_SUBMIT of each buffer the driver may still be
///   reading, or NULL.
/// @param submitArgs The arguments of each buffer's GFX_MSG_SUBMIT, which
///   live as long as the command buffer.
typedef struct GfxCmdBuf {
    GfxDisplay     *disp;
    uint8_t        *data[2];
    size_t          capacity;
    size_t          used;
    uint8_t         writeIdx;
    uint8_t         flags;
    uint8_t         error;
    ProcessMessage *inFlight[2];
    GfxSubmitArgs  *submitArgs[2];
} GfxCmdBuf;

/// @fn static inline void gfxCmdBufFail_(GfxCmdBuf *buf, GfxError error)
///
/// @brief Record an error in a command buffer unless one is already there.
///
/// @param buf The command buffer.
/// @param error The error.
///
/// @return This function returns no value.
static inline void gfxCmdBufFail_(GfxCmdBuf *buf, GfxError error) {
    if (buf->error == GFX_OK) {
        buf->error = (uint8_t) error;
    }
}

/// @fn static inline void gfxCmdBufSlot_(GfxCmdBuf *buf,
///   GfxSubmitArgs *args, uint8_t idx, bool send)
///
/// @brief Wait for the driver to be done with one buffer, folding its status
/// into the sticky error and ending its sharing, then, if asked, share and
/// submit that buffer's recording.  Settling and sending are one function so
/// that every share of a buffer is paired with its unshare in one place.
///
/// @param buf The command buffer.
/// @param args buf->submitArgs[idx].
/// @param idx The buffer.
/// @param send Whether to submit the buffer once it is settled.
///
/// @return This function returns no value.
GFX_OUT_OF_LINE void gfxCmdBufSlot_(GfxCmdBuf *buf,
    GfxSubmitArgs *args, uint8_t idx, bool send
) {
    ProcessMessage *message = buf->inFlight[idx];
    if (message != NULL) {
        processMessageWaitForDone(message, NULL);
        if (args->status != GFX_OK) {
            gfxCmdBufFail_(buf, (GfxError) args->status);
        }
        MESSAGE_BUFFER_UNSHARE(args->cmdData, buf->data[idx],
            MESSAGE_BUFFER_IN);
        args->cmdData = NULL;
        processMessageRelease(message);
        buf->inFlight[idx] = NULL;
    }
    if (send == false) {
        return;
    }

    args->status  = GFX_ERR_DEVICE_LOST;
    args->cmdSize = (uint32_t) buf->used;
    args->cmdData = MESSAGE_BUFFER_SHARE(buf->data[idx], buf->used,
        MESSAGE_BUFFER_IN, buf->disp->driverPid);
    if (args->cmdData == NULL) {
        gfxCmdBufFail_(buf, GFX_ERR_OUT_OF_MEMORY);
        return;
    }
    message = getAvailableMessage();
    GfxError status = GFX_ERR_OUT_OF_MEMORY;
    if (message != NULL) {
        processMessageInit(message, GFX_COMMAND_SIGNATURE | GFX_MSG_SUBMIT,
            args, sizeof(*args), /* waiting= */ true);
        if (sendProcessMessageToPid(buf->disp->driverPid, message)
            == processSuccess
        ) {
            buf->inFlight[idx] = message;
            return;
        }
        processMessageRelease(message);
        status = GFX_ERR_DEVICE_LOST;
    }
    MESSAGE_BUFFER_UNSHARE(args->cmdData, buf->data[idx], MESSAGE_BUFFER_IN);
    args->cmdData = NULL;
    gfxCmdBufFail_(buf, status);
}

/// @fn static inline void gfxCmdBufFlush_(GfxCmdBuf *buf)
///
/// @brief Submit the write buffer if anything is recorded in it, then switch
/// to the other buffer once the driver is done with it.
///
/// @param buf The command buffer.
///
/// @return This function returns no value.
GFX_OUT_OF_LINE void gfxCmdBufFlush_(GfxCmdBuf *buf) {
    uint8_t idx = buf->writeIdx;
    if (buf->used == 0) {
        return;
    }
    gfxCmdBufSlot_(buf, buf->submitArgs[idx], idx, true);
    idx ^= 1;
    buf->writeIdx = idx;
    buf->used = 0;
    gfxCmdBufSlot_(buf, buf->submitArgs[idx], idx, false);
}

/// @fn static inline GfxCmdBuf *gfxCmdBufCreate(GfxDisplay *disp,
///   size_t capacity)
///
/// @brief Allocate a command buffer of two buffers of a given size.
///
/// @param disp The display it submits to.
/// @param capacity The size of each buffer.
///
/// @return Returns the command buffer, or NULL on failure.
static inline GfxCmdBuf *gfxCmdBufCreate(GfxDisplay *disp, size_t capacity) {
    GfxCmdBuf *buf = (GfxCmdBuf *)calloc(1, sizeof(*buf));
    if (!buf) return NULL;
    buf->disp     = disp;
    buf->capacity = capacity;
    for (int ii = 0; ii < 2; ii++) {
        buf->data[ii] = (uint8_t *)malloc(capacity);
        buf->submitArgs[ii] = MESSAGE_ARGS_CREATE(GfxSubmitArgs,
            MESSAGE_ARGS_ROUND_TRIP);
    }
    if (!buf->data[0] || !buf->data[1]
        || !buf->submitArgs[0] || !buf->submitArgs[1]
    ) {
        for (int ii = 0; ii < 2; ii++) {
            free(buf->data[ii]);
            if (buf->submitArgs[ii] != NULL) {
                MESSAGE_ARGS_DESTROY(buf->submitArgs[ii]);
            }
        }
        free(buf);
        return NULL;
    }
    return buf;
}

/// @fn static inline GfxCmdBuf *gfxCmdBufDestroy(GfxCmdBuf *buf)
///
/// @brief Wait for the driver to be done with both buffers, then free the
/// command buffer.  Anything recorded and not submitted is discarded.
///
/// @param buf The command buffer, or NULL.
///
/// @return Always returns NULL, for `buf = gfxCmdBufDestroy(buf);`.
static inline GfxCmdBuf *gfxCmdBufDestroy(GfxCmdBuf *buf) {
    if (!buf) return NULL;
    for (uint8_t ii = 0; ii < 2; ii++) {
        gfxCmdBufSlot_(buf, buf->submitArgs[ii], ii, false);
        free(buf->data[ii]);
        MESSAGE_ARGS_DESTROY(buf->submitArgs[ii]);
    }
    free(buf);
    return NULL;
}

/// @fn static inline void gfxCmdBufBegin(GfxCmdBuf *buf, uint8_t flags)
///
/// @brief Start a recording, discarding anything recorded and not submitted
/// and clearing the sticky error.
///
/// @param buf The command buffer.
/// @param flags GFX_CMDBUF_* flags, or 0.
///
/// @return This function returns no value.
static inline void gfxCmdBufBegin(GfxCmdBuf *buf, uint8_t flags) {
    buf->used  = 0;
    buf->flags = flags;
    buf->error = GFX_OK;
}

/// @fn static inline uint8_t* gfxCmdBufReserve_(GfxCmdBuf *buf, size_t size)
///
/// @brief Make room for a whole record in the write buffer, submitting what
/// is already recorded if the record doesn't fit after it.
///
/// @param buf The command buffer.
/// @param size The record's size, its tag included.
///
/// @return Returns where to write the record, or NULL, with
/// GFX_ERR_CMDBUF_OVERFLOW recorded, if it can't fit in an empty buffer or
/// the recording is GFX_CMDBUF_NO_FLUSH.
GFX_OUT_OF_LINE uint8_t* gfxCmdBufReserve_(GfxCmdBuf *buf,
    size_t size
) {
    if (size > buf->capacity - buf->used) {
        if ((size > buf->capacity)
            || ((buf->flags & GFX_CMDBUF_NO_FLUSH) != 0)
        ) {
            gfxCmdBufFail_(buf, GFX_ERR_CMDBUF_OVERFLOW);
            return NULL;
        }
        gfxCmdBufFlush_(buf);
    }
    uint8_t *record = buf->data[buf->writeIdx] + buf->used;
    buf->used += size;
    return record;
}

/// @fn static inline void gfxCmdRecord_(GfxCmdBuf *buf, GfxCmdType tag,
///   const void *params, size_t size)
///
/// @brief Record a fixed-size record.
///
/// @param buf The command buffer.
/// @param tag The record's GfxCmdType.
/// @param params The record's packed parameters.
/// @param size The size of the parameters.
///
/// @return This function returns no value.
GFX_OUT_OF_LINE void gfxCmdRecord_(GfxCmdBuf *buf, GfxCmdType tag,
    const void *params, size_t size
) {
    uint8_t *record = gfxCmdBufReserve_(buf, 1 + size);
    if (record != NULL) {
        record[0] = (uint8_t) tag;
        if (size > 0) {
            memcpy(&record[1], params, size);
        }
    }
}

/// @fn static inline void gfxCmdRecordRect_(GfxCmdBuf *buf, GfxCmdType tag,
///   const GfxRect *rect, const void *extra, size_t extraSize)
///
/// @brief Record a record whose parameters are a rectangle and up to four
/// more bytes, or record GFX_ERR_INVALID_STATE if the rectangle is NULL.
///
/// @param buf The command buffer.
/// @param tag The record's GfxCmdType.
/// @param rect The rectangle.
/// @param extra The bytes after the rectangle.
/// @param extraSize The number of bytes at extra.
///
/// @return This function returns no value.
GFX_OUT_OF_LINE void gfxCmdRecordRect_(GfxCmdBuf *buf,
    GfxCmdType tag, const GfxRect *rect, const void *extra, size_t extraSize
) {
    if (rect == NULL) {
        gfxCmdBufFail_(buf, GFX_ERR_INVALID_STATE);
        return;
    }
    uint8_t params[sizeof(GfxRect) + 4];
    memcpy(params, rect, sizeof(GfxRect));
    if (extraSize > 0) {
        memcpy(&params[sizeof(GfxRect)], extra, extraSize);
    }
    gfxCmdRecord_(buf, tag, params, sizeof(GfxRect) + extraSize);
}

/// @fn static inline GfxError gfxSubmit(GfxCmdBuf *buf)
///
/// @brief Submit what has been recorded, and get and clear the sticky error.
/// The driver's status for a submit is folded into the error once the
/// client next waits for that buffer.
///
/// @param buf The command buffer.
///
/// @return Returns the first error since gfxCmdBufBegin or the last
/// gfxSubmit.
static inline GfxError gfxSubmit(GfxCmdBuf *buf) {
    gfxCmdBufFlush_(buf);
    GfxError error = (GfxError) buf->error;
    buf->error = GFX_OK;
    return error;
}

/// @fn static inline GfxError gfxPreupload(GfxCmdBuf *buf,
///   GfxHandle *outHandle)
///
/// @brief Upload a whole recording, begun with GFX_CMDBUF_NO_FLUSH, for later
/// GFX_CMD_CALL records, instead of executing it.  The recording is consumed
/// either way.
///
/// @param buf The command buffer.
/// @param outHandle Where to store the preupload's handle, which is
///   GFX_INVALID_HANDLE on failure.
///
/// @return Returns the recording's sticky error without sending anything if
/// there is one, the status of the preupload otherwise.
static inline GfxError gfxPreupload(GfxCmdBuf *buf, GfxHandle *outHandle) {
    *outHandle = GFX_INVALID_HANDLE;
    GfxError status = (GfxError) buf->error;
    uint8_t *data = buf->data[buf->writeIdx];
    size_t used = buf->used;
    buf->used  = 0;
    buf->error = GFX_OK;
    if (status != GFX_OK) {
        return status;
    }

    MESSAGE_ARGS_ALLOCATE(GfxPreuploadArgs, args);
    if (args == NULL) {
        return GFX_ERR_OUT_OF_MEMORY;
    }
    args->cmdSize = (uint32_t) used;
    args->cmdData = MESSAGE_BUFFER_SHARE(data, used, MESSAGE_BUFFER_IN,
        buf->disp->driverPid);
    status = GFX_ERR_OUT_OF_MEMORY;
    if (args->cmdData != NULL) {
        status = gfxCall_(buf->disp, GFX_MSG_PREUPLOAD, args, sizeof(*args));
        MESSAGE_BUFFER_UNSHARE(args->cmdData, data, MESSAGE_BUFFER_IN);
    }
    if (status == GFX_OK) {
        *outHandle = args->handle;
    }
    MESSAGE_ARGS_FREE(args);
    return status;
}

/// @fn static inline GfxError gfxDiscardPreupload(GfxDisplay *disp,
///   GfxHandle handle)
///
/// @brief Free a preupload.  One-way.
///
/// @param disp The display.
/// @param handle The preupload, or GFX_INVALID_HANDLE to do nothing.
///
/// @return Returns the result of sending the message.
static inline GfxError gfxDiscardPreupload(GfxDisplay *disp,
    GfxHandle handle
) {
    if (handle == GFX_INVALID_HANDLE) {
        return GFX_OK;
    }
    return gfxPostHandle_(disp, GFX_MSG_DISCARD_PREUPLOAD, handle);
}


// =========================================================================
// COMMAND RECORDING  (client-side, no IPC)
// =========================================================================

/// @fn static inline void gfxCmdSetViewport(GfxCmdBuf *buf,
///   const GfxRect *rect)
///
/// @brief Record GFX_CMD_SET_VIEWPORT.
///
/// @param buf The command buffer.
/// @param rect The viewport, in screen coordinates.
///
/// @return This function returns no value.
static inline void gfxCmdSetViewport(GfxCmdBuf *buf, const GfxRect *rect) {
    gfxCmdRecordRect_(buf, GFX_CMD_SET_VIEWPORT, rect, NULL, 0);
}

/// @fn static inline void gfxCmdSetOrigin(GfxCmdBuf *buf, GfxPoint origin)
///
/// @brief Record GFX_CMD_SET_ORIGIN.
///
/// @param buf The command buffer.
/// @param origin The screen position of record coordinate (0,0).
///
/// @return This function returns no value.
static inline void gfxCmdSetOrigin(GfxCmdBuf *buf, GfxPoint origin) {
    gfxCmdRecord_(buf, GFX_CMD_SET_ORIGIN, &origin, sizeof(origin));
}

/// @fn static inline void gfxCmdPushScissor(GfxCmdBuf *buf,
///   const GfxRect *rect)
///
/// @brief Record GFX_CMD_PUSH_SCISSOR.
///
/// @param buf The command buffer.
/// @param rect The scissor, relative to the origin.
///
/// @return This function returns no value.
static inline void gfxCmdPushScissor(GfxCmdBuf *buf, const GfxRect *rect) {
    gfxCmdRecordRect_(buf, GFX_CMD_PUSH_SCISSOR, rect, NULL, 0);
}

/// @fn static inline void gfxCmdPopScissor(GfxCmdBuf *buf)
///
/// @brief Record GFX_CMD_POP_SCISSOR.
///
/// @param buf The command buffer.
///
/// @return This function returns no value.
static inline void gfxCmdPopScissor(GfxCmdBuf *buf) {
    gfxCmdRecord_(buf, GFX_CMD_POP_SCISSOR, NULL, 0);
}

/// @fn static inline void gfxCmdSetFgColor(GfxCmdBuf *buf, GfxColor fg)
///
/// @brief Record GFX_CMD_SET_FG_COLOR.
///
/// @param buf The command buffer.
/// @param fg The foreground color.
///
/// @return This function returns no value.
static inline void gfxCmdSetFgColor(GfxCmdBuf *buf, GfxColor fg) {
    gfxCmdRecord_(buf, GFX_CMD_SET_FG_COLOR, &fg, sizeof(fg));
}

/// @fn static inline void gfxCmdSetBgColor(GfxCmdBuf *buf, GfxColor bg)
///
/// @brief Record GFX_CMD_SET_BG_COLOR.
///
/// @param buf The command buffer.
/// @param bg The background color.
///
/// @return This function returns no value.
static inline void gfxCmdSetBgColor(GfxCmdBuf *buf, GfxColor bg) {
    gfxCmdRecord_(buf, GFX_CMD_SET_BG_COLOR, &bg, sizeof(bg));
}

/// @fn static inline void gfxCmdSetBlend(GfxCmdBuf *buf, GfxBlendMode mode)
///
/// @brief Record GFX_CMD_SET_BLEND.
///
/// @param buf The command buffer.
/// @param mode The blend mode.
///
/// @return This function returns no value.
static inline void gfxCmdSetBlend(GfxCmdBuf *buf, GfxBlendMode mode) {
    uint8_t blendMode = (uint8_t) mode;
    gfxCmdRecord_(buf, GFX_CMD_SET_BLEND, &blendMode, sizeof(blendMode));
}

/// @fn static inline void gfxCmdSetFont(GfxCmdBuf *buf, GfxHandle font)
///
/// @brief Record GFX_CMD_SET_FONT.
///
/// @param buf The command buffer.
/// @param font The font, or GFX_INVALID_HANDLE for the default font.
///
/// @return This function returns no value.
static inline void gfxCmdSetFont(GfxCmdBuf *buf, GfxHandle font) {
    gfxCmdRecord_(buf, GFX_CMD_SET_FONT, &font, sizeof(font));
}

/// @fn static inline void gfxCmdClear(GfxCmdBuf *buf)
///
/// @brief Record GFX_CMD_CLEAR.
///
/// @param buf The command buffer.
///
/// @return This function returns no value.
static inline void gfxCmdClear(GfxCmdBuf *buf) {
    gfxCmdRecord_(buf, GFX_CMD_CLEAR, NULL, 0);
}

/// @fn static inline void gfxCmdDrawRect(GfxCmdBuf *buf,
///   const GfxRect *rect, bool filled)
///
/// @brief Record GFX_CMD_DRAW_RECT.
///
/// @param buf The command buffer.
/// @param rect The rectangle.
/// @param filled Whether to fill it rather than outline it.
///
/// @return This function returns no value.
static inline void gfxCmdDrawRect(GfxCmdBuf *buf, const GfxRect *rect,
    bool filled
) {
    uint8_t filledByte = filled ? 1 : 0;
    gfxCmdRecordRect_(buf, GFX_CMD_DRAW_RECT, rect,
        &filledByte, sizeof(filledByte));
}

/// @fn static inline void gfxCmdDrawLine(GfxCmdBuf *buf, GfxPoint start,
///   GfxPoint end)
///
/// @brief Record GFX_CMD_DRAW_LINE.
///
/// @param buf The command buffer.
/// @param start One end.
/// @param end The other end.
///
/// @return This function returns no value.
static inline void gfxCmdDrawLine(GfxCmdBuf *buf, GfxPoint start,
    GfxPoint end
) {
    GfxPoint points[2] = { start, end };
    gfxCmdRecord_(buf, GFX_CMD_DRAW_LINE, points, sizeof(points));
}

/// @fn static inline void gfxCmdDrawTriangle(GfxCmdBuf *buf, GfxPoint first,
///   GfxPoint second, GfxPoint third, bool filled)
///
/// @brief Record GFX_CMD_DRAW_TRIANGLE.
///
/// @param buf The command buffer.
/// @param first The first corner.
/// @param second The second corner.
/// @param third The third corner.
/// @param filled Whether to fill it rather than outline it.
///
/// @return This function returns no value.
static inline void gfxCmdDrawTriangle(GfxCmdBuf *buf, GfxPoint first,
    GfxPoint second, GfxPoint third, bool filled
) {
    uint8_t params[3 * sizeof(GfxPoint) + 1];
    memcpy(&params[0], &first, sizeof(first));
    memcpy(&params[4], &second, sizeof(second));
    memcpy(&params[8], &third, sizeof(third));
    params[12] = filled ? 1 : 0;
    gfxCmdRecord_(buf, GFX_CMD_DRAW_TRIANGLE, params, sizeof(params));
}

/// @fn static inline void gfxCmdDrawCircle(GfxCmdBuf *buf, GfxPoint center,
///   int16_t radius, bool filled)
///
/// @brief Record GFX_CMD_DRAW_CIRCLE.
///
/// @param buf The command buffer.
/// @param center The center.
/// @param radius The radius, in pixels.
/// @param filled Whether to fill it rather than outline it.
///
/// @return This function returns no value.
static inline void gfxCmdDrawCircle(GfxCmdBuf *buf, GfxPoint center,
    int16_t radius, bool filled
) {
    uint8_t params[sizeof(GfxPoint) + sizeof(int16_t) + 1];
    memcpy(&params[0], &center, sizeof(center));
    memcpy(&params[4], &radius, sizeof(radius));
    params[6] = filled ? 1 : 0;
    gfxCmdRecord_(buf, GFX_CMD_DRAW_CIRCLE, params, sizeof(params));
}

/// @fn static inline void gfxCmdDrawImage(GfxCmdBuf *buf, GfxHandle image,
///   GfxPoint pos)
///
/// @brief Record GFX_CMD_DRAW_IMAGE.
///
/// @param buf The command buffer.
/// @param image The image.
/// @param pos Where to draw its top-left corner.
///
/// @return This function returns no value.
static inline void gfxCmdDrawImage(GfxCmdBuf *buf, GfxHandle image,
    GfxPoint pos
) {
    uint8_t params[sizeof(GfxHandle) + sizeof(GfxPoint)];
    memcpy(&params[0], &image, sizeof(image));
    memcpy(&params[2], &pos, sizeof(pos));
    gfxCmdRecord_(buf, GFX_CMD_DRAW_IMAGE, params, sizeof(params));
}

/// @fn static inline void gfxCmdDrawSubImage(GfxCmdBuf *buf,
///   GfxHandle image, const GfxRect *srcRect, GfxPoint pos)
///
/// @brief Record GFX_CMD_DRAW_SUB_IMAGE, or record GFX_ERR_INVALID_STATE if
/// there is no source rectangle.
///
/// @param buf The command buffer.
/// @param image The image.
/// @param srcRect The part of the image to draw.
/// @param pos Where to draw the part's top-left corner.
///
/// @return This function returns no value.
static inline void gfxCmdDrawSubImage(GfxCmdBuf *buf, GfxHandle image,
    const GfxRect *srcRect, GfxPoint pos
) {
    if (srcRect == NULL) {
        gfxCmdBufFail_(buf, GFX_ERR_INVALID_STATE);
        return;
    }
    uint8_t params[sizeof(GfxHandle) + sizeof(GfxRect) + sizeof(GfxPoint)];
    memcpy(&params[0], &image, sizeof(image));
    memcpy(&params[2], srcRect, sizeof(*srcRect));
    memcpy(&params[10], &pos, sizeof(pos));
    gfxCmdRecord_(buf, GFX_CMD_DRAW_SUB_IMAGE, params, sizeof(params));
}

/// @fn static inline void gfxCmdDrawText(GfxCmdBuf *buf, GfxPoint pos,
///   const char *str)
///
/// @brief Record GFX_CMD_DRAW_TEXT, or record GFX_ERR_CMDBUF_OVERFLOW if the
/// string is longer than GFX_TEXT_MAX bytes.  The record holds a copy.
///
/// @param buf The command buffer.
/// @param pos Where to draw the first glyph's top-left corner.
/// @param str The text, or NULL to record nothing.
///
/// @return This function returns no value.
static inline void gfxCmdDrawText(GfxCmdBuf *buf, GfxPoint pos,
    const char *str
) {
    if (!str) return;
    size_t len = strlen(str);
    if (len > GFX_TEXT_MAX) {
        gfxCmdBufFail_(buf, GFX_ERR_CMDBUF_OVERFLOW);
        return;
    }
    uint8_t *record = gfxCmdBufReserve_(buf, GFX_CMD_DRAW_TEXT_SIZE(len));
    if (record != NULL) {
        record[0] = GFX_CMD_DRAW_TEXT;
        memcpy(&record[1], &pos, sizeof(pos));
        record[5] = (uint8_t) len;
        memcpy(&record[6], str, len);
    }
}

/// @fn static inline void gfxCmdCall(GfxCmdBuf *buf, GfxHandle preloaded)
///
/// @brief Record GFX_CMD_CALL.
///
/// @param buf The command buffer.
/// @param preloaded A handle from gfxPreupload.
///
/// @return This function returns no value.
static inline void gfxCmdCall(GfxCmdBuf *buf, GfxHandle preloaded) {
    gfxCmdRecord_(buf, GFX_CMD_CALL, &preloaded, sizeof(preloaded));
}

/// @fn static inline void gfxCmdCopyRect(GfxCmdBuf *buf,
///   const GfxRect *src, GfxPoint dst)
///
/// @brief Record GFX_CMD_COPY_RECT.
///
/// @param buf The command buffer.
/// @param src The rectangle to copy.
/// @param dst Where to copy its top-left corner to.
///
/// @return This function returns no value.
static inline void gfxCmdCopyRect(GfxCmdBuf *buf, const GfxRect *src,
    GfxPoint dst
) {
    gfxCmdRecordRect_(buf, GFX_CMD_COPY_RECT, src, &dst, sizeof(dst));
}

/// @fn static inline void gfxCmdScrollRect(GfxCmdBuf *buf,
///   const GfxRect *rect, int16_t deltaX, int16_t deltaY)
///
/// @brief Record GFX_CMD_SCROLL_RECT.
///
/// @param buf The command buffer.
/// @param rect The rectangle to scroll.
/// @param deltaX The distance to move its contents right, in pixels.
/// @param deltaY The distance to move its contents down, in pixels.
///
/// @return This function returns no value.
static inline void gfxCmdScrollRect(GfxCmdBuf *buf, const GfxRect *rect,
    int16_t deltaX, int16_t deltaY
) {
    int16_t delta[2] = { deltaX, deltaY };
    gfxCmdRecordRect_(buf, GFX_CMD_SCROLL_RECT, rect, delta, sizeof(delta));
}


// =========================================================================
// SYNCHRONIZATION, ERRORS, AND THE CURSOR  (messages to driver)
// =========================================================================

/// @fn static inline GfxError gfxStatusCall_(GfxDisplay *disp, uint8_t type)
///
/// @brief Send a synchronous message whose arguments are only a status.
///
/// @param disp The display.
/// @param type The GfxMsgType.
///
/// @return Returns the result of gfxCall_.
static inline GfxError gfxStatusCall_(GfxDisplay *disp, uint8_t type) {
    MESSAGE_ARGS_ALLOCATE(GfxFinishArgs, args);
    GfxError status = gfxCall_(disp, type, args, sizeof(*args));
    MESSAGE_ARGS_FREE(args);
    return status;
}

/// @fn static inline GfxError gfxFinish(GfxDisplay *disp)
///
/// @brief Wait until the driver has drawn everything sent before this call.
///
/// @param disp The display.
///
/// @return Returns the driver's status.
static inline GfxError gfxFinish(GfxDisplay *disp) {
    return gfxStatusCall_(disp, GFX_MSG_FINISH);
}

/// @fn static inline GfxError gfxGetError(GfxDisplay *disp)
///
/// @brief Get and clear the driver's sticky error.
///
/// @param disp The display.
///
/// @return Returns the driver's sticky error, or the status of the query if
/// it failed.
static inline GfxError gfxGetError(GfxDisplay *disp) {
    MESSAGE_ARGS_ALLOCATE(GfxGetErrorArgs, args);
    GfxError status = gfxCall_(disp, GFX_MSG_GET_ERROR, args, sizeof(*args));
    if (status == GFX_OK) {
        status = (GfxError) args->error;
    }
    MESSAGE_ARGS_FREE(args);
    return status;
}

/// @fn static inline GfxError gfxSetCursor(GfxDisplay *disp,
///   GfxHandle image, uint8_t hotX, uint8_t hotY)
///
/// @brief Make an image the pointer's cursor.
///
/// @param disp The display.
/// @param image One of the client's images, or GFX_INVALID_HANDLE for the
///   driver's own cursor.
/// @param hotX The hotspot's column within the image.
/// @param hotY The hotspot's row within the image.
///
/// @return Returns the driver's status.
static inline GfxError gfxSetCursor(GfxDisplay *disp, GfxHandle image,
    uint8_t hotX, uint8_t hotY
) {
    MESSAGE_ARGS_ALLOCATE(GfxSetCursorArgs, args);
    if (args != NULL) {
        args->image = image;
        args->hotX  = hotX;
        args->hotY  = hotY;
    }
    GfxError status = gfxCall_(disp, GFX_MSG_SET_CURSOR, args, sizeof(*args));
    MESSAGE_ARGS_FREE(args);
    return status;
}

/// @fn static inline GfxError gfxShowCursor(GfxDisplay *disp, bool show)
///
/// @brief Show or hide the pointer's cursor.  One-way.
///
/// @param disp The display.
/// @param show Whether to show it.
///
/// @return Returns the result of sending the message.
static inline GfxError gfxShowCursor(GfxDisplay *disp, bool show) {
    GfxShowCursorArgs *args = MESSAGE_ARGS_CREATE(GfxShowCursorArgs,
        MESSAGE_ARGS_ONE_WAY);
    if (args == NULL) {
        return GFX_ERR_OUT_OF_MEMORY;
    }
    args->show = show ? 1 : 0;
    return gfxPost_(disp, GFX_MSG_SHOW_CURSOR, args, sizeof(*args));
}

/// @fn static inline GfxError gfxWarpCursor(GfxDisplay *disp,
///   int16_t pointerX, int16_t pointerY)
///
/// @brief Move the pointer.  One-way.
///
/// @param disp The display.
/// @param pointerX The new column, in pixels.
/// @param pointerY The new row, in pixels.
///
/// @return Returns the result of sending the message.
static inline GfxError gfxWarpCursor(GfxDisplay *disp, int16_t pointerX,
    int16_t pointerY
) {
    GfxWarpCursorArgs *args = MESSAGE_ARGS_CREATE(GfxWarpCursorArgs,
        MESSAGE_ARGS_ONE_WAY);
    if (args == NULL) {
        return GFX_ERR_OUT_OF_MEMORY;
    }
    args->x = pointerX;
    args->y = pointerY;
    return gfxPost_(disp, GFX_MSG_WARP_CURSOR, args, sizeof(*args));
}


#ifdef __cplusplus
}
#endif

#endif /* GRAPHICS_H */
