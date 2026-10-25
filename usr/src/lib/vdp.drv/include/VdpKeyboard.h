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
/// @file VdpKeyboard.h
///
/// @brief Parsing of the packets the Agon Light 2 VDP sends and translation of
/// its keyboard events into the byte stream a terminal sends.

#ifndef VDP_KEYBOARD_H
#define VDP_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @def VDP_PACKET_GENERAL_POLL
///
/// @brief The command of the VDP's reply to a general poll.
#define VDP_PACKET_GENERAL_POLL 0x00

/// @def VDP_GENERAL_POLL_COOKIE
///
/// @brief The byte the driver sends with its general poll, which the VDP
/// echoes in its reply.  Must stay below 0x80: vdpParseByte treats a poll
/// reply's data byte with the high bit set as the next packet's header.
#define VDP_GENERAL_POLL_COOKIE 0x4e

/// @def VDP_SETUP_POLL_COOKIE
///
/// @brief The byte the driver sends with the general poll that follows its
/// screen setup, so that a late reply to a handshake poll can't be taken for
/// its reply.  Must stay below 0x80 for the same reason as
/// VDP_GENERAL_POLL_COOKIE.
#define VDP_SETUP_POLL_COOKIE 0x4f

/// @def VDP_PACKET_KEYBOARD
///
/// @brief The command of a VDP keyboard event packet.
#define VDP_PACKET_KEYBOARD 0x01

/// @def VDP_PACKET_MAX_LENGTH
///
/// @brief The longest data length VdpPacketParser accepts for a command whose
/// length it doesn't know.
#define VDP_PACKET_MAX_LENGTH 16

/// @def VDP_PACKET_MAX_DATA
///
/// @brief The number of data bytes of a packet that VdpPacketParser keeps.
/// Longer packets are parsed, but only this many of their bytes are kept.
#define VDP_PACKET_MAX_DATA 8

/// @def VDP_KEY_MAX_BYTES
///
/// @brief The most bytes a single key event translates to.
#define VDP_KEY_MAX_BYTES 4

/// @struct VdpPacketParser
///
/// @brief State of the parse of the packets the VDP sends: a header byte of
/// 0x80 | command, a length byte, then that many data bytes.
///
/// @param state Which byte of a packet is expected next.
/// @param command The command of the packet being parsed.
/// @param length The number of data bytes in the packet being parsed.
/// @param received The number of data bytes received so far.
/// @param data The first VDP_PACKET_MAX_DATA data bytes of the packet.
typedef struct VdpPacketParser {
  uint8_t state;
  uint8_t command;
  uint8_t length;
  uint8_t received;
  uint8_t data[VDP_PACKET_MAX_DATA];
} VdpPacketParser;

bool vdpParseByte(VdpPacketParser *parser, uint8_t byte);
size_t vdpTranslateKey(const uint8_t *keyData, uint8_t *output);
bool vdpIsPollReply(const VdpPacketParser *parser, uint8_t cookie);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VDP_KEYBOARD_H
