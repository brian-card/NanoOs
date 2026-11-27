///////////////////////////////////////////////////////////////////////////////
///
/// @file              Input.c
///
/// @brief             The gfxprobe steps that receive input events and
///                    set the mouse cursor.
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "GfxProbe.h"

/// @fn static void printEvent(const GfxEvent *event)
///
/// @brief Print an input event's fields.
///
/// @param event The event.
///
/// @return This function returns no value.
static void printEvent(const GfxEvent *event) {
  if (event->type == GFX_EVENT_POINTER) {
    printf("gfxprobe: pointer %d %d buttons %u wheel %d\n", (int) event->x,
      (int) event->y, (unsigned int) event->buttons, (int) event->wheel);
  } else if (event->type == GFX_EVENT_KEY) {
    printf("gfxprobe: key %u keycode %u modifiers 0x%x down %u\n",
      (unsigned int) event->ascii, (unsigned int) event->keycode,
      (unsigned int) event->modifiers, (unsigned int) event->down);
  } else {
    printf("gfxprobe: event type %u\n", (unsigned int) event->type);
  }
}

/// @fn static int receiveEventsAt(int count, long timeoutMs,
///   const GfxEvent *target)
///
/// @brief Take the input events the driver sends until a number of them
/// have arrived, the display is taken back, or a time runs out, finishing
/// every other message that arrives meanwhile.  Events are printed, unless
/// a target is given, in which case only a pointer event at the target's
/// position counts and nothing is printed.
///
/// @param count The number of events to wait for.
/// @param timeoutMs How long to wait, in milliseconds.
/// @param target The pointer event to wait for, or NULL.
///
/// @return Returns the number of events that counted.
static int receiveEventsAt(int count, long timeoutMs,
  const GfxEvent *target
) {
  struct timespec deadline;
  timespec_get(&deadline, TIME_UTC);
  deadline.tv_sec += timeoutMs / 1000L;
  deadline.tv_nsec += (timeoutMs % 1000L) * 1000000L;

  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_nsec -= 1000000000L;
    deadline.tv_sec++;
  }

  int received = 0;
  while (received < count) {
    ProcessMessage *message = processMessageQueueWait(&deadline);
    if (message == NULL) {
      struct timespec now;
      timespec_get(&now, TIME_UTC);

      if ((now.tv_sec > deadline.tv_sec) || ((now.tv_sec == deadline.tv_sec)
          && (now.tv_nsec >= deadline.tv_nsec))
      ) {
        break;
      }

      continue;
    }
    if (gfxIsInputEvents(message)) {
      const GfxInputEventsArgs *events = gfxInputEvents(message);
      for (int ii = 0; ii < ((int) events->count); ii++) {
        const GfxEvent *event = &events->events[ii];

        if (target == NULL) {
          printEvent(event);
          received++;
        } else if ((event->type == GFX_EVENT_POINTER)
          && (event->x == target->x) && (event->y == target->y)
        ) {
          received++;
        }
      }

      gfxInputEventsDone(message);
      continue;
    }

    int64_t type = processMessageType(message);

    processMessageSetDone(message);

    if (processMessageWaiting(message) == false) {
      processMessageRelease(message);
    }

    if (type == (GFX_COMMAND_SIGNATURE | GFX_MSG_DISPLAY_REVOKED)) {
      printf("gfxprobe: revoked\n");
      break;
    }
  }

  return received;
}

/// @fn void* gfxprobeInput(void *args)
///
/// @brief Carry out one step, printing its result:  "events <count> <ms>"
/// (print the input events that arrive until count have or ms milliseconds
/// pass), "cursor <image> <hotX> <hotY>", "show <0|1>", "warp <x> <y>", and
/// "lag <x> <y>" (warp, and print how long the pointer event took to come).
///
/// @param args A pointer to the PaintArgs, whose consumed stays 0 for any
///   other step.
///
/// @return Always returns the provided pointer.
void* gfxprobeInput(void *args) {
  PaintArgs *paintArgs = (PaintArgs*) args;
  GfxDisplay *disp = paintArgs->disp;
  char **words = paintArgs->words;
  int numWords = paintArgs->numWords;
  const char *step = words[0];

  if ((strcmp(step, "events") == 0) && (numWords >= 3)) {
    int received = receiveEventsAt((int) strtol(words[1], NULL, 0),
      strtol(words[2], NULL, 0), NULL);
    printf("gfxprobe: events %d\n", received);
    paintArgs->consumed = 3;
  } else if ((strcmp(step, "cursor") == 0) && (numWords >= 4)) {
    report("cursor", gfxSetCursor(disp, (GfxHandle) strtol(words[1], NULL, 0),
      (uint8_t) strtol(words[2], NULL, 0),
      (uint8_t) strtol(words[3], NULL, 0)));
    paintArgs->consumed = 4;
  } else if ((strcmp(step, "show") == 0) && (numWords >= 2)) {
    report("show", gfxShowCursor(disp, strtol(words[1], NULL, 0) != 0));
    paintArgs->consumed = 2;
  } else if ((strcmp(step, "lag") == 0) && (numWords >= 3)) {
    GfxEvent target;
    memset(&target, 0, sizeof(target));
    target.x = (int16_t) strtol(words[1], NULL, 0);
    target.y = (int16_t) strtol(words[2], NULL, 0);
    struct timespec start, now;
    timespec_get(&start, TIME_UTC);
    gfxWarpCursor(disp, target.x, target.y);
    int received = receiveEventsAt(1, 5000, &target);

    timespec_get(&now, TIME_UTC);
    printf("gfxprobe: lag %d %d %s %ld ms\n", (int) target.x, (int) target.y,
      (received == 1) ? "arrived" : "lost",
      (((long) (now.tv_sec - start.tv_sec)) * 1000L)
      + ((now.tv_nsec - start.tv_nsec) / 1000000L));
    paintArgs->consumed = 3;
  } else if ((strcmp(step, "warp") == 0) && (numWords >= 3)) {
    report("warp", gfxWarpCursor(disp, (int16_t) strtol(words[1], NULL, 0),
      (int16_t) strtol(words[2], NULL, 0)));
    paintArgs->consumed = 3;
  }

  return args;
}
