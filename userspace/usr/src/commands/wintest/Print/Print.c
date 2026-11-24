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


/// @file Print.c
///
/// @brief What wintest prints:  each event, one line each, and the commands
/// that failed.  Entered once per line.

#include "Wintest.h"

/// @var _stateNames
///
/// @brief The name of each WinState, in order.
static const char *const _stateNames[] = {
  "normal", "minimized", "maximized",
};

/// @fn static void printKey(const WinEvent *event)
///
/// @brief Print a WIN_EVENT_KEY:  KEY, the key as its character or its
/// number, the modifiers held, and "up" for a release.
///
/// @param event The event.
///
/// @return This function returns no value.
static void printKey(const WinEvent *event) {
  static const char *const modifiers[] = {
    " ctrl", " shift", " alt", " altgr",
  };
  char name[8];
  if ((event->key > ' ') && (event->key < 0x7f)) {
    name[0] = (char) event->key;
    name[1] = '\0';
  } else {
    snprintf(name, sizeof(name), "0x%02x", (unsigned int) event->key);
  }
  printf("KEY %s", name);
  for (int ii = 0; ii < 4; ii++) {
    if ((event->modifiers & (1 << ii)) != 0) {
      fputs(modifiers[ii], stdout);
    }
  }
  puts((event->detail != 0) ? "" : " up");
}

/// @fn static bool printEvent(const WinEvent *event)
///
/// @brief Print an event, one line each.
///
/// @param event The event.
///
/// @return Returns false once the window server has gone away, true
/// otherwise.
static bool printEvent(const WinEvent *event) {
  unsigned int window = event->window;
  if (event->type == WIN_EVENT_DISCONNECTED) {
    puts("wintest: disconnected");
    return false;
  } else if (event->type == WIN_EVENT_CLOSE_REQUESTED) {
    printf("CLOSE_REQUESTED %u\n", window);
  } else if (event->type == WIN_EVENT_FOCUS) {
    printf("FOCUS %u %s\n", window,
      (event->detail != 0) ? "activated" : "deactivated");
  } else if (event->type == WIN_EVENT_STATE) {
    printf("STATE %u %s\n", window,
      _stateNames[(event->detail <= WIN_STATE_MAXIMIZED) ? event->detail : 0]);
  } else if (event->type == WIN_EVENT_RESIZED) {
    printf("RESIZED %u %dx%d\n", window, event->x, event->y);
  } else if (event->type == WIN_EVENT_KEY) {
    printKey(event);
  } else if (event->type == WIN_EVENT_OVERFLOW) {
    puts("OVERFLOW");
  } else {
    printf("EVENT %u %u\n", (unsigned int) event->type, window);
  }
  return true;
}

/// @fn static void report(const char *command, const char *window, int rc)
///
/// @brief Print a command's failure.
///
/// @param command The command.
/// @param window The window it named, or NULL.
/// @param rc The negative errno value it returned.
///
/// @return This function returns no value.
static void report(const char *command, const char *window, int rc) {
  printf("wintest: %s%s%s: %s\n", command, (window != NULL) ? " " : "",
    (window != NULL) ? window : "", strerror(-rc));
}

/// @fn void* wintestPrint(void *args)
///
/// @brief Print the context's error, or its event if it has no command.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer.
void* wintestPrint(void *args) {
  Wintest *wintest = (Wintest*) args;
  if (wintest->command != NULL) {
    report(wintest->command, wintest->window, wintest->rc);
  } else {
    wintest->more = printEvent(&wintest->event);
  }
  return args;
}
