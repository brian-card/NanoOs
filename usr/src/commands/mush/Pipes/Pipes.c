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
  
  FsCommandArgs *fsCommandArgs = (FsCommandArgs*) args;
  
  printDebugString("Evaluating command line ");
  printDebugString(fsCommandArgs->commandLine);
  printDebugString("\n");
  
  char *pipeAt = NULL;
  if (fsCommandArgs->numProcessesLaunched == 0) {
    pipeAt = strchr(fsCommandArgs->commandLine, '|');
    fsCommandArgs->numPipes = 0;
    while (pipeAt != NULL) {
      fsCommandArgs->numPipes++;
      pipeAt = pipeAt + 1;
      pipeAt = strchr(pipeAt, '|');
    }
    
    // We need to allocate memory for everything in one block.  If this fails,
    // everything else fails.  Allocating a single block means that everything
    // is represented by a single MemNode in dynamic memory, which (a) keeps us
    // from punching a bunch of holes in dynamic memory and (b) reduces the
    // metadata consumption, thereby making more overall RAM available in the
    // system.
    fsCommandArgs->fileActions = (posix_spawn_file_actions_t*)
      calloc(1,
          sizeof(posix_spawn_file_actions_t)
        + sizeof(int) * fsCommandArgs->numPipes // fsCommandArgs->pids
        + sizeof(int) * 4                       // fsCommandArgs->pipes
      );
    if (fsCommandArgs->fileActions == NULL) {
      errno = ENOMEM;
      goto exit;
    }
    fsCommandArgs->pids = (int*) &fsCommandArgs->fileActions[1];
    fsCommandArgs->pipes[0] = &fsCommandArgs->pids[fsCommandArgs->numPipes];
    fsCommandArgs->pipes[1] = &fsCommandArgs->pipes[0][2];
    fsCommandArgs->pipeIndex = 0;
  } else if ((fsCommandArgs->pids[fsCommandArgs->numProcessesLaunched] < 0)
    || (errno != 0)
  ) {
    goto freeFileActions;
  }
  
  pipeAt = strchr(fsCommandArgs->commandLine, '|');
  if (pipeAt != NULL) {
    *pipeAt = '\0';
    
    // Create the pipes
    if (pipe(fsCommandArgs->pipes[fsCommandArgs->pipeIndex]) != 0) {
      // errno is already set
      goto freeFileActions;
    }
    if (fcntl(fsCommandArgs->pipes[fsCommandArgs->pipeIndex][0],
      F_SETFD, FD_CLOEXEC) != 0
    ) {
      // errno is already set
      goto freeFileActions;
    }
    if (fcntl(fsCommandArgs->pipes[fsCommandArgs->pipeIndex][1],
      F_SETFD, FD_CLOEXEC) != 0
    ) {
      // errno is already set
      goto freeFileActions;
    }
    
    // Initialize the fileActions
    errno = posix_spawn_file_actions_init(fsCommandArgs->fileActions);
    if (errno != 0) {
      // errno is already set
      goto freeFileActions;
    }
    if (fsCommandArgs->numProcessesLaunched > 0) {
      errno = posix_spawn_file_actions_adddup2(fsCommandArgs->fileActions,
        fsCommandArgs->pipes[fsCommandArgs->pipeIndex ^ 1][0], STDIN_FILENO);
      if (errno != 0) {
        // errno is already set
        posix_spawn_file_actions_destroy(fsCommandArgs->fileActions);
        goto freeFileActions;
      }
    }
    errno = posix_spawn_file_actions_adddup2(fsCommandArgs->fileActions,
      fsCommandArgs->pipes[fsCommandArgs->pipeIndex][1], STDOUT_FILENO);
    if (errno != 0) {
      // errno is already set
      posix_spawn_file_actions_destroy(fsCommandArgs->fileActions);
      goto freeFileActions;
    }
    
    // Launch the command in the background by having the caller run the
    // runFsCommand overlay function.
    fsCommandArgs->launchBackground = true;
    return fsCommandArgs;
  }
  
  // Make a backup copy of our stdin in case something goes wrong.
  int stdinDup = dup(STDIN_FILENO);
  if (stdinDup < 0) {
    fprintf(stderr, "ERROR: dup of STDIN_FILENO returned %d\n", stdinDup);
  }
  
  // dup the last command's pipe onto our stdin instead of using file actions
  if (dup2(fsCommandArgs->pipes[fsCommandArgs->pipeIndex ^ 1][0],
    STDIN_FILENO) != 0
  ) {
    // errno is already set
    goto freeFileActions;
  }
  close(fsCommandArgs->pipes[fsCommandArgs->pipeIndex ^ 1][0]);
  close(fsCommandArgs->pipes[fsCommandArgs->pipeIndex ^ 1][1]);
  
  // Launch the last command in the foreground by having the caller run the
  // runFsCommand overlay function.
  fsCommandArgs->launchBackground = false;
  free(fsCommandArgs->fileActions); fsCommandArgs->fileActions = NULL;
  fsCommandArgs->numPipes = 0;
  fsCommandArgs->pids = NULL;
  fsCommandArgs->pipes[0] = NULL;
  fsCommandArgs->pipes[1] = NULL;
  fsCommandArgs->numProcessesLaunched = 0;
  fsCommandArgs->pipeIndex = 0;
  return fsCommandArgs;
  
  // No error is possible here, so we're done and we don't need to do any
  // cleanup.
  
freeFileActions:
  // kill potentially changes the value of errno.  We want to use the value
  // that was set from above.
  tmpErrno = errno;
  for (int ii = 0; ii < fsCommandArgs->numProcessesLaunched; ii++) {
    kill(fsCommandArgs->pids[ii], SIGKILL);
  }
  errno = tmpErrno;

  // All memory is allocated under fileActions, so that's the only pointer we
  // need to free.
  tmpErrno = errno;
  posix_spawn_file_actions_destroy(fsCommandArgs->fileActions);
  errno = tmpErrno;
  free(fsCommandArgs->fileActions); fsCommandArgs->fileActions = NULL;
  fsCommandArgs->launchBackground = false;
  fsCommandArgs->numPipes = 0;
  fsCommandArgs->pids = NULL;
  fsCommandArgs->pipes[0] = NULL;
  fsCommandArgs->pipes[1] = NULL;
  fsCommandArgs->numProcessesLaunched = 0;
  fsCommandArgs->pipeIndex = 0;
  
exit:
  // The fact that we failed likely means that there wasn't enough contiguous
  // memory in the system to start a process.  Even if that's not true (because,
  // for instance, maybe we just ran out of process slots), we definitely
  // allocated and then deallocated a bunch of values while attempting to
  // set things up.  So, either way, memory is fragmented now.  If we return
  // normally, the memory will remain fragmented.  What we really want to do is
  // to exit, thereby releasing all of our allocations and invoking memory
  // compaction.  But, if we exit normally, the user will go back to logging in.
  // BUT! if we kill ourselves, the scheduler will detect a "self-kill", do the
  // cleanup, and then start the shell again.  Do that instead.
  kill(getpid(), SIGKILL);
  
  return NULL;
}

