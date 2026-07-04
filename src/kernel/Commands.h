///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              05.16.2026
///
/// @file              Commands.h
///
/// @brief             Built-in shell and commands when not loading from a
///                    filesystem.
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

#ifndef COMMANDS_H
#define COMMANDS_H

#include "NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Special exit statuses
#define COMMAND_CANNOT_EXECUTE 126
#define COMMAND_NOT_FOUND      127
#define COMMAND_EXIT_INVALID   128

/// @struct CommandEntry
///
/// @brief Descriptor for a command that can be looked up and run by the
/// handleCommand function.
///
/// @param name The textual name of the command.
/// @param func A function pointer to the process that will be spawned to
///   execute the command.
/// @param help A one-line summary of what this command does.
typedef struct CommandEntry {
  const char      *name;
  CommandFunction  func;
  const char      *help;
} CommandEntry;

// Exported tasks
void* execBuiltinCommand(void *args);
int32_t restartBuiltinShell(ProcessDescriptor *processDescriptor);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // COMMANDS_H
