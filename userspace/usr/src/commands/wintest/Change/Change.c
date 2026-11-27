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




/// @file Change.c
///
/// @brief wintest's commands that make a window of a template and add one to
/// a window.

#include "Wintest.h"

/// @def WINTEST_SCRIBBLE
///
/// @brief The byte a template's buffer is overwritten with once the window
/// server has it, so that a server that kept a pointer into it would show
/// garbage.
#define WINTEST_SCRIBBLE 0xA5

/// @fn static void reset(Wintest *wintest)
///
/// @brief Overwrite the template's buffer, as an application may once the
/// window server has its widgets, and start an empty one.
///
/// @param wintest The context.
///
/// @return This function returns no value.
static void reset(Wintest *wintest) {
  memset(wintest->widgets.buffer, WINTEST_SCRIBBLE, WINTEST_TEMPLATE_SIZE);
  wintest->widgets.size = 0;
}

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

/// @fn void* wintestChange(void *args)
///
/// @brief Carry out the widget command at the context's index, and move the
/// index past it:  window X,Y,W,H TITLE, which makes a window of the
/// template so far and then overwrites the template, or add N, which adds
/// the template to a window and then overwrites it.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer, with handled cleared if the
/// command isn't one of these.
void* wintestChange(void *args) {
  Wintest *wintest = (Wintest*) args;
  char **argv = wintest->argv;
  int ii = wintest->index;
  const char *command = argv[ii];
  int next = -1;

  if ((strcmp(command, "window") == 0)
    && ((ii + 2) < wintest->argc)
  ) {
    GfxRect frame;
    WinTemplate *widgets = pending(wintest);
    char *title = spaced(argv[ii + 2]);
    int window = -EINVAL;

    if ((widgets == NULL) || (title == NULL)) {
      window = -ENOMEM;
    } else if (parseRect(argv[ii + 1], &frame)) {
      window = winCreateWindowWith(&wintest->session, title, frame,
        WIN_LAYER_NORMAL, WIN_FLAG_DECORATED | WIN_FLAG_RESIZABLE
        | WIN_FLAG_IN_TASKBAR, widgets->buffer, widgets->size);
      reset(wintest);
    }

    free(title);

    if (window < 0) {
      report(command, argv[ii + 1], window);
    } else {
      printf("wintest: window %d\n", window);
      wintest->last = (uint8_t) window;
    }

    next = ii + 3;
  } else if ((next < 0) && (strcmp(command, "add") == 0)
    && ((ii + 1) < wintest->argc)
  ) {
    WinTemplate *widgets = pending(wintest);
    int rc = -ENOMEM;

    if (widgets != NULL) {
      rc = winAddWidgets(&wintest->session,
        (uint8_t) strtol(argv[ii + 1], NULL, 0), widgets);
      reset(wintest);
    }

    report(command, argv[ii + 1], rc);
    next = ii + 2;
  }

  wintest->handled = (next >= 0);

  if (next < 0) {
    return args;
  }

  wintest->index = next;

  return args;
}
