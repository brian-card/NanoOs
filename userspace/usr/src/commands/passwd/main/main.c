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
/// @brief Implementation of the `passwd` command:  run the trusted
/// /system/commands/passwd, which can change a password, and wait for it.

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include "NanoOsUtils.h"

/// @fn void printUsage(FILE *stream)
///
/// @brief Print the usage summary for passwd.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: passwd [user name]\n"
    "\n"
    "Change your password.  You are asked for your current password first.\n"
    "Only root can name another user, and root is never asked for a current\n"
    "password.\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  } else if (argc > 2) {
    printUsage(stderr);
    return 1;
  }

  char *childArgv[] = { "passwd", argv[1], NULL };
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SYSTEM | POSIX_SPAWN_FOREGROUND);
  pid_t pid = 0;
  int returnValue = posix_spawn(&pid, "passwd", NULL, &attr, childArgv, NULL);
  posix_spawnattr_destroy(&attr);
  if (returnValue != 0) {
    fprintf(stderr, "passwd: %s\n", strerror(returnValue));
    return 1;
  }

  int status = 0;
  if ((waitpid(pid, &status, 0) != pid) || !WIFEXITED(status)) {
    return 1;
  }
  return WEXITSTATUS(status);
}
