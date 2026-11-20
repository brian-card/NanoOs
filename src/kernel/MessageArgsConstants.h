///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.19.2026
///
/// @file              MessageArgsConstants.h
///
/// @brief             The ownership and direction values of the message
///                    argument API, shared by the kernel's MessageArgs.h and
///                    the overlays' ExecutiveProcesses.h.
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

#ifndef MESSAGE_ARGS_CONSTANTS_H
#define MESSAGE_ARGS_CONSTANTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @typedef MessageArgsOwnership
///
/// @brief Who frees storage made with MESSAGE_ARGS_CREATE or
/// MESSAGE_ARGS_CREATE_BYTES.
typedef uint8_t MessageArgsOwnership;

/// @def MESSAGE_ARGS_ROUND_TRIP
///
/// @brief The sender owns the storage before and after the exchange and frees
/// it, unless the recipient adopts it with MESSAGE_ARGS_ADOPT.
#define MESSAGE_ARGS_ROUND_TRIP ((MessageArgsOwnership) 0x01)

/// @def MESSAGE_ARGS_ONE_WAY
///
/// @brief Ownership passes to the recipient when the message is sent, and the
/// recipient frees the storage.  The only kind that may be sent without
/// waiting.
#define MESSAGE_ARGS_ONE_WAY ((MessageArgsOwnership) 0x02)


/// @typedef MessageBufferDirection
///
/// @brief Which way the data in a buffer shared with MESSAGE_BUFFER_SHARE
/// moves.
typedef uint8_t MessageBufferDirection;

/// @def MESSAGE_BUFFER_IN
///
/// @brief The recipient reads the buffer and must not write it.
#define MESSAGE_BUFFER_IN ((MessageBufferDirection) 0x01)

/// @def MESSAGE_BUFFER_OUT
///
/// @brief The recipient writes the buffer and the sender reads it after
/// unsharing it.
#define MESSAGE_BUFFER_OUT ((MessageBufferDirection) 0x02)

/// @def MESSAGE_BUFFER_INOUT
///
/// @brief The recipient reads and writes the buffer.
#define MESSAGE_BUFFER_INOUT \
  ((MessageBufferDirection) (MESSAGE_BUFFER_IN | MESSAGE_BUFFER_OUT))

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MESSAGE_ARGS_CONSTANTS_H
