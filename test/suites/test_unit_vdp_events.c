///////////////////////////////////////////////////////////////////////////////
///
/// @file              test_unit_vdp_events.c
///
/// @brief             Unit tests (no kernel boot) for the VDP driver's queue
///                    of the input events it sends the display's owner.
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

#include "NanoOsTest.h"

#include <string.h>

#include "../../userspace/usr/src/lib/vdp.drv/main/VdpEvents.c"

/// @fn static void mouse(GfxInputEventsArgs *queue, VdpEventState *state,
///   uint16_t pointerX, uint16_t pointerY, uint8_t buttons, int8_t wheel)
///
/// @brief Queue the mouse packet the VDP sends for a pointer state.
///
/// @param queue The queue.
/// @param state The queue's VdpEventState.
/// @param pointerX The pointer's column.
/// @param pointerY The pointer's row.
/// @param buttons The button bits.
/// @param wheel The wheel delta.
///
/// @return This function returns no value.
static void mouse(GfxInputEventsArgs *queue, VdpEventState *state,
  uint16_t pointerX, uint16_t pointerY, uint8_t buttons, int8_t wheel
) {
  VdpPacketParser parser = {
    .command = VDP_PACKET_MOUSE, .length = VDP_MOUSE_PACKET_LENGTH,
    .data = { (uint8_t) pointerX, (uint8_t) (pointerX >> 8),
      (uint8_t) pointerY, (uint8_t) (pointerY >> 8), buttons, (uint8_t) wheel,
      0x55, 0x55, 0x55, 0x55 },
  };
  vdpQueueEvent(queue, state, &parser);
}

/// @fn static void key(GfxInputEventsArgs *queue, VdpEventState *state,
///   uint8_t ascii, uint8_t modifiers, uint8_t keycode, uint8_t down)
///
/// @brief Queue the keyboard packet the VDP sends for a key.
///
/// @param queue The queue.
/// @param state The queue's VdpEventState.
/// @param ascii The key's ASCII value.
/// @param modifiers The modifier bits.
/// @param keycode The FabGL keycode.
/// @param down 1 for a press, 0 for a release.
///
/// @return This function returns no value.
static void key(GfxInputEventsArgs *queue, VdpEventState *state,
  uint8_t ascii, uint8_t modifiers, uint8_t keycode, uint8_t down
) {
  VdpPacketParser parser = {
    .command = VDP_PACKET_KEYBOARD, .length = 4,
    .data = { ascii, modifiers, keycode, down },
  };
  vdpQueueEvent(queue, state, &parser);
}

/// @fn static GfxEvent pointerEvent(int16_t pointerX, int16_t pointerY,
///   uint8_t buttons, int8_t wheel)
///
/// @brief Make the pointer event a test expects.
///
/// @param pointerX The column.
/// @param pointerY The row.
/// @param buttons The button bits.
/// @param wheel The wheel delta.
///
/// @return Returns the event, every other field zero.
static GfxEvent pointerEvent(int16_t pointerX, int16_t pointerY,
  uint8_t buttons, int8_t wheel
) {
  GfxEvent event;
  memset(&event, 0, sizeof(event));
  event.type = GFX_EVENT_POINTER;
  event.buttons = buttons;
  event.wheel = wheel;
  event.x = pointerX;
  event.y = pointerY;
  return event;
}

/// @fn static GfxEvent keyEvent(uint8_t ascii, uint8_t modifiers,
///   uint8_t keycode, uint8_t down)
///
/// @brief Make the key event a test expects.
///
/// @param ascii The ASCII value.
/// @param modifiers The modifier bits.
/// @param keycode The FabGL keycode.
/// @param down 1 for a press, 0 for a release.
///
/// @return Returns the event, every other field zero.
static GfxEvent keyEvent(uint8_t ascii, uint8_t modifiers, uint8_t keycode,
  uint8_t down
) {
  GfxEvent event;
  memset(&event, 0, sizeof(event));
  event.type = GFX_EVENT_KEY;
  event.ascii = ascii;
  event.modifiers = modifiers;
  event.keycode = keycode;
  event.down = down;
  return event;
}

/// @fn static bool sameEvent(const GfxEvent *expected, const GfxEvent *actual)
///
/// @brief Compare every field of two events.
///
/// @param expected The event the test expects.
/// @param actual The queued event.
///
/// @return Returns true if every field is equal, false otherwise.
static bool sameEvent(const GfxEvent *expected, const GfxEvent *actual) {
  return (expected->type == actual->type)
    && (expected->buttons == actual->buttons)
    && (expected->wheel == actual->wheel)
    && (expected->ascii == actual->ascii)
    && (expected->keycode == actual->keycode)
    && (expected->modifiers == actual->modifiers)
    && (expected->down == actual->down)
    && (expected->x == actual->x)
    && (expected->y == actual->y);
}

NANO_OS_TEST(unit_vdp_events, hundred_motions_leave_one_event_at_the_last) {
  GfxInputEventsArgs queue;
  memset(&queue, 0, sizeof(queue));
  VdpEventState state = { 0 };

  for (int ii = 0; ii < 100; ii++) {
    mouse(&queue, &state, (uint16_t) (ii * 3), (uint16_t) (ii * 2), 0, 0);
  }

  GfxEvent expected = pointerEvent(297, 198, 0, 0);
  NANO_OS_ASSERT_EQ_INT(1, queue.count);
  NANO_OS_ASSERT_TRUE(sameEvent(&expected, &queue.events[0]));
}

NANO_OS_TEST(unit_vdp_events, press_motion_and_release_keep_all_three) {
  GfxInputEventsArgs queue;
  memset(&queue, 0, sizeof(queue));
  VdpEventState state = { 0 };

  mouse(&queue, &state, 10, 20, 1, 0);
  mouse(&queue, &state, 30, 40, 1, 0);
  mouse(&queue, &state, 35, 45, 1, 0);
  mouse(&queue, &state, 35, 45, 0, 0);

  GfxEvent expected[] = {
    pointerEvent(10, 20, 1, 0),
    pointerEvent(35, 45, 1, 0),
    pointerEvent(35, 45, 0, 0),
  };
  NANO_OS_ASSERT_EQ_INT(3, queue.count);
  for (int ii = 0; ii < 3; ii++) {
    NANO_OS_ASSERT_TRUE(sameEvent(&expected[ii], &queue.events[ii]));
  }
}

NANO_OS_TEST(unit_vdp_events, twenty_keys_leave_seven_and_an_overflow) {
  GfxInputEventsArgs queue;
  memset(&queue, 0, sizeof(queue));
  VdpEventState state = { 0 };

  for (int ii = 0; ii < 20; ii++) {
    key(&queue, &state, (uint8_t) ('a' + ii), 0x40, (uint8_t) (0x20 + ii), 1);
  }

  NANO_OS_ASSERT_EQ_INT(GFX_EVENTS_PER_MESSAGE, queue.count);
  for (int ii = 0; ii < GFX_EVENTS_PER_MESSAGE - 1; ii++) {
    GfxEvent expected
      = keyEvent((uint8_t) ('a' + ii), 0x40, (uint8_t) (0x20 + ii), 1);
    NANO_OS_ASSERT_TRUE(sameEvent(&expected, &queue.events[ii]));
  }
  GfxEvent overflow;
  memset(&overflow, 0, sizeof(overflow));
  overflow.type = GFX_EVENT_OVERFLOW;
  NANO_OS_ASSERT_TRUE(sameEvent(&overflow,
    &queue.events[GFX_EVENTS_PER_MESSAGE - 1]));
}

NANO_OS_TEST(unit_vdp_events, key_releases_and_raw_fields_are_kept) {
  GfxInputEventsArgs queue;
  memset(&queue, 0, sizeof(queue));
  VdpEventState state = { 0 };

  key(&queue, &state, 0, 0x81, 0x9c, 1);
  key(&queue, &state, 0, 0x81, 0x9c, 0);

  GfxEvent press = keyEvent(0, 0x81, 0x9c, 1);
  GfxEvent release = keyEvent(0, 0x81, 0x9c, 0);
  NANO_OS_ASSERT_EQ_INT(2, queue.count);
  NANO_OS_ASSERT_TRUE(sameEvent(&press, &queue.events[0]));
  NANO_OS_ASSERT_TRUE(sameEvent(&release, &queue.events[1]));
}

NANO_OS_TEST(unit_vdp_events, coalesced_motion_adds_up_the_wheel) {
  GfxInputEventsArgs queue;
  memset(&queue, 0, sizeof(queue));
  VdpEventState state = { 0 };

  mouse(&queue, &state, 1, 1, 0, 1);
  mouse(&queue, &state, 2, 2, 0, 1);
  mouse(&queue, &state, 3, 3, 0, -3);

  GfxEvent expected = pointerEvent(3, 3, 0, -1);
  NANO_OS_ASSERT_EQ_INT(1, queue.count);
  NANO_OS_ASSERT_TRUE(sameEvent(&expected, &queue.events[0]));
}

NANO_OS_TEST(unit_vdp_events, a_key_between_motions_keeps_them_apart) {
  GfxInputEventsArgs queue;
  memset(&queue, 0, sizeof(queue));
  VdpEventState state = { 0 };

  mouse(&queue, &state, 5, 6, 0, 0);
  key(&queue, &state, 'q', 0, 0x30, 1);
  mouse(&queue, &state, 7, 8, 0, 0);
  mouse(&queue, &state, 9, 10, 0, 0);

  GfxEvent expected[] = {
    pointerEvent(5, 6, 0, 0),
    keyEvent('q', 0, 0x30, 1),
    pointerEvent(9, 10, 0, 0),
  };
  NANO_OS_ASSERT_EQ_INT(3, queue.count);
  for (int ii = 0; ii < 3; ii++) {
    NANO_OS_ASSERT_TRUE(sameEvent(&expected[ii], &queue.events[ii]));
  }
}

NANO_OS_TEST(unit_vdp_events, motion_after_a_send_starts_a_new_event) {
  GfxInputEventsArgs queue;
  memset(&queue, 0, sizeof(queue));
  VdpEventState state = { 0 };

  mouse(&queue, &state, 5, 6, 0, 0);
  queue.count = 0;
  mouse(&queue, &state, 640, 479, 0, 0);

  GfxEvent expected = pointerEvent(640, 479, 0, 0);
  NANO_OS_ASSERT_EQ_INT(1, queue.count);
  NANO_OS_ASSERT_TRUE(sameEvent(&expected, &queue.events[0]));
}

NANO_OS_TEST(unit_vdp_events, a_full_queue_overflows_on_motion_too) {
  GfxInputEventsArgs queue;
  memset(&queue, 0, sizeof(queue));
  VdpEventState state = { 0 };

  for (int ii = 0; ii < GFX_EVENTS_PER_MESSAGE; ii++) {
    mouse(&queue, &state, 1, 1, (uint8_t) (ii & 1), 0);
  }
  mouse(&queue, &state, 2, 2, 1, 0);
  mouse(&queue, &state, 3, 3, 1, 0);

  GfxEvent overflow;
  memset(&overflow, 0, sizeof(overflow));
  overflow.type = GFX_EVENT_OVERFLOW;
  GfxEvent last = pointerEvent(1, 1, 0, 0);
  NANO_OS_ASSERT_EQ_INT(GFX_EVENTS_PER_MESSAGE, queue.count);
  NANO_OS_ASSERT_TRUE(sameEvent(&last,
    &queue.events[GFX_EVENTS_PER_MESSAGE - 2]));
  NANO_OS_ASSERT_TRUE(sameEvent(&overflow,
    &queue.events[GFX_EVENTS_PER_MESSAGE - 1]));
  NANO_OS_ASSERT_EQ_INT(1, state.buttons);
}

NANO_OS_TEST(unit_vdp_events, other_packets_are_not_events) {
  GfxInputEventsArgs queue;
  memset(&queue, 0, sizeof(queue));
  VdpEventState state = { 0 };
  VdpPacketParser parser = {
    .command = VDP_PACKET_GENERAL_POLL, .length = 1, .data = { 0x4e },
  };

  vdpQueueEvent(&queue, &state, &parser);

  NANO_OS_ASSERT_EQ_INT(0, queue.count);
}
