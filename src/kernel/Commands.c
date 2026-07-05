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

/// @file Commands.c
///
/// @brief Built-in commands when not running a shell from the filesystem.

// Standard C includes.
#define FILE C_FILE
#define gid_t C_gid_t
#define uid_t C_uid_t
#define pid_t C_pid_t
#include "stdio.h"
#undef FILE
#undef gid_t
#undef uid_t
#undef pid_t
#include "string.h"

// NanoOs includes.
#include "Commands.h"
#include "Console.h"
#include "NanoOs.h"
#include "Processes.h"
#include "Scheduler.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsHardware.h"
#include "../user/NanoOsSpawn.h"

// Must come last
#include "../user/NanoOsStdio.h"

// Defined in NanoOs.c:
extern const User users[];
extern const int NUM_USERS;

// Defined at the bottom of this file:
extern const CommandEntry commands[];
extern const int NUM_COMMANDS;

// Defined in Processes.c:
char** parseArgs(char *command, int *argc);

// Commands

/// @fn int psCommandHandler(int argc, char **argv);
///
/// @brief Display a list of running processes and their process IDs.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.
///
/// @return This function always returns 0.
int psCommandHandler(int argc, char **argv) {
  (void) argc;
  (void) argv;
  int returnValue = 0;

  printf("- Dynamic memory left: %d\n", getFreeMemory());

  ProcessInfo *processInfo = schedulerGetProcessInfo();
  if (processInfo != NULL) {
    uint8_t numRunningProcesses = processInfo->numProcesses;
    ProcessInfoElement *processes = processInfo->processes;
    for (uint8_t ii = 0; ii < numRunningProcesses; ii++) {
      printf("%d  %s %s\n",
        processes[ii].pid,
        getUsernameByUserId(processes[ii].userId),
        processes[ii].name);
    }
    free(processInfo); processInfo = NULL;
  } else {
    printf("ERROR: Could not get process information from scheduler.\n");
  }

  printf("- Dynamic memory left: %d\n", getFreeMemory());
  return returnValue;
}

/// @fn int killCommandHandler(int argc, char **argv);
///
/// @brief Kill a running process identified by its process ID.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.
///
/// @return Returns 0 on success, 1 on failure.
int killCommandHandler(int argc, char **argv) {
  (void) argc;
  (void) argv;

  if (argc < 2) {
    printf("Usage:\n");
    printf("  kill <process ID>\n");
    printf("\n");
    return 1;
  }
  ProcessId processId = (ProcessId) strtol(argv[1], NULL, 10);

  int returnValue = schedulerKillProcess(processId);

  return returnValue;
}

/// @fn int echoCommandHandler(int argc, char **argv);
///
/// @brief Echo a command line from the user back to the console output.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.  They will be echoed back to the console
///   output separated by a space.
///
/// @return This function always returns 0.
int echoCommandHandler(int argc, char **argv) {
  for (int ii = 1; ii < argc; ii++) {
    fputs(argv[ii], stdout);
    if (ii < (argc - 1)) {
      fputs(" ", stdout);
    }
  }
  fputs("\n", stdout);

  return 0;
}

/// @fn int grepCommandHandler(int argc, char **argv);
///
/// @brief Echo a line of text from standard input to the console output if it
/// contains the string the user is looking for.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.  They will be greped back to the console
///   output separated by a space.
///
/// @return This function always returns 0.
int grepCommandHandler(int argc, char **argv) {
  char buffer[96];

  if (argc < 2) {
    printf("Usage:  %s <string to find>\n", argv[0]);
    return 1;
  }

  while (fgets(buffer, sizeof(buffer), stdin)) {
    if (strstr(buffer, argv[1])) {
      fputs(buffer, stdout);
    }
  }

  if ((strlen(buffer) > 0) && (buffer[strlen(buffer) - 1] != '\n')) {
    fputs("\n", stdout);
  }

  return 0;
}

/// @fn int helloworldCommandHandler(int argc, char **argv);
///
/// @brief Run the "helloworld" command from the filesystem.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.
///
/// @return This function always returns 0.
int helloworldCommandHandler(int argc, char **argv) {
  (void) argc;
  (void) argv;
  printf("Hello, world!\n");
  
  return 0;
}

/// @fn int helpCommandHandler(int argc, char **argv);
///
/// @brief Print the help strings for all the commands in the system.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.
///
/// @return This function always returns 0.
int helpCommandHandler(int argc, char **argv) {
  (void) argc;
  (void) argv;
  char formatString[19];

  size_t maxCommandNameLength = 0;
  for (int ii = 0; ii < NUM_COMMANDS; ii++) {
    size_t commandNameLength = strlen(commands[ii].name);
    if (commandNameLength > maxCommandNameLength) {
      maxCommandNameLength = commandNameLength;
    }
  }
  maxCommandNameLength++;
  sprintf(formatString, "%%-%us %%s\n", (unsigned int) maxCommandNameLength);

  char *commandName = (char*) malloc(maxCommandNameLength + 2);
  for (int ii = 0; ii < NUM_COMMANDS; ii++) {
    strcpy(commandName, commands[ii].name);
    strcat(commandName, ":");
    const char *help = commands[ii].help;
    printf(formatString, commandName, help);
  }
  commandName = stringDestroy(commandName);

  return 0;
}

/// @fn int logoutCommandHandler(int argc, char **argv)
///
/// @brief Logout of a running shell.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.  Ignored by this function.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.  Ignored by this function.
///
/// @return This function always returns 0.
int logoutCommandHandler(int argc, char **argv) {
  (void) argc;
  (void) argv;

  if (schedulerSetProcessUser(NO_USER_ID) != 0) {
    fputs("WARNING: Could not clear owner of current process.\n", stderr);
  }

  return 0;
}

/// @fn int looseLoopCommandHandler(int argc, char **argv)
///
/// @brief Run a loop WITH yielding to test cooperative multitasking.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.  Ignored by this function.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.  Ignored by this function.
///
/// @return This function always returns 0.
int looseLoopCommandHandler(int argc, char **argv) {
  (void) argc;
  (void) argv;

  while (1) processYield();

  return 0;
}

/// @fn int shutdownCommandHandler(int argc, char **argv)
///
/// @brief Shutdown the hardware.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.  Ignored by this function.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.  Ignored by this function.
///
/// This function never returns, but would return 0 if it did.
int shutdownCommandHandler(int argc, char **argv) {
  int returnValue = 1;

  if (argc < 2) {
    goto usage;
  }

  NanoOsShutdownType shutdownType = NANO_OS_SHUTDOWN_NUM_TYPES;
  if (strcmp(argv[1], "-r") == 0) {
    shutdownType = NANO_OS_SHUTDOWN_RESET;
  } else if (strcmp(argv[1], "-h") == 0) {
    shutdownType = NANO_OS_SHUTDOWN_OFF;
  }

  if (shutdownType == NANO_OS_SHUTDOWN_NUM_TYPES) {
    goto usage;
  }

  SchedulerShutdownArgs schedulerShutdownArgs = {
    .shutdownType = shutdownType,
    .returnValue = 0,
  };
  ProcessMessage *processMessage
    = initSendProcessMessageToPid(
    SCHEDULER_STATE->schedulerPid,
    SCHEDULER_COMMAND_SIGNATURE | SCHEDULER_SHUTDOWN,
    /* data= */ &schedulerShutdownArgs,
    /* size= */ sizeof(schedulerShutdownArgs),
    true);
  if (processMessage == NULL) {
    fprintf(stderr, "ERROR: Could not communicate with scheduler.\n");
    return returnValue; // 1
  }

  processMessageWaitForDone(processMessage, NULL);
  processMessageRelease(processMessage);

  returnValue = schedulerShutdownArgs.returnValue;

  return returnValue;

usage:
  fprintf(stderr, "Usage: %s <mode>\n", argv[0]);
  fprintf(stderr, "\n");
  fprintf(stderr, "modes:\n");
  fprintf(stderr, "-r    Reset the system\n");
  fprintf(stderr, "-h    Halt the system\n");
  fprintf(stderr, "\n");
  return returnValue;
}

/// @fn int tightLoopCommandHandler(int argc, char **argv)
///
/// @brief Run a loop WITHOUT yielding to test preemptive multitasking.
///
/// @param argc The number or arguments parsed from the command line, including
///   the name of the command.  Ignored by this function.
/// @param argv The array of arguments parsed from the command line with one
///   argument per array element.  Ignored by this function.
///
/// This function never returns, but would return 0 if it did.
int tightLoopCommandHandler(int argc, char **argv) {
  (void) argc;
  (void) argv;

  while (1);

  return 0;
}

/// @fn const CommandEntry* getCommandEntryFromInput(char *consoleInput)
///
/// @brief Get the command specified by consoleInput.
///
/// @param consoleInput The raw input captured in the console buffer.
///
/// @return Returns a pointer to the found CommandEntry on success, NULL on
/// failure.
const CommandEntry* getCommandEntryFromInput(char *consoleInput) {
  const CommandEntry *commandEntry = NULL;
  if (*consoleInput != '\0') {
    int searchIndex = NUM_COMMANDS >> 1;
    size_t commandNameLength = strcspn(consoleInput, " \t\r\n&");
    for (int ii = 0, jj = NUM_COMMANDS - 1; ii <= jj;) {
      const char *commandName = commands[searchIndex].name;
      int comparisonValue
        = strncmp(commandName, consoleInput, commandNameLength);
      if (comparisonValue == 0) {
        // We need an exact match.  So, the character at index commandNameLength
        // of commandName needs to be zero.  If it's anything else, we don't
        // have an exact match and we need to continue our search.  Since the
        // order we're comparing is commandName - consoleInput, what we're
        // asserting is that consoleInput[commandNameLength] is a NULL byte (0).
        // However, it isn't really because of the whitespace inherent to
        // commands.  So, we can't do a literal subtration here.  However, since
        // it's assumed to be 0, this is the same as
        // commandName[commandNameLength] - 0, or simply
        // commandName[commandNameLength].  So, just use that value as the final
        // comparison value here.
        comparisonValue = ((int) commandName[commandNameLength]);
      }

      if (comparisonValue == 0) {
        commandEntry = &commands[searchIndex];
        break;
      } else if (comparisonValue < 0) {
        ii = searchIndex + 1;
      } else { // comparisonValue > 0
        jj = searchIndex - 1;
      }

      searchIndex = (ii + jj) >> 1;
    }
  }

  return commandEntry;
}

// Exported functions

/// @fn void* execBuiltinCommand(void *args)
///
/// @brief Wrapper function that calls a built-in command.
///
/// @param args A pointer to an ExecArgs structure describing the command to
///   run, cast to a void*.
///
/// @return If the comamnd is run, returns the result of the command cast to a
/// void*.  If the command is not run, returns -1 cast to a void*.
void* execBuiltinCommand(void *args) {
  // The scheduler may be suspended because of launching this process.
  // Immediately call processYield as a best practice to make sure the scheduler
  // goes back to its work.
  ExecArgs *execArgs = (ExecArgs*) args;
  if (execArgs == NULL) {
    printString("ERROR: No arguments provided to execOverlayCommand.\n");
    releaseConsole();
    return (void*) ((intptr_t) -1);
  }
  // Let the caller finish its work.
  processYield();
  char *pathname = execArgs->pathname;
  char **argv = execArgs->argv;

  if ((argv == NULL) || (argv[0] == NULL)) {
    // Fail.
    printString("ERROR: Invalid argv.\n");
    releaseConsole();
    return (void*) ((intptr_t) -1);
  }
  int argc = 0;
  for (; argv[argc] != NULL; argc++);

  // Load the overlay information into the ProcessDescriptor.
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor == NULL) {
    // This should be impossible.
    printString("ERROR: No running process.\n");
    releaseConsole();
    return (void*) ((intptr_t) -1);
  }

  FileDescriptor **fileDescriptors = processDescriptor->fileDescriptors;
  while (
      ((fileDescriptors[0]->pipeEnd != NULL)
      && (fileDescriptors[0]->inputChannel.pid == PROCESS_ID_NOT_SET))
    || ((fileDescriptors[1]->pipeEnd != NULL)
      && (fileDescriptors[1]->outputChannel.pid == PROCESS_ID_NOT_SET))
    || ((fileDescriptors[2]->pipeEnd != NULL)
      && (fileDescriptors[2]->outputChannel.pid == PROCESS_ID_NOT_SET))
  ) {
    // We've been spawned via posix_spawn from a command line that contains
    // pipes and the pipes haven't been setup yet.  We need to wait until that
    // process completes, otherwise either we'll miss input or the process
    // downstream will.
    processYield();
  }

  printDebugString("Call the process function\n");
  const CommandEntry *commandEntry = getCommandEntryFromInput(pathname);
  if (commandEntry == NULL) {
    fprintf(stderr, "ERROR: CommandEntry Not found for command \"%s\"\n",
      pathname);
    return (void*) ((intptr_t) -1);
  }
  int returnValue = commandEntry->func(argc, argv);

  releaseConsole();

  // ***DO NOT*** attempt to free the ExecArgs that were passed in, period.
  // The memory will be cleaned up by the scheduler after we exit.  Freeing
  // that memory here can result in nasty consequences if we get preempted
  // between freeing the memory and returning from this function.
  return (void*) ((intptr_t) returnValue);
}

/// @def USERNAME_BUFFER_SIZE
///
/// @brief Size to use for the username buffer in the login function.
#define USERNAME_BUFFER_SIZE 16

/// @def PASSWORD_BUFFER_SIZE
///
/// @brief Size to use for the password buffer in the login function.
#define PASSWORD_BUFFER_SIZE 16

/// @fn void login(void)
///
/// @brief Authenticate a user for login.  Sets the owner of the current task
/// to the ID of the authenticated user before returning.
///
/// @param This function returns no value.
void login(void) {
  UserId userId = NO_USER_ID;

  char *username = (char*) malloc(USERNAME_BUFFER_SIZE);
  char *password = (char*) malloc(PASSWORD_BUFFER_SIZE);
  char *newlineAt = NULL;
  size_t usernameLength = 0, passwordLength = 0, ii = 0;

  while (userId == NO_USER_ID) {
    unsigned int checksum = 0;

    fputs("login: ", stdout);
    fgets(username, USERNAME_BUFFER_SIZE, stdin);
    setConsoleEcho(false);
    fputs("Password: ", stdout);
    fgets(password, PASSWORD_BUFFER_SIZE, stdin);
    setConsoleEcho(true);
    fputs("\n\n", stdout);

    newlineAt = strchr(username, '\r');
    if (newlineAt == NULL) {
      newlineAt = strchr(username, '\n');
    }
    if (newlineAt != NULL) {
      // Terminate the string at the newline.
      *newlineAt = '\0';
    }
    usernameLength = strlen(username);
    for (ii = 0; ii < usernameLength; ii++) {
      checksum += (unsigned int) username[ii];
    }

    newlineAt = strchr(password, '\r');
    if (newlineAt == NULL) {
      newlineAt = strchr(password, '\n');
    }
    if (newlineAt != NULL) {
      // Terminate the string at the newline.
      *newlineAt = '\0';
    }
    passwordLength = strlen(password);
    for (ii = 0; ii < passwordLength; ii++) {
      checksum += (unsigned int) password[ii];
    }

    for (int ii = 0; ii < NUM_USERS; ii++) {
      if (strcmp(users[ii].username, username) == 0) {
        if (users[ii].checksum == checksum) {
          userId = users[ii].userId;
        }
        break;
      }
    }

    if (userId == NO_USER_ID) {
      fputs("Login incorrect\n", stderr);
    }
  }

  username = stringDestroy(username);
  password = stringDestroy(password);

  if (schedulerSetProcessUser(userId) != 0) {
    fputs("WARNING: "
      "Could not set owner of current task to authenticated user.\n",
      stderr);
  }

  return;
}

/// @fn void* runBuiltinShell(void *args)
///
/// @brief Process function for interactive user shell.
///
/// @param args Any arguments passed by the scheduler.  Ignored by this
///   function.
///
/// @return This function never exits, but would return NULL if it did.
void* runBuiltinShell(void *args) {
  (void) args;
  char commandBuffer[CONSOLE_BUFFER_SIZE];
  int consolePort = getOwnedConsolePort();
  while (consolePort < 0) {
    processYield();
    consolePort = getOwnedConsolePort();
  }

  if (getRunningProcess()->userId == NO_USER_ID) {
    printf("\nNanoOs " NANO_OS_VERSION " %s console %d\n\n",
      SCHEDULER_STATE->hostname, consolePort);
    login();
  }

  const char *prompt = "$";
  if (getRunningProcess()->userId == ROOT_USER_ID) {
    prompt = "#";
  }
  const char *processUsername
    = getUsernameByUserId(getRunningProcess()->userId);
  while (1) {
    printf("%s@%s built-in%s ",
      processUsername, SCHEDULER_STATE->hostname, prompt);
    commandBuffer[0] = '\0';
    char *input = fgets(commandBuffer, sizeof(commandBuffer), stdin);
    if (input == NULL) {
    }
    size_t inputLength = strlen(input);
    if ((inputLength > 0) && (input[inputLength - 1] == '\n')) {
      input[inputLength - 1] = '\0';
    }
    
    input = &input[strspn(input, " \t")];
    if (*input == '\0') {
      continue;
    } else if (strcmp(input, "exit") == 0) {
      getRunningProcess()->userId = NO_USER_ID;
      break;
    }
    
    char *ampersandAt = strrchr(input, '&');
    bool launchBackground = false;
    if ((ampersandAt != NULL) && (ampersandAt[-1] != '&')) {
      *ampersandAt = '\0';
      launchBackground = true;
    }
    
    char **argv = parseArgs(input, NULL);
    if (argv == NULL) {
      fprintf(stderr, "Failed to parse command line\n");
      continue;
    }
    
    // Run the built-in command.
    if (launchBackground == false) {
      // Run the command in the foreground.  i.e. Replace this shell.  This is
      // the usual case.
      schedulerExecve(argv[0], argv, getRunningProcess()->envp);
    } else { // launchBackground == true
      // Spawn a new task in the background.
      pid_t pid;
      errno = nanoOsSpawn(&pid, argv[0], NULL, NULL, argv,
        getRunningProcess()->envp);
    }
  }

  return NULL;
}

/// @var commands
///
/// @brief Array of CommandEntry values that contain the names of the commands,
/// a pointer to the command handler functions, and a help string that
/// summarizes the command.
///
/// @details
/// REMINDER:  These commands have to be in alphabetical order so that the
///            binary search will work:
const CommandEntry commands[] = {
  {
    .name = "echo",
    .func = echoCommandHandler,
    .help = "Echo a string back to the console."
  },
  {
    .name = "exit",
    .func = logoutCommandHandler,
    .help = "Exit the current shell."
  },
  {
    .name = "grep",
    .func = grepCommandHandler,
    .help = "Find text in piped output."
  },
  {
    .name = "helloworld",
    .func = helloworldCommandHandler,
    .help = "Run the \"helloworld\" command from the filesystem."
  },
  {
    .name = "help",
    .func = helpCommandHandler,
    .help = "Print this help message."
  },
  {
    .name = "kill",
    .func = killCommandHandler,
    .help = "Kill a running process."
  },
  {
    .name = "logout",
    .func = logoutCommandHandler,
    .help = "Logout of the system."
  },
  {
    .name = "looseLoop",
    .func = looseLoopCommandHandler,
    .help = "Run a process in a loop that does yield."
  },
  {
    .name = "ps",
    .func = psCommandHandler,
    .help = "List the running processes."
  },
  {
    .name = "shutdown",
    .func = shutdownCommandHandler,
    .help = "Shutdown the system."
  },
  {
    .name = "tightLoop",
    .func = tightLoopCommandHandler,
    .help = "Run a process in a loop that does not yield."
  },
};

/// @var NUM_COMMANDS
///
/// @brief Integer constant value that holds the number of commands in the
/// commands array above.  Used in the binary search that looks up commands by
/// their names.
const int NUM_COMMANDS = sizeof(commands) / sizeof(commands[0]);

