///////////////////////////////////////////////////////////////////////////////
///
/// @file              SessionTest.h
///
/// @brief             What sessiontest's overlays share.
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

#ifndef SESSION_TEST_H
#define SESSION_TEST_H

#include <stdint.h>

/// @struct BridgeArgs
///
/// @brief The arguments of the Bridge overlay's bridgeSession.
///
/// @param uart The ID of the UART to write the session's output to.
/// @param rows The number of rows of the session.
/// @param columns The number of columns of the session.
/// @param status The exit status:  0 once the session has ended, 1 if it
///   couldn't be opened.
typedef struct BridgeArgs {
  int32_t uart;
  uint8_t rows;
  uint8_t columns;
  int     status;
} BridgeArgs;

#endif // SESSION_TEST_H
