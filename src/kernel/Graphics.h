///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              05.22.2026
///
/// @file              Graphics.h
///
/// @brief             A minimal graphics API common to command-stream GPU
///                    architectures and the Agon Light 2 VDP.
///
/// @details
/// MESSAGE-PASSING ARCHITECTURE
///
///   The graphics driver runs as a separate NanoOS process. All API functions
///   in this header are static inline wrappers that send messages to the
///   driver via the HAL:
///
///     ProcessMessage *sent = sendMessage(deviceId, type, data, size, waiting);
///
///   Messages are zero-copy. For synchronous calls (waiting = true), the
///   caller blocks until the driver marks the message done, then reads the
///   response:
///
///     processMessageWaitForDone(sent, NULL);
///     void *resp = processMessageData(sent);
///     processMessageRelease(sent);
///
///   For asynchronous calls (waiting = false), the payload must generally
///   be malloc'd by the caller. The driver frees it when processing is
///   complete. The caller does not call processMessageRelease.
///   Exception: GFX_MSG_SUBMIT payloads are embedded in the GfxCmdBuf
///   struct - the driver signals completion via a done flag instead.
///
///   FIFO ordering is guaranteed: messages to a given deviceId are processed
///   in the order they were sent.
///
/// COMMAND BUFFER MODEL
///
///   Draw calls and state changes are recorded client-side into a GfxCmdBuf
///   as abstract command records (a uint8_t tag followed by parameters). No
///   messages are sent during recording - zero IPC overhead.
///
///   The command buffer is double-buffered internally. When gfxSubmit is
///   called (or when the active write buffer fills and auto-flushes), the
///   buffer is sent zero-copy to the driver and the write pointer swaps
///   to the other buffer. If the other buffer is still being read by the
///   driver, the caller blocks (yielding) until the driver signals
///   completion.
///
///   This keeps all backend encoding knowledge in the driver process and all
///   client code backend-agnostic.
///
/// FONT MEASUREMENT CACHING
///
///   Font metrics (glyph width, height, codepoint range) are cached
///   client-side in the GfxDisplay struct at upload time. Text measurement
///   functions (gfxMeasureText, gfxFitText, gfxHitTestText, etc.) are
///   pure client-side computations - no IPC. System font metrics are
///   received in the gfxOpenDisplay response.
///
/// Design principles (unchanged from original):
///
///   1. COMMAND BUFFER MODEL
///   2. STATE IS RECORDED, NOT GLOBAL
///   3. OPAQUE HANDLES FOR RESOURCES
///   4. EXPLICIT SYNCHRONIZATION
///   5. NO HEAP IN THE HOT PATH
///   6. DISPLAY-CENTRIC ADDRESSING
///
/// What this API deliberately omits:
///   - Shaders / programmable pipeline stages
///   - Vertex and index buffers
///   - Depth/stencil buffers
///   - Render passes
///   - Compute
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
#include "Processes.h"

#ifdef __cplusplus
extern "C" {
#endif


/// @def GFX_COMMAND_SIGNATURE
///
/// @brief 64-bit, little-endian value to use as a signature to indicate to the
/// graphics driver process that the command is intended for it.  This is
/// "\0GRAPHIX" expressed as a little-endian value.
#define GFX_COMMAND_SIGNATURE ((int64_t) 0x5849485041524700)

// Function pointer type for the HAL sendMessage call.
// Stored in GfxCtx so that Graphics.h does not depend on HAL struct layout.
typedef ProcessMessage *(*GfxSendMessageFn)(int deviceId, int64_t type,
                                            void *data, size_t size,
                                            bool waiting);


// =========================================================================
// PRIMITIVE TYPES
// =========================================================================

// Integer pixel coordinate. Origin is top-left; Y increases downward.
typedef struct GfxPoint { int16_t x, y; } GfxPoint;

// Axis-aligned rectangle. x,y is the top-left corner.
typedef struct GfxRect { int16_t x, y, w, h; } GfxRect;

// Packed RGBA color: 0xRRGGBBAA.
//
// The VDP backend maps this to the nearest entry in its 64-color palette.
// A GPU backend passes it through to the fragment pipeline or clear color.
typedef uint32_t GfxColor;

#define GFX_RGBA(r,g,b,a) \
    (((GfxColor)(r) << 24) | \
     ((GfxColor)(g) << 16) | \
     ((GfxColor)(b) <<  8) | \
     ((GfxColor)(a)))

#define GFX_RGB(r,g,b)  GFX_RGBA(r, g, b, 0xFF)
#define GFX_TRANSPARENT GFX_RGBA(0, 0, 0, 0x00)

// Common palette entries that map cleanly to the VDP's 64-color set.
//                         RGBA8888       RGB2222
#define CGA_BLACK          0x000000FF  // (0,0,0,3)
#define CGA_BLUE           0x0000AAFF  // (0,0,2,3)
#define CGA_GREEN          0x00AA00FF  // (0,2,0,3)
#define CGA_CYAN           0x00AAAAFF  // (0,2,2,3)
#define CGA_RED            0xAA0000FF  // (2,0,0,3)
#define CGA_MAGENTA        0xAA00AAFF  // (2,0,2,3)
#define CGA_BROWN          0xAA5500FF  // (2,1,0,3)
#define CGA_LIGHT_GRAY     0xAAAAAAFF  // (2,2,2,3)
#define CGA_DARK_GRAY      0x555555FF  // (1,1,1,3)
#define CGA_LIGHT_BLUE     0x5555FFFF  // (1,1,3,3)
#define CGA_LIGHT_GREEN    0x55FF55FF  // (1,3,1,3)
#define CGA_LIGHT_CYAN     0x55FFFFFF  // (1,3,3,3)
#define CGA_LIGHT_RED      0xFF5555FF  // (3,1,1,3)
#define CGA_LIGHT_MAGENTA  0xFF55FFFF  // (3,1,3,3)
#define CGA_YELLOW         0xFFFF55FF  // (3,3,1,3)
#define CGA_WHITE          0xFFFFFFFF  // (3,3,3,3)

extern const GfxColor cgaColorPalette[16];
#define CGA_COLOR_PALETTE \
const GfxColor cgaColorPalette[16] = { \
  0x000000FF, /* CGA_BLACK         */ \
  0x0000AAFF, /* CGA_BLUE          */ \
  0x00AA00FF, /* CGA_GREEN         */ \
  0x00AAAAFF, /* CGA_CYAN          */ \
  0xAA0000FF, /* CGA_RED           */ \
  0xAA00AAFF, /* CGA_MAGENTA       */ \
  0xAA5500FF, /* CGA_BROWN         */ \
  0xAAAAAAFF, /* CGA_LIGHT_GRAY    */ \
  0x555555FF, /* CGA_DARK_GRAY     */ \
  0x5555FFFF, /* CGA_LIGHT_BLUE    */ \
  0x55FF55FF, /* CGA_LIGHT_GREEN   */ \
  0x55FFFFFF, /* CGA_LIGHT_CYAN    */ \
  0xFF5555FF, /* CGA_LIGHT_RED     */ \
  0xFF55FFFF, /* CGA_LIGHT_MAGENTA */ \
  0xFFFF55FF, /* CGA_YELLOW        */ \
  0xFFFFFFFF, /* CGA_WHITE         */ \
};

extern const uint8_t rgb222ToCgaMap[64];
#define RGB222_TO_CGA_MAP \
const uint8_t rgb222ToCgaMap[64] = { \
  0,  0,  1,  1,  0,  8,  1,  9, \
  2,  2,  3,  3,  2, 10,  3, 11, \
  0,  8,  1,  9,  6,  8,  8,  9, \
  2,  8,  3,  9, 10, 10, 10, 11, \
  4,  4,  5,  5,  6,  6,  5,  9, \
  6,  7,  7,  7, 10, 10,  7, 11, \
  4, 12,  5, 13,  6, 12, 12, 13, \
  6, 12,  7, 13, 14, 14, 14, 15, \
};

// Common conversions.
#define rgba8888ToRgb888(color) ((color) >> 8)
#define rgba8888ToRgba2222(color) ( \
    ((((((color) >> 24) & 0xff) + 42) / 85) << 6) \
  | ((((((color) >> 16) & 0xff) + 42) / 85) << 4) \
  | ((((((color) >>  8) & 0xff) + 42) / 85) << 2) \
  | ((((((color) >>  0) & 0xff) + 42) / 85) << 0) )
#define rgba8888ToRgb222(color) (rgba8888ToRgba2222(color) >> 2)
#define rgb888ToRgb222(color) ( \
    ((((((color) >> 16) & 0xff) + 42) / 85) << 4) \
  | ((((((color) >>  8) & 0xff) + 42) / 85) << 2) \
  | ((((((color) >>  0) & 0xff) + 42) / 85) << 0) )
#define rgba2222ToRgb222(color) ((color) >> 2)

#define rgb222ToCga(color)   rgb222ToCgaMap[(color)]
#define rgba2222ToCga(color) rgb222ToCgaMap[rgba2222ToRgb222(color)]
#define rgba2222Alpha(color) (color & 0x03)
#define rgb888ToCga(color)   rgb222ToCgaMap[rgb888ToRgb222(color)]
#define rgba8888ToCga(color) rgb222ToCgaMap[rgba8888ToRgb222(color)]
#define rgba8888Alpha(color) ((color) & 0xff)


// =========================================================================
// ENUMERATIONS - BLEND MODES, PIXEL FORMATS
// =========================================================================

// Blend mode applied to draw operations.
//
// GFX_BLEND_OPAQUE   - overwrite destination (VDP GCOL mode 0)
// GFX_BLEND_AND      - AND with destination   (VDP GCOL mode 1)
// GFX_BLEND_OR       - OR with destination    (VDP GCOL mode 2)
// GFX_BLEND_XOR      - XOR with destination   (VDP GCOL mode 3)
// GFX_BLEND_INVERT   - invert destination      (VDP GCOL mode 4)
typedef enum GfxBlendMode {
    GFX_BLEND_OPAQUE = 0,
    GFX_BLEND_AND,
    GFX_BLEND_OR,
    GFX_BLEND_XOR,
    GFX_BLEND_INVERT,
} GfxBlendMode;

// 3x3 row-major affine transform matrix (in fixed-point Q8.8 format).
// Identity: { {256,0,0}, {0,256,0}, {0,0,256} }.
typedef struct GfxMat3 {
    int16_t m[3][3];  /* Q8.8 fixed point; 256 == 1.0 */
} GfxMat3;

extern const GfxMat3 GFX_IDENTITY;

// Opaque resource handle.
//
// Returned by upload/preupload functions. On the VDP backend this wraps
// a buffer ID. On a GPU backend it may wrap a descriptor or texture
// object. GFX_INVALID_HANDLE indicates failure.
typedef void *GfxHandle;
#define GFX_INVALID_HANDLE ((GfxHandle)NULL)

// Pixel format for uploaded images.
typedef enum GfxPixelFmt {
    GFX_FMT_RGBA8888 = 0,  // 4 bytes/pixel: R G B A
    GFX_FMT_RGBA2222,      // 1 byte/pixel:  RR GG BB AA packed
    GFX_FMT_RGB888,        // 3 bytes/pixel: R G B (alpha treated as 0xFF)
} GfxPixelFmt;


// =========================================================================
// ERROR CODES
// =========================================================================

typedef enum GfxError {
    GFX_OK = 0,
    GFX_ERR_OUT_OF_MEMORY,      // device or host memory exhausted
    GFX_ERR_OUT_OF_HANDLES,     // handle table full
    GFX_ERR_CMDBUF_OVERFLOW,    // command buffer backing store too small
    GFX_ERR_INVALID_HANDLE,     // stale or uninitialized handle
    GFX_ERR_INVALID_STATE,      // API misuse (e.g., draw before begin)
    GFX_ERR_DEVICE_LOST,        // VDP stopped responding / GPU device lost
    GFX_ERR_NOT_SUPPORTED,      // feature not available in this backend
} GfxError;

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
    default:                      return "unknown error";
    }
}


// =========================================================================
// MESSAGE TYPES  (IPC to driver process)
//
// Each value identifies a message sent via sendMessage(). These go in
// the 'type' parameter. The driver switches on these to dispatch.
// =========================================================================

typedef enum GfxMsgType {
    // Context lifecycle
    GFX_MSG_CREATE_CONTEXT,         // -> GfxMsgErrorResp
    GFX_MSG_DESTROY_CONTEXT,        // async, no response

    // Display lifecycle
    GFX_MSG_OPEN_DISPLAY,           // -> GfxMsgOpenDisplayResp
    GFX_MSG_CLOSE_DISPLAY,          // async, no response
    GFX_MSG_GET_SURFACE_SIZE,       // -> GfxMsgSizeResp

    // Resource management - images
    GFX_MSG_UPLOAD_IMAGE,           // -> GfxMsgHandleResp
    GFX_MSG_DESTROY_IMAGE,          // async, no response
    GFX_MSG_GET_IMAGE_SIZE,         // -> GfxMsgSizeResp

    // Resource management - fonts
    GFX_MSG_UPLOAD_FONT,            // -> GfxMsgHandleResp
    GFX_MSG_DESTROY_FONT,           // async, no response
    // NOTE: gfxGetGlyphSize, gfxMeasureText, gfxMeasureTextSize,
    // gfxFitText, gfxHitTestText, and gfxGetFontRange are client-side
    // operations using cached font metrics. No message types needed.

    // Command buffer submission
    GFX_MSG_SUBMIT,                 // async, zero-copy, sets doneFlag
    GFX_MSG_PREUPLOAD,              // -> GfxMsgHandleResp
    GFX_MSG_DISCARD_PREUPLOAD,      // async, no response
    GFX_MSG_SUBMIT_PRELOADED,       // async, no response

    // Synchronization and presentation
    GFX_MSG_PRESENT,                // sync, blocks until VSYNC
    GFX_MSG_INSERT_FENCE,           // -> GfxMsgFenceResp
    GFX_MSG_WAIT_FENCE,             // -> GfxMsgBoolResp

    // Readback
    GFX_MSG_READ_PIXEL,             // -> GfxMsgColorResp
    GFX_MSG_CAPTURE_RECT,           // -> GfxMsgHandleResp

    // Error
    GFX_MSG_GET_ERROR,              // -> GfxMsgErrorResp

    GFX_MSG_COUNT
} GfxMsgType;


// =========================================================================
// COMMAND RECORD TYPES  (in-buffer encoding)
//
// These tag the abstract command records written into a GfxCmdBuf during
// client-side recording. The driver walks the buffer and interprets each
// record, translating to backend-specific commands.
//
// Buffer format:  [uint8_t tag][parameter bytes...]
//
// All records have a fixed size determinable from the tag, except
// GFX_CMD_DRAW_TEXT which includes a uint16_t string length followed
// by that many bytes of string data (null-terminated in the buffer for
// convenience).
// =========================================================================

typedef enum GfxCmdType {
    // State commands
    GFX_CMD_SET_VIEWPORT,       // GfxRect
    GFX_CMD_SET_SCISSOR,        // GfxRect
    GFX_CMD_PUSH_SCISSOR,       // GfxRect
    GFX_CMD_POP_SCISSOR,        // (no parameters)
    GFX_CMD_SET_FG_COLOR,       // GfxColor
    GFX_CMD_SET_BG_COLOR,       // GfxColor
    GFX_CMD_SET_BLEND,          // uint8_t (GfxBlendMode)
    GFX_CMD_SET_FONT,           // GfxHandle
    GFX_CMD_SET_TRANSFORM,      // GfxMat3

    // Draw commands
    GFX_CMD_CLEAR,              // (no parameters)
    GFX_CMD_DRAW_RECT,          // GfxRect + uint8_t filled
    GFX_CMD_DRAW_LINE,          // GfxPoint a + GfxPoint b
    GFX_CMD_DRAW_TRIANGLE,      // GfxPoint a,b,c + uint8_t filled
    GFX_CMD_DRAW_CIRCLE,        // GfxPoint center + int16_t r + uint8_t f
    GFX_CMD_DRAW_IMAGE,         // GfxHandle + GfxPoint pos
    GFX_CMD_DRAW_SUB_IMAGE,     // GfxHandle + GfxRect src + GfxPoint pos
    GFX_CMD_DRAW_TEXT,          // GfxPoint + uint16_t len + char[len+1]
    GFX_CMD_CALL,               // GfxHandle preloaded
    GFX_CMD_SCROLL_RECT,        // GfxRect + int16_t dx + int16_t dy

    GFX_CMD_COUNT
} GfxCmdType;


// =========================================================================
// RESOURCE DESCRIPTORS  (unchanged from original API)
// =========================================================================

typedef struct GfxImageDesc {
    uint16_t       width, height;
    GfxPixelFmt    format;
    const void    *pixels;    // CPU-side pixel data; may be NULL to
                              // allocate without initializing.
    size_t         size;      // byte count of pixels buffer
} GfxImageDesc;

typedef struct GfxFontDesc {
    uint8_t      glyphW, glyphH;  // pixel dimensions of each glyph cell
    uint8_t      firstChar;       // first codepoint in the bitmap
    uint8_t      numChars;        // number of glyphs
    const void  *bitmap;          // packed 1-bit glyph data
    size_t       size;
} GfxFontDesc;


// =========================================================================
// FENCE
// =========================================================================

typedef struct GfxFence { uint32_t _opaque; } GfxFence;
#define GFX_FENCE_INVALID ((GfxFence){ 0 })


// =========================================================================
// MESSAGE PAYLOAD STRUCTS  (request payloads sent to driver)
//
// For synchronous messages, these may live on the caller's stack - the
// caller blocks until the driver is done with the data.
//
// For asynchronous messages, these must be malloc'd. The driver frees
// the allocation when processing completes.
// =========================================================================

// --- Context ---

typedef struct GfxMsgCreateContext {
    uint16_t    maxResources;
    const void *backendCfg;
} GfxMsgCreateContext;

// GFX_MSG_DESTROY_CONTEXT: no payload (deviceId identifies the target)

// --- Display ---

typedef struct GfxMsgOpenDisplay {
    uint16_t    width, height;
    const void *displayCfg;
} GfxMsgOpenDisplay;

typedef struct GfxMsgCloseDisplay {
    uint8_t displayId;
} GfxMsgCloseDisplay;

typedef struct GfxMsgGetSurfaceSize {
    uint8_t displayId;
} GfxMsgGetSurfaceSize;

// --- Images ---

typedef struct GfxMsgUploadImage {
    uint8_t      displayId;
    GfxImageDesc desc;
} GfxMsgUploadImage;

typedef struct GfxMsgDestroyImage {
    uint8_t   displayId;
    GfxHandle handle;
} GfxMsgDestroyImage;

typedef struct GfxMsgGetImageSize {
    uint8_t   displayId;
    GfxHandle handle;
} GfxMsgGetImageSize;

// --- Fonts ---

typedef struct GfxMsgUploadFont {
    uint8_t     displayId;
    GfxFontDesc desc;
} GfxMsgUploadFont;

typedef struct GfxMsgDestroyFont {
    uint8_t   displayId;
    GfxHandle handle;
} GfxMsgDestroyFont;

// --- Command buffer submission ---

// GFX_MSG_SUBMIT payload. Always async, zero-copy.
//
// cmdData points directly into one of the GfxCmdBuf's double buffers.
// When the driver has finished processing, it sets *doneFlag = true so
// the client knows the buffer is available for reuse.
//
// One GfxMsgSubmit is embedded per buffer slot in GfxCmdBuf, so no
// malloc is needed on the submit path.
typedef struct GfxMsgSubmit {
    uint8_t        displayId;
    size_t         cmdSize;       // byte count of command data
    uint8_t       *cmdData;       // pointer into GfxCmdBuf double buffer
    volatile bool *doneFlag;      // driver sets *doneFlag = true when done
} GfxMsgSubmit;

typedef struct GfxMsgPreupload {
    uint8_t  displayId;
    size_t   cmdSize;
    uint8_t *cmdData;       // points to caller-owned GfxCmdBuf.data
} GfxMsgPreupload;

typedef struct GfxMsgDiscardPreupload {
    uint8_t   displayId;
    GfxHandle handle;
} GfxMsgDiscardPreupload;

typedef struct GfxMsgSubmitPreloaded {
    uint8_t   displayId;
    GfxHandle handle;
} GfxMsgSubmitPreloaded;

// --- Synchronization ---

typedef struct GfxMsgPresent {
    uint8_t displayId;
} GfxMsgPresent;

typedef struct GfxMsgInsertFence {
    uint8_t displayId;
} GfxMsgInsertFence;

typedef struct GfxMsgWaitFence {
    uint8_t  displayId;
    GfxFence fence;
    uint32_t timeoutUs;
} GfxMsgWaitFence;

// --- Readback ---

typedef struct GfxMsgReadPixel {
    uint8_t  displayId;
    GfxPoint pos;
} GfxMsgReadPixel;

typedef struct GfxMsgCaptureRect {
    uint8_t displayId;
    GfxRect rect;
} GfxMsgCaptureRect;

// --- Error ---

typedef struct GfxMsgGetError {
    uint8_t displayId;
} GfxMsgGetError;


// =========================================================================
// MESSAGE RESPONSE STRUCTS  (set by driver, read by caller)
//
// Retrieved via processMessageData(sent) after processMessageWaitForDone.
// Valid until processMessageRelease.
// =========================================================================

typedef struct GfxMsgErrorResp {
    GfxError err;
} GfxMsgErrorResp;

typedef struct GfxMsgHandleResp {
    GfxHandle handle;
} GfxMsgHandleResp;

typedef struct GfxMsgSizeResp {
    uint16_t w, h;
} GfxMsgSizeResp;

typedef struct GfxMsgBoolResp {
    bool result;
} GfxMsgBoolResp;

typedef struct GfxMsgColorResp {
    GfxColor color;
} GfxMsgColorResp;

typedef struct GfxMsgFenceResp {
    GfxFence fence;
} GfxMsgFenceResp;

typedef struct GfxMsgOpenDisplayResp {
    uint8_t  displayId;
    uint16_t width, height;
    uint8_t  sysFontW, sysFontH;
    uint8_t  sysFontFirst, sysFontCount;
    GfxError err;
} GfxMsgOpenDisplayResp;


// =========================================================================
// CLIENT-SIDE CONTEXT AND DISPLAY STRUCTS
//
// These are concrete, caller-owned structs (not opaque). GfxCtx holds the
// HAL sendMessage function pointer and device ID. GfxDisplay holds cached
// surface dimensions and font metrics for client-side text measurement.
// =========================================================================

#define GFX_MAX_FONTS 8

typedef struct GfxFontCacheEntry {
    GfxHandle handle;
    uint8_t   glyphW, glyphH;
    uint8_t   firstChar, numChars;
} GfxFontCacheEntry;

typedef struct GfxCtx {
    GfxSendMessageFn sendMessage;
    int              deviceId;
} GfxCtx;

typedef struct GfxCtxDesc {
    uint16_t    maxResources;
    const void *backendCfg;
} GfxCtxDesc;

typedef struct GfxDisplay {
    GfxCtx   *ctx;
    uint8_t   displayId;
    uint16_t  width, height;

    // System font metrics (populated at open time from driver response).
    uint8_t   sysFontW, sysFontH;
    uint8_t   sysFontFirst, sysFontCount;

    // Uploaded font metadata cache (populated by gfxUploadFont,
    // removed by gfxDestroyFont).
    uint8_t          fontCount;
    GfxFontCacheEntry fontCache[GFX_MAX_FONTS];
} GfxDisplay;

typedef struct GfxDisplayDesc {
    uint16_t    width, height;
    const void *displayCfg;
} GfxDisplayDesc;


// =========================================================================
// COMMAND BUFFER
//
// Double-buffered command recording. Two equal-size backing buffers are
// allocated by gfxCmdBufCreate. Commands are recorded into the active write
// buffer. On gfxSubmit (explicit or auto-flush when the write buffer
// fills), the active buffer is sent zero-copy to the driver and the
// write pointer swaps to the other buffer. If the other buffer is
// still being read by the driver, the caller blocks (yielding to the
// scheduler) until the driver signals completion.
//
// States:
//   INITIAL    - after gfxCmdBufCreate().
//   RECORDING  - after gfxCmdBufBegin(); commands may be written.
//   EXECUTABLE - after gfxCmdBufEnd().
// =========================================================================

#define GFX_CMDBUF_STATE_INITIAL    0
#define GFX_CMDBUF_STATE_RECORDING  1
#define GFX_CMDBUF_STATE_EXECUTABLE 2

typedef struct GfxCmdBuf {
    uint8_t       *data[2];       // double-buffered backing stores
    size_t         capacity;      // byte capacity of each buffer
    size_t         used;          // bytes written to data[writeIdx]
    uint8_t        writeIdx;      // 0 or 1: which buffer is being written
    volatile bool  available[2];  // driver sets true when done reading
    GfxMsgSubmit   submitMsg[2];  // embedded message structs (no malloc)
    GfxDisplay    *disp;          // back-pointer for auto-flush
    uint8_t        _state;
} GfxCmdBuf;


// ---- Command buffer lifecycle ----

// Create a double-buffered command buffer. Allocates the GfxCmdBuf
// struct and both backing buffers. Returns NULL on allocation failure.
//
// The GfxDisplay pointer is stored for auto-flush support.
// Use gfxCmdBufDestroy to free all associated memory.
static inline GfxCmdBuf *gfxCmdBufCreate(GfxDisplay *disp, size_t capacity) {
    GfxCmdBuf *buf = (GfxCmdBuf *)calloc(1, sizeof(*buf));
    if (!buf) return NULL;
    buf->data[0] = (uint8_t *)malloc(capacity);
    buf->data[1] = (uint8_t *)malloc(capacity);
    if (!buf->data[0] || !buf->data[1]) {
        free(buf->data[0]);
        free(buf->data[1]);
        free(buf);
        return NULL;
    }
    buf->capacity     = capacity;
    buf->writeIdx     = 0;
    buf->available[0] = true;
    buf->available[1] = true;
    buf->disp         = disp;
    buf->_state       = GFX_CMDBUF_STATE_INITIAL;
    return buf;
}

// Destroy a command buffer. Blocks until any in-flight buffer completes,
// then frees both backing stores and the GfxCmdBuf struct itself.
// Always returns NULL so the caller can write:
//
//   cmdBuf = gfxCmdBufDestroy(cmdBuf);
static inline GfxCmdBuf *gfxCmdBufDestroy(GfxCmdBuf *buf) {
    if (!buf) return NULL;
    for (int i = 0; i < 2; i++) {
        while (!buf->available[i])
            processYield();
        free(buf->data[i]);
    }
    free(buf);
    return NULL;
}

static inline void gfxCmdBufBegin(GfxCmdBuf *buf) {
    buf->used   = 0;
    buf->_state = GFX_CMDBUF_STATE_RECORDING;
}

static inline void gfxCmdBufEnd(GfxCmdBuf *buf) {
    buf->_state = GFX_CMDBUF_STATE_EXECUTABLE;
}

static inline void gfxCmdBufReset(GfxCmdBuf *buf) {
    buf->used   = 0;
    buf->_state = GFX_CMDBUF_STATE_RECORDING;
}


// ---- Internal: forward declaration for auto-flush ----

static inline void gfxSubmit(GfxDisplay *disp, GfxCmdBuf *buf);

// ---- Internal: write bytes to the command buffer ----

// Write n bytes to the active buffer. If the write would overflow,
// auto-flush: submit the current buffer, swap to the other one
// (blocking if it's still in flight), and then write into the fresh
// buffer.
//
// Returns false only if the record is larger than an entire buffer,
// which indicates a programming error.
static inline bool gfxCmdBufWrite_(GfxCmdBuf *buf,
                                   const void *src, size_t n) {
    if (buf->used + n > buf->capacity) {
        /* Record too large for any single buffer - cannot proceed. */
        if (n > buf->capacity)
            return false;

        /* Auto-flush: submit current buffer and swap. */
        gfxSubmit(buf->disp, buf);
    }
    memcpy(buf->data[buf->writeIdx] + buf->used, src, n);
    buf->used += n;
    return true;
}

static inline void gfxCmdBufTag_(GfxCmdBuf *buf, GfxCmdType type) {
    uint8_t tag = (uint8_t)type;
    gfxCmdBufWrite_(buf, &tag, 1);
}


// =========================================================================
// COMMAND RECORDING - STATE COMMANDS  (client-side, no IPC)
// =========================================================================

static inline void gfxCmdSetViewport(GfxCmdBuf *buf, const GfxRect *rect) {
    gfxCmdBufTag_(buf, GFX_CMD_SET_VIEWPORT);
    gfxCmdBufWrite_(buf, rect, sizeof(*rect));
}

static inline void gfxCmdSetScissor(GfxCmdBuf *buf, const GfxRect *rect) {
    gfxCmdBufTag_(buf, GFX_CMD_SET_SCISSOR);
    gfxCmdBufWrite_(buf, rect, sizeof(*rect));
}

static inline void gfxCmdPushScissor(GfxCmdBuf *buf, const GfxRect *rect) {
    gfxCmdBufTag_(buf, GFX_CMD_PUSH_SCISSOR);
    gfxCmdBufWrite_(buf, rect, sizeof(*rect));
}

static inline void gfxCmdPopScissor(GfxCmdBuf *buf) {
    gfxCmdBufTag_(buf, GFX_CMD_POP_SCISSOR);
}

static inline void gfxCmdSetFgColor(GfxCmdBuf *buf, GfxColor fg) {
    gfxCmdBufTag_(buf, GFX_CMD_SET_FG_COLOR);
    gfxCmdBufWrite_(buf, &fg, sizeof(fg));
}

static inline void gfxCmdSetBgColor(GfxCmdBuf *buf, GfxColor bg) {
    gfxCmdBufTag_(buf, GFX_CMD_SET_BG_COLOR);
    gfxCmdBufWrite_(buf, &bg, sizeof(bg));
}

static inline void gfxCmdSetBlend(GfxCmdBuf *buf, GfxBlendMode mode) {
    gfxCmdBufTag_(buf, GFX_CMD_SET_BLEND);
    uint8_t m = (uint8_t)mode;
    gfxCmdBufWrite_(buf, &m, 1);
}

static inline void gfxCmdSetFont(GfxCmdBuf *buf, GfxHandle font) {
    gfxCmdBufTag_(buf, GFX_CMD_SET_FONT);
    gfxCmdBufWrite_(buf, &font, sizeof(font));
}

static inline void gfxCmdSetTransform(GfxCmdBuf *buf, const GfxMat3 *m) {
    gfxCmdBufTag_(buf, GFX_CMD_SET_TRANSFORM);
    gfxCmdBufWrite_(buf, m, sizeof(*m));
}


// =========================================================================
// COMMAND RECORDING - DRAW COMMANDS  (client-side, no IPC)
// =========================================================================

static inline void gfxCmdClear(GfxCmdBuf *buf) {
    gfxCmdBufTag_(buf, GFX_CMD_CLEAR);
}

static inline void gfxCmdDrawRect(GfxCmdBuf *buf,
                                  const GfxRect *rect, bool filled) {
    gfxCmdBufTag_(buf, GFX_CMD_DRAW_RECT);
    gfxCmdBufWrite_(buf, rect, sizeof(*rect));
    uint8_t f = filled ? 1 : 0;
    gfxCmdBufWrite_(buf, &f, 1);
}

static inline void gfxCmdDrawLine(GfxCmdBuf *buf,
                                  GfxPoint a, GfxPoint b) {
    gfxCmdBufTag_(buf, GFX_CMD_DRAW_LINE);
    gfxCmdBufWrite_(buf, &a, sizeof(a));
    gfxCmdBufWrite_(buf, &b, sizeof(b));
}

static inline void gfxCmdDrawTriangle(GfxCmdBuf *buf,
                                      GfxPoint a, GfxPoint b, GfxPoint c,
                                      bool filled) {
    gfxCmdBufTag_(buf, GFX_CMD_DRAW_TRIANGLE);
    gfxCmdBufWrite_(buf, &a, sizeof(a));
    gfxCmdBufWrite_(buf, &b, sizeof(b));
    gfxCmdBufWrite_(buf, &c, sizeof(c));
    uint8_t f = filled ? 1 : 0;
    gfxCmdBufWrite_(buf, &f, 1);
}

static inline void gfxCmdDrawCircle(GfxCmdBuf *buf,
                                    GfxPoint center,
                                    int16_t radius,
                                    bool filled) {
    gfxCmdBufTag_(buf, GFX_CMD_DRAW_CIRCLE);
    gfxCmdBufWrite_(buf, &center, sizeof(center));
    gfxCmdBufWrite_(buf, &radius, sizeof(radius));
    uint8_t f = filled ? 1 : 0;
    gfxCmdBufWrite_(buf, &f, 1);
}

static inline void gfxCmdDrawImage(GfxCmdBuf *buf,
                                   GfxHandle image, GfxPoint pos) {
    gfxCmdBufTag_(buf, GFX_CMD_DRAW_IMAGE);
    gfxCmdBufWrite_(buf, &image, sizeof(image));
    gfxCmdBufWrite_(buf, &pos, sizeof(pos));
}

static inline void gfxCmdDrawSubImage(GfxCmdBuf *buf,
                                      GfxHandle image,
                                      const GfxRect *srcRect,
                                      GfxPoint pos) {
    gfxCmdBufTag_(buf, GFX_CMD_DRAW_SUB_IMAGE);
    gfxCmdBufWrite_(buf, &image, sizeof(image));
    GfxRect src = srcRect ? *srcRect : (GfxRect){0, 0, 0, 0};
    gfxCmdBufWrite_(buf, &src, sizeof(src));
    gfxCmdBufWrite_(buf, &pos, sizeof(pos));
}

// Record a text draw command.
//
// The string is copied into the command buffer at record time. The format
// in the buffer is: [tag][GfxPoint pos][uint16_t len][char str[len+1]]
// where len is strlen(str) and the string is null-terminated.
static inline void gfxCmdDrawText(GfxCmdBuf *buf,
                                  GfxPoint pos,
                                  const char *str) {
    if (!str) return;
    uint16_t len = (uint16_t)strlen(str);
    gfxCmdBufTag_(buf, GFX_CMD_DRAW_TEXT);
    gfxCmdBufWrite_(buf, &pos, sizeof(pos));
    gfxCmdBufWrite_(buf, &len, sizeof(len));
    gfxCmdBufWrite_(buf, str, len + 1);  /* include null terminator */
}

static inline void gfxCmdCall(GfxCmdBuf *buf, GfxHandle preloaded) {
    gfxCmdBufTag_(buf, GFX_CMD_CALL);
    gfxCmdBufWrite_(buf, &preloaded, sizeof(preloaded));
}

static inline void gfxCmdScrollRect(GfxCmdBuf *buf,
                                    const GfxRect *rect,
                                    int16_t dx, int16_t dy) {
    gfxCmdBufTag_(buf, GFX_CMD_SCROLL_RECT);
    gfxCmdBufWrite_(buf, rect, sizeof(*rect));
    gfxCmdBufWrite_(buf, &dx, sizeof(dx));
    gfxCmdBufWrite_(buf, &dy, sizeof(dy));
}


// =========================================================================
// CONTEXT LIFECYCLE  (messages to driver)
// =========================================================================

// Initialize a graphics context and notify the driver.
//
// The caller provides storage for the GfxCtx struct and the sendMessage
// function pointer (typically HAL->gfx->sendMessage). Returns true on
// success.
static inline bool gfxCreateContext(GfxCtx *ctx,
                                    GfxSendMessageFn sendMessage,
                                    int deviceId,
                                    const GfxCtxDesc *desc) {
    ctx->sendMessage = sendMessage;
    ctx->deviceId    = deviceId;

    GfxMsgCreateContext msg;
    msg.maxResources = desc->maxResources;
    msg.backendCfg   = desc->backendCfg;

    ProcessMessage *sent = ctx->sendMessage(
        ctx->deviceId, GFX_MSG_CREATE_CONTEXT,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgErrorResp *resp = (GfxMsgErrorResp *)processMessageData(sent);
    GfxError err = resp->err;
    processMessageRelease(sent);
    return err == GFX_OK;
}

// Tear down the graphics context. Async - driver handles cleanup.
static inline void gfxDestroyContext(GfxCtx *ctx) {
    // No payload needed; deviceId identifies the target. Send a
    // zero-byte message so there's nothing for the driver to free.
    ctx->sendMessage(ctx->deviceId, GFX_MSG_DESTROY_CONTEXT,
                     NULL, 0, false);
}


// =========================================================================
// DISPLAY LIFECYCLE  (messages to driver)
// =========================================================================

// Open a display. The driver response includes the actual surface
// dimensions (may differ from requested if the backend snapped to the
// nearest mode) and system font metrics for client-side caching.
//
// Returns true on success.
static inline bool gfxOpenDisplay(GfxDisplay *disp,
                                  GfxCtx *ctx,
                                  const GfxDisplayDesc *desc) {
    memset(disp, 0, sizeof(*disp));
    disp->ctx = ctx;

    GfxMsgOpenDisplay msg;
    msg.width      = desc->width;
    msg.height     = desc->height;
    msg.displayCfg = desc->displayCfg;

    ProcessMessage *sent = ctx->sendMessage(
        ctx->deviceId, GFX_MSG_OPEN_DISPLAY,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgOpenDisplayResp *resp =
        (GfxMsgOpenDisplayResp *)processMessageData(sent);

    if (resp->err != GFX_OK) {
        processMessageRelease(sent);
        return false;
    }

    disp->displayId    = resp->displayId;
    disp->width        = resp->width;
    disp->height       = resp->height;
    disp->sysFontW     = resp->sysFontW;
    disp->sysFontH     = resp->sysFontH;
    disp->sysFontFirst = resp->sysFontFirst;
    disp->sysFontCount = resp->sysFontCount;

    processMessageRelease(sent);
    return true;
}

// Close a display. Async - driver frees device-side resources.
static inline void gfxCloseDisplay(GfxDisplay *disp) {
    GfxMsgCloseDisplay *msg = (GfxMsgCloseDisplay *)malloc(sizeof(*msg));
    if (!msg) return;
    msg->displayId = disp->displayId;
    disp->ctx->sendMessage(disp->ctx->deviceId, GFX_MSG_CLOSE_DISPLAY,
                           msg, sizeof(*msg), false);
}

// Query the actual surface dimensions. Sync - returns from driver.
static inline void gfxGetSurfaceSize(const GfxDisplay *disp,
                                     uint16_t *outWidth,
                                     uint16_t *outHeight) {
    // Surface size is cached at open time. If a mode change could
    // occur dynamically, uncomment the sync message below.
    if (outWidth)  *outWidth  = disp->width;
    if (outHeight) *outHeight = disp->height;
}


// =========================================================================
// RESOURCE MANAGEMENT - IMAGES  (messages to driver)
// =========================================================================

// Upload pixel data to device memory. Sync - blocks until the driver
// returns a handle. The desc->pixels pointer is valid for the duration
// of the call (zero-copy, caller blocks).
static inline GfxHandle gfxUploadImage(GfxDisplay *disp,
                                       const GfxImageDesc *desc) {
    GfxMsgUploadImage msg;
    msg.displayId = disp->displayId;
    msg.desc      = *desc;

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_UPLOAD_IMAGE,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgHandleResp *resp = (GfxMsgHandleResp *)processMessageData(sent);
    GfxHandle handle = resp->handle;
    processMessageRelease(sent);
    return handle;
}

// Free device-side image memory. Async. Safe with GFX_INVALID_HANDLE.
static inline void gfxDestroyImage(GfxDisplay *disp, GfxHandle image) {
    if (image == GFX_INVALID_HANDLE) return;
    GfxMsgDestroyImage *msg = (GfxMsgDestroyImage *)malloc(sizeof(*msg));
    if (!msg) return;
    msg->displayId = disp->displayId;
    msg->handle    = image;
    disp->ctx->sendMessage(disp->ctx->deviceId, GFX_MSG_DESTROY_IMAGE,
                           msg, sizeof(*msg), false);
}

// Query pixel dimensions of an uploaded image. Sync.
static inline bool gfxGetImageSize(GfxDisplay *disp,
                                   GfxHandle image,
                                   uint16_t *outW,
                                   uint16_t *outH) {
    GfxMsgGetImageSize msg;
    msg.displayId = disp->displayId;
    msg.handle    = image;

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_GET_IMAGE_SIZE,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgSizeResp *resp = (GfxMsgSizeResp *)processMessageData(sent);
    bool ok = (resp->w != 0 || resp->h != 0);
    if (outW) *outW = resp->w;
    if (outH) *outH = resp->h;
    processMessageRelease(sent);
    return ok;
}


// =========================================================================
// RESOURCE MANAGEMENT - FONTS  (messages + client-side cache)
// =========================================================================

// Upload a font. Sync - returns handle. Caches glyph metrics locally
// for client-side text measurement.
static inline GfxHandle gfxUploadFont(GfxDisplay *disp,
                                      const GfxFontDesc *desc) {
    GfxMsgUploadFont msg;
    msg.displayId = disp->displayId;
    msg.desc      = *desc;

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_UPLOAD_FONT,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgHandleResp *resp = (GfxMsgHandleResp *)processMessageData(sent);
    GfxHandle handle = resp->handle;
    processMessageRelease(sent);

    // Cache font metrics for client-side text measurement.
    if (handle != GFX_INVALID_HANDLE &&
        disp->fontCount < GFX_MAX_FONTS) {
        GfxFontCacheEntry *e = &disp->fontCache[disp->fontCount++];
        e->handle   = handle;
        e->glyphW   = desc->glyphW;
        e->glyphH   = desc->glyphH;
        e->firstChar = desc->firstChar;
        e->numChars  = desc->numChars;
    }

    return handle;
}

// Destroy a font. Async. Removes the font from the client-side cache.
static inline void gfxDestroyFont(GfxDisplay *disp, GfxHandle font) {
    if (font == GFX_INVALID_HANDLE) return;

    // Remove from client-side cache.
    for (uint8_t i = 0; i < disp->fontCount; i++) {
        if (disp->fontCache[i].handle == font) {
            // Shift remaining entries down.
            for (uint8_t j = i; j + 1 < disp->fontCount; j++)
                disp->fontCache[j] = disp->fontCache[j + 1];
            disp->fontCount--;
            break;
        }
    }

    GfxMsgDestroyFont *msg = (GfxMsgDestroyFont *)malloc(sizeof(*msg));
    if (!msg) return;
    msg->displayId = disp->displayId;
    msg->handle    = font;
    disp->ctx->sendMessage(disp->ctx->deviceId, GFX_MSG_DESTROY_FONT,
                           msg, sizeof(*msg), false);
}


// ---- Internal: look up cached font metrics ----

static inline bool gfxFontLookup_(const GfxDisplay *disp,
                                  GfxHandle font,
                                  uint8_t *outW, uint8_t *outH,
                                  uint8_t *outFirst, uint8_t *outCount) {
    if (font == GFX_INVALID_HANDLE) {
        // System font.
        if (outW)     *outW     = disp->sysFontW;
        if (outH)     *outH     = disp->sysFontH;
        if (outFirst) *outFirst = disp->sysFontFirst;
        if (outCount) *outCount = disp->sysFontCount;
        return true;
    }
    for (uint8_t i = 0; i < disp->fontCount; i++) {
        if (disp->fontCache[i].handle == font) {
            if (outW)     *outW     = disp->fontCache[i].glyphW;
            if (outH)     *outH     = disp->fontCache[i].glyphH;
            if (outFirst) *outFirst = disp->fontCache[i].firstChar;
            if (outCount) *outCount = disp->fontCache[i].numChars;
            return true;
        }
    }
    return false;  // handle not found in cache
}


// =========================================================================
// TEXT MEASUREMENT  (client-side, no IPC)
//
// All text measurement uses cached font metrics. These functions never
// send messages. For fixed-width fonts (the only kind currently
// supported), the calculations are trivial multiplications. The
// functions exist so the contract remains correct if proportional font
// support is added in the future.
// =========================================================================

// Query glyph cell dimensions.
static inline void gfxGetGlyphSize(GfxDisplay *disp,
                                   GfxHandle font,
                                   uint8_t *outW,
                                   uint8_t *outH) {
    if (!gfxFontLookup_(disp, font, outW, outH, NULL, NULL)) {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
    }
}

// Measure pixel width of a string.
static inline uint16_t gfxMeasureText(GfxDisplay *disp,
                                      GfxHandle font,
                                      const char *str) {
    if (!str) return 0;
    uint8_t w;
    if (!gfxFontLookup_(disp, font, &w, NULL, NULL, NULL)) return 0;
    return (uint16_t)(strlen(str) * w);
}

// Measure pixel width and height of a (possibly multi-line) string.
static inline bool gfxMeasureTextSize(GfxDisplay *disp,
                                      GfxHandle font,
                                      const char *str,
                                      uint16_t *outW,
                                      uint16_t *outH) {
    uint8_t gw, gh;
    if (!gfxFontLookup_(disp, font, &gw, &gh, NULL, NULL))
        return false;

    if (!str || *str == '\0') {
        if (outW) *outW = 0;
        if (outH) *outH = gh;
        return true;
    }

    uint16_t maxLineLen = 0;
    uint16_t curLineLen = 0;
    uint16_t lines = 1;
    for (const char *p = str; *p; p++) {
        if (*p == '\n') {
            if (curLineLen > maxLineLen) maxLineLen = curLineLen;
            curLineLen = 0;
            lines++;
        } else {
            curLineLen++;
        }
    }
    if (curLineLen > maxLineLen) maxLineLen = curLineLen;

    if (outW) *outW = maxLineLen * gw;
    if (outH) *outH = lines * gh;
    return true;
}

// How many leading characters fit within maxWidth pixels.
static inline uint16_t gfxFitText(GfxDisplay *disp,
                                  GfxHandle font,
                                  const char *str,
                                  uint16_t maxWidth) {
    if (!str || maxWidth == 0) return 0;
    uint8_t w;
    if (!gfxFontLookup_(disp, font, &w, NULL, NULL, NULL) || w == 0)
        return 0;
    uint16_t fits = maxWidth / w;
    uint16_t len  = (uint16_t)strlen(str);
    return fits < len ? fits : len;
}

// Map a pixel X offset to a character index (nearest-edge rounding).
static inline uint16_t gfxHitTestText(GfxDisplay *disp,
                                      GfxHandle font,
                                      const char *str,
                                      uint16_t pixelX) {
    if (!str) return 0;
    uint8_t w;
    if (!gfxFontLookup_(disp, font, &w, NULL, NULL, NULL) || w == 0)
        return 0;
    /* Nearest-edge: add half a glyph width before dividing. */
    uint16_t idx = (pixelX + w / 2) / w;
    uint16_t len = (uint16_t)strlen(str);
    return idx < len ? idx : len;
}

// Query the codepoint range of a font.
static inline bool gfxGetFontRange(GfxDisplay *disp,
                                   GfxHandle font,
                                   uint8_t *outFirst,
                                   uint8_t *outCount) {
    return gfxFontLookup_(disp, font, NULL, NULL, outFirst, outCount);
}


// =========================================================================
// COMMAND BUFFER SUBMISSION  (messages to driver)
// =========================================================================

// Submit a command buffer for execution. Async, zero-copy.
//
// Sends the current write buffer directly to the driver (no malloc,
// no memcpy). Marks the buffer as in-flight, swaps to the other
// buffer, and resets the write position. If the other buffer is still
// being processed by the driver, blocks (yielding to the scheduler)
// until the driver signals completion.
//
// Does nothing if the current write buffer is empty.
static inline void gfxSubmit(GfxDisplay *disp, GfxCmdBuf *buf) {
    if (buf->used == 0)
        return;

    uint8_t idx = buf->writeIdx;

    // Fill the embedded message struct for this buffer slot.
    GfxMsgSubmit *msg = &buf->submitMsg[idx];
    msg->displayId = disp->displayId;
    msg->cmdSize   = buf->used;
    msg->cmdData   = buf->data[idx];
    msg->doneFlag  = &buf->available[idx];

    // Mark this buffer as in-flight.
    buf->available[idx] = false;

    // Send zero-copy - the driver reads directly from data[idx] and
    // sets *doneFlag = true when finished.
    disp->ctx->sendMessage(disp->ctx->deviceId, GFX_MSG_SUBMIT,
                           msg, sizeof(*msg), false);

    // Swap to the other buffer.
    buf->writeIdx = 1 - idx;
    buf->used     = 0;

    // If the other buffer is still in flight, block until done.
    while (!buf->available[buf->writeIdx])
        processYield();
}

// Pre-upload a command buffer to device memory. Sync - returns handle.
//
// The active write buffer is passed by pointer (zero-copy). The caller
// blocks until the driver has finished reading and uploading.
static inline GfxHandle gfxPreupload(GfxDisplay *disp,
                                     GfxCmdBuf *buf) {
    GfxMsgPreupload msg;
    msg.displayId = disp->displayId;
    msg.cmdSize   = buf->used;
    msg.cmdData   = buf->data[buf->writeIdx];

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_PREUPLOAD,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgHandleResp *resp = (GfxMsgHandleResp *)processMessageData(sent);
    GfxHandle handle = resp->handle;
    processMessageRelease(sent);
    return handle;
}

// Free a pre-uploaded buffer from device memory. Async.
static inline void gfxDiscardPreupload(GfxDisplay *disp, GfxHandle handle) {
    GfxMsgDiscardPreupload *msg =
        (GfxMsgDiscardPreupload *)malloc(sizeof(*msg));
    if (!msg) return;
    msg->displayId = disp->displayId;
    msg->handle    = handle;
    disp->ctx->sendMessage(disp->ctx->deviceId, GFX_MSG_DISCARD_PREUPLOAD,
                           msg, sizeof(*msg), false);
}

// Execute a pre-uploaded buffer. Async - very cheap (small message).
static inline void gfxSubmitPreloaded(GfxDisplay *disp, GfxHandle handle) {
    GfxMsgSubmitPreloaded *msg =
        (GfxMsgSubmitPreloaded *)malloc(sizeof(*msg));
    if (!msg) return;
    msg->displayId = disp->displayId;
    msg->handle    = handle;
    disp->ctx->sendMessage(disp->ctx->deviceId, GFX_MSG_SUBMIT_PRELOADED,
                           msg, sizeof(*msg), false);
}


// =========================================================================
// SYNCHRONIZATION AND PRESENTATION  (messages to driver)
// =========================================================================

// Present the current frame and wait for vertical sync. Sync.
static inline void gfxPresent(GfxDisplay *disp) {
    GfxMsgPresent msg;
    msg.displayId = disp->displayId;

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_PRESENT,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    processMessageRelease(sent);
}

// Insert a fence. Sync - returns the fence token.
static inline GfxFence gfxInsertFence(GfxDisplay *disp) {
    GfxMsgInsertFence msg;
    msg.displayId = disp->displayId;

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_INSERT_FENCE,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgFenceResp *resp = (GfxMsgFenceResp *)processMessageData(sent);
    GfxFence fence = resp->fence;
    processMessageRelease(sent);
    return fence;
}

// Wait for a fence. Sync - blocks until signalled or timeout.
//
// timeoutUs: microseconds to wait; 0 = poll; UINT32_MAX = indefinite.
// Returns true if the fence was signalled.
static inline bool gfxWaitFence(GfxDisplay *disp,
                                GfxFence fence,
                                uint32_t timeoutUs) {
    GfxMsgWaitFence msg;
    msg.displayId = disp->displayId;
    msg.fence     = fence;
    msg.timeoutUs = timeoutUs;

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_WAIT_FENCE,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgBoolResp *resp = (GfxMsgBoolResp *)processMessageData(sent);
    bool signaled = resp->result;
    processMessageRelease(sent);
    return signaled;
}


// =========================================================================
// READBACK  (messages to driver)
//
// Expensive - stalls the pipeline. Never use in the hot rendering path.
// =========================================================================

// Read a single pixel color. Sync.
static inline bool gfxReadPixel(GfxDisplay *disp,
                                GfxPoint pos,
                                GfxColor *outColor) {
    GfxMsgReadPixel msg;
    msg.displayId = disp->displayId;
    msg.pos       = pos;

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_READ_PIXEL,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgColorResp *resp = (GfxMsgColorResp *)processMessageData(sent);
    if (outColor) *outColor = resp->color;
    processMessageRelease(sent);
    // Convention: color == GFX_TRANSPARENT on failure.
    return resp->color != GFX_TRANSPARENT;
}

// Capture a rectangular region of the display to a new image. Sync.
//
// Returns a handle to the captured bitmap, or GFX_INVALID_HANDLE on
// failure. The caller must eventually call gfxDestroyImage.
static inline GfxHandle gfxCaptureRect(GfxDisplay *disp,
                                       const GfxRect *rect) {
    GfxMsgCaptureRect msg;
    msg.displayId = disp->displayId;
    msg.rect      = *rect;

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_CAPTURE_RECT,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgHandleResp *resp = (GfxMsgHandleResp *)processMessageData(sent);
    GfxHandle handle = resp->handle;
    processMessageRelease(sent);
    return handle;
}


// =========================================================================
// ERROR HANDLING  (message to driver + client-side string lookup)
// =========================================================================

// Return the last error from the driver, then clear it. Sync.
static inline GfxError gfxGetError(GfxDisplay *disp) {
    GfxMsgGetError msg;
    msg.displayId = disp->displayId;

    ProcessMessage *sent = disp->ctx->sendMessage(
        disp->ctx->deviceId, GFX_MSG_GET_ERROR,
        &msg, sizeof(msg), true);
    processMessageWaitForDone(sent, NULL);
    GfxMsgErrorResp *resp = (GfxMsgErrorResp *)processMessageData(sent);
    GfxError err = resp->err;
    processMessageRelease(sent);
    return err;
}

// gfxErrorString is defined above with the error enum - pure lookup,
// no IPC.


#ifdef __cplusplus
}
#endif

#endif /* GRAPHICS_H */
