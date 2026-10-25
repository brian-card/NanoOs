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

/// @file AgonResetKeys.c
///
/// @brief Watch for Ctrl+Alt+Del in the bytes the Agon Light 2's VDP sends
/// over UART0.

#include "AgonKeycodes.h"
#include "AgonResetKeys.h"

/// @enum AgonResetKeysState
///
/// @brief Which byte of a VDP packet AgonResetKeys expects next.
enum AgonResetKeysState {
  AGON_RESET_KEYS_STATE_HEADER = 0,
  AGON_RESET_KEYS_STATE_LENGTH,
  AGON_RESET_KEYS_STATE_DATA,
};

/// @def AGON_PACKET_GENERAL_POLL
///
/// @brief The command of the VDP's reply to a general poll.
#define AGON_PACKET_GENERAL_POLL 0x00

/// @def AGON_PACKET_KEYBOARD
///
/// @brief The command of a VDP keyboard event packet.
#define AGON_PACKET_KEYBOARD 0x01

/// @def AGON_PACKET_MAX_LENGTH
///
/// @brief The longest data length accepted for a command whose length isn't
/// known.
#define AGON_PACKET_MAX_LENGTH 16

/// @def AGON_RESET_KEYS_CTRL
///
/// @brief The bits of AgonResetKeys.held for the left and right Ctrl keys.
#define AGON_RESET_KEYS_CTRL 0x03

/// @def AGON_RESET_KEYS_ALT
///
/// @brief The bits of AgonResetKeys.held for the left and right Alt keys.
#define AGON_RESET_KEYS_ALT 0x0c

/// @fn static uint8_t agonResetKeysHeldBit(uint8_t keycode)
///
/// @brief Get the bit of AgonResetKeys.held that tracks a key.
///
/// @param keycode The FabGL keycode of the key.
///
/// @return Returns the key's bit, or 0 if the key isn't a Ctrl or Alt key.
static uint8_t agonResetKeysHeldBit(uint8_t keycode) {
  switch (keycode) {
    case KEYCODE_LCTRL:
      return 0x01;
    case KEYCODE_RCTRL:
      return 0x02;
    case KEYCODE_LALT:
      return 0x04;
    case KEYCODE_RALT:
      return 0x08;
    default:
      return 0;
  }
}

/// @fn static bool agonResetKeysLengthIsValid(uint8_t command, uint8_t length)
///
/// @brief Determine whether a length byte can follow a packet's header.
///
/// @param command The command of the packet.
/// @param length The length byte.
///
/// @return Returns true if a packet with the command can have the length,
/// false otherwise.
static bool agonResetKeysLengthIsValid(uint8_t command, uint8_t length) {
  if (command == AGON_PACKET_GENERAL_POLL) {
    return length == 1;
  } else if (command == AGON_PACKET_KEYBOARD) {
    return length == 4;
  }
  return (length > 0) && (length <= AGON_PACKET_MAX_LENGTH);
}

/// @fn bool agonResetKeysScanByte(AgonResetKeys *keys, uint8_t byte)
///
/// @brief Feed one byte from the VDP to the watch for Ctrl+Alt+Del, tracking
/// which Ctrl and Alt keys are down.  Packets are framed the way the VDP
/// driver frames them, so the watch resynchronizes on the same bytes.
///
/// @param keys A pointer to the AgonResetKeys.
/// @param byte The byte received from the VDP.
///
/// @return Returns true if the byte completed a press of Del while a Ctrl key
/// and an Alt key were down, false otherwise.
bool agonResetKeysScanByte(AgonResetKeys *keys, uint8_t byte) {
  if (keys->state == AGON_RESET_KEYS_STATE_DATA) {
    if ((keys->command == AGON_PACKET_GENERAL_POLL) && ((byte & 0x80) != 0)) {
      // A poll reply's cookie is below 0x80, so this is the next header.
      keys->state = AGON_RESET_KEYS_STATE_HEADER;
    } else {
      keys->received++;
      if (keys->received == keys->length) {
        keys->state = AGON_RESET_KEYS_STATE_HEADER;
      }
      if (keys->command != AGON_PACKET_KEYBOARD) {
        return false;
      } else if (keys->received == 3) {
        keys->keycode = byte;
        return false;
      } else if (keys->received != 4) {
        return false;
      }

      uint8_t heldBit = agonResetKeysHeldBit(keys->keycode);
      if (byte == 0) {
        keys->held &= (uint8_t) ~heldBit;
        return false;
      }
      keys->held |= heldBit;
      return ((keys->keycode == KEYCODE_DELETE)
          || (keys->keycode == KEYCODE_KEYPAD_DELETE))
        && ((keys->held & AGON_RESET_KEYS_CTRL) != 0)
        && ((keys->held & AGON_RESET_KEYS_ALT) != 0);
    }
  }

  if (keys->state == AGON_RESET_KEYS_STATE_LENGTH) {
    if (agonResetKeysLengthIsValid(keys->command, byte) == true) {
      keys->length = byte;
      keys->received = 0;
      keys->state = AGON_RESET_KEYS_STATE_DATA;
      return false;
    }
    keys->state = AGON_RESET_KEYS_STATE_HEADER;
  }

  if ((byte & 0x80) != 0) {
    keys->command = byte & 0x7f;
    keys->state = AGON_RESET_KEYS_STATE_LENGTH;
  }
  return false;
}
