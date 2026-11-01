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
/// @brief Test-fixture command for the e2e regression suite:  exercises
/// tcgetwinsize and in-band resize reports.  Not a general-purpose utility,
/// like dirtest, looseLoop, and tightLoop.

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "NanoOsUtils.h"

/// @fn static void printUsage(FILE *stream)
///
/// @brief Print the usage summary for termtest.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: termtest size|report|leave|erase [row column]\n"
    "\n"
    "Exercise the terminal size functions.\n"
    "  size    Print the terminal's rows and columns.\n"
    "  report  Turn on resize reports in raw mode, print the next input read\n"
    "          with escapes shown as ^[, then restore the terminal.\n"
    "  leave   Turn on resize reports and exit without turning them off.\n"
    "  erase   Write abc at a row and column (3 and 5 if not given), erase\n"
    "          to the end of the line, then wait for a line of input.\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

/// @fn static int printSize(void)
///
/// @brief Print the terminal's rows and columns.
///
/// @return Returns 0 on success, 1 on failure.
static int printSize(void) {
  struct winsize winsize;
  if (tcgetwinsize(STDIN_FILENO, &winsize) != 0) {
    fputs("termtest: tcgetwinsize failed\n", stderr);
    return 1;
  }
  printf("%u %u\n", (unsigned) winsize.ws_row, (unsigned) winsize.ws_col);
  return 0;
}

/// @fn static int printReport(bool restore)
///
/// @brief Turn on resize reports in raw mode, then either print the next
/// input read and restore the terminal or exit with the reports still on.
///
/// @param restore Whether to read input and restore the terminal.
///
/// @return Returns 0 on success, 1 on failure.
static int printReport(bool restore) {
  struct termios saved;
  if (tcgetattr(STDIN_FILENO, &saved) != 0) {
    fputs("termtest: tcgetattr failed\n", stderr);
    return 1;
  }
  struct termios raw = saved;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  raw.c_lflag |= RESIZEREPORTS;
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  if (!restore) {
    return 0;
  }

  char input[32];
  size_t length = fread(input, 1, sizeof(input), stdin);
  tcsetattr(STDIN_FILENO, TCSANOW, &saved);
  for (size_t ii = 0; ii < length; ii++) {
    if (input[ii] == '\033') {
      fputs("^[", stdout);
    } else {
      fwrite(&input[ii], 1, 1, stdout);
    }
  }
  fputs("\n", stdout);
  return 0;
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  } else if ((argc == 2) && (strcmp(argv[1], "size") == 0)) {
    return printSize();
  } else if ((argc == 2) && (strcmp(argv[1], "report") == 0)) {
    return printReport(true);
  } else if ((argc == 2) && (strcmp(argv[1], "leave") == 0)) {
    return printReport(false);
  } else if (((argc == 2) || (argc == 4)) && (strcmp(argv[1], "erase") == 0)) {
    printf("\033[%s;%sHabc\033[K",
      (argc == 4) ? argv[2] : "3", (argc == 4) ? argv[3] : "5");
    char line[8];
    fgets(line, sizeof(line), stdin);
    return 0;
  }

  printUsage(stderr);
  return 1;
}
