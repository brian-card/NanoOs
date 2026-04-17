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
typedef struct FsCommandArgs {
  char *commandLine;
  bool launchBackground;
  posix_spawn_file_actions_t *fileActions;
} FsCommandArgs;

#ifdef __cplusplus
}
#endif

#endif // MUSH_H

