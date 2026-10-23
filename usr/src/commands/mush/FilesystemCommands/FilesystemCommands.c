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
/// @param args A pointer to an FsCommandArgs structure containing the arguments
///   for this function.
///
/// @return On success, the value of the provided FsCommandArgs pointer is
/// returned.  On failure the value of errno is set and NULL is returned.
void* runFsCommand(void *args) {
  FsCommandArgs *fsCommandArgs = (FsCommandArgs*) args;
  if (fsCommandArgs == NULL) {
    fprintf(stderr, "ERROR:  NULL fsCommandArgs provided to runFsCommand\n");
    return NULL;
  }
  
  printDebugString("Evaluating command line ");
  printDebugString(fsCommandArgs->commandLine);
  printDebugString("\n");
  
  fsCommandArgs->commandLine
    = &fsCommandArgs->commandLine[strspn(fsCommandArgs->commandLine, " \t")];
  const char *charAt = strchr(fsCommandArgs->commandLine, ' ');
  if (charAt == NULL) {
    charAt = fsCommandArgs->commandLine + strlen(fsCommandArgs->commandLine);
  }
  size_t commandNameLength
    = ((uintptr_t) charAt)
    - ((uintptr_t) fsCommandArgs->commandLine);
  size_t commandLineLength = strlen(fsCommandArgs->commandLine);
  
  void *returnValue = fsCommandArgs;
  
  char **argv = parseArgs(fsCommandArgs->commandLine, NULL);
  if (argv == NULL) {
    fprintf(stderr, "Failed to parse command line\n");
    errno = EINVAL;
    returnValue = NULL;
    goto exit;
  }

  // Try each directory in PATH until the command is found.  execve only
  // returns on failure, and fails with ENOENT if the command isn't there.
  pid_t pid = 0; // We'll return this if posix_spawn is successful.
  int launchErrno = ENOENT;
  const char *path = getenv("PATH");
  while ((launchErrno == ENOENT) && (path != NULL) && (*path != '\0')) {
    charAt = strchr(path, ':');
    if (charAt == NULL) {
      charAt = path + strlen(path);
    }
    size_t pathDirLength = ((uintptr_t) charAt) - ((uintptr_t) path);

    // We're appending the command name to the path, so we need pathDirLength
    // extra characters for the path, plus the slash, commandNameLength bytes
    // for the command name, plus the NUL byte at the end.  execve takes the
    // command's directory, not the path to its main.overlay.
    char *commandPath = (char*) malloc(pathDirLength + commandNameLength + 2);
    if (commandPath == NULL) {
      launchErrno = ENOMEM;
      break;
    }

    strncpy(commandPath, path, pathDirLength);
    path = (*charAt == ':') ? charAt + 1 : charAt;
    if ((pathDirLength == 0) || (commandPath[pathDirLength - 1] != '/')) {
      // This is the expected case.
      commandPath[pathDirLength] = '/';
      pathDirLength++;
    }
    commandPath[pathDirLength] = '\0';
    strncat(commandPath, fsCommandArgs->commandLine, commandNameLength);

    if (fsCommandArgs->launchBackground == false) {
      // Run the command in the foreground.  i.e. Replace this shell.  This is
      // the usual case.
      execve(commandPath, argv, environ);
      launchErrno = errno;
    } else { // fsCommandArgs->launchBackground == true
      // Spawn a new task in the background.
      launchErrno = posix_spawn(&pid, commandPath, fsCommandArgs->fileActions,
        NULL, argv, environ);
    }
    free(commandPath); commandPath = NULL;
  }

  if (launchErrno == ENOENT) {
    // No such command on the filesystem.
    fputs(argv[0], stdout);
    fputs(": command not found\n", stdout);
    free(argv); argv = NULL;
    errno = ENOENT;
    returnValue = NULL;
    goto exit;
  }
  if (launchErrno == ENOEXEC) {
    fputs(argv[0], stdout);
    fputs(": cannot execute\n", stdout);
    free(argv); argv = NULL;
    errno = ENOEXEC;
    returnValue = NULL;
    goto exit;
  }

  errno = launchErrno;
  if (fsCommandArgs->launchBackground == true) {
    if (fsCommandArgs->numPipes > 0) {
      // We're in the middle of processing a command line with pipes, so we
      // need to behave differently.
      fsCommandArgs->pids[fsCommandArgs->numProcessesLaunched] = pid;
      if ((pid <= 0) || (errno != 0)) {
        fprintf(stderr, "Launching \"%s\" failed\n",
          fsCommandArgs->commandLine);
        if (errno == 0) {
          // This should be impossible since we set errno to the return value
          // of posix_spawn, but don't chance it.
          errno = EAGAIN; // Generic "try again later" error.
        }
        close(fsCommandArgs->pipes[fsCommandArgs->pipeIndex ^ 1][1]);
        return fsCommandArgs;
      }
      if (fsCommandArgs->numProcessesLaunched > 0) {
        close(fsCommandArgs->pipes[fsCommandArgs->pipeIndex ^ 1][0]);
        close(fsCommandArgs->pipes[fsCommandArgs->pipeIndex ^ 1][1]);
      }
      fsCommandArgs->numProcessesLaunched++;
      errno = posix_spawn_file_actions_destroy(fsCommandArgs->fileActions);
      fsCommandArgs->pipeIndex ^= 1;
      fsCommandArgs->commandLine += commandLineLength + 1;
    }
  }
  
  // If we made it this far then we either called posix_spawn or execve failed.
  // Either way, we need to clean up argv.
  free(argv); argv = NULL;
  
  if (errno != 0) {
    // We need to return a NULL from this function.
    fprintf(stderr, "%s failed\n", __func__);
    returnValue = NULL;
  }
  
exit:
  return returnValue;
}

