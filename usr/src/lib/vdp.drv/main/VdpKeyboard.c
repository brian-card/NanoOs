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
/// @file VdpKeyboard.c
///
/// @brief Parsing of the packets the Agon Light 2 VDP sends and translation of
/// its keyboard events into the byte stream a terminal sends.

#include <string.h>

#include "AgonKeycodes.h"
#include "VdpKeyboard.h"

/// @enum VdpPacketState
///
/// @brief Which byte of a packet VdpPacketParser expects next.
enum VdpPacketState {
  VDP_PACKET_STATE_HEADER,
  VDP_PACKET_STATE_LENGTH,
  VDP_PACKET_STATE_DATA,
};

/// @fn static bool vdpPacketLengthIsValid(uint8_t command, uint8_t length)
///
/// @brief Determine whether a length byte can follow a packet's header.
///
/// @param command The command of the packet.
/// @param length The length byte.
///
/// @return Returns true if a packet with the command can have the length,
/// false otherwise.
static bool vdpPacketLengthIsValid(uint8_t command, uint8_t length) {
  if (command == VDP_PACKET_GENERAL_POLL) {
    return length == 1;
  } else if (command == VDP_PACKET_KEYBOARD) {
    return length == 4;
  }
  return length <= VDP_PACKET_MAX_LENGTH;
}

/// @fn bool vdpParseByte(VdpPacketParser *parser, uint8_t byte)
///
/// @brief Feed one byte from the VDP to a packet parser.  Bytes that can't
/// start a packet are skipped, and a header followed by a length its command
/// can't have is abandoned, so a packet that lost a byte in transit costs no
/// more than itself and the parser resynchronizes on the next header.
///
/// @param parser A pointer to the VdpPacketParser, zeroed before its first
///   byte.
/// @param byte The byte received from the VDP.
///
/// @return Returns true if the byte completed a packet, whose command,
/// length, and data are then in the parser, false otherwise.
bool vdpParseByte(VdpPacketParser *parser, uint8_t byte) {
  if (parser->state == VDP_PACKET_STATE_HEADER) {
    if ((byte & 0x80) != 0) {
      parser->command = byte & 0x7f;
      parser->state = VDP_PACKET_STATE_LENGTH;
    }
    return false;
  } else if (parser->state == VDP_PACKET_STATE_LENGTH) {
    if (vdpPacketLengthIsValid(parser->command, byte) == false) {
      parser->state = VDP_PACKET_STATE_HEADER;
      return vdpParseByte(parser, byte);
    }
    parser->length = byte;
    parser->received = 0;
    if (byte == 0) {
      parser->state = VDP_PACKET_STATE_HEADER;
      return true;
    }
    parser->state = VDP_PACKET_STATE_DATA;
    return false;
  }

  if ((parser->command == VDP_PACKET_GENERAL_POLL) && ((byte & 0x80) != 0)) {
    // VDP_GENERAL_POLL_COOKIE is below 0x80, so this is the header of the
    // next packet and the reply lost its cookie.
    parser->state = VDP_PACKET_STATE_HEADER;
    return vdpParseByte(parser, byte);
  }
  if (parser->received < VDP_PACKET_MAX_DATA) {
    parser->data[parser->received] = byte;
  }
  parser->received++;
  if (parser->received == parser->length) {
    parser->state = VDP_PACKET_STATE_HEADER;
    return true;
  }
  return false;
}

/// @fn size_t vdpTranslateKey(const uint8_t *keyData, uint8_t *output)
///
/// @brief Translate the data of a keyboard packet into the bytes a terminal
/// sends for the key: an escape sequence for a navigation or function key,
/// the key's ASCII value otherwise, and nothing for a key release.
///
/// @param keyData The 4 data bytes of a keyboard packet: ASCII value,
///   modifiers, FabGL keycode, and 1 if the key went down.
/// @param output A buffer of at least VDP_KEY_MAX_BYTES bytes to fill.
///
/// @return Returns the number of bytes written to output.
size_t vdpTranslateKey(const uint8_t *keyData, uint8_t *output) {
  if (keyData[3] == 0) {
    return 0;
  }

  const char *sequence = NULL;
  switch (keyData[2]) {
    case KEYCODE_UP:
    case KEYCODE_KEYPAD_UP:
      sequence = "\x1b[A";
      break;
    case KEYCODE_DOWN:
    case KEYCODE_KEYPAD_DOWN:
      sequence = "\x1b[B";
      break;
    case KEYCODE_RIGHT:
    case KEYCODE_KEYPAD_RIGHT:
      sequence = "\x1b[C";
      break;
    case KEYCODE_LEFT:
    case KEYCODE_KEYPAD_LEFT:
      sequence = "\x1b[D";
      break;
    case KEYCODE_HOME:
    case KEYCODE_KEYPAD_HOME:
      sequence = "\x1b[H";
      break;
    case KEYCODE_END:
    case KEYCODE_KEYPAD_END:
      sequence = "\x1b[F";
      break;
    case KEYCODE_INSERT:
    case KEYCODE_KEYPAD_INSERT:
      sequence = "\x1b[2~";
      break;
    case KEYCODE_DELETE:
    case KEYCODE_KEYPAD_DELETE:
      sequence = "\x1b[3~";
      break;
    case KEYCODE_PAGEUP:
    case KEYCODE_KEYPAD_PAGEUP:
      sequence = "\x1b[5~";
      break;
    case KEYCODE_PAGEDOWN:
    case KEYCODE_KEYPAD_PAGEDOWN:
      sequence = "\x1b[6~";
      break;
    case KEYCODE_F1:
      sequence = "\x1bOP";
      break;
    case KEYCODE_F2:
      sequence = "\x1bOQ";
      break;
    case KEYCODE_F3:
      sequence = "\x1bOR";
      break;
    case KEYCODE_F4:
      sequence = "\x1bOS";
      break;
    default:
      break;
  }

  if (sequence != NULL) {
    size_t length = strlen(sequence);
    memcpy(output, sequence, length);
    return length;
  } else if (keyData[0] != 0) {
    output[0] = keyData[0];
    return 1;
  }

  return 0;
}

/// @fn bool vdpIsPollReply(const VdpPacketParser *parser)
///
/// @brief Determine whether the packet a parser just completed is the VDP's
/// reply to the driver's general poll.
///
/// @param parser A pointer to the VdpPacketParser whose vdpParseByte call
///   just returned true.
///
/// @return Returns true if the packet is a poll reply carrying
/// VDP_GENERAL_POLL_COOKIE, false otherwise.
bool vdpIsPollReply(const VdpPacketParser *parser) {
  return (parser->command == VDP_PACKET_GENERAL_POLL)
    && (parser->length == 1)
    && (parser->data[0] == VDP_GENERAL_POLL_COOKIE);
}
