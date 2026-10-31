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
#include <signal.h>
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
    "Usage: mush\n"
    "\n"
    "Run the MUSH shell.  Each line is a command followed by its arguments,\n"
    "separated by spaces.  Commands are looked up in the directories in PATH.\n"
    "Lines starting with # are ignored.\n"
    "\n"
    "  command &          run a command in the background\n"
    "  command | command  send one command's output to the next one's input\n"
    "\n"
    "Up and Down recall earlier commands, which are kept in\n"
    "$HOME/.mush_history.\n"
    "\n"
    "A login shell first runs $HOME/" MUSH_PROFILE_NAME ", which can only use\n"
    "built-in commands.\n"
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

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    // Exiting would log the user out; a killed shell is restarted instead.
    kill(getpid(), SIGKILL);
    return 0;
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
  
  // login starts the shell as "-mush"; restarts after a command use "mush".
  FILE *profile = NULL;
  int profileLine = 0;
  char *home = getenv("HOME");
  if ((argv[0][0] == '-') && (home != NULL)) {
    snprintf(buffer, MUSH_COMMAND_LINE_LENGTH, "%s/" MUSH_PROFILE_NAME, home);
    profile = fopen(buffer, "r");
  }
  
  do {
    if (profile != NULL) {
      if (fgets(lineEditorArgs.line, MUSH_COMMAND_LINE_LENGTH, profile)
        == NULL
      ) {
        fclose(profile);
        profile = NULL;
        continue;
      }
      profileLine++;
      if ((strchr(lineEditorArgs.line, '\n') == NULL) && !feof(profile)) {
        fprintf(stderr, MUSH_PROFILE_NAME ": line %d: too long\n",
          profileLine);
        while ((fgets(lineEditorArgs.line, MUSH_COMMAND_LINE_LENGTH, profile)
          != NULL) && (strchr(lineEditorArgs.line, '\n') == NULL));
        continue;
      }
      lineEditorArgs.line[strcspn(lineEditorArgs.line, "\r\n")] = '\0';
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
      break;
    }
    
    // Attempt to process the command line as a built-in first before looking
    // on the filesystem.
    //
    // The variable 'input' is the same as the variable 'buffer', which is a
    // pointer to dynamic memory.  So, it's safe to pass as a parameter to
    // callOverlayFunction.
    printDebug("Checking to see if command is a builtin\n");
    returnValue = (intptr_t) callOverlayFunction(
      OVERLAY_SAME_NAMESPACE, "Builtins", "processBuiltin", input);
    if (returnValue < -1)  {
      // The command wasn't processed as a built-in.  Try running it from the
      // filesystem.
      printDebug("Command is *NOT* a builtin\n");
      if (profile != NULL) {
        // Running a command would exec over the shell and end the profile.
        if (returnValue == -2) {
          fprintf(stderr,
            MUSH_PROFILE_NAME ": line %d: %.*s: not a built-in command\n",
            profileLine, (int) strcspn(input, " \t"), input);
        }
      } else if (strchr(input, '|')) {
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
        errno = 0;
        FsCommandArgs *nextCommand = &fsCommandArgs;
        while (nextCommand == &fsCommandArgs) {
          nextCommand = (FsCommandArgs*) callOverlayFunction(
            OVERLAY_SAME_NAMESPACE, "Pipes", "processPipes", &fsCommandArgs);
          nextCommand = callOverlayFunction(
            OVERLAY_SAME_NAMESPACE, "FilesystemCommands", "runFsCommand",
            nextCommand);
        }
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
      }
    }
  } while (returnValue != -1);
  if (profile != NULL) {
    fclose(profile);
  }
  free(buffer);
  
  printf("Gracefully exiting %s\n", argv[0]);
  return 0;
}

