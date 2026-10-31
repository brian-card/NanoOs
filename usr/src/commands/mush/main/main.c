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

// Doxygen marker
/// @file

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mush.h"
#include "NanoOsUtils.h"

/// @def MUSH_COMMAND_LINE_LENGTH
///
/// @brief The maximum number of bytes that the CLI can accept in MUSH.
#define MUSH_COMMAND_LINE_LENGTH 96

/// @def MUSH_PROFILE_NAME
///
/// @brief The name of the file in HOME that a login shell runs at startup.
#define MUSH_PROFILE_NAME ".mush_profile"

#undef printDebug
#ifdef MUSH_DEBUG
#define printDebug(...) fprintf(stderr, __VA_ARGS__)
#else
#define printDebug(...) {}
#endif // MUSH_DEBUG


/// @brief Print the usage summary for mush.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: mush [script]\n"
    "\n"
    "Run the MUSH shell.  Each line is a command followed by its arguments,\n"
    "separated by spaces.  Commands are looked up in the directories in PATH.\n"
    "Lines starting with # are ignored.  $NAME and ${NAME} are replaced with\n"
    "the variable's value, $? with the last command's exit status, and \\$\n"
    "with $.\n"
    "\n"
    "  command &          run a command in the background\n"
    "  command | command  send one command's output to the next one's input\n"
    "\n"
    "Up and Down recall earlier commands, which are kept in\n"
    "$HOME/.mush_history.\n"
    "\n"
    "A script is a file of command lines, run with \"mush script\" or by\n"
    "naming it like a command.  Changes it makes to the environment are undone\n"
    "when it ends, and exit ends it.  A login shell first runs\n"
    "$HOME/" MUSH_PROFILE_NAME ", whose changes are kept.\n"
    "\n"
    "Built-in commands:\n"
    "  cd [directory]  change the working directory; cd alone goes to HOME\n"
    "                  and \"cd -\" goes back to the previous directory\n"
    "  pwd             print the working directory\n"
    "  export NAME=value\n"
    "                  set an environment variable\n"
    "  exit            exit the shell\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

/// @fn static const char* lookupVariable(const char *name, size_t nameLength)
///
/// @brief Look up an environment variable by a name that isn't NUL-terminated.
///
/// @param name The start of the variable's name.
/// @param nameLength The number of characters in the name.
///
/// @return Returns the variable's value, or an empty string if it isn't set.
static const char* lookupVariable(const char *name, size_t nameLength) {
  for (char **variable = environ; (variable != NULL) && (*variable != NULL);
    variable++
  ) {
    if ((strncmp(*variable, name, nameLength) == 0)
      && ((*variable)[nameLength] == '=')
    ) {
      return &(*variable)[nameLength + 1];
    }
  }
  return "";
}

/// @fn static bool isNameCharacter(char c, bool first)
///
/// @brief Determine whether a character can be part of a variable's name.
///
/// @param c The character to check.
/// @param first Whether the character would be the first one in the name,
///   which can't be a digit.
///
/// @return Returns true if the character can be in the name, false if not.
static bool isNameCharacter(char c, bool first) {
  return ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'))
    || (c == '_') || (!first && (c >= '0') && (c <= '9'));
}

/// @fn static size_t expandVariables(const char *line, char *expanded,
///   const char *status)
///
/// @brief Replace each $NAME, ${NAME}, and $? in a command line with its
/// value.  \$ is a literal $.
///
/// @param line The command line to expand.
/// @param expanded The buffer to write the expanded line to, or NULL to only
///   measure it.
/// @param status The exit status of the last command, as a string.
///
/// @return Returns the length of the expanded line, not counting its NUL
/// terminator.
static size_t expandVariables(const char *line, char *expanded,
  const char *status
) {
  size_t length = 0;
  while (*line != '\0') {
    const char *value = line;
    size_t valueLength = 1;
    const char *next = &line[1];
    if ((*line == '\\') && (line[1] == '$')) {
      value = &line[1];
      next = &line[2];
    } else if (*line == '$') {
      const char *name = &line[1];
      bool braced = (*name == '{');
      if (braced) {
        name++;
      }
      size_t nameLength = 0;
      if (*name == '?') {
        nameLength = 1;
      } else {
        while (isNameCharacter(name[nameLength], nameLength == 0)) {
          nameLength++;
        }
      }
      if ((nameLength > 0) && (!braced || (name[nameLength] == '}'))) {
        value = ((nameLength == 1) && (*name == '?'))
          ? status : lookupVariable(name, nameLength);
        valueLength = strlen(value);
        next = &name[nameLength + (braced ? 1 : 0)];
      }
    }
    if (expanded != NULL) {
      memcpy(&expanded[length], value, valueLength);
    }
    length += valueLength;
    line = next;
  }
  if (expanded != NULL) {
    expanded[length] = '\0';
  }
  return length;
}

/// @fn static int launchFailureStatus(void)
///
/// @brief Get the exit status for a command that couldn't be launched from the
/// errno that launching it left.
///
/// @return Returns 127 if the command wasn't found, 126 if it couldn't be
/// executed, and 1 otherwise.
static int launchFailureStatus(void) {
  if (errno == ENOENT) {
    return 127;
  } else if (errno == ENOEXEC) {
    return 126;
  }
  return 1;
}

int main(int argc, char **argv) {
  int exitStatus = 0;
  char *scriptPath = (argc > 1) ? argv[1] : NULL;
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    scriptPath = NULL;
  } else if ((argc > 1) && (strncmp(argv[1], "--status=", 9) == 0)) {
    // The scheduler passes the exit status of the command that replaced us.
    exitStatus = (int) strtol(&argv[1][9], NULL, 10);
    scriptPath = NULL;
  }
  
  char *buffer = (char*) malloc(MUSH_COMMAND_LINE_LENGTH);
  LineEditorArgs lineEditorArgs = {
    .line = buffer,
    .size = MUSH_COMMAND_LINE_LENGTH,
    .prompt = '$',
    .afterReturn = false,
    .returnValue = 0,
  };
  
  intptr_t returnValue = 0;
  FsCommandArgs fsCommandArgs;
  
  if (strcmp(getenv("UID"), "0") == 0) {
    lineEditorArgs.prompt = '#';
  }
  
  ScriptArgs scriptArgs = {
    .line = buffer,
    .size = MUSH_COMMAND_LINE_LENGTH,
    .path = scriptPath,
    .isProfile = false,
    .inBackground = false,
  };
  // login starts the shell as "-mush"; restarts after a command use "mush".
  char *home = getenv("HOME");
  if ((argv[0][0] == '-') && (home != NULL)) {
    snprintf(buffer, MUSH_COMMAND_LINE_LENGTH, "%s/" MUSH_PROFILE_NAME, home);
    scriptArgs.path = buffer;
    scriptArgs.isProfile = true;
  }
  if ((scriptArgs.path != NULL) && (callOverlayFunction(
      OVERLAY_SAME_NAMESPACE, "Script", "startScript", &scriptArgs) == NULL)
    && !scriptArgs.isProfile
  ) {
    exitStatus = 1;
  }
  
  do {
    bool fromScript = false;
    if ((getenv("MUSH_SCRIPT1") != NULL) && (callOverlayFunction(
      OVERLAY_SAME_NAMESPACE, "Script", "readScriptLine", &scriptArgs)
      != NULL)
    ) {
      fromScript = true;
    } else if (callOverlayFunction(
      OVERLAY_SAME_NAMESPACE, "LineEditor", "readLine", &lineEditorArgs)
      == NULL
    ) {
      // Without the line editor, input still has to be read so exit works.
      printf("%c ", lineEditorArgs.prompt);
      lineEditorArgs.returnValue = (fgets(lineEditorArgs.line,
        MUSH_COMMAND_LINE_LENGTH, stdin) != NULL) ? 0 : -1;
      lineEditorArgs.line[strcspn(lineEditorArgs.line, "\r\n")] = '\0';
    }
    if (lineEditorArgs.returnValue != 0) {
      // Our stdin file descriptor has been closed.  Bail.
      break;
    }
    char *input = lineEditorArgs.line;
#ifdef MUSH_DEBUG
    size_t inputLength = strlen(input);
    printDebug("inputLength = %d\n", inputLength);
    printDebug("Read ");
    for (size_t ii = 0; ii < inputLength; ii++) {
        printDebug("0x%x ", input[ii]);
    }
#endif // MUSH_DEBUG
    printDebug("\n");
    
    // We need to check for exit outside of processing it as a built-in.  This
    // is because, if the system gets into a bad state, we can fail to load
    // overlays.  We should always be able to exit and release all the process
    // resources.
    input = &input[strspn(input, " \t")];
    if ((*input == '\0') || (*input == '#')) {
      continue;
    } else if (strcmp(input, "exit") == 0) {
      if (fromScript) {
        callOverlayFunction(
          OVERLAY_SAME_NAMESPACE, "Script", "endScript", NULL);
        continue;
      }
      break;
    }
    
    char *expanded = NULL;
    if (strchr(input, '$') != NULL) {
      char status[4];
      snprintf(status, sizeof(status), "%d", exitStatus);
      expanded = (char*) malloc(expandVariables(input, NULL, status) + 1);
      if (expanded == NULL) {
        fputs("mush: Out of memory\n", stderr);
        exitStatus = 1;
        continue;
      }
      expandVariables(input, expanded, status);
      input = &expanded[strspn(expanded, " \t")];
      if (*input == '\0') {
        free(expanded);
        continue;
      }
    }
    
    // Attempt to process the command line as a built-in first before looking
    // on the filesystem.
    //
    // The variable 'input' points into 'buffer' or 'expanded', which are
    // dynamic memory.  So, it's safe to pass as a parameter to
    // callOverlayFunction.
    printDebug("Checking to see if command is a builtin\n");
    returnValue = (intptr_t) callOverlayFunction(
      OVERLAY_SAME_NAMESPACE, "Builtins", "processBuiltin", input);
    if (returnValue >= 0) {
      exitStatus = (int) returnValue;
    } else if (returnValue < -1)  {
      // The command wasn't processed as a built-in.  Try running it from the
      // filesystem.
      printDebug("Command is *NOT* a builtin\n");
      fsCommandArgs.scriptPath = NULL;
      if (strchr(input, '|')) {
        // Command line contains pipes.  Process it that way.
        fsCommandArgs.commandLine = input;
        fsCommandArgs.launchBackground = false;
        fsCommandArgs.fileActions = NULL;
        fsCommandArgs.numPipes = 0;
        fsCommandArgs.pids = NULL;
        fsCommandArgs.pipes[0] = NULL;
        fsCommandArgs.pipes[1] = NULL;
        fsCommandArgs.numProcessesLaunched = 0;
        fsCommandArgs.pipeIndex = 0;
        fsCommandArgs.stdinDup = -1;
        errno = 0;
        FsCommandArgs *nextCommand = &fsCommandArgs;
        while (nextCommand == &fsCommandArgs) {
          nextCommand = (FsCommandArgs*) callOverlayFunction(
            OVERLAY_SAME_NAMESPACE, "Pipes", "processPipes", &fsCommandArgs);
          nextCommand = callOverlayFunction(
            OVERLAY_SAME_NAMESPACE, "FilesystemCommands", "runFsCommand",
            nextCommand);
        }
        if (fsCommandArgs.stdinDup >= 0) {
          // The last command didn't replace the shell, so take stdin back.
          dup2(fsCommandArgs.stdinDup, STDIN_FILENO);
          close(fsCommandArgs.stdinDup);
        }
        exitStatus = launchFailureStatus();
        scriptArgs.inBackground = true;
      } else {
        fsCommandArgs.commandLine = input;
        fsCommandArgs.launchBackground = false;
        char *ampersandAt = strrchr(input, '&');
        if ((ampersandAt != NULL) && (ampersandAt[-1] != '&')) {
          *ampersandAt = '\0';
          fsCommandArgs.launchBackground = true;
        }
        fsCommandArgs.fileActions = NULL;
        fsCommandArgs.numPipes = 0;
        fsCommandArgs.pids = NULL;
        fsCommandArgs.pipes[0] = NULL;
        fsCommandArgs.pipes[1] = NULL;
        fsCommandArgs.numProcessesLaunched = 0;
        fsCommandArgs.pipeIndex = 0;
        errno = 0;
        returnValue = (intptr_t) callOverlayFunction(
          OVERLAY_SAME_NAMESPACE, "FilesystemCommands", "runFsCommand",
          &fsCommandArgs);
        exitStatus = (returnValue != 0) ? 0 : launchFailureStatus();
        scriptArgs.inBackground = fsCommandArgs.launchBackground;
      }
      if (fsCommandArgs.scriptPath != NULL) {
        scriptArgs.path = fsCommandArgs.scriptPath;
        scriptArgs.isProfile = false;
        exitStatus = (callOverlayFunction(
          OVERLAY_SAME_NAMESPACE, "Script", "startScript", &scriptArgs)
          != NULL) ? 0 : 1;
        free(fsCommandArgs.scriptPath);
      }
    }
    free(expanded);
  } while (returnValue != -1);
  free(buffer);
  
  printf("Gracefully exiting %s\n", argv[0]);
  return 0;
}

