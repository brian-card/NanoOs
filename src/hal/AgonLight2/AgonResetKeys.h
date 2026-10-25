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

/// @file AgonResetKeys.h
///
/// @brief Watch for Ctrl+Alt+Del in the bytes the Agon Light 2's VDP sends
/// over UART0.

#ifndef AGON_RESET_KEYS_H
#define AGON_RESET_KEYS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @struct AgonResetKeys
///
/// @brief State of the watch for Ctrl+Alt+Del, zeroed before its first byte.
///
/// @param state Which byte of a VDP packet is expected next.
/// @param command The command of the packet being parsed.
/// @param length The number of data bytes in the packet being parsed.
/// @param received The number of data bytes received so far.
/// @param keycode The FabGL keycode of the keyboard packet being parsed.
/// @param held One bit for each Ctrl and Alt key that is down.
typedef struct AgonResetKeys {
  uint8_t state;
  uint8_t command;
  uint8_t length;
  uint8_t received;
  uint8_t keycode;
  uint8_t held;
} AgonResetKeys;

bool agonResetKeysScanByte(AgonResetKeys *keys, uint8_t byte);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AGON_RESET_KEYS_H
