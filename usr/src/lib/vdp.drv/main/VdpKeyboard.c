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

/// @var _vdpKeypadNavigation
///
/// @brief The keycodes the VDP reports for keypad 0 through 9 and . while its
/// own Num Lock is off, in that order.
static const uint8_t _vdpKeypadNavigation[] = {
  KEYCODE_KEYPAD_INSERT, KEYCODE_KEYPAD_END, KEYCODE_KEYPAD_DOWN,
  KEYCODE_KEYPAD_PAGEDOWN, KEYCODE_KEYPAD_LEFT, KEYCODE_KEYPAD_CENTER,
  KEYCODE_KEYPAD_RIGHT, KEYCODE_KEYPAD_HOME, KEYCODE_KEYPAD_UP,
  KEYCODE_KEYPAD_PAGEUP, KEYCODE_KEYPAD_DELETE,
};

/// @fn static int vdpKeypadIndex(uint8_t keycode)
///
/// @brief Find which of keypad 0 through 9 and . a keycode belongs to, in
/// either of the forms the VDP reports depending on its own Num Lock.
///
/// @param keycode The FabGL keycode of the key.
///
/// @return Returns 0 through 9 for the digits, 10 for ., and -1 for a key that
/// isn't one of them.
static int vdpKeypadIndex(uint8_t keycode) {
  if ((keycode >= KEYCODE_KEYPAD_0) && (keycode <= KEYCODE_KEYPAD_9)) {
    return keycode - KEYCODE_KEYPAD_0;
  } else if (keycode == KEYCODE_KEYPAD_PERIOD) {
    return 10;
  }
  for (int ii = 0; ii < (int) sizeof(_vdpKeypadNavigation); ii++) {
    if (_vdpKeypadNavigation[ii] == keycode) {
      return ii;
    }
  }

  return -1;
}

/// @fn size_t vdpTranslateKey(const uint8_t *keyData, bool numLock,
///   uint8_t *output)
///
/// @brief Translate the data of a keyboard packet into the bytes a terminal
/// sends for the key: an escape sequence for a navigation or function key,
/// the key's ASCII value otherwise, and nothing for a key release.  The
/// keypad follows the driver's Num Lock rather than the VDP's, because the
/// VDP has no command that sets its own.
///
/// @param keyData The 4 data bytes of a keyboard packet: ASCII value,
///   modifiers, FabGL keycode, and 1 if the key went down.
/// @param numLock Whether the driver's Num Lock is on.
/// @param output A buffer of at least VDP_KEY_MAX_BYTES bytes to fill.
///
/// @return Returns the number of bytes written to output.
size_t vdpTranslateKey(const uint8_t *keyData, bool numLock, uint8_t *output) {
  if (keyData[3] == 0) {
    return 0;
  }

  uint8_t keycode = keyData[2];
  int keypadIndex = vdpKeypadIndex(keycode);
  if (keypadIndex >= 0) {
    if (numLock && ((keyData[1] & AGON_KEYMOD_SHIFT) == 0)) {
      output[0] = (uint8_t) "0123456789."[keypadIndex];
      return 1;
    }
    keycode = _vdpKeypadNavigation[keypadIndex];
  }

  const char *sequence = NULL;
  switch (keycode) {
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
  } else if ((keypadIndex < 0) && (keyData[0] != 0)) {
    output[0] = keyData[0];
    return 1;
  }

  return 0;
}

/// @fn bool vdpUpdateLocks(const uint8_t *keyData, bool *numLock,
///   uint8_t *leds)
///
/// @brief Toggle the driver's Num Lock on the release of the Num Lock key, and
/// work out the LEDs the keyboard should show after the release of any lock
/// key, when the VDP overwrites all three LEDs with its own lock states.
///
/// @param keyData The 4 data bytes of a keyboard packet.
/// @param numLock A pointer to whether the driver's Num Lock is on.
/// @param leds Set to the LED byte of VDU 23,0,&88 when true is returned.
///
/// @return Returns true if the packet is the release of a lock key, so that
/// leds has to be sent to the VDP, false otherwise.
bool vdpUpdateLocks(const uint8_t *keyData, bool *numLock, uint8_t *leds) {
  if (keyData[3] != 0) {
    return false;
  }

  // The VDP reports the lock states from before this release toggles them.
  uint8_t modifiers = keyData[1];
  if (keyData[2] == KEYCODE_NUMLOCK) {
    *numLock = !*numLock;
  } else if (keyData[2] == KEYCODE_CAPSLOCK) {
    modifiers ^= AGON_KEYMOD_CAPS_LOCK;
  } else if (keyData[2] == KEYCODE_SCROLLLOCK) {
    modifiers ^= AGON_KEYMOD_SCROLL_LOCK;
  } else {
    return false;
  }

  *leds = 0;
  if ((modifiers & AGON_KEYMOD_SCROLL_LOCK) != 0) {
    *leds |= VDP_LED_SCROLL_LOCK;
  }
  if ((modifiers & AGON_KEYMOD_CAPS_LOCK) != 0) {
    *leds |= VDP_LED_CAPS_LOCK;
  }
  if (*numLock) {
    *leds |= VDP_LED_NUM_LOCK;
  }

  return true;
}

/// @fn bool vdpIsPollReply(const VdpPacketParser *parser, uint8_t cookie)
///
/// @brief Determine whether the packet a parser just completed is the VDP's
/// reply to a general poll the driver sent with a given cookie.
///
/// @param parser A pointer to the VdpPacketParser whose vdpParseByte call
///   just returned true.
/// @param cookie The cookie the poll was sent with.
///
/// @return Returns true if the packet is a poll reply carrying cookie, false
/// otherwise.
bool vdpIsPollReply(const VdpPacketParser *parser, uint8_t cookie) {
  return (parser->command == VDP_PACKET_GENERAL_POLL)
    && (parser->length == 1)
    && (parser->data[0] == cookie);
}
