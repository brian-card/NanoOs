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
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "NanoOsUtils.h"

int main(int argc, char **argv) {
  (void) argc;
  
  char *buffer = (char*) malloc(96);
  if (buffer == NULL) {
    fprintf(stderr, "ERROR! Could not allocate space for buffer in %s.\n",
      argv[0]);
    return 1;
  }
  *buffer = '\0';
  
  intptr_t returnValue = 0;
  do {
    fputs("$ ", stdout);
    char *input = fgets(buffer, 96, stdin);
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
    if ((input != NULL) && (inputLength > 0)
      && (input[inputLength - 1] == '\n')
    ) {
      input[inputLength - 1] = '\0';
    }
    printDebugString("input is now \"");
    printDebugString(input);
    printDebugString("\"\n");
    
    // Attempt to process the command line as a built-in first before looking
    // on the filesystem.
    //
    // The variable 'input' is the same as the variable 'buffer', which is a
    // pointer to dynamic memory.  So, it's safe to pass as a parameter to
    // callOverlayFunction.
    printDebugString("Checking to see if command is a builtin\n");
    returnValue = (intptr_t) callOverlayFunction(
      NULL, "Builtins", "processBuiltin", input);
    if (returnValue < -1)  {
      // The command wasn't processed as a built-in.  Try running it from the
      // filesystem.
      printDebugString("Command is *NOT* a builtin\n");
      returnValue = (intptr_t) callOverlayFunction(
      NULL, "FilesystemCommands", "runFsCommand", input);
    }
  } while (returnValue != -1);
  
  free(buffer);
  
  printf("Gracefully exiting %s\n", argv[0]);
  return 0;
}

