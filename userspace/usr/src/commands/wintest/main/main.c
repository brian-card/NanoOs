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


/// @file main.c
///
/// @brief Test-fixture command for the e2e regression suite:  connects to
/// the window server, makes and changes windows as its arguments say, and
/// prints what it hears.  Not a general-purpose utility, like termtest.  The
/// Script overlay carries out wintest run's commands, and the Print overlay
/// prints what it hears.

#include "Wintest.h"

/// @fn static void printUsage(FILE *stream)
///
/// @brief Print the usage summary for wintest.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: wintest hello|wait|hold\n"
    "       wintest run COMMAND...|@FILE\n"
    "\n"
    "Connect to the window server.\n"
    "  hello  Print its greeting, then disconnect.\n"
    "  wait   Print its greeting, then wait until the window server goes\n"
    "         away.\n"
    "  hold   Wait until the window server goes away, printing nothing.\n"
    "  run    Carry out the commands, or the words of FILE, then print each\n"
    "         event until the window server goes away:\n"
    "    create X,Y,W,H TITLE [layer=L] [flags=F,...]  L is desktop, normal,\n"
    "      topmost or popup; F is plain, notask, fixed, modal, top, bottom,\n"
    "      left or right\n"
    "    title N TITLE   destroy N   state N normal|minimized|maximized\n"
    "    sleep MS   wait MS (print events for MS)   closes (destroy a window\n"
    "    asked to close)   exit\n"
    "    grid X,Y COLSxROWS [session|twice]   put N ROW,COL,ATTR TEXT\n"
    "    clear N TOP,LEFT,BOTTOM,RIGHT,ATTR   scroll N TOP,BOTTOM,COUNT,ATTR\n"
    "    cursor N ROW,COL,SHOWN   reports N 0|1   burst N COUNT   inject\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

/// @fn static char** readCommands(const char *path, char **text, int *argc)
///
/// @brief Make the arguments of wintest run from the words of a file, which
/// keeps a long list of commands within the console's line length.
///
/// @param path The file.
/// @param text Where to keep the file's contents, which the arguments point
///   into and the caller frees.
/// @param argc Where to store the number of arguments.
///
/// @return Returns the arguments, which the caller frees, or NULL on failure.
static char** readCommands(const char *path, char **text, int *argc) {
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    return NULL;
  }
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);
  *text = (char*) malloc((size_t) length + 1);
  // A word and the space after it take at least two bytes.
  char **argv = (char**) malloc(sizeof(char*)
    * (size_t) (WINTEST_SCRIPT_START + (length / 2) + 1));
  if ((*text == NULL) || (argv == NULL)
    || (fread(*text, 1, (size_t) length, file) != (size_t) length)
  ) {
    fclose(file);
    free(argv);
    return NULL;
  }
  fclose(file);
  (*text)[length] = '\0';
  int count = WINTEST_SCRIPT_START;
  char *word = *text;
  while (*(word += strspn(word, " \t\r\n")) != '\0') {
    argv[count++] = word;
    word += strcspn(word, " \t\r\n");
    if (*word != '\0') {
      *word++ = '\0';
    }
  }
  *argc = count;
  return argv;
}

/// @fn static const char* roleName(uint8_t role)
///
/// @brief Get the name wintest prints for a WinRole.
///
/// @param role The role.
///
/// @return Returns the name.
static const char* roleName(uint8_t role) {
  if (role == WIN_ROLE_DESKTOP) {
    return "desktop";
  } else if (role == WIN_ROLE_TASKBAR) {
    return "taskbar";
  }
  return "none";
}

int main(int argc, char **argv) {
  Wintest *wintest = NULL;
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }
  bool script = (argc > 1) && (strcmp(argv[1], "run") == 0);
  if (!script && ((argc != 2)
    || ((strcmp(argv[1], "hello") != 0) && (strcmp(argv[1], "wait") != 0)
      && (strcmp(argv[1], "hold") != 0)))
  ) {
    printUsage(stderr);
    return 1;
  }

  wintest = (Wintest*) calloc(1, sizeof(Wintest));
  if (wintest == NULL) {
    fputs("wintest: out of memory\n", stderr);
    return 1;
  }
  WinSession *session = &wintest->session;
  WinError status = winConnect(session);
  if (status == WIN_ERR_NOT_FOUND) {
    fputs("wintest: no window server\n", stderr);
    free(wintest);
    return 1;
  } else if (status != WIN_OK) {
    fprintf(stderr, "wintest: error %d\n", (int) status);
    free(wintest);
    return 1;
  }
  if (script) {
    char *text = NULL;
    char **words = NULL;
    wintest->argc = argc;
    wintest->argv = argv;
    if ((argc == 3) && (argv[2][0] == '@')) {
      words = readCommands(&argv[2][1], &text, &wintest->argc);
      if (words == NULL) {
        fprintf(stderr, "wintest: can't read %s\n", &argv[2][1]);
        free(text);
        free(wintest);
        return 1;
      }
      words[0] = argv[0];
      words[1] = argv[1];
      wintest->argv = words;
    }
    callOverlayFunction(OVERLAY_SAME_NAMESPACE, "Script", "wintestRun",
      wintest);
    int exitStatus = wintest->rc;
    free(words);
    free(text);
    free(wintest);
    return exitStatus;
  }

  bool quiet = (strcmp(argv[1], "hold") == 0);
  const WinHelloArgs *hello = &session->hello;
  if (!quiet) {
    printf("wintest: screen %ux%u work %d,%d,%d,%d cell %ux%u role %s\n",
      (unsigned int) hello->screenWidth, (unsigned int) hello->screenHeight,
      hello->workArea.x, hello->workArea.y, hello->workArea.w,
      hello->workArea.h, (unsigned int) hello->cellWidth,
      (unsigned int) hello->cellHeight, roleName(hello->role));
  }
  if (strcmp(argv[1], "hello") == 0) {
    winDisconnect(session);
    free(wintest);
    return 0;
  }

  WinEvent event;
  while (winNextEvent(session, &event, NULL) == 0) {
    if (event.type == WIN_EVENT_DISCONNECTED) {
      if (!quiet) {
        puts("wintest: disconnected");
      }
      break;
    }
  }
  free(wintest);
  return 0;
}
