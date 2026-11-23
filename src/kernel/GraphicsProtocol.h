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

/// @struct GfxPoint
///
/// @brief An integer pixel coordinate.  The origin is the top left, and y
/// increases downward.
///
/// @param x The column.
/// @param y The row.
typedef struct GfxPoint { int16_t x, y; } GfxPoint;

/// @struct GfxRect
///
/// @brief An axis-aligned rectangle.
///
/// @param x The column of its top-left corner.
/// @param y The row of its top-left corner.
/// @param w Its width.
/// @param h Its height.
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

/// @enum GfxBlendMode
///
/// @brief The blend mode applied to draw operations.
///
/// @param GFX_BLEND_OPAQUE Overwrite the destination (VDP GCOL mode 0).
/// @param GFX_BLEND_AND AND with the destination (VDP GCOL mode 2).
/// @param GFX_BLEND_OR OR with the destination (VDP GCOL mode 1).
/// @param GFX_BLEND_XOR XOR with the destination (VDP GCOL mode 3).
/// @param GFX_BLEND_INVERT Invert the destination (VDP GCOL mode 4).
typedef enum GfxBlendMode {
  GFX_BLEND_OPAQUE = 0,
  GFX_BLEND_AND,
  GFX_BLEND_OR,
  GFX_BLEND_XOR,
  GFX_BLEND_INVERT,
} GfxBlendMode;

/// @enum GfxPixelFmt
///
/// @brief The pixel format of an uploaded image.
///
/// @param GFX_FMT_RGBA8888 4 bytes a pixel:  R, G, B, A.
/// @param GFX_FMT_RGBA2222 1 byte a pixel:  RR GG BB AA packed, red highest.
/// @param GFX_FMT_RGB888 3 bytes a pixel:  R, G, B, with alpha taken as 0xFF.
typedef enum GfxPixelFmt {
  GFX_FMT_RGBA8888 = 0,
  GFX_FMT_RGBA2222,
  GFX_FMT_RGB888,
} GfxPixelFmt;

/// @enum GfxError
///
/// @brief The graphics driver's error codes.  Every value is below 256,
/// because a message's status is a uint8_t.
///
/// @param GFX_OK No error.
/// @param GFX_ERR_OUT_OF_MEMORY Device or host memory is exhausted.
/// @param GFX_ERR_OUT_OF_HANDLES The handle table is full.
/// @param GFX_ERR_CMDBUF_OVERFLOW A record doesn't fit the command buffer.
/// @param GFX_ERR_INVALID_HANDLE A stale or uninitialized handle.
/// @param GFX_ERR_INVALID_STATE API misuse.
/// @param GFX_ERR_DEVICE_LOST The driver is gone or never answered.
/// @param GFX_ERR_NOT_SUPPORTED The feature isn't available in this driver.
/// @param GFX_ERR_DISPLAY_BUSY Another process owns the display, or the
///   driver is still setting it up.
/// @param GFX_ERR_NOT_OWNER The sender doesn't own the display.
typedef enum GfxError {
  GFX_OK = 0,
  GFX_ERR_OUT_OF_MEMORY,
  GFX_ERR_OUT_OF_HANDLES,
  GFX_ERR_CMDBUF_OVERFLOW,
  GFX_ERR_INVALID_HANDLE,
  GFX_ERR_INVALID_STATE,
  GFX_ERR_DEVICE_LOST,
  GFX_ERR_NOT_SUPPORTED,
  GFX_ERR_DISPLAY_BUSY,
  GFX_ERR_NOT_OWNER,
} GfxError;

/// @enum GfxMsgType
///
/// @brief The low byte of each graphics message's type, under
/// GFX_COMMAND_SIGNATURE.
///
/// @note Each synchronous message's arguments start with a uint8_t status the
/// driver sets.  The client presets it to GFX_ERR_DEVICE_LOST, which is what
/// remains if the driver ends without answering.
///
/// @note Synchronous arguments are inputs, then outputs.  The driver writes
/// only the outputs, so no answer is larger than its request.  Every buffer an
/// argument points at is shared MESSAGE_BUFFER_IN with the driver.
///
/// @param GFX_MSG_OPEN_DISPLAY Sync, GfxOpenDisplayArgs:  take ownership of
///   the display.  It starts with the whole screen as its viewport, the origin
///   at (0,0), a white foreground, a black background, GFX_BLEND_OPAQUE, and
///   the default font.
/// @param GFX_MSG_CLOSE_DISPLAY One-way, no arguments.
/// @param GFX_MSG_GET_SURFACE_SIZE Sync, GfxSurfaceSizeArgs.
/// @param GFX_MSG_UPLOAD_IMAGE Sync, GfxUploadImageArgs.
/// @param GFX_MSG_DESTROY_IMAGE One-way, GfxHandleArgs.
/// @param GFX_MSG_GET_IMAGE_SIZE Sync, GfxImageSizeArgs.
/// @param GFX_MSG_UPLOAD_FONT Sync, GfxUploadFontArgs.
/// @param GFX_MSG_DESTROY_FONT One-way, GfxHandleArgs.
/// @param GFX_MSG_SET_DEFAULT_FONT Sync, GfxSetDefaultFontArgs.
/// @param GFX_MSG_SUBMIT Persistent round trip, GfxSubmitArgs.
/// @param GFX_MSG_PREUPLOAD Sync, GfxPreuploadArgs.  A preupload holds only
///   color, blend, font, and drawing records, and no GFX_CMD_DRAW_SUB_IMAGE or
///   GFX_CMD_CALL.
/// @param GFX_MSG_DISCARD_PREUPLOAD One-way, GfxHandleArgs.
/// @param GFX_MSG_FINISH Sync, GfxFinishArgs; done once drawn.
/// @param GFX_MSG_CAPTURE_RECT Sync, GfxCaptureRectArgs.
/// @param GFX_MSG_GET_ERROR Sync, GfxGetErrorArgs.
/// @param GFX_MSG_SET_CURSOR Sync, GfxSetCursorArgs.
/// @param GFX_MSG_SHOW_CURSOR One-way, GfxShowCursorArgs.
/// @param GFX_MSG_WARP_CURSOR One-way, GfxWarpCursorArgs.
/// @param GFX_MSG_COUNT The number of client message types.
/// @param GFX_MSG_INPUT_EVENTS Persistent round trip, GfxInputEventsArgs.
/// @param GFX_MSG_DISPLAY_REVOKED One-way, no arguments.
/// @param GFX_MSG_CLIENT_ENDED One-way:  the driver's graphics client ended.
///   The data pointer is the client's ProcessId, not a pointer to storage.
typedef enum GfxMsgType {
  // Client to driver:
  GFX_MSG_OPEN_DISPLAY = 0,
  GFX_MSG_CLOSE_DISPLAY,
  GFX_MSG_GET_SURFACE_SIZE,
  GFX_MSG_UPLOAD_IMAGE,
  GFX_MSG_DESTROY_IMAGE,
  GFX_MSG_GET_IMAGE_SIZE,
  GFX_MSG_UPLOAD_FONT,
  GFX_MSG_DESTROY_FONT,
  GFX_MSG_SET_DEFAULT_FONT,
  GFX_MSG_SUBMIT,
  GFX_MSG_PREUPLOAD,
  GFX_MSG_DISCARD_PREUPLOAD,
  GFX_MSG_FINISH,
  GFX_MSG_CAPTURE_RECT,
  GFX_MSG_GET_ERROR,
  GFX_MSG_SET_CURSOR,
  GFX_MSG_SHOW_CURSOR,
  GFX_MSG_WARP_CURSOR,

  GFX_MSG_COUNT,

  // Driver to client:
  GFX_MSG_INPUT_EVENTS = 24,
  GFX_MSG_DISPLAY_REVOKED = 25,

  // Scheduler to driver:
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

/// @struct GfxOpenDisplayArgs
///
/// @brief Arguments and return values for GFX_MSG_OPEN_DISPLAY.
///
/// @param status The GfxError the driver sets.
/// @param width In:  the requested width.  Out:  the actual width.
/// @param height In:  the requested height.  Out:  the actual height.
/// @param defaultFontW Out:  the width of the default font's glyph cell.
/// @param defaultFontH Out:  the height of the default font's glyph cell.
/// @param defaultFontFirst Out:  the default font's first codepoint.
/// @param defaultFontCount Out:  the number of glyphs in the default font.
typedef struct GfxOpenDisplayArgs {
  uint8_t   status;
  uint16_t  width, height;
  uint8_t   defaultFontW, defaultFontH, defaultFontFirst;
  uint16_t  defaultFontCount;
} GfxOpenDisplayArgs;

/// @struct GfxSurfaceSizeArgs
///
/// @brief Arguments and return values for GFX_MSG_GET_SURFACE_SIZE.
///
/// @param status The GfxError the driver sets.
/// @param width Out:  the display's width.
/// @param height Out:  the display's height.
typedef struct GfxSurfaceSizeArgs {
  uint8_t   status;
  uint16_t  width, height;
} GfxSurfaceSizeArgs;

/// @struct GfxUploadImageArgs
///
/// @brief Arguments and return values for GFX_MSG_UPLOAD_IMAGE.
///
/// @param status The GfxError the driver sets.
/// @param format In:  the GfxPixelFmt.
/// @param width In:  the image's width.
/// @param height In:  the image's height.
/// @param size In:  the bytes at pixels.
/// @param pixels In:  the pixels, or NULL to allocate the image without
///   initializing it.
/// @param handle Out:  the new image.
typedef struct GfxUploadImageArgs {
  uint8_t     status;
  uint8_t     format;
  uint16_t    width, height;
  uint32_t    size;
  const void *pixels;
  GfxHandle   handle;
} GfxUploadImageArgs;

/// @struct GfxImageSizeArgs
///
/// @brief Arguments and return values for GFX_MSG_GET_IMAGE_SIZE.
///
/// @param status The GfxError the driver sets.
/// @param handle In:  the image.
/// @param width Out:  the image's width.
/// @param height Out:  the image's height.
typedef struct GfxImageSizeArgs {
  uint8_t   status;
  GfxHandle handle;
  uint16_t  width, height;
} GfxImageSizeArgs;

/// @struct GfxUploadFontArgs
///
/// @brief Arguments and return values for GFX_MSG_UPLOAD_FONT.
///
/// @param status The GfxError the driver sets.
/// @param glyphW In:  the width of each glyph cell, in pixels.
/// @param glyphH In:  the height of each glyph cell, in pixels.
/// @param firstChar In:  the first codepoint in the bitmap.
/// @param numChars In:  the number of glyphs.
/// @param size In:  the bytes at bitmap.
/// @param bitmap In:  1-bit glyph rows, MSB leftmost, each row padded to a
///   whole byte.
/// @param handle Out:  the new font.
typedef struct GfxUploadFontArgs {
  uint8_t     status;
  uint8_t     glyphW, glyphH;
  uint8_t     firstChar;
  uint16_t    numChars;
  uint32_t    size;
  const void *bitmap;
  GfxHandle   handle;
} GfxUploadFontArgs;

/// @struct GfxSetDefaultFontArgs
///
/// @brief Arguments and return values for GFX_MSG_SET_DEFAULT_FONT.
///
/// @param status The GfxError the driver sets.
/// @param handle In:  the font, or GFX_INVALID_HANDLE for the built-in font.
typedef struct GfxSetDefaultFontArgs {
  uint8_t   status;
  GfxHandle handle;
} GfxSetDefaultFontArgs;

/// @struct GfxSubmitArgs
///
/// @brief Arguments and return values for GFX_MSG_SUBMIT.
///
/// @param status The GfxError the driver sets.
/// @param cmdSize In:  the bytes at cmdData.
/// @param cmdData In:  the command records.
typedef struct GfxSubmitArgs {
  uint8_t        status;
  uint32_t       cmdSize;
  const uint8_t *cmdData;
} GfxSubmitArgs;

/// @struct GfxPreuploadArgs
///
/// @brief Arguments and return values for GFX_MSG_PREUPLOAD.
///
/// @param status The GfxError the driver sets.
/// @param cmdSize In:  the bytes at cmdData.
/// @param cmdData In:  the command records.
/// @param handle Out:  the new preupload.
typedef struct GfxPreuploadArgs {
  uint8_t        status;
  uint32_t       cmdSize;
  const uint8_t *cmdData;
  GfxHandle      handle;
} GfxPreuploadArgs;

/// @struct GfxFinishArgs
///
/// @brief Arguments and return values for GFX_MSG_FINISH.
///
/// @param status The GfxError the driver sets.
typedef struct GfxFinishArgs {
  uint8_t status;
} GfxFinishArgs;

/// @struct GfxCaptureRectArgs
///
/// @brief Arguments and return values for GFX_MSG_CAPTURE_RECT.
///
/// @param status The GfxError the driver sets.
/// @param rect In:  the rectangle, in screen coordinates.
/// @param handle Out:  a new image of it.
typedef struct GfxCaptureRectArgs {
  uint8_t   status;
  GfxRect   rect;
  GfxHandle handle;
} GfxCaptureRectArgs;

/// @struct GfxGetErrorArgs
///
/// @brief Arguments and return values for GFX_MSG_GET_ERROR.
///
/// @param status The GfxError the driver sets.
/// @param error Out:  the driver's sticky GfxError.
typedef struct GfxGetErrorArgs {
  uint8_t status;
  uint8_t error;
} GfxGetErrorArgs;

/// @struct GfxSetCursorArgs
///
/// @brief Arguments and return values for GFX_MSG_SET_CURSOR.
///
/// @param status The GfxError the driver sets.
/// @param image In:  the cursor's image, or GFX_INVALID_HANDLE for the
///   driver's own.
/// @param hotX In:  the hotspot's column within the image.
/// @param hotY In:  the hotspot's row within the image.
typedef struct GfxSetCursorArgs {
  uint8_t   status;
  GfxHandle image;
  uint8_t   hotX, hotY;
} GfxSetCursorArgs;

/// @struct GfxHandleArgs
///
/// @brief Arguments for GFX_MSG_DESTROY_IMAGE, GFX_MSG_DESTROY_FONT, and
/// GFX_MSG_DISCARD_PREUPLOAD.
///
/// @param handle The image, font, or preupload.
typedef struct GfxHandleArgs {
  GfxHandle handle;
} GfxHandleArgs;

/// @struct GfxShowCursorArgs
///
/// @brief Arguments for GFX_MSG_SHOW_CURSOR.
///
/// @param show Nonzero to show the cursor, 0 to hide it.
typedef struct GfxShowCursorArgs {
  uint8_t show;
} GfxShowCursorArgs;

/// @struct GfxWarpCursorArgs
///
/// @brief Arguments for GFX_MSG_WARP_CURSOR.
///
/// @param x The pointer's new column, in pixels.
/// @param y The pointer's new row, in pixels.
typedef struct GfxWarpCursorArgs {
  int16_t x, y;
} GfxWarpCursorArgs;

/// @def GFX_EVENTS_PER_MESSAGE
///
/// @brief The number of events one GFX_MSG_INPUT_EVENTS carries.
#define GFX_EVENTS_PER_MESSAGE 8

/// @enum GfxEventType
///
/// @brief The kind of a GfxEvent.
///
/// @param GFX_EVENT_POINTER The pointer moved, or its buttons or wheel did.
/// @param GFX_EVENT_KEY A key was pressed or released.
/// @param GFX_EVENT_OVERFLOW Events were dropped after this one.
typedef enum GfxEventType {
  GFX_EVENT_POINTER = 0,
  GFX_EVENT_KEY,
  GFX_EVENT_OVERFLOW,
} GfxEventType;

/// @enum GfxKey
///
/// @brief The key a GFX_EVENT_KEY names, the same on every platform.  A key
/// that types a printable character is that character, a letter in lower
/// case, and the keypad is reported as the keys it acts as.  The named keys'
/// values have gaps because they follow the order of the Agon VDP's keycodes,
/// which lets its driver translate them by subtraction.
typedef enum GfxKey {
  GFX_KEY_NONE = 0,
  GFX_KEY_LEFT_SHIFT = 0x80,
  GFX_KEY_RIGHT_SHIFT = 0x81,
  GFX_KEY_LEFT_ALT = 0x82,
  GFX_KEY_RIGHT_ALT = 0x83,
  GFX_KEY_LEFT_CTRL = 0x84,
  GFX_KEY_RIGHT_CTRL = 0x85,
  GFX_KEY_LEFT_GUI = 0x86,
  GFX_KEY_RIGHT_GUI = 0x87,
  GFX_KEY_ESCAPE = 0x88,
  GFX_KEY_PRINT_SCREEN = 0x89,
  GFX_KEY_SYSREQ = 0x8A,
  GFX_KEY_INSERT = 0x8B,
  GFX_KEY_DELETE = 0x8D,
  GFX_KEY_BACKSPACE = 0x8F,
  GFX_KEY_HOME = 0x90,
  GFX_KEY_END = 0x92,
  GFX_KEY_PAUSE = 0x94,
  GFX_KEY_BREAK = 0x95,
  GFX_KEY_SCROLL_LOCK = 0x96,
  GFX_KEY_NUM_LOCK = 0x97,
  GFX_KEY_CAPS_LOCK = 0x98,
  GFX_KEY_TAB = 0x99,
  GFX_KEY_ENTER = 0x9A,
  GFX_KEY_MENU = 0x9C,
  GFX_KEY_PAGE_UP = 0x9D,
  GFX_KEY_PAGE_DOWN = 0x9F,
  GFX_KEY_UP = 0xA1,
  GFX_KEY_DOWN = 0xA3,
  GFX_KEY_LEFT = 0xA5,
  GFX_KEY_RIGHT = 0xA7,
  GFX_KEY_F1 = 0xAA,
  GFX_KEY_F2 = 0xAB,
  GFX_KEY_F3 = 0xAC,
  GFX_KEY_F4 = 0xAD,
  GFX_KEY_F5 = 0xAE,
  GFX_KEY_F6 = 0xAF,
  GFX_KEY_F7 = 0xB0,
  GFX_KEY_F8 = 0xB1,
  GFX_KEY_F9 = 0xB2,
  GFX_KEY_F10 = 0xB3,
  GFX_KEY_F11 = 0xB4,
  GFX_KEY_F12 = 0xB5,
} GfxKey;

/// @def GFX_MOD_CTRL
///
/// @brief A GFX_EVENT_KEY modifier bit:  Ctrl is held.
#define GFX_MOD_CTRL        0x01

/// @def GFX_MOD_SHIFT
///
/// @brief A GFX_EVENT_KEY modifier bit:  Shift is held.
#define GFX_MOD_SHIFT       0x02

/// @def GFX_MOD_ALT
///
/// @brief A GFX_EVENT_KEY modifier bit:  Alt is held.
#define GFX_MOD_ALT         0x04

/// @def GFX_MOD_ALT_GR
///
/// @brief A GFX_EVENT_KEY modifier bit:  AltGr is held.
#define GFX_MOD_ALT_GR      0x08

/// @def GFX_MOD_CAPS_LOCK
///
/// @brief A GFX_EVENT_KEY modifier bit:  Caps Lock is on.
#define GFX_MOD_CAPS_LOCK   0x10

/// @def GFX_MOD_NUM_LOCK
///
/// @brief A GFX_EVENT_KEY modifier bit:  Num Lock is on.
#define GFX_MOD_NUM_LOCK    0x20

/// @def GFX_MOD_SCROLL_LOCK
///
/// @brief A GFX_EVENT_KEY modifier bit:  Scroll Lock is on.
#define GFX_MOD_SCROLL_LOCK 0x40

/// @def GFX_MOD_GUI
///
/// @brief A GFX_EVENT_KEY modifier bit:  a GUI key is held.
#define GFX_MOD_GUI         0x80

/// @struct GfxEvent
///
/// @brief One input event from the graphics driver.
///
/// @param type The GfxEventType.
/// @param buttons Pointer:  the button bits after this event.
/// @param wheel Pointer:  the wheel delta.
/// @param ascii Key:  the character it types, or 0.
/// @param keycode Key:  the GfxKey.
/// @param modifiers Key:  the GFX_MOD_* bits.
/// @param down Key:  1 pressed, 0 released.
/// @param x Pointer:  the column, in pixels.
/// @param y Pointer:  the row, in pixels.
typedef struct GfxEvent {
  uint8_t type;
  uint8_t buttons;
  int8_t  wheel;
  uint8_t ascii;
  uint8_t keycode;
  uint8_t modifiers;
  uint8_t down;
  int16_t x, y;
} GfxEvent;

/// @struct GfxInputEventsArgs
///
/// @brief Arguments for GFX_MSG_INPUT_EVENTS.
///
/// @param count The number of events.
/// @param events The events, oldest first.
typedef struct GfxInputEventsArgs {
  uint8_t  count;
  GfxEvent events[GFX_EVENTS_PER_MESSAGE];
} GfxInputEventsArgs;

/// @enum GfxCmdType
///
/// @brief The tag of each command record.  A record is a uint8_t tag followed
/// by its parameters, packed with no padding, each multi-byte value
/// little-endian.
///
/// @note Every coordinate in a record is relative to the origin.  The clip is
/// the viewport intersected with the top of the scissor stack.  An unknown or
/// short record, or a bad handle, stops the buffer.
///
/// @param GFX_CMD_SET_VIEWPORT GfxRect, in screen coordinates; empties the
///   scissor stack.
/// @param GFX_CMD_SET_ORIGIN GfxPoint, the screen position of (0,0).
/// @param GFX_CMD_PUSH_SCISSOR GfxRect, intersected with the current clip.  On
///   a full stack it sets the driver's sticky error and is ignored.
/// @param GFX_CMD_POP_SCISSOR No parameters.  On an empty stack it sets the
///   driver's sticky error and is ignored.
/// @param GFX_CMD_SET_FG_COLOR GfxColor.
/// @param GFX_CMD_SET_BG_COLOR GfxColor.
/// @param GFX_CMD_SET_BLEND uint8_t GfxBlendMode.
/// @param GFX_CMD_SET_FONT GfxHandle; GFX_INVALID_HANDLE is the default font.
/// @param GFX_CMD_CLEAR No parameters; fills the clip with the background.
/// @param GFX_CMD_DRAW_RECT GfxRect, uint8_t filled.
/// @param GFX_CMD_DRAW_LINE GfxPoint a, b.
/// @param GFX_CMD_DRAW_TRIANGLE GfxPoint a, b, c, uint8_t filled.
/// @param GFX_CMD_DRAW_CIRCLE GfxPoint center, int16_t r, uint8_t filled.
/// @param GFX_CMD_DRAW_IMAGE GfxHandle, GfxPoint; pixels with an alpha of 0
///   aren't drawn.
/// @param GFX_CMD_DRAW_SUB_IMAGE GfxHandle, GfxRect src, GfxPoint.
/// @param GFX_CMD_DRAW_TEXT GfxPoint, uint8_t len, and len bytes of text, the
///   one record without a fixed size.  The text has no terminator, and control
///   bytes in it are drawn as glyphs.
/// @param GFX_CMD_CALL GfxHandle of a preupload.  It starts with the colors,
///   blend, and font current when it was uploaded, draws relative to the
///   origin and inside the clip current when it is called, and leaves the
///   caller's colors, blend, and font as they were.
/// @param GFX_CMD_COPY_RECT GfxRect src, GfxPoint dst; clipped at dst.
/// @param GFX_CMD_SCROLL_RECT GfxRect, int16_t dx, dy; the uncovered strip is
///   cleared to the background.
/// @param GFX_CMD_COUNT The number of record types.
typedef enum GfxCmdType {
  GFX_CMD_SET_VIEWPORT = 0,
  GFX_CMD_SET_ORIGIN,
  GFX_CMD_PUSH_SCISSOR,
  GFX_CMD_POP_SCISSOR,
  GFX_CMD_SET_FG_COLOR,
  GFX_CMD_SET_BG_COLOR,
  GFX_CMD_SET_BLEND,
  GFX_CMD_SET_FONT,
  GFX_CMD_CLEAR,
  GFX_CMD_DRAW_RECT,
  GFX_CMD_DRAW_LINE,
  GFX_CMD_DRAW_TRIANGLE,
  GFX_CMD_DRAW_CIRCLE,
  GFX_CMD_DRAW_IMAGE,
  GFX_CMD_DRAW_SUB_IMAGE,
  GFX_CMD_DRAW_TEXT,
  GFX_CMD_CALL,
  GFX_CMD_COPY_RECT,
  GFX_CMD_SCROLL_RECT,

  GFX_CMD_COUNT
} GfxCmdType;

/// @def GFX_CMD_SET_VIEWPORT_SIZE
///
/// @brief The size of a GFX_CMD_SET_VIEWPORT record, its tag included.
#define GFX_CMD_SET_VIEWPORT_SIZE    9

/// @def GFX_CMD_SET_ORIGIN_SIZE
///
/// @brief The size of a GFX_CMD_SET_ORIGIN record, its tag included.
#define GFX_CMD_SET_ORIGIN_SIZE      5

/// @def GFX_CMD_PUSH_SCISSOR_SIZE
///
/// @brief The size of a GFX_CMD_PUSH_SCISSOR record, its tag included.
#define GFX_CMD_PUSH_SCISSOR_SIZE    9

/// @def GFX_CMD_POP_SCISSOR_SIZE
///
/// @brief The size of a GFX_CMD_POP_SCISSOR record, its tag included.
#define GFX_CMD_POP_SCISSOR_SIZE     1

/// @def GFX_CMD_SET_FG_COLOR_SIZE
///
/// @brief The size of a GFX_CMD_SET_FG_COLOR record, its tag included.
#define GFX_CMD_SET_FG_COLOR_SIZE    5

/// @def GFX_CMD_SET_BG_COLOR_SIZE
///
/// @brief The size of a GFX_CMD_SET_BG_COLOR record, its tag included.
#define GFX_CMD_SET_BG_COLOR_SIZE    5

/// @def GFX_CMD_SET_BLEND_SIZE
///
/// @brief The size of a GFX_CMD_SET_BLEND record, its tag included.
#define GFX_CMD_SET_BLEND_SIZE       2

/// @def GFX_CMD_SET_FONT_SIZE
///
/// @brief The size of a GFX_CMD_SET_FONT record, its tag included.
#define GFX_CMD_SET_FONT_SIZE        3

/// @def GFX_CMD_CLEAR_SIZE
///
/// @brief The size of a GFX_CMD_CLEAR record, its tag included.
#define GFX_CMD_CLEAR_SIZE           1

/// @def GFX_CMD_DRAW_RECT_SIZE
///
/// @brief The size of a GFX_CMD_DRAW_RECT record, its tag included.
#define GFX_CMD_DRAW_RECT_SIZE      10

/// @def GFX_CMD_DRAW_LINE_SIZE
///
/// @brief The size of a GFX_CMD_DRAW_LINE record, its tag included.
#define GFX_CMD_DRAW_LINE_SIZE       9

/// @def GFX_CMD_DRAW_TRIANGLE_SIZE
///
/// @brief The size of a GFX_CMD_DRAW_TRIANGLE record, its tag included.
#define GFX_CMD_DRAW_TRIANGLE_SIZE  14

/// @def GFX_CMD_DRAW_CIRCLE_SIZE
///
/// @brief The size of a GFX_CMD_DRAW_CIRCLE record, its tag included.
#define GFX_CMD_DRAW_CIRCLE_SIZE     8

/// @def GFX_CMD_DRAW_IMAGE_SIZE
///
/// @brief The size of a GFX_CMD_DRAW_IMAGE record, its tag included.
#define GFX_CMD_DRAW_IMAGE_SIZE      7

/// @def GFX_CMD_DRAW_SUB_IMAGE_SIZE
///
/// @brief The size of a GFX_CMD_DRAW_SUB_IMAGE record, its tag included.
#define GFX_CMD_DRAW_SUB_IMAGE_SIZE 15

/// @def GFX_CMD_DRAW_TEXT_SIZE
///
/// @brief The size of a GFX_CMD_DRAW_TEXT record with len bytes of text, its
/// tag included.
#define GFX_CMD_DRAW_TEXT_SIZE(len)  (6 + (len))

/// @def GFX_CMD_CALL_SIZE
///
/// @brief The size of a GFX_CMD_CALL record, its tag included.
#define GFX_CMD_CALL_SIZE            3

/// @def GFX_CMD_COPY_RECT_SIZE
///
/// @brief The size of a GFX_CMD_COPY_RECT record, its tag included.
#define GFX_CMD_COPY_RECT_SIZE      13

/// @def GFX_CMD_SCROLL_RECT_SIZE
///
/// @brief The size of a GFX_CMD_SCROLL_RECT record, its tag included.
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
