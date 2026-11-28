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




/// @file Files.c
///
/// @brief wintest's open and save commands, and the file dialog a dialog's
/// browse button opens, in an overlay of their own to keep WindowFiles.h
/// out of the others.

#include "Wintest.h"
#include "../../../src/kernel/WindowFiles.h"

/// @def WINTEST_OPTION_SUFFIX
///
/// @brief The open and save option that names the only suffix shown.
#define WINTEST_OPTION_SUFFIX "suffix="

/// @fn void* wintestBrowse(void *args)
///
/// @brief Open a file dialog over the dialog that sent the context's event,
/// for its browse button, and print the path chosen.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer.
void* wintestBrowse(void *args) {
  Wintest *wintest = (Wintest*) args;
  char *path = (char*) calloc(1, WINTEST_PATH_SIZE);

  if (path != NULL) {
    printResult("browse", winOpenFile(&wintest->session,
      wintest->event.window, "Browse", path, WINTEST_PATH_SIZE, NULL), path);
    free(path);
  }

  return args;
}

/// @fn void* wintestFiles(void *args)
///
/// @brief Carry out the file dialog command at the context's index, and
/// move the index past it:  open or save OWNER PATH [suffix=S].
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer, with handled cleared if the
/// command isn't one of these.
void* wintestFiles(void *args) {
  Wintest *wintest = (Wintest*) args;
  WinSession *session = &wintest->session;
  char **argv = wintest->argv;
  int argc = wintest->argc;
  int ii = wintest->index;
  const char *command = argv[ii];
  int next = -1;

  if (((strcmp(command, "open") == 0) || (strcmp(command, "save") == 0))
    && ((ii + 2) < argc)
  ) {
    const char *suffix = NULL;

    next = ii + 3;

    if ((next < argc) && (strncmp(argv[next], WINTEST_OPTION_SUFFIX,
      WINTEST_LITERAL_LENGTH(WINTEST_OPTION_SUFFIX)) == 0)
    ) {
      suffix = &argv[next][WINTEST_LITERAL_LENGTH(WINTEST_OPTION_SUFFIX)];
      next++;
    }

    char *path = initialText(argv[ii + 2]);
    uint8_t owner = windowWord(wintest, argv[ii + 1]);

    if (path == NULL) {
      printResult(command, -ENOMEM, NULL);
    } else if (command[0] == 'o') {
      printResult(command, winOpenFile(session, owner, "Open", path,
        WINTEST_PATH_SIZE, suffix), path);
    } else {
      printResult(command, winSaveFile(session, owner, "Save", path,
        WINTEST_PATH_SIZE, suffix), path);
    }

    free(path);
  }

  wintest->handled = (next >= 0);

  if (next >= 0) {
    wintest->index = next;
  }

  return args;
}
