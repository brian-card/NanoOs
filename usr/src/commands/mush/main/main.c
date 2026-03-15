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

extern char **environ;

int runFilesystemCommand(char *commandLine) {
  const char *charAt = strchr(commandLine, ' ');
  if (charAt == NULL) {
    charAt = commandLine + strlen(commandLine);
  }
  size_t commandNameLength = ((uintptr_t) charAt) - ((uintptr_t) commandLine);
  
  char *commandPath = NULL;
  const char *path = getenv("PATH");
  while ((path != NULL) && (*path != '\0')) {
    charAt = strchr(path, ':');
    if (charAt == NULL) {
      charAt = path + strlen(path);
    }
    size_t pathDirLength = ((uintptr_t) charAt) - ((uintptr_t) path);
    
    // We're appending the command name to the path, so we need pathDirLength
    // extra characters for the path, plus the slash, commandNameLength bytes
    // for the command name, and one more for the slash for that directory.
    // Then, we need 4 bytes for "main", and OVERLAY_EXT_LEN bytes on top of
    // that plus the NULL byte // at the end.
    commandPath
      = (char*) malloc(pathDirLength + commandNameLength + OVERLAY_EXT_LEN + 7);
    if (commandPath == NULL) {
      return -ENOMEM;
    }
    
    strncpy(commandPath, path, pathDirLength);
    path += pathDirLength + 1; // Skip over the colon
    if (commandPath[pathDirLength - 1] != '/') {
      // This is the expected case.
      commandPath[pathDirLength] = '/';
      pathDirLength++;
    }
    commandPath[pathDirLength] = '\0';
    strncat(commandPath, commandLine, commandNameLength);
    commandPath[pathDirLength + commandNameLength] = '\0';
    strcat(commandPath, "/main");
    strcat(commandPath, OVERLAY_EXT);
    
    FILE *filesystemEntry = fopen(commandPath, "r");
    if (filesystemEntry != NULL) {
      // There is a valid command to run on the filesystem and filesystemEntry
      // is a valid FILE pointer.  Close the file and run the command.
      fclose(filesystemEntry); filesystemEntry = NULL;
      break;
    }
    
    // If we made it this far then filesystemEntry is NULL and we need to
    // evaluate the next directory in the path.
    free(commandPath); commandPath = NULL;
  }
  
  if (commandPath == NULL) {
    // No such command on the filesystem.
    fputs(commandLine, stdout);
    fputs(": command not found\n", stdout);
    return -ENOENT;
  }
  
  // The file we opened above is the full path to the main.overlay of the
  // executable.  The fact that programs are broken up into overlays with the
  // entrypoint being in main.overlay is an implementation detail of NanoOs and
  // is not exposed to userspace.  execve expects a path to the base directory
  // of the command, *NOT* a path to the main.overlay file.  It will at that
  // itself.  So, we need to terminate the path we pass into execve at the last
  // slash in the path.  We're guaranteed that exists because of the logic
  // above, so we can blindly dereference the value returned by strrchr here.
  *strrchr(commandPath, '/') = '\0';
  
  // We need to check to see if the task should be run in the background beore
  // calling parseArgs because that function modifies the string.
  bool launchBackground = false;
  if (commandLine[strlen(commandLine) - 1] == '&') {
    launchBackground = true;
  }
  
  char **argv = parseArgs(commandLine, NULL);
  if (argv == NULL) {
    fprintf(stderr, "Failed to parse command line\n");
    free(commandPath); commandPath = NULL;
    return -EINVAL;
  }
  
  // Run the command from the filesystem.
  if (launchBackground == false) {
    // Run the command in the foreground.  i.e. Replace this shell.  This is
    // the usual case.
    execve(commandPath, argv, environ);
  } else { // launchBackground == true
    // Spawn a new task in the background.
    pid_t pid; // We actually don't care about this but it's required.
    errno = posix_spawn(&pid, commandPath, NULL, NULL, argv, environ);
  }
  
  // If we made it this far then we either called posix_spawn or execve failed.
  // Either way, we need to clean up argv.
  free(argv); argv = NULL;
  
  // We need to return a negative errno from this function.  execve sets the
  // value of errno on failure, so we don't have to do anything there.  Above,
  // we set the return value of posix_spawn to errno so that we can just use
  // whatever the value of errno is here for the return value.  We return a
  // negative errno on failure, so negate whatever errno is.
  return -errno;
}

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
    printDebugString("Read \"");
    printDebugString(input);
    printDebugString("\" from command line\n");
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
    printDebugString("\n");
    
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
      returnValue = runFilesystemCommand(input);
    }
  } while (returnValue != -1);
  
  free(buffer);
  
  printf("Gracefully exiting %s\n", argv[0]);
  return 0;
}

