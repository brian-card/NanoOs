////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mush.h"
#include "NanoOsUtils.h"

/// @def MUSH_COMMAND_LINE_LENGTH
///
/// @brief The maximum number of bytes that the CLI can accept in MUSH.
#define MUSH_COMMAND_LINE_LENGTH 96

int main(int argc, char **argv) {
  (void) argc;
  
  char *buffer = (char*) malloc(MUSH_COMMAND_LINE_LENGTH);
  if (buffer == NULL) {
    fprintf(stderr,
      "ERROR: Could not allocate space for mush command line buffer\n");
    return 1;
  }
  *buffer = '\0';
  
  intptr_t returnValue = 0;
  FsCommandArgs fsCommandArgs;
  
  char prompt = '$';
  if (strcmp(getenv("UID"), "0") == 0) {
    prompt = '#';
  }
  
  char *username = getenv("USER");
  char *hostname = getenv("HOSTNAME");
  if (hostname == NULL) {
    hostname = "localhost";
  }
  
  do {
    char *pwd = getenv("PWD");
    if (pwd == NULL) {
      pwd = "<unknown directory>";
    }
    printf("mush %s@%s:%s%c ", username, hostname, pwd, prompt);
    char *input = fgets(buffer, MUSH_COMMAND_LINE_LENGTH, stdin);
    if (input == NULL) {
      // Our stdin file descriptor has been closed.  Bail.
      break;
    }
    printDebugString("Read \"");
    printDebugString(input);
    printDebugString("\" from command line\n");
    printDebug("Read \"%s\" from command line\n", input);
    printDebugString("strlen = 0x");
    printDebugHex(overlayMap.header.osApi->strlen);
    printDebugString("\n");
    size_t inputLength = strlen(input);
    printDebugString("inputLength = ");
    printDebugInt(inputLength);
    printDebugString("\n");
    if ((inputLength > 0) && (input[inputLength - 1] == '\n')) {
      input[inputLength - 1] = '\0';
    }
    printDebugString("input is now \"");
    printDebugString(input);
    printDebugString("\"\n");
    
    // We need to check for exit outside of processing it as a built-in.  This
    // is because, if the system gets into a bad state, we can fail to load
    // overlays.  We should always be able to exit and release all the process
    // resources.
    input = &input[strspn(input, " \t")];
    if (*input == '\0') {
      continue;
    } else if (strcmp(input, "exit") == 0) {
      break;
    }
    
    // Attempt to process the command line as a built-in first before looking
    // on the filesystem.
    //
    // The variable 'input' is the same as the variable 'buffer', which is a
    // pointer to dynamic memory.  So, it's safe to pass as a parameter to
    // callOverlayFunction.
    printDebugString("Checking to see if command is a builtin\n");
    returnValue = (intptr_t) callOverlayFunction(
      OVERLAY_SAME_NAMESPACE, "Builtins", "processBuiltin", input);
    if (returnValue < -1)  {
      // The command wasn't processed as a built-in.  Try running it from the
      // filesystem.
      printDebugString("Command is *NOT* a builtin\n");
      if (strchr(input, '|')) {
        // Command line contains pipes.  Process it that way.
        fsCommandArgs.commandLine = input;
        fsCommandArgs.launchBackground = false;
        fsCommandArgs.fileActions = NULL;
        fsCommandArgs.numPipes = 0;
        fsCommandArgs.pids = NULL;
        fsCommandArgs.pipes[0] = NULL;
        fsCommandArgs.pipes[1] = NULL;
        fsCommandArgs.numProcessesLaunched = 0;
        fsCommandArgs.pipeIndex = 0;
        errno = 0;
        FsCommandArgs *nextCommand = &fsCommandArgs;
        while (nextCommand == &fsCommandArgs) {
          nextCommand = (FsCommandArgs*) callOverlayFunction(
            OVERLAY_SAME_NAMESPACE, "Pipes", "processPipes", &fsCommandArgs);
          nextCommand = callOverlayFunction(
            OVERLAY_SAME_NAMESPACE, "FilesystemCommands", "runFsCommand",
            nextCommand);
        }
      } else {
        fsCommandArgs.commandLine = input;
        fsCommandArgs.launchBackground = false;
        char *ampersandAt = strrchr(input, '&');
        if ((ampersandAt != NULL) && (ampersandAt[-1] != '&')) {
          *ampersandAt = '\0';
          fsCommandArgs.launchBackground = true;
        }
        fsCommandArgs.fileActions = NULL;
        fsCommandArgs.numPipes = 0;
        fsCommandArgs.pids = NULL;
        fsCommandArgs.pipes[0] = NULL;
        fsCommandArgs.pipes[1] = NULL;
        fsCommandArgs.numProcessesLaunched = 0;
        fsCommandArgs.pipeIndex = 0;
        errno = 0;
        returnValue = (intptr_t) callOverlayFunction(
          OVERLAY_SAME_NAMESPACE, "FilesystemCommands", "runFsCommand",
          &fsCommandArgs);
      }
    }
  } while (returnValue != -1);
  
  free(buffer);
  
  printf("Gracefully exiting %s\n", argv[0]);
  return 0;
}

