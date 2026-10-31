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

// Doxygen marker
/// @file

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "NanoOsUtils.h"

/// @fn void* processBuiltin(void *args)
///
/// @brief Process commands built into the MUSH shell.
///
/// @param args A pointer to a C string, cast to a void*.
///
/// @return Returns the integer return value of the built-in command, cast to a
/// void*.  A return value of -1 indicates that the shell should exit.  A
/// return value of -2 indicates that the command was not found.  Any
/// non-negative return value is the return value from the command that was
/// run.
void* processBuiltin(void *args) {
  char *input = (char*) args;
  printDebugString("Evaluating builtin ");
  printDebugString(input);
  printDebugString("\n");
  
  void *returnValue = (void*) ((intptr_t) 0); // Default to good status.
  if (strcmp(input, "pwd") == 0) {
    fputs(getenv("PWD"), stdout);
    fputs("\n", stdout);
  } else if ((strncmp(input, "cd", 2) == 0)
    && ((input[2] == '\0') || (input[2] == ' ') || (input[2] == '\t'))
  ) {
    input = &input[2];
    input = &input[strspn(input, " \t")];
    char *end = &input[strcspn(input, " \t")];
    *end = '\0';
    
    if (*input == '\0') {
      input = getenv("HOME");
    } else if (strcmp(input, "-") == 0) {
      input = getenv("OLDPWD");
    }
    if (input == NULL) {
      return returnValue;
    }
    
    char *path = (char*) malloc(96);
    if (path == NULL) {
      errno = ENOMEM;
      return (void*) ((intptr_t) -3);
    }
    
    if (*input == '/') {
      strcpy(path, input);
    } else {
      snprintf(path, 96, "%s/%s", getenv("PWD"), input);
    }
    pathCollapse(path);
    setenv("OLDPWD", getenv("PWD"), true);
    setenv("PWD", path, true);
    
    free(path); path = NULL;
  } else if ((strncmp(input, "export", 6) == 0)
    && ((input[6] == '\0') || (input[6] == ' ') || (input[6] == '\t'))
  ) {
    input = &input[6];
    input = &input[strspn(input, " \t")];
    char *equals = &input[strcspn(input, " \t=")];
    if ((equals == input) || (*equals != '=')) {
      fputs("Usage: export NAME=value\n", stderr);
      return (void*) ((intptr_t) 1);
    }
    *equals = '\0';
    char *value = &equals[1];
    char *end = &value[strlen(value)];
    while ((end > value) && ((end[-1] == ' ') || (end[-1] == '\t'))) {
      end--;
    }
    *end = '\0';
    if (setenv(input, value, true) != 0) {
      fprintf(stderr, "export: %s\n", strerror(errno));
      return (void*) ((intptr_t) 1);
    }
  } else {
    returnValue = (void*) ((intptr_t) -2);
  }
  
  return returnValue;
}

