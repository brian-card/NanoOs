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
/// @brief Entrypoint into the help program.

// Standard C includes
#include <stdio.h>

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;
  int returnValue = 0;

  printf("%s %-2s\n",
    "echo:       ",
    "Echo a string back to the console.");
  printf("%s %s\n",
    "exit:       ",
    "Exit the current shell.");
  printf("%s %s\n",
    "getty:      ",
    "Run the getty application.");
  printf("%s %s\n",
    "grep:       ",
    "Find text in piped output.");
  printf("%s %s\n",
    "helloworld: ",
    "Run the \"helloworld\" command from the filesystem.");
  printf("%s %s\n",
    "help:       ",
    "Print this help message.");
  printf("%s %s\n",
    "kill:       ",
    "Kill a running process.");
  printf("%s %s\n",
    "looseLoop:  ",
    "Run a process in a loop that does yield.");
  printf("%s %s\n",
    "ps:         ",
    "List the running processes.");
  printf("%s %s\n",
    "shutdown:   ",
    "Power off or reset the system.");
  printf("%s %s\n",
    "tightLoop:  ",
    "Run a process in a loop that does not yield.");

  return returnValue;
}

