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

/// @typedef GfxHandle
///
/// @brief A driver resource:  an image, a font, or a preuploaded command
/// buffer.  GFX_INVALID_HANDLE is none.
typedef uint16_t GfxHandle;

/// @def GFX_INVALID_HANDLE
///
/// @brief The GfxHandle that names no resource.  As a font, it is the default
/// font.
#define GFX_INVALID_HANDLE ((GfxHandle) 0)


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

// Pixel format for uploaded images.
typedef enum GfxPixelFmt {
    GFX_FMT_RGBA8888 = 0,  // 4 bytes/pixel: R G B A
    GFX_FMT_RGBA2222,      // 1 byte/pixel:  RR GG BB AA packed
    GFX_FMT_RGB888,        // 3 bytes/pixel: R G B (alpha treated as 0xFF)
} GfxPixelFmt;


// =========================================================================
// ERROR CODES
//
// Every value is below 256, because a message's status is a uint8_t.
// =========================================================================

typedef enum GfxError {
    GFX_OK = 0,
    GFX_ERR_OUT_OF_MEMORY,      // device or host memory exhausted
    GFX_ERR_OUT_OF_HANDLES,     // handle table full
    GFX_ERR_CMDBUF_OVERFLOW,    // a record doesn't fit the command buffer
    GFX_ERR_INVALID_HANDLE,     // stale or uninitialized handle
    GFX_ERR_INVALID_STATE,      // API misuse
    GFX_ERR_DEVICE_LOST,        // the driver is gone or never answered
    GFX_ERR_NOT_SUPPORTED,      // feature not available in this driver
    GFX_ERR_DISPLAY_BUSY,       // another process owns the display
    GFX_ERR_NOT_OWNER,          // the sender doesn't own the display
} GfxError;


// =========================================================================
// MESSAGE TYPES
//
// Each value is the low byte of a message's type, under
// GFX_COMMAND_SIGNATURE.  Each synchronous message's arguments start with a
// uint8_t status the driver sets; the client presets it to
// GFX_ERR_DEVICE_LOST, which is what remains if the driver ends without
// answering.
// =========================================================================

typedef enum GfxMsgType {
    // Client to driver
    GFX_MSG_OPEN_DISPLAY = 0,       // sync, GfxOpenDisplayArgs; takes ownership
    GFX_MSG_CLOSE_DISPLAY,          // one-way, no arguments
    GFX_MSG_GET_SURFACE_SIZE,       // sync, GfxSurfaceSizeArgs
    GFX_MSG_UPLOAD_IMAGE,           // sync, GfxUploadImageArgs
    GFX_MSG_DESTROY_IMAGE,          // one-way, GfxHandleArgs
    GFX_MSG_GET_IMAGE_SIZE,         // sync, GfxImageSizeArgs
    GFX_MSG_UPLOAD_FONT,            // sync, GfxUploadFontArgs
    GFX_MSG_DESTROY_FONT,           // one-way, GfxHandleArgs
    GFX_MSG_SET_DEFAULT_FONT,       // sync, GfxSetDefaultFontArgs
    GFX_MSG_SUBMIT,                 // persistent round trip, GfxSubmitArgs
    GFX_MSG_PREUPLOAD,              // sync, GfxPreuploadArgs
    GFX_MSG_DISCARD_PREUPLOAD,      // one-way, GfxHandleArgs
    GFX_MSG_FINISH,                 // sync, GfxFinishArgs; done once drawn
    GFX_MSG_CAPTURE_RECT,           // sync, GfxCaptureRectArgs
    GFX_MSG_GET_ERROR,              // sync, GfxGetErrorArgs
    GFX_MSG_SET_CURSOR,             // sync, GfxSetCursorArgs
    GFX_MSG_SHOW_CURSOR,            // one-way, GfxShowCursorArgs
    GFX_MSG_WARP_CURSOR,            // one-way, GfxWarpCursorArgs

    GFX_MSG_COUNT,

    // Driver to client
    GFX_MSG_INPUT_EVENTS = 24,      // persistent round trip, GfxInputEventsArgs
    GFX_MSG_DISPLAY_REVOKED = 25,   // one-way, no arguments

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

/// @def GFX_STATIC_ASSERT
///
/// @brief A compile-time check in C or C++, which spell it differently.
#ifdef __cplusplus
#define GFX_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define GFX_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

GFX_STATIC_ASSERT(GFX_MSG_COUNT <= GFX_MSG_INPUT_EVENTS,
  "Every client message type must be below the driver's");
GFX_STATIC_ASSERT(GFX_MSG_DISPLAY_REVOKED < GFX_MSG_CLIENT_ENDED,
  "Every driver message type must be below GFX_MSG_CLIENT_ENDED");
GFX_STATIC_ASSERT(GFX_NUM_MESSAGE_TYPES <= 32,
  "An IPC capability holds 32 message types");
GFX_STATIC_ASSERT((GFX_CLIENT_MESSAGE_MASK & GFX_DRIVER_MESSAGE_MASK) == 0,
  "A graphics message type can't go both ways");
GFX_STATIC_ASSERT(
  (GFX_CLIENT_MESSAGE_MASK & (((uint32_t) 1) << GFX_MSG_CLIENT_ENDED)) == 0,
  "Only the scheduler may send GFX_MSG_CLIENT_ENDED");

/// @def GFX_DRIVER_PROCESS_NAME
///
/// @brief The name the display driver from config.sys's GRAPHICS line is
/// registered under, for findProcessByName.
#define GFX_DRIVER_PROCESS_NAME "graphics"

/// @def GFX_MAX_FONTS
///
/// @brief The number of fonts a client can have uploaded at once.
#define GFX_MAX_FONTS 3


// =========================================================================
// MESSAGE ARGUMENTS
//
// Synchronous arguments are inputs, then outputs.  The driver writes only
// the outputs, so no answer is larger than its request.  Every buffer an
// argument points at is shared MESSAGE_BUFFER_IN with the driver.
// =========================================================================

typedef struct GfxOpenDisplayArgs {
    uint8_t   status;
    uint16_t  width, height;      // in: requested; out: actual
    uint8_t   defaultFontW, defaultFontH, defaultFontFirst;  // out
    uint16_t  defaultFontCount;   // out
} GfxOpenDisplayArgs;

typedef struct GfxSurfaceSizeArgs {
    uint8_t   status;
    uint16_t  width, height;      // out
} GfxSurfaceSizeArgs;

typedef struct GfxUploadImageArgs {
    uint8_t     status;
    uint8_t     format;           // in: GfxPixelFmt
    uint16_t    width, height;    // in
    uint32_t    size;             // in: bytes at pixels
    const void *pixels;           // in: NULL allocates without initializing
    GfxHandle   handle;           // out
} GfxUploadImageArgs;

typedef struct GfxImageSizeArgs {
    uint8_t   status;
    GfxHandle handle;             // in
    uint16_t  width, height;      // out
} GfxImageSizeArgs;

typedef struct GfxUploadFontArgs {
    uint8_t     status;
    uint8_t     glyphW, glyphH;   // in: pixel size of each glyph cell
    uint8_t     firstChar;        // in: first codepoint in the bitmap
    uint16_t    numChars;         // in: number of glyphs
    uint32_t    size;             // in: bytes at bitmap
    const void *bitmap;           // in: 1-bit glyph rows, MSB leftmost, each
                                  // row padded to a whole byte
    GfxHandle   handle;           // out
} GfxUploadFontArgs;

typedef struct GfxSetDefaultFontArgs {
    uint8_t   status;
    GfxHandle handle;             // in: GFX_INVALID_HANDLE is the built-in font
} GfxSetDefaultFontArgs;

typedef struct GfxSubmitArgs {
    uint8_t        status;
    uint32_t       cmdSize;       // in
    const uint8_t *cmdData;       // in
} GfxSubmitArgs;

typedef struct GfxPreuploadArgs {
    uint8_t        status;
    uint32_t       cmdSize;       // in
    const uint8_t *cmdData;       // in
    GfxHandle      handle;        // out
} GfxPreuploadArgs;

typedef struct GfxFinishArgs {
    uint8_t status;
} GfxFinishArgs;

typedef struct GfxCaptureRectArgs {
    uint8_t   status;
    GfxRect   rect;               // in: screen coordinates
    GfxHandle handle;             // out: a new image
} GfxCaptureRectArgs;

typedef struct GfxGetErrorArgs {
    uint8_t status;
    uint8_t error;                // out: the driver's sticky GfxError
} GfxGetErrorArgs;

typedef struct GfxSetCursorArgs {
    uint8_t   status;
    GfxHandle image;              // in: GFX_INVALID_HANDLE is the driver's own
    uint8_t   hotX, hotY;         // in: the hotspot within the image
} GfxSetCursorArgs;

typedef struct GfxHandleArgs {
    GfxHandle handle;
} GfxHandleArgs;

typedef struct GfxShowCursorArgs {
    uint8_t show;
} GfxShowCursorArgs;

typedef struct GfxWarpCursorArgs {
    int16_t x, y;
} GfxWarpCursorArgs;


// =========================================================================
// INPUT EVENTS  (driver to client)
// =========================================================================

/// @def GFX_EVENTS_PER_MESSAGE
///
/// @brief The number of events one GFX_MSG_INPUT_EVENTS carries.
#define GFX_EVENTS_PER_MESSAGE 8

typedef enum GfxEventType {
    GFX_EVENT_POINTER = 0,
    GFX_EVENT_KEY,
    GFX_EVENT_OVERFLOW,           // events were dropped after this one
} GfxEventType;

typedef struct GfxEvent {
    uint8_t type;                 // GfxEventType
    uint8_t buttons;              // pointer: button bits after this event
    int8_t  wheel;                // pointer: wheel delta
    uint8_t ascii;                // key
    uint8_t keycode;              // key: FabGL virtual key
    uint8_t modifiers;            // key: the VDP's modifier bits
    uint8_t down;                 // key: 1 pressed, 0 released
    int16_t x, y;                 // pointer: position in pixels
} GfxEvent;

typedef struct GfxInputEventsArgs {
    uint8_t  count;
    GfxEvent events[GFX_EVENTS_PER_MESSAGE];
} GfxInputEventsArgs;


// =========================================================================
// COMMAND RECORDS  (in-buffer encoding)
//
// A record is a uint8_t tag followed by its parameters, packed with no
// padding, each multi-byte value little-endian.  Every record has a fixed
// size except GFX_CMD_DRAW_TEXT, whose length byte says how many bytes of
// text follow; the text has no terminator, and control bytes in it are
// drawn as glyphs.
//
// Every coordinate in a record is relative to the origin.  The clip is the
// viewport intersected with the top of the scissor stack, which holds
// GFX_MAX_SCISSOR_DEPTH entries; pushing onto a full stack or popping an
// empty one sets the driver's sticky error, and the record is ignored.
// =========================================================================

typedef enum GfxCmdType {
    GFX_CMD_SET_VIEWPORT = 0,   // GfxRect, screen coordinates; empties the
                                // scissor stack
    GFX_CMD_SET_ORIGIN,         // GfxPoint, the screen position of (0,0)
    GFX_CMD_PUSH_SCISSOR,       // GfxRect, intersected with the current clip
    GFX_CMD_POP_SCISSOR,        // (no parameters)
    GFX_CMD_SET_FG_COLOR,       // GfxColor
    GFX_CMD_SET_BG_COLOR,       // GfxColor
    GFX_CMD_SET_BLEND,          // uint8_t GfxBlendMode
    GFX_CMD_SET_FONT,           // GfxHandle; GFX_INVALID_HANDLE is the default
    GFX_CMD_CLEAR,              // (no parameters); fills the clip with the
                                // background
    GFX_CMD_DRAW_RECT,          // GfxRect, uint8_t filled
    GFX_CMD_DRAW_LINE,          // GfxPoint a, b
    GFX_CMD_DRAW_TRIANGLE,      // GfxPoint a, b, c, uint8_t filled
    GFX_CMD_DRAW_CIRCLE,        // GfxPoint center, int16_t r, uint8_t filled
    GFX_CMD_DRAW_IMAGE,         // GfxHandle, GfxPoint; alpha 0 isn't drawn
    GFX_CMD_DRAW_SUB_IMAGE,     // GfxHandle, GfxRect src, GfxPoint
    GFX_CMD_DRAW_TEXT,          // GfxPoint, uint8_t len, len bytes
    GFX_CMD_CALL,               // GfxHandle from a preupload
    GFX_CMD_COPY_RECT,          // GfxRect src, GfxPoint dst; clipped at dst
    GFX_CMD_SCROLL_RECT,        // GfxRect, int16_t dx, dy; the uncovered strip
                                // is cleared to the background

    GFX_CMD_COUNT
} GfxCmdType;

// The size of each record, its tag included.
#define GFX_CMD_SET_VIEWPORT_SIZE    9
#define GFX_CMD_SET_ORIGIN_SIZE      5
#define GFX_CMD_PUSH_SCISSOR_SIZE    9
#define GFX_CMD_POP_SCISSOR_SIZE     1
#define GFX_CMD_SET_FG_COLOR_SIZE    5
#define GFX_CMD_SET_BG_COLOR_SIZE    5
#define GFX_CMD_SET_BLEND_SIZE       2
#define GFX_CMD_SET_FONT_SIZE        3
#define GFX_CMD_CLEAR_SIZE           1
#define GFX_CMD_DRAW_RECT_SIZE      10
#define GFX_CMD_DRAW_LINE_SIZE       9
#define GFX_CMD_DRAW_TRIANGLE_SIZE  14
#define GFX_CMD_DRAW_CIRCLE_SIZE     8
#define GFX_CMD_DRAW_IMAGE_SIZE      7
#define GFX_CMD_DRAW_SUB_IMAGE_SIZE 15
#define GFX_CMD_DRAW_TEXT_SIZE(len)  (6 + (len))
#define GFX_CMD_CALL_SIZE            3
#define GFX_CMD_COPY_RECT_SIZE      13
#define GFX_CMD_SCROLL_RECT_SIZE    13

/// @def GFX_TEXT_MAX
///
/// @brief The most bytes of text one GFX_CMD_DRAW_TEXT record holds.
#define GFX_TEXT_MAX 255

/// @def GFX_MAX_SCISSOR_DEPTH
///
/// @brief The number of entries the driver's scissor stack holds.
#define GFX_MAX_SCISSOR_DEPTH 4


#ifdef __cplusplus
}
#endif

#endif // GRAPHICS_PROTOCOL_H
