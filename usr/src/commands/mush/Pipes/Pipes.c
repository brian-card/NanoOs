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
  (void) cmd1Pid;
  (void) cmd2Pid;
  (void) cmd3Pid;
  
  // File actions
  posix_spawn_file_actions_t cmd1FileActions, cmd2FileActions, cmd3FileActions;
  (void) cmd1FileActions;
  (void) cmd2FileActions;
  (void) cmd3FileActions;
  
  // argv/envp — fill these in for your actual commands
  char *cmd1Argv[] = { "cmd1", NULL };
  char *cmd2Argv[] = { "cmd2", NULL };
  char *cmd3Argv[] = { "cmd3", NULL };
  (void) cmd1Argv;
  (void) cmd2Argv;
  (void) cmd3Argv;
  
  // Create pipes
  pipe(cmd1ToCmd2);
  fcntl(cmd1ToCmd2[0], F_SETFD, FD_CLOEXEC);
  fcntl(cmd1ToCmd2[1], F_SETFD, FD_CLOEXEC);
  
  pipe(cmd2ToCmd3);
  fcntl(cmd2ToCmd3[0], F_SETFD, FD_CLOEXEC);
  fcntl(cmd2ToCmd3[1], F_SETFD, FD_CLOEXEC);
  
  return NULL;
}

