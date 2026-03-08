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
/// @brief Entrypoint into the ps program.

// Standard C includes
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>

#include "NanoOsTasks.h"
#include "NanoOsUtils.h"

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;
  int returnValue = 0;

  printf("- Dynamic memory left: %d\n", getFreeMemory());
  char *passwdStringBuffer = (char*) malloc(NANO_OS_PASSWD_STRING_BUF_SIZE);
  if (passwdStringBuffer == NULL) {
    fprintf(stderr,
      "ERROR! Could not allocate space for passwdStringBuffer in %s.\n",
      argv[0]);
    return 1;
  }
  
  struct passwd *pwd = (struct passwd*) malloc(sizeof(struct passwd));
  if (pwd == NULL) {
    fprintf(stderr,
      "ERROR! Could not allocate space for pwd in %s.\n", argv[0]);
    returnValue = 1;
    goto freePasswdStringBuffer;
  }
  
  TaskInfo *taskInfo = getTaskInfo();
  if (taskInfo != NULL) {
    uint8_t numRunningTasks = taskInfo->numTasks;
    TaskInfoElement *tasks = taskInfo->tasks;
    for (uint8_t ii = 0; ii < numRunningTasks; ii++) {
      struct passwd *result = NULL;
      returnValue = getpwuid_r(tasks[ii].userId, pwd,
        passwdStringBuffer, NANO_OS_PASSWD_STRING_BUF_SIZE, &result);
      if (returnValue != 0) {
        fprintf(stderr, "getpwnam_r returned status %d\n", returnValue);
        returnValue = 1;
        goto freePwd;
      } else if (result == NULL) {
        // returnValue is 0 but the result passwd struct is NULL.  This means
        // that the function completed successfully but that there's no such
        // user.  This is not an error for the function but we need to have
        // something to show for this user.  Set it to "unknown".
        pwd->pw_name = "unknown";
      }
      
      printf("%d  %s %s\n",
        tasks[ii].pid,
        pwd->pw_name,
        tasks[ii].name);
    }
    free(taskInfo); taskInfo = NULL;
  } else {
    printf("ERROR: Could not get process information from scheduler.\n");
  }

freePwd:
  free(pwd);

freePasswdStringBuffer:
  free(passwdStringBuffer);

  printf("- Dynamic memory left: %d\n", getFreeMemory());
  return returnValue;
}

