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

#include "AgonKeycodes.h"
#include "GraphicsProtocol.h"
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
  } else if (command == VDP_PACKET_MOUSE) {
    return length == VDP_MOUSE_PACKET_LENGTH;
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

_Static_assert(
  (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_ESCAPE - KEYCODE_ESCAPE)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_INSERT - KEYCODE_INSERT)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_DELETE - KEYCODE_DELETE)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT
    == GFX_KEY_BACKSPACE - KEYCODE_BACKSPACE)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_HOME - KEYCODE_HOME)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_END - KEYCODE_END)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT
    == GFX_KEY_CAPS_LOCK - KEYCODE_CAPSLOCK)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_TAB - KEYCODE_TAB)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_ENTER - KEYCODE_RETURN)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT
    == GFX_KEY_MENU - KEYCODE_APPLICATION)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_PAGE_UP - KEYCODE_PAGEUP)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT
    == GFX_KEY_PAGE_DOWN - KEYCODE_PAGEDOWN)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_UP - KEYCODE_UP)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_DOWN - KEYCODE_DOWN)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_LEFT - KEYCODE_LEFT)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_RIGHT - KEYCODE_RIGHT)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_F1 - KEYCODE_F1)
  && (GFX_KEY_LEFT_SHIFT - KEYCODE_LSHIFT == GFX_KEY_F12 - KEYCODE_F12),
  "GfxKey's named keys must follow the VDP's keycodes");
_Static_assert((KEYCODE_KEYPAD_INSERT == KEYCODE_INSERT + 1)
  && (KEYCODE_KEYPAD_DELETE == KEYCODE_DELETE + 1)
  && (KEYCODE_KEYPAD_HOME == (KEYCODE_HOME + 1))
  && (KEYCODE_KEYPAD_END == (KEYCODE_END + 1))
  && (KEYCODE_KEYPAD_PAGEUP == (KEYCODE_PAGEUP + 1))
  && (KEYCODE_KEYPAD_PAGEDOWN == (KEYCODE_PAGEDOWN + 1))
  && (KEYCODE_KEYPAD_UP == (KEYCODE_UP + 1))
  && (KEYCODE_KEYPAD_DOWN == (KEYCODE_DOWN + 1))
  && (KEYCODE_KEYPAD_LEFT == (KEYCODE_LEFT + 1))
  && (KEYCODE_KEYPAD_RIGHT == (KEYCODE_RIGHT + 1)),
  "vdpGfxKey takes a keypad navigation key's main key to be one below it");
_Static_assert((GFX_MOD_CTRL == AGON_KEYMOD_CTRL)
  && (GFX_MOD_SHIFT == AGON_KEYMOD_SHIFT)
  && (GFX_MOD_ALT == AGON_KEYMOD_ALT_LEFT)
  && (GFX_MOD_ALT_GR == AGON_KEYMOD_ALT_RIGHT)
  && (GFX_MOD_CAPS_LOCK == AGON_KEYMOD_CAPS_LOCK)
  && (GFX_MOD_NUM_LOCK == AGON_KEYMOD_NUM_LOCK)
  && (GFX_MOD_SCROLL_LOCK == AGON_KEYMOD_SCROLL_LOCK)
  && (GFX_MOD_GUI == AGON_KEYMOD_GUI),
  "GFX_MOD_* must be the VDP's modifier bits");

/// @fn uint8_t vdpGfxKey(const uint8_t *keyData, bool numLock,
///   uint8_t *ascii)
///
/// @brief Translate the data of a keyboard packet into the GfxKey it names
/// and the character it types.  The keypad follows the driver's Num Lock
/// rather than the VDP's, because the VDP has no command that sets its own.
///
/// @param keyData The 4 data bytes of a keyboard packet: ASCII value,
///   modifiers, FabGL keycode, and 1 if the key went down.
/// @param numLock Whether the driver's Num Lock is on.
/// @param ascii Set to the character the key types, or 0.
///
/// @return Returns the GfxKey.
uint8_t vdpGfxKey(const uint8_t *keyData, bool numLock, uint8_t *ascii) {
  uint8_t keycode = keyData[2];

  *ascii = keyData[0];
  int keypadIndex = vdpKeypadIndex(keycode);
  if (keypadIndex >= 0) {
    *ascii = 0;

    if (numLock && ((keyData[1] & AGON_KEYMOD_SHIFT) == 0)) {
      *ascii = (uint8_t) "0123456789."[keypadIndex];
      return *ascii;
    }

    // Every keypad navigation key's keycode is one above its main key's.
    keycode = _vdpKeypadNavigation[keypadIndex] - 1;
    if (keycode == (KEYCODE_KEYPAD_CENTER - 1)) {
      return GFX_KEY_NONE;
    }
  } else if (keycode == KEYCODE_KEYPAD_ENTER) {
    keycode = KEYCODE_RETURN;
  }

  if (keycode >= KEYCODE_LSHIFT) {
    if (keycode > KEYCODE_F12) {
      return GFX_KEY_NONE;
    }

    return (uint8_t) (keycode - KEYCODE_LSHIFT + GFX_KEY_LEFT_SHIFT);
  } else if ((keycode >= KEYCODE_a) && (keycode <= KEYCODE_Z)) {
    keycode -= KEYCODE_a;

    if (keycode >= 26) {
      keycode -= 26;
    }

    return (uint8_t) ('a' + keycode);
  } else if ((keycode >= KEYCODE_0) && (keycode <= KEYCODE_9)) {
    return (uint8_t) ('0' + keycode - KEYCODE_0);
  } else if (keycode == KEYCODE_SPACE) {
    return ' ';
  } else if ((*ascii >= ' ') && (*ascii <= '~')) {
    return *ascii;
  }

  return GFX_KEY_NONE;
}

/// @var _vdpKeySequenceEnds
///
/// @brief For each GfxKey from GFX_KEY_INSERT to GFX_KEY_RIGHT, the last
/// character of the escape sequence a terminal sends for it:  a letter for
/// ESC [ letter, a digit for ESC [ digit ~, or 0 for no sequence.  A table
/// costs the main overlay less than a switch over the keys.
static const uint8_t _vdpKeySequenceEnds[] = {
  '2', 0, '3', 0, 0, 'H', 0, 'F', 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, '5', 0,
  '6', 0, 'A', 0, 'B', 0, 'D', 0, 'C',
};

_Static_assert(sizeof(_vdpKeySequenceEnds)
  == GFX_KEY_RIGHT - GFX_KEY_INSERT + 1,
  "_vdpKeySequenceEnds must cover GFX_KEY_INSERT to GFX_KEY_RIGHT");

/// @fn size_t vdpTranslateKey(const uint8_t *keyData, bool numLock,
///   uint8_t *output)
///
/// @brief Translate the data of a keyboard packet into the bytes a terminal
/// sends for the key: an escape sequence for a navigation key or F1 to F4,
/// the character it types otherwise, and nothing for a key release.
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

  uint8_t ascii = 0;
  uint8_t key = vdpGfxKey(keyData, numLock, &ascii);

  output[0] = '\x1b';

  if ((key >= GFX_KEY_F1) && (key <= GFX_KEY_F4)) {
    output[1] = 'O';
    output[2] = (uint8_t) ('P' + key - GFX_KEY_F1);
    return 3;
  }

  uint8_t end = 0;

  if ((key >= GFX_KEY_INSERT) && (key <= GFX_KEY_RIGHT)) {
    end = _vdpKeySequenceEnds[key - GFX_KEY_INSERT];
  }

  if (end != 0) {
    output[1] = '[';
    output[2] = end;

    if (end >= 'A') {
      return 3;
    }

    output[3] = '~';
    return 4;
  } else if (ascii != 0) {
    output[0] = ascii;
    return 1;
  }

  return 0;
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

/// @fn bool vdpIsReleaseKey(const uint8_t *keyData)
///
/// @brief Determine whether a keyboard packet is Ctrl+Alt+Backspace going
/// down, which takes the display back from its owner for the console.
///
/// @param keyData The four data bytes of a keyboard packet:  ASCII value,
///   modifiers, FabGL keycode, and 1 if the key went down.
///
/// @return Returns true if it is, false otherwise.
bool vdpIsReleaseKey(const uint8_t *keyData) {
  return (keyData[3] == 1) && (keyData[2] == KEYCODE_BACKSPACE)
    && ((keyData[1] & AGON_KEYMOD_CTRL) != 0)
    && ((keyData[1] & (AGON_KEYMOD_ALT_LEFT | AGON_KEYMOD_ALT_RIGHT)) != 0);
}
