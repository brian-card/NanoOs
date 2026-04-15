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

#include <stdio.h>
#include <spawn.h>
#include <fcntl.h>
#include <unistd.h>


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
  char *input = (char*) args;
  (void) input;
  
  printDebugString("Evaluating command line ");
  printDebugString(input);
  printDebugString("\n");
  
  // Pipes
  int cmd1ToCmd2[2], cmd2ToCmd3[2];
  
  // PIDs
  pid_t cmd1Pid, cmd2Pid, cmd3Pid;
  
  // File actions
  posix_spawn_file_actions_t cmd1FileActions, cmd2FileActions, cmd3FileActions;
  
  // argv/envp — fill these in for your actual commands
  char *cmd1Argv[] = { "cmd1", NULL };
  char *cmd2Argv[] = { "cmd2", NULL };
  char *cmd3Argv[] = { "cmd3", NULL };
  
  // Create pipes
  pipe(cmd1ToCmd2);
  fcntl(cmd1ToCmd2[0], F_SETFD, FD_CLOEXEC);
  fcntl(cmd1ToCmd2[1], F_SETFD, FD_CLOEXEC);
  
  pipe(cmd2ToCmd3);
  fcntl(cmd2ToCmd3[0], F_SETFD, FD_CLOEXEC);
  fcntl(cmd2ToCmd3[1], F_SETFD, FD_CLOEXEC);
  
  // Spawn cmd1
  posix_spawn_file_actions_init(&cmd1FileActions);
  posix_spawn_file_actions_adddup2(&cmd1FileActions,
    cmd1ToCmd2[1], STDOUT_FILENO);
  posix_spawn(&cmd1Pid, "/path/to/cmd1", &cmd1FileActions, NULL,
    cmd1Argv, environ);
  
  // Spawn cmd2
  posix_spawn_file_actions_init(&cmd2FileActions);
  posix_spawn_file_actions_adddup2(&cmd2FileActions,
    cmd1ToCmd2[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&cmd2FileActions,
    cmd2ToCmd3[1], STDOUT_FILENO);
  posix_spawn(&cmd2Pid, "/path/to/cmd2", &cmd2FileActions, NULL,
    cmd2Argv, environ);
  
  // Spawn cmd3
  posix_spawn_file_actions_init(&cmd3FileActions);
  posix_spawn_file_actions_adddup2(&cmd3FileActions,
    cmd2ToCmd3[0], STDIN_FILENO);
  posix_spawn(&cmd3Pid, "/path/to/cmd3", &cmd3FileActions, NULL,
    cmd3Argv, environ);
  
  close(cmd1ToCmd2[0]);
  close(cmd1ToCmd2[1]);
  close(cmd2ToCmd3[0]);
  close(cmd2ToCmd3[1]);
  
  return NULL;
}

