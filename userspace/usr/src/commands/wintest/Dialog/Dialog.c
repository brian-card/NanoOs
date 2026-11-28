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




/// @file Dialog.c
///
/// @brief wintest's dialog command, which runs an application's own dialog,
/// and its drop command, which drops a directory capability.

#include <sys/dircap.h>
#include "Wintest.h"

/// @def WINTEST_OPTION_BROWSE
///
/// @brief The dialog option that names the button that opens a file dialog
/// over the dialog.
#define WINTEST_OPTION_BROWSE "browse="

/// @fn static void dialogEvent(WinSession *session, const WinEvent *event,
///   void *context)
///
/// @brief Print an event of an application's dialog that doesn't end it,
/// and open a file dialog over it for its browse button.
///
/// @param session The connection.
/// @param event The event.
/// @param context A pointer to the Wintest.
///
/// @return This function returns no value.
static void dialogEvent(WinSession *session, const WinEvent *event,
  void *context
) {
  Wintest *wintest = (Wintest*) context;

  wintest->event = *event;
  wintest->command = NULL;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Print", "wintestPrint",
    wintest);

  (void) session;

  if ((event->type == WIN_EVENT_COMMAND) && (wintest->browse != 0)
    && (event->widget == wintest->browse)
  ) {
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Files", "wintestBrowse",
      wintest);
  }
}

/// @fn void* wintestDialog(void *args)
///
/// @brief Carry out the command at the context's index, and move the index
/// past it:  dialog OWNER X,Y,W,H TITLE [browse=ID], which runs a modal
/// window of the template so far until a default, cancel or ends button is
/// pressed, or drop DIRECTORY.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer, with handled cleared if the
/// command isn't one of these.
void* wintestDialog(void *args) {
  Wintest *wintest = (Wintest*) args;
  WinSession *session = &wintest->session;
  char **argv = wintest->argv;
  int argc = wintest->argc;
  int ii = wintest->index;
  const char *command = argv[ii];
  int next = -1;

  if ((strcmp(command, "dialog") == 0) && ((ii + 3) < argc)) {
    GfxRect frame;
    WinTemplate *widgets = pending(wintest);
    char *title = spaced(argv[ii + 3]);
    int window = -EINVAL;

    next = ii + 4;
    wintest->browse = 0;

    if ((next < argc) && (strncmp(argv[next], WINTEST_OPTION_BROWSE,
      WINTEST_LITERAL_LENGTH(WINTEST_OPTION_BROWSE)) == 0)
    ) {
      wintest->browse = (uint8_t) strtol(
        &argv[next][WINTEST_LITERAL_LENGTH(WINTEST_OPTION_BROWSE)], NULL, 0);
      next++;
    }

    if ((widgets == NULL) || (title == NULL)) {
      window = -ENOMEM;
    } else if (parseRect(argv[ii + 2], &frame)) {
      window = winCreateOwnedWindow(session, windowWord(wintest,
        argv[ii + 1]), title, frame, WIN_LAYER_NORMAL,
        WIN_FLAG_DECORATED | WIN_FLAG_MODAL, widgets->buffer, widgets->size);
      widgets->size = 0;
    }

    free(title);

    if (window < 0) {
      printResult(command, window, NULL);
    } else {
      printf("wintest: window %d\n", window);
      wintest->last = (uint8_t) window;
      int ended = winRunDialog(session, (uint8_t) window, dialogEvent,
        wintest);
      printf("wintest: dialog %d: %d\n", window, ended);
    }
  } else if ((strcmp(command, "drop") == 0) && ((ii + 1) < argc)) {
    int rc = dircapDrop(argv[ii + 1]);
    printf("wintest: drop %s: %s\n", argv[ii + 1],
      (rc < 0) ? strerror(errno) : "ok");
    next = ii + 2;
  }

  wintest->handled = (next >= 0);

  if (next >= 0) {
    wintest->index = next;
  }

  return args;
}
