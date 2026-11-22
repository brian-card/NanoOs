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

/// @file VdpEvents.c
///
/// @brief The queue of input events the VDP driver sends the display's owner.

#include <string.h>

#include "VdpEvents.h"

/// @fn void vdpQueueEvent(GfxInputEventsArgs *queue, VdpEventState *state,
///   const VdpPacketParser *parser)
///
/// @brief Add the keyboard or mouse packet a parser just completed to a queue
/// of events, ignoring any other packet.  Motion, a mouse packet that changes
/// no button, replaces the position of the newest event and adds to its wheel
/// if that event is motion too.  An event that doesn't fit is dropped, and the
/// newest event becomes GFX_EVENT_OVERFLOW.
///
/// @param queue The events waiting to be sent.
/// @param state The queue's VdpEventState, zeroed when the display is opened.
/// @param parser A pointer to the VdpPacketParser whose vdpParseByte call
///   just returned true.
///
/// @return This function returns no value.
void vdpQueueEvent(GfxInputEventsArgs *queue, VdpEventState *state,
  const VdpPacketParser *parser
) {
  const uint8_t *data = parser->data;
  GfxEvent event;
  memset(&event, 0, sizeof(event));
  bool motion = false;
  if (parser->command == VDP_PACKET_KEYBOARD) {
    event.type = GFX_EVENT_KEY;
    event.ascii = data[0];
    event.modifiers = data[1];
    event.keycode = data[2];
    event.down = data[3];
  } else if (parser->command == VDP_PACKET_MOUSE) {
    event.x = (int16_t) (data[0] | (data[1] << 8));
    event.y = (int16_t) (data[2] | (data[3] << 8));
    event.buttons = data[4];
    event.wheel = (int8_t) data[5];
    motion = (data[4] == state->buttons);
    state->buttons = data[4];
  } else {
    return;
  }

  GfxEvent *slot = &queue->events[queue->count];
  if ((motion == true) && (state->motionQueued == true) && (queue->count > 0)) {
    slot--;
    event.wheel = (int8_t) (event.wheel + slot->wheel);
  } else if (queue->count == GFX_EVENTS_PER_MESSAGE) {
    slot--;
    memset(&event, 0, sizeof(event));
    event.type = GFX_EVENT_OVERFLOW;
    motion = false;
  } else {
    queue->count++;
  }
  state->motionQueued = motion;
  *slot = event;
}
