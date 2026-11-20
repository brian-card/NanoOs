///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.19.2026
///
/// @file              GraphicsProtocol.h
///
/// @brief             The messages, command records, and structures that the
///                    graphics driver and its clients exchange, with nothing
///                    that needs the process API.
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

#ifndef GRAPHICS_PROTOCOL_H
#define GRAPHICS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "GraphicsColor.h"

#ifdef __cplusplus
extern "C" {
#endif


/// @def GFX_COMMAND_SIGNATURE
///
/// @brief 64-bit, little-endian value to use as a signature to indicate to the
/// graphics driver process that the command is intended for it.  This is
/// "\0GRAPHIX" expressed as a little-endian value.
#define GFX_COMMAND_SIGNATURE ((int64_t) 0x5849485041524700)


// =========================================================================
// PRIMITIVE TYPES
// =========================================================================

// Integer pixel coordinate. Origin is top-left; Y increases downward.
typedef struct GfxPoint { int16_t x, y; } GfxPoint;

// Axis-aligned rectangle. x,y is the top-left corner.
typedef struct GfxRect { int16_t x, y, w, h; } GfxRect;


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
    GFX_MSG_SET_SYSTEM_FONT,        // -> GfxMsgErrorResp
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

    GFX_MSG_COUNT,

    // Driver to client
    GFX_MSG_INPUT_EVENTS = 24,
    GFX_MSG_DISPLAY_REVOKED = 25,

    // Scheduler to driver, one-way: the driver's graphics client ended.  The
    // data pointer is the client's ProcessId, not a pointer to storage.
    GFX_MSG_CLIENT_ENDED = 31,
} GfxMsgType;

/// @def GFX_NUM_MESSAGE_TYPES
///
/// @brief One past the highest GfxMsgType value, which is what an IPC
/// capability's message-type mask has to hold.  The types are not
/// contiguous, so this is not a count.
#define GFX_NUM_MESSAGE_TYPES (GFX_MSG_CLIENT_ENDED + 1)

/// @def GFX_CLIENT_MESSAGE_MASK
///
/// @brief The message types a graphics client may send its driver.
#define GFX_CLIENT_MESSAGE_MASK ((((uint32_t) 1) << GFX_MSG_COUNT) - 1)

/// @def GFX_DRIVER_MESSAGE_MASK
///
/// @brief The message types a graphics driver may send its client.
#define GFX_DRIVER_MESSAGE_MASK \
  ((((uint32_t) 1) << GFX_MSG_INPUT_EVENTS) \
  | (((uint32_t) 1) << GFX_MSG_DISPLAY_REVOKED))

_Static_assert((GFX_CLIENT_MESSAGE_MASK & GFX_DRIVER_MESSAGE_MASK) == 0,
  "A graphics message type can't go both ways");
_Static_assert(
  (GFX_CLIENT_MESSAGE_MASK & (((uint32_t) 1) << GFX_MSG_CLIENT_ENDED)) == 0,
  "Only the scheduler may send GFX_MSG_CLIENT_ENDED");

/// @def GFX_DRIVER_PROCESS_NAME
///
/// @brief The name the display driver from config.sys's GRAPHICS line is
/// registered under, for findProcessByName.
#define GFX_DRIVER_PROCESS_NAME "graphics"


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
    uint16_t     numChars;        // number of glyphs
    const void  *bitmap;          // 1-bit glyph rows, MSB leftmost, each
                                  // row padded to a whole byte; not in
                                  // the caller's overlay, which the
                                  // driver's replaces
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

typedef struct GfxMsgSetSystemFont {
    uint8_t   displayId;
    GfxHandle handle;
} GfxMsgSetSystemFont;

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
    uint8_t  sysFontFirst;
    uint16_t sysFontCount;
    GfxError err;
} GfxMsgOpenDisplayResp;


#ifdef __cplusplus
}
#endif

#endif // GRAPHICS_PROTOCOL_H
