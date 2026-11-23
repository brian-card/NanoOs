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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>

// NanoOs includes
#include "UserProcesses.h"
#include "NanoOsUtils.h"

/// @brief Print the usage summary for ps.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: ps [-l]\n"
    "\n"
    "List the running processes with their process IDs, owners, and names,\n"
    "between two lines that show the dynamic memory left.\n"
    "\n"
    "Options:\n"
    "  -l      Show each process's privilege level after its owner.\n"
    "  --help  Show this help and exit.\n",
    stream);
}

/// @brief Get the name of a privilege level.
///
/// @param privilegeLevel The PrivilegeLevel.
///
/// @return Returns the level's name.
static const char* privilegeLevelName(uint8_t privilegeLevel) {
  if (privilegeLevel == PRIVILEGE_LEVEL_KERNEL) {
    return "kernel";
  } else if (privilegeLevel == PRIVILEGE_LEVEL_EXECUTIVE) {
    return "executive";
  } else if (privilegeLevel == PRIVILEGE_LEVEL_SUPERVISOR) {
    return "supervisor";
  } else if (privilegeLevel == PRIVILEGE_LEVEL_USER) {
    return "user";
  }
  return "unknown";
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }
  bool showLevels = false;
  if ((argc == 2) && (strcmp(argv[1], "-l") == 0)) {
    showLevels = true;
  } else if (argc > 1) {
    printUsage(stderr);
    return 1;
  }
  int returnValue = 0;

  printf("- Dynamic memory left: %d\n", getFreeMemory());
  // NANO_OS_PASSWD_STRING_BUF_SIZE is only 96 bytes, so put it on the stack.
  char passwdStringBuffer[NANO_OS_PASSWD_STRING_BUF_SIZE];
  
  // struct passwd is smaller than the size of a MemNode, so put it on the
  // stack, too.
  struct passwd pwd;
  ProcessInfo *processInfo = getProcessInfo();
  if (processInfo != NULL) {
    uint8_t numRunningProcesses = processInfo->numProcesses;
    ProcessInfoElement *processes = processInfo->processes;
    for (uint8_t ii = 0; ii < numRunningProcesses; ii++) {
      struct passwd *result = NULL;
      returnValue = getpwuid_r(processes[ii].userId, &pwd,
        passwdStringBuffer, NANO_OS_PASSWD_STRING_BUF_SIZE, &result);
      if (returnValue != 0) {
        fprintf(stderr, "getpwnam_r returned status %d\n", returnValue);
        returnValue = 1;
        goto exit;
      } else if (result == NULL) {
        // returnValue is 0 but the result passwd struct is NULL.  This means
        // that the function completed successfully but that there's no such
        // user.  This is not an error for the function but we need to have
        // something to show for this user.
        if (processes[ii].userId == SYSTEM_USER_ID) {
          pwd.pw_name = "system";
        } else {
          pwd.pw_name = "unknown";
        }
      }
      
      if (showLevels) {
        printf("%d  %s %s %s\n",
          processes[ii].pid,
          pwd.pw_name,
          privilegeLevelName(processes[ii].privilegeLevel),
          processes[ii].name);
      } else {
        printf("%d  %s %s\n",
          processes[ii].pid,
          pwd.pw_name,
          processes[ii].name);
      }
    }
    free(processInfo); processInfo = NULL;
  } else {
    printf("ERROR: Could not get process information from scheduler.\n");
  }

exit:
  printf("- Dynamic memory left: %d\n", getFreeMemory());
  return returnValue;
}

