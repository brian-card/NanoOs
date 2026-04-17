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

/// @file FilesystemCommands.c
///
/// @brief Overlay for handling commands that run from the filesystem.

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mush.h"
#include "NanoOsUtils.h"


/// @fn void* runFsCommand(void *args)
///
/// @brief Run a command from the filesystem.
///
/// @param args A pointer to a C string holding the command line, cast to a
///   void*.
///
/// @return On success, this function execs the command line provided and does
/// not return.  On failure, -errno is returned, cast to a void*.
void* runFsCommand(void *args) {
  FsCommandArgs *fsCommandArgs = (FsCommandArgs*) args;
  char *commandLine = fsCommandArgs->commandLine;
  bool launchBackground = fsCommandArgs->launchBackground;
  posix_spawn_file_actions_t *fileActions = fsCommandArgs->fileActions;

  printDebugString("Evaluating command line ");
  printDebugString(commandLine);
  printDebugString("\n");
  
  void *returnValue = (void*) ((intptr_t) 0); // Default to good status.
  
  commandLine = &commandLine[strspn(commandLine, " \t")];
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
      returnValue = (void*) ((intptr_t) -ENOMEM);
      goto exit;
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
    returnValue = (void*) ((intptr_t) -ENOENT);
    goto exit;
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
  
  char **argv = parseArgs(commandLine, NULL);
  if (argv == NULL) {
    fprintf(stderr, "Failed to parse command line\n");
    free(commandPath); commandPath = NULL;
    returnValue = (void*) ((intptr_t) -EINVAL);
    goto exit;
  }
  
  // Run the command from the filesystem.
  if (launchBackground == false) {
    // Run the command in the foreground.  i.e. Replace this shell.  This is
    // the usual case.
    execve(commandPath, argv, environ);
  } else { // launchBackground == true
    // Spawn a new task in the background.
    pid_t pid; // We actually don't care about this but it's required.
    errno = posix_spawn(&pid, commandPath, fileActions, NULL, argv, environ);
  }
  
  // If we made it this far then we either called posix_spawn or execve failed.
  // Either way, we need to clean up argv.
  free(argv); argv = NULL;
  
  // We need to return a negative errno from this function.  execve sets the
  // value of errno on failure, so we don't have to do anything there.  Above,
  // we set the return value of posix_spawn to errno so that we can just use
  // whatever the value of errno is here for the return value.  We return a
  // negative errno on failure, so negate whatever errno is.
  returnValue = (void*) ((intptr_t) -errno);
  
exit:
  return returnValue;
}

