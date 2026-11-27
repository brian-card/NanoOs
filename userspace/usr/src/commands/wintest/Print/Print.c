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

#include "ConsoleOutput.h"
#include "Wintest.h"
#include "../../../src/kernel/Console.h"

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
  static const uint8_t modifierBits[] = {
    GFX_MOD_CTRL, GFX_MOD_SHIFT, GFX_MOD_ALT, GFX_MOD_ALT_GR,
  };
  char name[sizeof("0xff")];
  if ((event->key > ' ') && (event->key < ASCII_DELETE)) {
    name[0] = (char) event->key;
    name[1] = '\0';
  } else {
    snprintf(name, sizeof(name), "0x%02x", (unsigned int) event->key);
  }
  printf("KEY %s", name);
  for (size_t ii = 0; ii < WINTEST_COUNT(modifiers); ii++) {
    if ((event->modifiers & modifierBits[ii]) != 0) {
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
  } else if ((event->type == WIN_EVENT_RESIZED) && (event->widget != 0)) {
    printf("RESIZED %u %dx%d grid %ux%u\n", window, event->x, event->y,
      (unsigned int) event->columns, (unsigned int) event->rows);
  } else if (event->type == WIN_EVENT_RESIZED) {
    printf("RESIZED %u %dx%d\n", window, event->x, event->y);
  } else if (event->type == WIN_EVENT_SESSION_ENDED) {
    printf("SESSION_ENDED %u\n", window);
  } else if (event->type == WIN_EVENT_COMMAND) {
    printf("COMMAND %u %u %u\n", window, event->widget, event->value);
  } else if (event->type == WIN_EVENT_CHANGED) {
    printf("CHANGED %u %u %u\n", window, event->widget, event->value);
  } else if (event->type == WIN_EVENT_POINTER) {
    printf("POINTER %u %u %d,%d %u\n", window, event->widget, event->x,
      event->y, (unsigned int) event->buttons);
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

/// @fn static void printOutput(const char *text)
///
/// @brief Print a session's output as one line, "wintest: output " and the
/// text with a newline shown as \n, a carriage return as \r and escape as
/// ^[.
///
/// @param text The text, or NULL for a message that isn't output.
///
/// @return This function returns no value.
static void printOutput(const char *text) {
  if (text == NULL) {
    return;
  }
  fputs("wintest: output ", stdout);
  for (; *text != '\0'; text++) {
    if (*text == '\n') {
      fputs("\\n", stdout);
    } else if (*text == '\r') {
      fputs("\\r", stdout);
    } else if (*text == '\033') {
      fputs("^[", stdout);
    } else {
      fwrite(text, 1, 1, stdout);
    }
  }
  fputs("\n", stdout);
}

/// @fn void* wintestEvents(void *args)
///
/// @brief Print each event until the window server goes away, or for the
/// context's waitMs, destroying a window asked to close if closes was
/// given.  Console output for a session
/// whose grid is wintest's is printed, then finished as a display finishes
/// it, its buffer given back.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer.
void* wintestEvents(void *args) {
  Wintest *wintest = (Wintest*) args;
  WinSession *session = &wintest->session;
  ProcessId consolePid = 0;
  findSystemProcess(CONSOLE_PROCESS_NAME, &consolePid);
  struct timespec deadline;
  struct timespec *until = NULL;
  if (wintest->waitMs > 0) {
    timespec_get(&deadline, TIME_UTC);
    deadline.tv_sec += wintest->waitMs / WINTEST_MS_PER_SECOND;
    deadline.tv_nsec
      += (wintest->waitMs % WINTEST_MS_PER_SECOND) * WINTEST_NS_PER_MS;
    if (deadline.tv_nsec >= WINTEST_NS_PER_SECOND) {
      deadline.tv_sec++;
      deadline.tv_nsec -= WINTEST_NS_PER_SECOND;
    }
    until = &deadline;
  }
  wintest->more = true;
  while (wintest->more) {
    if ((session->nextEvent < session->numEvents) || !session->connected) {
      if (winNextEvent(session, &wintest->event, until) != 0) {
        break;
      }
      wintest->more = printEvent(&wintest->event);
      if (wintest->closes
        && (wintest->event.type == WIN_EVENT_CLOSE_REQUESTED)
      ) {
        winDestroyWindow(session, wintest->event.window);
      }
      continue;
    }
    ProcessMessage *message = processMessageQueueWait(until);
    if ((message == NULL) && (until != NULL)) {
      break;
    } else if ((message != NULL) && !winHandleMessage(session, message)) {
      printOutput(consoleOutputText(message));
      consoleOutputDoneTo(message, consolePid);
    }
  }
  return args;
}
