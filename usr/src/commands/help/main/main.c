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
#include "NanoOsUtils.h"

/// @brief Print the usage summary for help.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: help\n"
    "\n"
    "List the commands with a short description of each.  Run a command with\n"
    "--help to see its own usage.\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }
  int returnValue = 0;

  printf("%s %s\n",
    "cat:                    ",
    "Write the contents of a file to the current console.");
  printf("%s %s\n",
    "cd:                     ",
    "Change the current working directory.");
  printf("%s %s\n",
    "clear:                  ",
    "Clear the screen.");
  printf("%s %s\n",
    "color:                  ",
    "Set the console's background and foreground colors.");
  printf("%s %s\n",
    "cp:                     ",
    "Copy files.");
  printf("%s %s\n",
    "dirtest:                ",
    "Exercise the directory-reading functions.");
  printf("%s %s\n",
    "dumpMemoryAllocations:  ",
    "Dump information about all dynamic memory allocations.");
  printf("%s %s\n",
    "echo:                   ",
    "Echo a string back to the console.");
  printf("%s %s\n",
    "ed:                     ",
    "Edit a text file.");
  printf("%s %s\n",
    "exit:                   ",
    "Exit the current shell.");
  printf("%s %s\n",
    "getty:                  ",
    "Run the getty application.");
  printf("%s %s\n",
    "grep:                   ",
    "Find text in a file or in piped output.");
  printf("%s %s\n",
    "helloworld:             ",
    "Run the \"helloworld\" command from the filesystem.");
  printf("%s %s\n",
    "help:                   ",
    "Print this help message.");
  printf("%s %s\n",
    "kill:                   ",
    "Kill a running process.");
  printf("%s %s\n",
    "ll:                     ",
    "List the contents of a directory.");
  printf("%s %s\n",
    "logger:                 ",
    "Run the system logger.");
  printf("%s %s\n",
    "login:                  ",
    "Log in as a user.");
  printf("%s %s\n",
    "looseLoop:              ",
    "Run a process in a loop that does yield.");
  printf("%s %s\n",
    "mkdir:                  ",
    "Create a directory.");
  printf("%s %s\n",
    "mush:                   ",
    "Run the MUSH shell.");
  printf("%s %s\n",
    "mv:                     ",
    "Move or rename a file.");
  printf("%s %s\n",
    "ps:                     ",
    "List the running processes.");
  printf("%s %s\n",
    "pwd:                    ",
    "Print the current working directory.");
  printf("%s %s\n",
    "rm:                     ",
    "Remove a file.");
  printf("%s %s\n",
    "rmdir:                  ",
    "Remove an empty directory.");
  printf("%s %s\n",
    "shutdown:               ",
    "Power off or reset the system.");
  printf("%s %s\n",
    "tightLoop:              ",
    "Run a process in a loop that does not yield.");
  printf("%s %s\n",
    "touch:                  ",
    "Create an empty file.");
  printf("%s %s\n",
    "ver:                    ",
    "Display the operating system version.");
  printf("\nRun a command with --help to see its usage.\n");

  return returnValue;
}

