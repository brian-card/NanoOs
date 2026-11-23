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
/// @file VdpLocks.c
///
/// @brief The keyboard's lock keys:  the driver's own Num Lock and the LEDs
/// the keyboard should show.  Kept out of the main overlay because lock keys
/// are rare.

#include "AgonKeycodes.h"
#include "VdpKeyboard.h"

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
