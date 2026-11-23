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

/// @file VdpEvents.h
///
/// @brief The queue of input events the VDP driver sends the display's owner.

#ifndef VDP_EVENTS_H
#define VDP_EVENTS_H

#include "GraphicsProtocol.h"
#include "VdpKeyboard.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @struct VdpEventState
///
/// @brief What the event queue needs to know about the packets before the
/// next one.
///
/// @param buttons The buttons of the last mouse packet.
/// @param motionQueued Whether the newest queued event is a pointer event that
///   changed no button, which later motion may replace.
typedef struct VdpEventState {
  uint8_t buttons;
  bool    motionQueued;
} VdpEventState;

void vdpQueueEvent(GfxInputEventsArgs *queue, VdpEventState *state,
  const VdpPacketParser *parser, bool numLock);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // VDP_EVENTS_H
