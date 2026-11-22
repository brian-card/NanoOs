////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file VdpDriver.h
///
/// @brief State shared by the overlays of the Agon Light 2 VDP driver.

#ifndef VDP_DRIVER_H
#define VDP_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "ExecutiveProcesses.h"
#include "Console.h"
#include "GraphicsProtocol.h"
#include "VdpEvents.h"
#include "VdpKeyboard.h"
#include "VdpText.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def VDP_UART
///
/// @brief The ID of the UART connected to the VDP.
#define VDP_UART 0

/// @def VDP_DRIVER_SIGNATURE
///
/// @brief The 64-bit signature of the messages the VDP driver receives from
/// the HAL.  "\0\0VDPDRV" as a little-endian value.
#define VDP_DRIVER_SIGNATURE ((int64_t) 0x5652445044560000)

/// @def VDP_UART_INPUT
///
/// @brief The message the HAL sends when the VDP has sent bytes.
#define VDP_UART_INPUT 1

/// @def VDP_PENDING_SIZE
///
/// @brief The size of the buffer of keys waiting to be sent to the console.
#define VDP_PENDING_SIZE 32

/// @def VDP_GENERAL_POLL_TIMEOUT_SECONDS
///
/// @brief How long, in seconds, to wait for the VDP to answer the general poll
/// before writing to it anyway.
#define VDP_GENERAL_POLL_TIMEOUT_SECONDS 15

/// @def VDP_NEVER
///
/// @brief The tv_nsec of a time that never comes.
#define VDP_NEVER (-1L)

/// @def VDP_WORD
///
/// @brief The two bytes of a 16-bit VDU argument, low byte first.
#define VDP_WORD(value) ((value) & 0xff), (((value) >> 8) & 0xff)

/// @def VDP_KEY_REPEAT_DELAY_MS
///
/// @brief The VDP's default keyboard repeat delay, in milliseconds.
#define VDP_KEY_REPEAT_DELAY_MS 500

/// @def VDP_KEY_REPEAT_RATE_MS
///
/// @brief The VDP's default keyboard repeat rate, in milliseconds.
#define VDP_KEY_REPEAT_RATE_MS 100

/// @def VDP_SCREEN_WIDTH
///
/// @brief The width, in pixels, of the screen mode the driver selects.
#define VDP_SCREEN_WIDTH 640

/// @def VDP_SCREEN_HEIGHT
///
/// @brief The height, in pixels, of the screen mode the driver selects.
#define VDP_SCREEN_HEIGHT 480

/// @def VDP_SYSTEM_GLYPH_SIZE
///
/// @brief The width and height, in pixels, of the glyphs of the VDP's own
/// system font, which it draws text in until another font is selected.
#define VDP_SYSTEM_GLYPH_SIZE 8

/// @def VDP_CURSOR_ID
///
/// @brief The ID of the VDP buffer, bitmap, and mouse cursor that hold the
/// driver's mouse cursor.  IDs up to 255 name the VDP's built-in cursors.
#define VDP_CURSOR_ID 0xF000

/// @def VDP_HIDDEN_CURSOR_ID
///
/// @brief The ID of the VDP buffer, bitmap, and mouse cursor of a 1x1
/// transparent cursor, which hides the pointer without disabling the mouse:
/// disabling it suspends its port, which a later reset can't detect it
/// through.
#define VDP_HIDDEN_CURSOR_ID 0xF001

/// @def VDP_FONT_ID_BASE
///
/// @brief The ID of the VDP buffer and font of font slot 0, the built-in
/// font.  Each slot after it takes the next ID, which is also the slot's
/// GfxHandle.
#define VDP_FONT_ID_BASE 0xF100

/// @def VDP_FONT_NUM_GLYPHS
///
/// @brief The number of glyphs a VDP font buffer must hold.
#define VDP_FONT_NUM_GLYPHS 256

/// @def VDP_MAX_FONTS
///
/// @brief The number of font slots:  slot 0 for the built-in font, and one
/// for each font a client can upload.
#define VDP_MAX_FONTS (1 + GFX_MAX_FONTS)

/// @def VDP_MAX_IMAGES
///
/// @brief The number of images a display owner can have at once.
#define VDP_MAX_IMAGES 16

/// @def VDP_IMAGE_ID_BASE
///
/// @brief The ID of the VDP buffer and bitmap of image slot 0.  Each slot
/// after it takes the next ID, which is also the image's GfxHandle.
#define VDP_IMAGE_ID_BASE 0xF200

/// @def VDP_MAX_PRELOADS
///
/// @brief The number of preuploaded command buffers a display owner can have
/// at once.
#define VDP_MAX_PRELOADS 16

/// @def VDP_PRELOAD_ID_BASE
///
/// @brief The ID of the VDP buffer of preload slot 0.  Each slot after it
/// takes the next ID, which is also the preload's GfxHandle.
#define VDP_PRELOAD_ID_BASE 0xF300

GFX_STATIC_ASSERT(VDP_MAX_IMAGES == VDP_MAX_PRELOADS,
  "The Startup overlay clears an image and a preload slot together");

/// @def VDP_UNKNOWN
///
/// @brief A VdpPen value the VDP may not have, which is always sent.
#define VDP_UNKNOWN 0xFF

/// @struct VdpPen
///
/// @brief The colors, blend mode and font records draw with, and what the VDP
/// has of each, so that only changes are sent.
///
/// @param fg The CGA index of the foreground.
/// @param bg The CGA index of the background.
/// @param blend The GfxBlendMode.
/// @param font The font slot.
/// @param vdpFg The foreground the VDP has, or VDP_UNKNOWN.
/// @param vdpBg The background the VDP has, or VDP_UNKNOWN.
/// @param vdpBlend The blend mode the VDP has, or VDP_UNKNOWN.
/// @param vdpFont The font slot the VDP has selected, or VDP_UNKNOWN.
typedef struct VdpPen {
  uint8_t fg;
  uint8_t bg;
  uint8_t blend;
  uint8_t font;
  uint8_t vdpFg;
  uint8_t vdpBg;
  uint8_t vdpBlend;
  uint8_t vdpFont;
} VdpPen;

/// @struct VdpDrawState
///
/// @brief The drawing state and resources of the display's owner, allocated
/// when the display is opened and freed when its ownership ends.
///
/// @param origin The screen position of records' (0,0).
/// @param clip The viewport, in screen coordinates and on the screen, then
///   each scissor intersected with the entry below it.
/// @param clipDepth The index in clip of the current clip.
/// @param vdpClip The graphics viewport the VDP has.
/// @param pen The pen records draw with.
/// @param imagesUsed A bit for each image slot that holds an image.
/// @param preloadsUsed A bit for each preload slot that holds a preload.
/// @param imageWidths The width of the image in each slot.
/// @param imageHeights The height of the image in each slot.
/// @param cursor The ID of the owner's mouse cursor:  one of its images, or
///   VDP_CURSOR_ID.
/// @param cursorHidden Whether the owner has hidden the mouse cursor.
typedef struct VdpDrawState {
  GfxPoint origin;
  GfxRect  clip[GFX_MAX_SCISSOR_DEPTH + 1];
  uint8_t  clipDepth;
  GfxRect  vdpClip;
  VdpPen   pen;
  uint16_t imagesUsed;
  uint16_t preloadsUsed;
  uint16_t imageWidths[VDP_MAX_IMAGES];
  uint16_t imageHeights[VDP_MAX_IMAGES];
  GfxHandle cursor;
  bool     cursorHidden;
} VdpDrawState;

/// @struct VdpInput
///
/// @brief State of the input the driver forwards to the console or to the
/// display's owner.
/// Kept on main's stack because the overlay's static data doesn't survive the
/// overlay window being given to another program.
///
/// @param consolePid The ProcessId of the console.
/// @param portId The index of the driver's console port, or -1 until the
///   console has said which it is.
/// @param portMessage The CONSOLE_ADD_FORWARDING_PORT message waiting on the
///   console, or NULL.
/// @param portArgs The arguments of portMessage, created at startup.
/// @param inputMessage The CONSOLE_INPUT_READY message waiting on the
///   console, or NULL.
/// @param inputArgs The arguments of inputMessage, created at startup.
/// @param parser The parser of the bytes the VDP sends.
/// @param pendingLength The number of bytes in pending.
/// @param pending The translations of the keys not yet sent to the console,
///   each preceded by its length so that no key is split between messages.
/// @param pollAnswered Whether the VDP has replied to the general poll.
/// @param pollCookie The cookie of the general poll being waited on.
/// @param numLock Whether the driver's Num Lock is on.
/// @param sizeMessage The CONSOLE_SET_WINSIZE message waiting on the console,
///   or NULL.
/// @param sizeArgs The arguments of sizeMessage, created at startup.
/// @param output A pointer to the driver's VdpOutput, which says whether the
///   display is owned.
/// @param events The events waiting to be sent to the display's owner, NULL
///   while nobody owns the display.
/// @param eventState The state of the events queue.
/// @param eventsMessage The GFX_MSG_INPUT_EVENTS waiting on the owner, or
///   NULL.
/// @param sentEvents The arguments of eventsMessage, or the spare that events
///   swaps with when it's sent, or NULL.
/// @param orphanMessage The GFX_MSG_INPUT_EVENTS a previous owner hadn't
///   finished with when it gave up the display, or NULL.
/// @param orphanEvents The arguments of orphanMessage.
typedef struct VdpInput {
  ProcessId                    consolePid;
  int                          portId;
  ProcessMessage              *portMessage;
  ConsoleAddForwardingPortArgs *portArgs;
  ProcessMessage              *inputMessage;
  ConsoleInputReadyArgs        *inputArgs;
  VdpPacketParser              parser;
  uint8_t                      pendingLength;
  uint8_t                      pending[VDP_PENDING_SIZE];
  bool                         pollAnswered;
  uint8_t                      pollCookie;
  bool                         numLock;
  ProcessMessage              *sizeMessage;
  ConsoleWinsizeArgs           *sizeArgs;
  struct VdpOutput             *output;
  GfxInputEventsArgs           *events;
  VdpEventState                eventState;
  ProcessMessage              *eventsMessage;
  GfxInputEventsArgs           *sentEvents;
  ProcessMessage              *orphanMessage;
  GfxInputEventsArgs           *orphanEvents;
} VdpInput;

/// @struct VdpOutput
///
/// @brief State of the output the driver draws on the VDP.  Kept on main's
/// stack for the same reason as VdpInput.
///
/// @param nextPoll When to send the next general poll, or to stop waiting for
///   the reply to a GFX_MSG_FINISH's:  tv_nsec is VDP_NEVER while neither is
///   due.
/// @param textState The state of the translation of the console's text.
/// @param listening Whether the VDP is drawing what the driver writes.  The
///   VDP ignores everything until it has answered a general poll.
/// @param giveUp When to stop waiting for the VDP to answer.
/// @param screenSetUp Whether the screen setup has been sent, so that the
///   poll being waited on is the one that follows it.
/// @param graphicsMessage The message for the Graphics overlay to handle.
/// @param fontsUsed A bit for each font slot that holds a font.
/// @param fontWidths The glyph width of the font in each slot.
/// @param fontHeights The glyph height of the font in each slot.
/// @param ownerDefaultFont The slot of the font a client's records draw in
///   unless they select another.
/// @param sizeChanged Whether the console hasn't yet been told the number of
///   rows and columns the font gives the screen.
/// @param owner The ProcessId of the process that owns the display, or
///   PROCESS_ID_NOT_SET when the console has it.
/// @param ownerDescriptor The owner's ProcessDescriptor, or NULL.
/// @param error The sticky GfxError that GFX_MSG_GET_ERROR returns and clears.
/// @param heldText The most recent console text written while the VDP isn't
///   listening yet or the display is owned, untranslated, or NULL while the
///   console's text is drawn.
/// @param textToHold The text for the Console overlay's vdpHoldText to add to
///   heldText.
/// @param replayText The held text to draw once the console has the display
///   back, or NULL.
/// @param draw The owner's drawing state, or NULL while the console has the
///   display.
/// @param finishMessage The owner's GFX_MSG_FINISH waiting on the VDP's
///   reply to a general poll, or NULL.
/// @param input A pointer to the driver's VdpInput, whose poll cookie a
///   GFX_MSG_FINISH uses.
typedef struct VdpOutput {
  struct timespec    nextPoll;
  VdpTextState       textState;
  bool               listening;
  struct timespec    giveUp;
  bool               screenSetUp;
  ProcessMessage    *graphicsMessage;
  uint8_t            fontsUsed;
  uint8_t            fontWidths[VDP_MAX_FONTS];
  uint8_t            fontHeights[VDP_MAX_FONTS];
  uint8_t            ownerDefaultFont;
  bool               sizeChanged;
  ProcessId          owner;
  ProcessDescriptor *ownerDescriptor;
  uint8_t            error;
  VdpOutputBuffer   *heldText;
  const char        *textToHold;
  char              *replayText;
  VdpDrawState      *draw;
  ProcessMessage    *finishMessage;
  struct VdpInput   *input;
} VdpOutput;

/// @struct VdpStartupArgs
///
/// @brief The arguments of the Startup overlay's vdpStartup function.
///
/// @param input A pointer to the driver's VdpInput.
/// @param output A pointer to the driver's VdpOutput.
/// @param argv The driver's NULL-terminated arguments.
typedef struct VdpStartupArgs {
  VdpInput   *input;
  VdpOutput  *output;
  char      **argv;
} VdpStartupArgs;

uint8_t vdpUploadFontSlot(VdpOutput *vdpOutput, uint8_t slot,
  const GfxUploadFontArgs *font);
void vdpWriteZeros(size_t length);

/// @fn static inline uint8_t vdpSlot(uint16_t used, GfxHandle handle,
///   GfxHandle base, uint8_t numSlots)
///
/// @brief Get the slot of an image or preload from its handle.
///
/// @param used A bit for each slot that is used.
/// @param handle The handle.
/// @param base The handle of slot 0.
/// @param numSlots The number of slots.
///
/// @return Returns the slot, or numSlots if the handle names no used slot.
static inline uint8_t vdpSlot(uint16_t used, GfxHandle handle,
  GfxHandle base, uint8_t numSlots
) {
  uint16_t slot = (uint16_t) (handle - base);
  if ((slot >= numSlots) || (((used >> slot) & 1) == 0)) {
    return numSlots;
  }
  return (uint8_t) slot;
}

/// @fn static inline void vdpSetError(VdpOutput *vdpOutput, uint8_t error)
///
/// @brief Make an error the sticky error unless there already is one.
///
/// @param vdpOutput A pointer to the driver's VdpOutput.
/// @param error The GfxError.
///
/// @return This function returns no value.
static inline void vdpSetError(VdpOutput *vdpOutput, uint8_t error) {
  if (vdpOutput->error == GFX_OK) {
    vdpOutput->error = error;
  }
}

/// @fn static inline uint8_t vdpFreeSlot(uint16_t used, uint8_t numSlots)
///
/// @brief Find the first slot that isn't used.
///
/// @param used A bit for each slot that is used.
/// @param numSlots The number of slots.
///
/// @return Returns the slot, or numSlots if every slot is used.
static inline uint8_t vdpFreeSlot(uint16_t used, uint8_t numSlots) {
  uint8_t slot = 0;
  while ((slot < numSlots) && (((used >> slot) & 1) != 0)) {
    slot++;
  }
  return slot;
}

/// @fn static inline uint8_t vdpChannelBits(uint8_t channel)
///
/// @brief Round an 8-bit color channel to 2 bits as colorChannelTo2Bits
/// does, with comparisons, which the eZ80 does far faster than its division.
///
/// @param channel The channel.
///
/// @return Returns the channel's 2 bits.
static inline uint8_t vdpChannelBits(uint8_t channel) {
  if (channel < 43) {
    return 0;
  } else if (channel < 128) {
    return 1;
  } else if (channel < 213) {
    return 2;
  }
  return 3;
}

/// @fn static inline void vdpTimeFromNow(struct timespec *time,
///   time_t seconds, long nanoseconds)
///
/// @brief Get the time a while from now.  The driver keeps its times as
/// timespecs because 64-bit nanoseconds cost the main overlay their
/// multiplication and division.
///
/// @param time Where to store the time.
/// @param seconds The whole seconds from now.
/// @param nanoseconds The nanoseconds from now on top of seconds, below one
///   second.
///
/// @return This function returns no value.
static inline void vdpTimeFromNow(struct timespec *time, time_t seconds,
  long nanoseconds
) {
  overlayMap.header.osApi->userApi->nanoOsTimespec_get(time, TIME_UTC);
  time->tv_sec += seconds;
  time->tv_nsec += nanoseconds;
  if (time->tv_nsec >= 1000000000L) {
    time->tv_nsec -= 1000000000L;
    time->tv_sec++;
  }
}

/// @fn static inline bool vdpTimeBefore(const struct timespec *earlier,
///   const struct timespec *later)
///
/// @brief Determine whether one time comes before another.  Neither may be a
/// time whose tv_nsec is VDP_NEVER.
///
/// @param earlier The time that may be earlier.
/// @param later The time that may be later.
///
/// @return Returns true if earlier comes before later, false otherwise.
static inline bool vdpTimeBefore(const struct timespec *earlier,
  const struct timespec *later
) {
  return (earlier->tv_sec < later->tv_sec)
    || ((earlier->tv_sec == later->tv_sec)
      && (earlier->tv_nsec < later->tv_nsec));
}

/// @fn static inline bool vdpTimeReached(const struct timespec *time)
///
/// @brief Determine whether a time has come.
///
/// @param time The time, which may not be one whose tv_nsec is VDP_NEVER.
///
/// @return Returns true if the time is now or past, false otherwise.
static inline bool vdpTimeReached(const struct timespec *time) {
  struct timespec now;
  vdpTimeFromNow(&now, 0, 0);
  return vdpTimeBefore(&now, time) == false;
}

/// @fn static inline void vdpWrite(const uint8_t *data, size_t length)
///
/// @brief Write raw VDU bytes to the VDP.
///
/// @param data The bytes to write.
/// @param length The number of bytes in data.
///
/// @return This function returns no value.
static inline void vdpWrite(const uint8_t *data, size_t length) {
  getHal()->uart->write(VDP_UART, data, (ssize_t) length, NULL);
}

/// @fn static inline void vdpSetKeyboardLeds(uint8_t leds)
///
/// @brief Set the keyboard's LEDs, and its repeat delay and rate to the
/// VDP's defaults, with VDU 23,0,&88,delay;rate;leds.
///
/// @param leds The VDP_LED_* bits of the LEDs to light.
///
/// @return This function returns no value.
static inline void vdpSetKeyboardLeds(uint8_t leds) {
  const uint8_t keyState[] = {
    23, 0, 0x88, VDP_WORD(VDP_KEY_REPEAT_DELAY_MS),
    VDP_WORD(VDP_KEY_REPEAT_RATE_MS), leds
  };
  vdpWrite(keyState, sizeof(keyState));
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VDP_DRIVER_H
