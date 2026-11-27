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




/// @file Query.c
///
/// @brief wintest's commands that set, get and remove a window's widgets.

#include "Wintest.h"

/// @def WINTEST_TEXT_SIZE
///
/// @brief The size of the buffer get reads a widget's text into, its
/// terminator included.
#define WINTEST_TEXT_SIZE 256

/// @fn static void report(const char *command, const char *window, int rc)
///
/// @brief Print the result of a widget command:  "ok", or its error.
///
/// @param command The command.
/// @param window The window it named.
/// @param rc 0, or a negative errno value.
///
/// @return This function returns no value.
static void report(const char *command, const char *window, int rc) {
  printf("wintest: %s %s: %s\n", command, window,
    (rc < 0) ? strerror(-rc) : "ok");
}

/// @fn static int property(const char *name)
///
/// @brief Get the WinProperty a set or get names.
///
/// @param name text, value, items, enabled, hidden, focus, rect or
///   menuitem.
///
/// @return Returns the WinProperty, or -1 if the name isn't one of these.
static int property(const char *name) {
  static const char *const names[] = {
    "text", "value", "items", "enabled", "hidden", "focus", "rect",
    "menuitem",
  };
  for (size_t ii = 0; ii < WINTEST_COUNT(names); ii++) {
    if (strcmp(name, names[ii]) == 0) {
      return (int) ii;
    }
  }

  return -1;
}

/// @fn static int change(Wintest *wintest, int ii)
///
/// @brief Carry out set N ID PROPERTY VALUE, get N ID PROPERTY or
/// remove N ID.
///
/// @param wintest The context.
/// @param ii The index of the command's word.
///
/// @return Returns the index of the word after the command, or -1 if it
/// isn't one of these or is malformed.
static int change(Wintest *wintest, int ii) {
  char **argv = wintest->argv;
  WinSession *session = &wintest->session;
  const char *command = argv[ii];
  bool set = (strcmp(command, "set") == 0);
  bool get = (strcmp(command, "get") == 0);
  bool remove = (strcmp(command, "remove") == 0);
  int words = 2;

  if (set) {
    words = 4;
  } else if (get) {
    words = 3;
  }
  if (((!set) && (!get) && (!remove)) || ((ii + words) >= wintest->argc)) {
    return -1;
  }

  uint8_t window = (uint8_t) strtol(argv[ii + 1], NULL, 0);
  uint8_t widget = (uint8_t) strtol(argv[ii + 2], NULL, 0);

  if (remove) {
    winRemoveWidget(session, window, widget);
    return ii + words + 1;
  }

  int which = property(argv[ii + 3]);
  if (which < 0) {
    return -1;
  }
  if (get) {
    char text[WINTEST_TEXT_SIZE];
    int rc = (which == WIN_PROP_TEXT)
      ? winGetText(session, window, widget, text, sizeof(text))
      : winGetValue(session, window, widget);

    if (rc < 0) {
      report(command, argv[ii + 1], rc);
    } else if (which == WIN_PROP_TEXT) {
      printf("wintest: get %d %d text %s\n", window, widget, text);
    } else {
      printf("wintest: get %d %d value %d\n", window, widget, rc);
    }

    return ii + words + 1;
  }

  const char *value = argv[ii + 4];
  int rc = 0;
  GfxRect rect;

  if (which == WIN_PROP_TEXT) {
    char *text = spaced(value);
    rc = (text == NULL) ? -ENOMEM : winSetText(session, window, widget, text);
    free(text);
  } else if (which == WIN_PROP_ITEMS) {
    uint16_t size = 0;
    char *items = listItems(value, &size);

    rc = (items == NULL) ? -ENOMEM
      : winSetItems(session, window, widget, items, size);
    free(items);
  } else if (which == WIN_PROP_RECT) {
    rc = parseRect(value, &rect) ? winSetRect(session, window, widget, rect)
      : -EINVAL;
  } else {
    rc = winSetWidget_(session, window, widget, (uint8_t) which,
      (uint16_t) strtol(value, NULL, 0), NULL, 0, NULL);
  }

  report(command, argv[ii + 1], rc);

  return ii + words + 1;
}

/// @fn void* wintestQuery(void *args)
///
/// @brief Carry out set N ID PROPERTY VALUE, get N ID PROPERTY or
/// remove N ID at the context's index, and move the index past it.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer, with handled cleared if the
/// command isn't one of these.
void* wintestQuery(void *args) {
  Wintest *wintest = (Wintest*) args;
  int next = change(wintest, wintest->index);

  wintest->handled = (next >= 0);
  if (wintest->handled) {
    wintest->index = next;
  }

  return args;
}
