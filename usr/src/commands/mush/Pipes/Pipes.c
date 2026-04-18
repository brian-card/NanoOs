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

/// @file Pipes.c
///
/// @brief Overlay for handling command lines that include pipes ('|').

// Standard C includes
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Unix includes
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

// NanoOs includes
#include "mush.h"
#include "NanoOsUtils.h"


/// @fn void* processPipes(void *args)
///
/// @brief Process a command line that contains one or more pipes ('|').
///
/// @param args A pointer to the C string containing the full comamnd line,
///   cast to a void*.
///
/// @return On success, this function execs the last command in the chain and
/// does not return.  On failure, NULL is returned and errno is set.
void* processPipes(void *args) {
  // Used in the error case at the bottom.
  int tmpErrno = 0;
  
  char *input = (char*) args;
  
  printDebugString("Evaluating command line ");
  printDebugString(input);
  printDebugString("\n");
  
  uint8_t numPipes = 0;
  char *pipeAt = strchr(input, '|');
  while (pipeAt != NULL) {
    numPipes++;
    pipeAt = pipeAt + 1;
    pipeAt = strchr(pipeAt, '|');
  }
  
  // We need to keep track of the processes we launch in case something in the
  // chain fails.  The number of commands we'll launch in the background is the
  // number of pipes in the command.
  int *pids = (int*) malloc(sizeof(int) * numPipes);
  if (pids == NULL) {
    errno = ENOMEM;
    goto exit;
  }
  // Reset numPipes to use as an index into the pids array below.
  numPipes = 0;
  
  bool firstCommand = true;
  FsCommandArgs *fsCommandArgs = (FsCommandArgs*) malloc(sizeof(FsCommandArgs));
  if (fsCommandArgs == NULL) {
    errno = ENOMEM;
    goto freePids;
  }
  
  // Pipe
  int pipes[2][2];
  int pipeIndex = 0;
  
  // File actions
  posix_spawn_file_actions_t *fileActions
    = (posix_spawn_file_actions_t*) malloc(sizeof(posix_spawn_file_actions_t));
  if (fileActions == NULL) {
    errno = ENOMEM;
    goto freeFsCommandArgs;
  }
  
  pipeAt = strchr(input, '|');
  while (pipeAt != NULL) {
    *pipeAt = '\0';
    
    // Create the pipes
    if (pipe(pipes[pipeIndex]) != 0) {
      // errno is already set
      goto freeFileActions;
    }
    if (fcntl(pipes[pipeIndex][0], F_SETFD, FD_CLOEXEC) != 0) {
      // errno is already set
      goto freeFileActions;
    }
    if (fcntl(pipes[pipeIndex][1], F_SETFD, FD_CLOEXEC) != 0) {
      // errno is already set
      goto freeFileActions;
    }
    
    // Initialize the fileActions
    errno = posix_spawn_file_actions_init(fileActions);
    if (errno != 0) {
      // errno is already set
      goto freeFileActions;
    }
    if (firstCommand == false) {
      errno = posix_spawn_file_actions_adddup2(fileActions,
        pipes[pipeIndex ^ 1][0], STDIN_FILENO);
      if (errno != 0) {
        // errno is already set
        posix_spawn_file_actions_destroy(fileActions);
        goto freeFileActions;
      }
    }
    errno = posix_spawn_file_actions_adddup2(fileActions,
      pipes[pipeIndex][1], STDOUT_FILENO);
    if (errno != 0) {
      // errno is already set
      posix_spawn_file_actions_destroy(fileActions);
      goto freeFileActions;
    }
    
    // Launch the command in the background
    fsCommandArgs->commandLine = input;
    fsCommandArgs->launchBackground = true;
    fsCommandArgs->fileActions = fileActions;
    pids[numPipes] = (int) ((intptr_t) callOverlayFunction(
      NULL, "FilesystemCommands", "runFsCommand", fsCommandArgs));
    if (pids[numPipes] < 0) {
      // errno is already set
      posix_spawn_file_actions_destroy(fileActions);
      goto freeFileActions;
    }
    numPipes++;
    
    // Destroy the fileActions
    errno = posix_spawn_file_actions_destroy(fileActions);
    if (errno != 0) {
      // errno is already set
      goto freeFileActions;
    }
    
    if (firstCommand == false) {
      // Close the last command's pipe
      close(pipes[pipeIndex ^ 1][0]);
      close(pipes[pipeIndex ^ 1][1]);
    }
    
    firstCommand = false;
    pipeIndex ^= 1;
    
    input = pipeAt + 1;
    pipeAt = strchr(input, '|');
  }
  
  // dup the last command's pipe onto our stdin instead of using file actions
  if (dup2(pipes[pipeIndex ^ 1][0], STDIN_FILENO) != 0) {
    // errno is already set
    goto freeFileActions;
  }
  close(pipes[pipeIndex ^ 1][0]);
  close(pipes[pipeIndex ^ 1][1]);
  
  // Launch the last command in the foreground
  fsCommandArgs->commandLine = input;
  fsCommandArgs->launchBackground = false;
  fsCommandArgs->fileActions = NULL;
  callOverlayFunction(
    NULL, "FilesystemCommands", "runFsCommand", fsCommandArgs);
  
  // If we made it this far then errno is already set
  
freeFileActions:
  free(fileActions); fileActions = NULL;
  
freeFsCommandArgs:
  free(fsCommandArgs); fsCommandArgs = NULL;
  
freePids:
  // kill potentially changes the value of errno.  We want to use the value
  // that was set from above.
  tmpErrno = errno;
  for (int ii = 0; ii < numPipes; ii++) {
    kill(pids[ii], SIGKILL);
  }
  errno = tmpErrno;
  free(pids); pids = NULL;

exit:
  return NULL;
}

