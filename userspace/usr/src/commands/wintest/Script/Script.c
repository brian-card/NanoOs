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


/// @file Script.c
///
/// @brief wintest run:  carrying out the commands, then reading events until
/// the window server goes away.  Entered once.

#include "Wintest.h"

/// @var _stateNames
///
/// @brief The name of each WinState, in order.
static const char *const _stateNames[] = {
  "normal", "minimized", "maximized",
};

/// @var _layerNames
///
/// @brief The name of each WinLayer, in order.
static const char *const _layerNames[] = {
  "desktop", "normal", "topmost", "popup",
};

/// @def WINTEST_OPTION_LAYER
///
/// @brief The start of create's option that picks the window's layer.
#define WINTEST_OPTION_LAYER "layer="

/// @def WINTEST_OPTION_FLAGS
///
/// @brief The start of create's option that lists the window's flags.
#define WINTEST_OPTION_FLAGS "flags="

/// @struct WintestCommands
///
/// @brief An overlay that carries out some of wintest's commands, and the
/// function that does.
///
/// @param overlay The overlay's name.
/// @param function The function's name.
typedef struct WintestCommands {
  const char *overlay;
  const char *function;
} WintestCommands;

/// @var _commands
///
/// @brief The overlays a command that isn't the script's own is offered to,
/// in turn, each from here, so that only one of them is on the stack at a
/// time.
static const WintestCommands _commands[] = {
  { "Grid", "wintestGrid" },
  { "Write", "wintestWrite" },
  { "Widget", "wintestWidget" },
  { "Draw", "wintestDraw" },
  { "Change", "wintestChange" },
  { "Query", "wintestQuery" },
};

/// @fn static int nameIndex(const char *name, const char *const *names,
///   int count)
///
/// @brief Find a name in a list.
///
/// @param name The name.
/// @param names The list.
/// @param count The number of names in the list.
///
/// @return Returns the name's index, or -1 if it isn't in the list.
static int nameIndex(const char *name, const char *const *names, int count) {
  for (int ii = 0; ii < count; ii++) {
    if (strcmp(name, names[ii]) == 0) {
      return ii;
    }
  }

  return -1;
}

/// @fn static bool parseFrame(const char *text, GfxRect *frame)
///
/// @brief Parse X,Y,W,H.
///
/// @param text The text.
/// @param frame Where to store the rectangle.
///
/// @return Returns true if the text is four numbers, false otherwise.
static bool parseFrame(const char *text, GfxRect *frame) {
  int16_t values[4];
  int last = WINTEST_COUNT(values) - 1;

  for (int ii = 0; ii <= last; ii++) {
    char *end = NULL;
    values[ii] = (int16_t) strtol(text, &end, 10);

    if ((end == text) || ((ii < last) && (*end != ','))
      || ((ii == last) && *end)
    ) {
      return false;
    }

    text = end + 1;
  }

  frame->x = values[0];
  frame->y = values[1];
  frame->w = values[2];
  frame->h = values[3];

  return true;
}

/// @fn static int parseFlags(const char *text)
///
/// @brief Turn a list of flag names into WIN_FLAG_* bits.  A window is
/// decorated, resizable and in the taskbar unless the list says otherwise.
///
/// @param text The names, separated by commas, or NULL.
///
/// @return Returns the bits, or -1 if a name isn't known.
static int parseFlags(const char *text) {
  static const char *const names[] = {
    "plain", "notask", "fixed", "modal", "top", "bottom", "left", "right",
  };
  static const uint8_t bits[] = {
    WIN_FLAG_DECORATED, WIN_FLAG_IN_TASKBAR, WIN_FLAG_RESIZABLE,
    WIN_FLAG_MODAL, WIN_FLAG_RESERVE_TOP, WIN_FLAG_RESERVE_BOTTOM,
    WIN_FLAG_RESERVE_LEFT, WIN_FLAG_RESERVE_RIGHT,
  };
  int flags = WIN_FLAG_DECORATED | WIN_FLAG_IN_TASKBAR | WIN_FLAG_RESIZABLE;

  while ((text != NULL) && (*text != '\0')) {
    const char *comma = strchr(text, ',');
    size_t length = (comma != NULL) ? (size_t) (comma - text) : strlen(text);
    int index = -1;

    for (int ii = 0; ii < ((int) WINTEST_COUNT(names)); ii++) {
      if ((strlen(names[ii]) == length)
        && (strncmp(text, names[ii], length) == 0)
      ) {
        index = ii;
      }
    }

    if (index < 0) {
      return -1;
    }

    flags ^= bits[index];
    text = (comma != NULL) ? (comma + 1) : (text + length);
  }

  return flags;
}

/// @fn static void print(Wintest *wintest, const char *command,
///   const char *window, int rc)
///
/// @brief Have the Print overlay print an error, or the event in the
/// context if command is NULL.
///
/// @param wintest The context.
/// @param command The command that failed, or NULL.
/// @param window The window it named, or NULL.
/// @param rc The negative errno value it returned.
///
/// @return This function returns no value.
static void print(Wintest *wintest, const char *command, const char *window,
  int rc
) {
  wintest->command = command;
  wintest->window = window;
  wintest->rc = rc;
  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Print", "wintestPrint",
    wintest);
}

/// @fn static int run(Wintest *wintest)
///
/// @brief Carry out wintest run's commands, then print events until the
/// window server goes away.
///
/// @param wintest The context, whose arguments have the commands from
///   argv[2].
///
/// @return Returns the exit status.
static int run(Wintest *wintest) {
  WinSession *session = &wintest->session;
  int argc = wintest->argc;
  char **argv = wintest->argv;
  int ii = WINTEST_SCRIPT_START;

  while (ii < argc) {
    const char *command = argv[ii++];
    const char *first = (ii < argc) ? argv[ii] : NULL;

    if ((strcmp(command, "create") == 0) && ((ii + 1) < argc)) {
      GfxRect frame;
      if (!parseFrame(argv[ii], &frame)) {
        fputs("wintest: bad command\n", stderr);
        return 1;
      }

      const char *title = argv[ii + 1];

      ii += 2;
      int layer = WIN_LAYER_NORMAL;
      const char *flags = NULL;

      while ((ii < argc) && (strchr(argv[ii], '=') != NULL)) {
        if (strncmp(argv[ii], WINTEST_OPTION_LAYER,
          WINTEST_LITERAL_LENGTH(WINTEST_OPTION_LAYER)) == 0
        ) {
          layer = nameIndex(
            &argv[ii][WINTEST_LITERAL_LENGTH(WINTEST_OPTION_LAYER)],
            _layerNames, WINTEST_COUNT(_layerNames));
        } else if (strncmp(argv[ii], WINTEST_OPTION_FLAGS,
          WINTEST_LITERAL_LENGTH(WINTEST_OPTION_FLAGS)) == 0
        ) {
          flags = &argv[ii][WINTEST_LITERAL_LENGTH(WINTEST_OPTION_FLAGS)];
        }

        ii++;
      }

      int bits = parseFlags(flags);
      if ((layer < 0) || (bits < 0)) {
        fputs("wintest: bad command\n", stderr);
        return 1;
      }

      int window = winCreateWindow(session, title, frame, (uint8_t) layer,
        (uint8_t) bits);
      if (window < 0) {
        print(wintest, command, NULL, window);
      } else {
        printf("wintest: window %d\n", window);
        wintest->last = (uint8_t) window;
      }
    } else if ((strcmp(command, "title") == 0) && ((ii + 1) < argc)) {
      int rc = winSetTitle(session, (uint8_t) strtol(first, NULL, 10),
        argv[ii + 1]);
      if (rc < 0) {
        print(wintest, command, first, rc);
      } else {
        printf("wintest: titled %s\n", first);
      }

      ii += 2;
    } else if ((strcmp(command, "state") == 0) && ((ii + 1) < argc)) {
      int state = nameIndex(argv[ii + 1], _stateNames,
        WINTEST_COUNT(_stateNames));
      if (state < 0) {
        fputs("wintest: bad command\n", stderr);
        return 1;
      }

      int rc = winSetState(session, (uint8_t) strtol(first, NULL, 10),
        (uint8_t) state);
      if (rc < 0) {
        print(wintest, command, first, rc);
      }

      ii += 2;
    } else if ((strcmp(command, "destroy") == 0) && (first != NULL)) {
      winDestroyWindow(session, (uint8_t) strtol(first, NULL, 10));
      ii++;
    } else if ((strcmp(command, "sleep") == 0) && (first != NULL)) {
      struct timespec delay = { 0, 0 };
      long ms = strtol(first, NULL, 10);

      delay.tv_sec = ms / WINTEST_MS_PER_SECOND;
      delay.tv_nsec = (ms % WINTEST_MS_PER_SECOND) * WINTEST_NS_PER_MS;
      nanosleep(&delay, NULL);
      ii++;
    } else if ((strcmp(command, "wait") == 0) && (first != NULL)) {
      wintest->waitMs = strtol(first, NULL, 10);
      callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Print", "wintestEvents",
        wintest);
      wintest->waitMs = 0;
      ii++;
    } else if (strcmp(command, "closes") == 0) {
      wintest->closes = true;
    } else if (strcmp(command, "exit") == 0) {
      return 0;
    } else {
      wintest->index = ii - 1;
      wintest->handled = false;
      for (size_t jj = 0;
        (!wintest->handled) && (jj < WINTEST_COUNT(_commands));
        jj++
      ) {
        callOverlayFunction(OVERLAY_SAME_NAMESPACE, _commands[jj].overlay,
          _commands[jj].function, wintest);
      }

      if (!wintest->handled) {
        fputs("wintest: bad command\n", stderr);
        return 1;
      }

      ii = wintest->index;
    }
  }

  callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Print", "wintestEvents",
    wintest);

  return 0;
}

/// @fn void* wintestRun(void *args)
///
/// @brief Carry out wintest run.  The exit status goes in the context's rc.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer.
void* wintestRun(void *args) {
  Wintest *wintest = (Wintest*) args;
  wintest->rc = run(wintest);

  return args;
}
