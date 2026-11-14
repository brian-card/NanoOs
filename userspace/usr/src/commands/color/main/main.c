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
/// @brief Implementation of a `color` command in the style of the MS-DOS one.

#include <stdio.h>
#include "NanoOsConsole.h"
#include "NanoOsUtils.h"

CGA_TO_ANSI_MAP

/// @brief Print the usage summary for color.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: color attr\n"
    "\n"
    "Set the console's background and foreground colors and clear the\n"
    "screen.  attr is a hexadecimal byte whose upper digit is the background\n"
    "and whose lower digit is the foreground, each one of:\n"
    "\n"
    "  0 = Black       8 = Dark Gray\n"
    "  1 = Blue        9 = Light Blue\n"
    "  2 = Green       A = Light Green\n"
    "  3 = Cyan        B = Light Cyan\n"
    "  4 = Red         C = Light Red\n"
    "  5 = Magenta     D = Light Magenta\n"
    "  6 = Brown       E = Yellow\n"
    "  7 = Light Gray  F = White\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

/// @brief Parse a color attribute of one or two hexadecimal digits.
///
/// @param attr The attribute to parse.
///
/// @return Returns the attribute's value on success, -1 if it isn't one or
/// two hexadecimal digits.
static int parseAttr(const char *attr) {
  int value = 0;
  int ii = 0;
  for (; attr[ii] != '\0'; ii++) {
    char c = attr[ii];
    int digit = 0;
    if (ii == 2) {
      return -1;
    } else if ((c >= '0') && (c <= '9')) {
      digit = c - '0';
    } else if ((c >= 'a') && (c <= 'f')) {
      digit = c - 'a' + 10;
    } else if ((c >= 'A') && (c <= 'F')) {
      digit = c - 'A' + 10;
    } else {
      return -1;
    }
    value = (value << 4) | digit;
  }

  return (ii > 0) ? value : -1;
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }
  int attr = (argc == 2) ? parseAttr(argv[1]) : -1;
  if (attr < 0) {
    printUsage(stderr);
    return 1;
  }

  if (setConsoleColors((uint8_t) attr) != 0) {
    fputs("ERROR: Could not set the console colors.\n", stderr);
    return 1;
  }
  printf("\033[%d;%dm\033[2J\033[H",
    cgaToAnsiBackground(attr >> 4), cgaToAnsiForeground(attr & 0xf));

  return 0;
}
