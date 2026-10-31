///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.16.2026
///
/// @file              mush.h
///
/// @brief             Definitions in support of the MUSH command line shell.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef MUSH_H
#define MUSH_H

#include <spawn.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// @struct FsCommandArgs
///
/// @brief Structure to hold the arguments required to launch a command from the
/// filesystem.
///
/// @param commandLine A C string containing the command to run and all of its
///   arguments.
/// @param launchBackground Whether or not to launch the command in the
///   background.
/// @param fileActions A pointer to the posix_spawn_file_actions_t to use with
///   the call to posix_spawn if launching the command in the background.
/// @param numPipes The number of '|' characters in the commandLine.  This is
///   also the number of process IDs in the pids array.
/// @param pids Array of process IDs that are launched during pipe parsing.
/// @param pipes Array of int arrays.  Each array an element points to is two
///   ints in size.
/// @param numProcessesLaunched The number of processes successfully launched
///   so far.  i.e. The number of elements in the pids array that hold valid
///   processIds.
/// @param pipeIndex The index into the pipes array that is to be used for the
///   next process launched.
/// @param stdinDup A duplicate of the shell's stdin, taken before the last
///   command of a pipeline replaces it, or -1.
/// @param scriptPath The full path to the command, in dynamic memory, if it
///   turned out to be a script rather than a program.  NULL otherwise.
typedef struct FsCommandArgs {
  char *commandLine;
  bool launchBackground;
  posix_spawn_file_actions_t *fileActions;
  uint8_t numPipes;
  int *pids;
  int *pipes[2];
  int numProcessesLaunched;
  int pipeIndex;
  int stdinDup;
  char *scriptPath;
} FsCommandArgs;

/// @struct LineEditorArgs
///
/// @brief Arguments and return value for the LineEditor overlay's readLine.
///
/// @param line The buffer the command line is edited in.  It holds the line,
///   without a newline, when readLine returns.
/// @param size The size of the line buffer.
/// @param prompt The character that ends the prompt.
/// @param afterReturn Whether the last line ended with a carriage return, so
///   that a line feed that follows it is ignored.
/// @param returnValue 0 if a line was read, -1 if stdin was closed.
typedef struct LineEditorArgs {
  char *line;
  int   size;
  char  prompt;
  bool  afterReturn;
  int   returnValue;
} LineEditorArgs;

/// @struct ScriptArgs
///
/// @brief Arguments for the Script overlay's functions.
///
/// @param line The buffer readScriptLine reads the next line of a script into.
///   It holds the line, without a newline, when readScriptLine returns.
/// @param size The size of the line buffer.
/// @param path The path of the script for startScript to start.
/// @param isProfile Whether startScript is starting the login profile, whose
///   environment changes outlast it and which may be missing.
/// @param inBackground Whether the script was asked to run in the background
///   or in a pipeline, which startScript refuses.
typedef struct ScriptArgs {
  char *line;
  int   size;
  char *path;
  bool  isProfile;
  bool  inBackground;
} ScriptArgs;

#ifdef __cplusplus
}
#endif

#endif // MUSH_H

