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




/// @file Stock.c
///
/// @brief wintest's message and input commands, in an overlay of their own
/// to keep Window.h's stock dialog calls out of the others.

#include "Wintest.h"

/// @var _iconNames
///
/// @brief The name of each WinDialogIcon.
static const char *const _iconNames[] = {
  "none", "information", "warning", "error", "question",
};

/// @var _buttonNames
///
/// @brief The name of each WinDialogButtons.
static const char *const _buttonNames[] = {
  "ok", "okcancel", "yesno", "yesnocancel",
};

/// @fn static int nameOf(const char *word, const char *const *names,
///   int count)
///
/// @brief Find a word in a list of names.
///
/// @param word The word.
/// @param names The names.
/// @param count The number of names.
///
/// @return Returns the word's index, or -1 if it isn't one of them.
static int nameOf(const char *word, const char *const *names, int count) {
  for (int ii = 0; ii < count; ii++) {
    if (strcmp(word, names[ii]) == 0) {
      return ii;
    }
  }

  return -1;
}

/// @fn void* wintestStock(void *args)
///
/// @brief Carry out the dialog command at the context's index, and move the
/// index past it:  message OWNER ICON BUTTONS TITLE TEXT or input OWNER
/// TITLE PROMPT TEXT.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer, with handled cleared if the
/// command isn't one of these.
void* wintestStock(void *args) {
  Wintest *wintest = (Wintest*) args;
  WinSession *session = &wintest->session;
  char **argv = wintest->argv;
  int argc = wintest->argc;
  int ii = wintest->index;
  const char *command = argv[ii];
  int next = -1;

  if ((strcmp(command, "message") == 0) && ((ii + 5) < argc)) {
    int icon = nameOf(argv[ii + 2], _iconNames,
      (int) WINTEST_COUNT(_iconNames));
    int buttons = nameOf(argv[ii + 3], _buttonNames,
      (int) WINTEST_COUNT(_buttonNames));
    char *title = spaced(argv[ii + 4]);
    char *text = spaced(argv[ii + 5]);

    if ((icon < 0) || (buttons < 0) || (title == NULL) || (text == NULL)) {
      printResult(command, -EINVAL, NULL);
    } else {
      printResult(command, winMessage(session, windowWord(wintest,
        argv[ii + 1]), title, text, (uint8_t) icon, (uint8_t) buttons),
        NULL);
    }

    free(title);
    free(text);
    next = ii + 6;
  } else if ((strcmp(command, "input") == 0) && ((ii + 4) < argc)) {
    char *title = spaced(argv[ii + 2]);
    char *prompt = spaced(argv[ii + 3]);
    char *text = initialText(argv[ii + 4]);

    if ((title == NULL) || (prompt == NULL) || (text == NULL)) {
      printResult(command, -ENOMEM, NULL);
    } else {
      printResult(command, winInput(session, windowWord(wintest,
        argv[ii + 1]), title, prompt, text, WINTEST_PATH_SIZE), text);
    }

    free(title);
    free(prompt);
    free(text);
    next = ii + 5;
  }

  wintest->handled = (next >= 0);

  if (next >= 0) {
    wintest->index = next;
  }

  return args;
}
