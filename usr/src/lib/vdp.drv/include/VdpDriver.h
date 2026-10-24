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

/// @def VDP_GENERAL_POLL_TIMEOUT_NS
///
/// @brief How long, in nanoseconds, to wait for the VDP to answer the general
/// poll before writing to it anyway.
#define VDP_GENERAL_POLL_TIMEOUT_NS 15000000000LL

/// @struct VdpInput
///
/// @brief State of the keyboard input the driver forwards to the console.
/// Kept on main's stack because the overlay's static data doesn't survive the
/// overlay window being given to another program.
///
/// @param consolePid The ProcessId of the console.
/// @param portId The index of the driver's console port, or -1 until the
///   console has said which it is.
/// @param portMessage The CONSOLE_ADD_FORWARDING_PORT message waiting on the
///   console, or NULL.
/// @param portArgs The arguments of portMessage.
/// @param inputMessage The CONSOLE_INPUT_READY message waiting on the
///   console, or NULL.
/// @param inputArgs The arguments of inputMessage.
/// @param parser The parser of the bytes the VDP sends.
/// @param pendingLength The number of bytes in pending.
/// @param pending The translations of the keys not yet sent to the console,
///   each preceded by its length so that no key is split between messages.
/// @param pollAnswered Whether the VDP has replied to the general poll.
typedef struct VdpInput {
  ProcessId                    consolePid;
  int                          portId;
  ProcessMessage              *portMessage;
  ConsoleAddForwardingPortArgs portArgs;
  ProcessMessage              *inputMessage;
  ConsoleInputReadyArgs        inputArgs;
  VdpPacketParser              parser;
  uint8_t                      pendingLength;
  uint8_t                      pending[VDP_PENDING_SIZE];
  bool                         pollAnswered;
} VdpInput;

/// @struct VdpOutput
///
/// @brief State of the output the driver draws on the VDP.  Kept on main's
/// stack for the same reason as VdpInput.
///
/// @param textState The state of the translation of the console's text.
/// @param listening Whether the VDP is drawing what the driver writes.  The
///   VDP ignores everything until it has answered a general poll.
/// @param nextPollNs When to send the next general poll, in nanoseconds.
/// @param giveUpNs When to stop waiting for the VDP to answer, in nanoseconds.
/// @param buffer The output written while the VDP wasn't listening, or NULL.
typedef struct VdpOutput {
  VdpTextState     textState;
  bool             listening;
  int64_t          nextPollNs;
  int64_t          giveUpNs;
  VdpOutputBuffer *buffer;
} VdpOutput;

/// @struct VdpStartupArgs
///
/// @brief The arguments of the Startup overlay's vdpStartup function.
///
/// @param input A pointer to the driver's VdpInput.
/// @param output A pointer to the driver's VdpOutput.
typedef struct VdpStartupArgs {
  VdpInput  *input;
  VdpOutput *output;
} VdpStartupArgs;

/// @fn static inline int64_t vdpNowNs(void)
///
/// @brief Get the current time in nanoseconds.
///
/// @return Returns the current time in nanoseconds.
static inline int64_t vdpNowNs(void) {
  struct timespec now;
  overlayMap.header.osApi->userApi->nanoOsTimespec_get(&now, TIME_UTC);
  return (((int64_t) now.tv_sec) * 1000000000LL) + now.tv_nsec;
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

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VDP_DRIVER_H
