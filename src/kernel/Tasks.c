////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file

// Custom includes
#include "Console.h"
#include "Hal.h"
#include "NanoOs.h"
#include "OverlayFunctions.h"
#include "Tasks.h"
#include "Scheduler.h"
#include "../user/NanoOsLibC.h"
#include "../user/NanoOsPwd.h"

// Must come last
#include "../user/NanoOsStdio.h"

/// @var messages
///
/// @brief Pointer to the array of task messages that will be stored in the
/// scheduler function's stack.
TaskMessage *messages = NULL;

/// @var nanoOsMessages
///
/// @brief Pointer to the array of NanoOsMessages that will be stored in the
/// scheduler function's stack.
NanoOsMessage *nanoOsMessages = NULL;

/// @fn char** stringArrayDestroy(char **stringArray)
///
/// @brief Destroy a NULL-terminated array of C strings.
///
/// @param stringArray An array of C strings that's terminated with a NULl
///   pointer.  This parameter may be NULL.
///
/// @return This function always succeeds and always returns NULL.
char** stringArrayDestroy(char **stringArray) {
  if (stringArray != NULL) {
    for (int ii = 0; stringArray[ii] != NULL; ii++) {
      free(stringArray[ii]);
    }
    free(stringArray);
  }
  
  return NULL;
}

/// @fn ExecArgs* execArgsDestroy(ExecArgs *execArgs)
///
/// @brief Free all of an ExecArgs structure.
///
/// @param execArgs A pointer to an ExecArgs structure.
///
/// @return This function always succeeds and always returns NULL.
ExecArgs* execArgsDestroy(ExecArgs *execArgs) {
  free(execArgs->pathname);

  execArgs->argv = stringArrayDestroy(execArgs->argv);
  execArgs->envp = stringArrayDestroy(execArgs->envp);

  // We don't need to and SHOULD NOT touch execArgs->schedulerState.

  free(execArgs);
  return NULL;
}

/// @fn SpawnArgs* spawnArgsDestroy(SpawnArgs *execArgs)
///
/// @brief Free all of an SpawnArgs structure.
///
/// @param spawnArgs A pointer to an SpawnArgs structure.
///
/// @return This function always succeeds and always returns NULL.
SpawnArgs* spawnArgsDestroy(SpawnArgs *spawnArgs) {
  free(spawnArgs->path);

  spawnArgs->argv = stringArrayDestroy(spawnArgs->argv);
  spawnArgs->envp = stringArrayDestroy(spawnArgs->envp);

  // We don't need to and SHOULD NOT touch spawnArgs->schedulerState.

  free(spawnArgs);
  return NULL;
}

/// @fn int getNumTokens(const char *input)
///
/// @brief Get the number of whitespace-delimited tokens in a string.
///
/// @param input A pointer to the input string to consider.
///
/// @return Returns the number of tokens discovered.
int getNumTokens(const char *input) {
  int numTokens = 0;
  if (input == NULL) {
    return numTokens;
  }

  while (*input != '\0') {
    numTokens++;
    input = &input[strcspn(input, " \t\r\n")];
    input = &input[strspn(input, " \t\r\n")];
  }

  return numTokens;
}

/// @fn int getNumLeadingBackslashes(char *strStart, char *strPos)
///
/// @brief Get the number of backslashes that precede a character.
///
/// @param strStart A pointer to the start of the string the character is in.
/// @param strPos A pointer to the character to look before.
int getNumLeadingBackslashes(char *strStart, char *strPos) {
  int numLeadingBackslashes = 0;

  strPos--;
  while ((((uintptr_t) strPos) >= ((uintptr_t) strStart))
    && (*strPos == '\\')
  ) {
    numLeadingBackslashes++;
    strPos--;
  }

  return numLeadingBackslashes;
}

/// @fn char *findEndQuote(char *input, char quote)
///
/// @brief Find the first double quote that is not escaped.
///
/// @brief input A pointer to the beginning of the string to search.
///
/// @return Returns a pointer to the end quote on success, NULL on failure.
char *findEndQuote(char *input, char quote) {
  char *quoteAt = strchr(input, quote);
  while ((quoteAt != NULL)
    && (getNumLeadingBackslashes(input, quoteAt) & 1)
  ) {
    input = quoteAt + 1;
    quoteAt = strchr(input, quote);
  }

  return quoteAt;
}

/// @fn char** parseArgs(char *command, int *argc)
///
/// @brief Parse a raw input string from the console into an array of individual
/// strings to pass as the argv array to a command function.
///
/// @param command The raw string of data read from the user's input by the
///   console.  The string is modified in place by this function.
/// @param argc A pointer to the integer where the number of parsed arguments
///   will be stored.
///
/// @return Returns a pointer to an array of strings on success, NULL on
/// failure.
char** parseArgs(char *command, int *argc) {
  char **argv = NULL;

  if (command == NULL) {
    // Failure.
    return argv; // NULL
  }
  char *endOfInput = &command[strlen(command)];

  // First, we need to declare an array that will hold all our arguments.  In
  // order to do this, we need to know the maximum number of arguments we'll be
  // working with.  That will be the number of tokens separated by whitespace.
  int maxNumArgs = getNumTokens(command);
  argv = (char**) malloc((maxNumArgs + 1) * sizeof(char*));
  if (argv == NULL) {
    // Nothing we can do.  Fail.
    return argv; // NULL
  }

  // Next, go through the input and fill in the elements of the argv array with
  // the addresses first letter of each argument and put a NULL byte at the end
  // of each argument.
  int numArgs = 0;
  char *endOfArg = NULL;
  while ((command != endOfInput) && (*command != '\0')) {
    if (*command == '"') {
      command++;
      endOfArg = findEndQuote(command, '"');
    } else if (*command == '\'') {
      command++;
      endOfArg = findEndQuote(command, '\'');
    } else {
      endOfArg = &command[strcspn(command, " \t\r\n")];
    }

    argv[numArgs] = command;
    numArgs++;

    if (endOfArg != NULL) {
      *endOfArg = '\0';
      if (endOfArg != endOfInput) {
        command = endOfArg + 1;
      } else {
        command = endOfInput;
      }
    } else {
      command += strlen(command);
    }

    command = &command[strspn(command, " \t\r\n")];
  }
  argv[numArgs] = NULL;

  if (argc != NULL) {
    *argc = numArgs;
  }

  return argv;
}

/// @fn void* execCommand(void *args)
///
/// @brief Wrapper task function that calls a command function.
///
/// @param args The message received from the console task that describes
///   the command to run, cast to a void*.
///
/// @return If the comamnd is run, returns the result of the command cast to a
/// void*.  If the command is not run, returns -1 cast to a void*.
void* execCommand(void *args) {
  // The scheduler may be suspended because of launching this task.
  // Immediately call taskYield as a best practice to make sure the scheduler
  // goes back to its work.
  ExecArgs *execArgs = (ExecArgs*) args;
  if (execArgs == NULL) {
    printString("ERROR: No arguments message provided to execCommand.\n");
    releaseConsole();
    schedulerCloseAllFileDescriptors();
    return (void*) ((intptr_t) -1);
  }
  // Let the caller finish its work.
  taskYield();
  char *pathname = execArgs->pathname;
  char **argv = execArgs->argv;
  char **envp = execArgs->envp;

  if ((argv == NULL) || (argv[0] == NULL)) {
    // Fail.
    printString("ERROR: Invalid argv.\n");
    releaseConsole();
    schedulerCloseAllFileDescriptors();
    return (void*) ((intptr_t) -1);
  }
  int argc = 0;
  for (; argv[argc] != NULL; argc++);

  // Load the overlay information into the TaskDescriptor.
  TaskDescriptor *taskDescriptor = getRunningTask();
  if (taskDescriptor == NULL) {
    // This should be impossible.
    printString("ERROR: No running task.\n");
    releaseConsole();
    schedulerCloseAllFileDescriptors();
    return (void*) ((intptr_t) -1);
  }

  // We need to construct the full path to the overlay file.  We need the
  // overlay directory, a slash, the name of the overlay (which is "main" in
  // this case), the overlay extension and a trailing NULL byte.
  char *overlayPath
    = (char*) malloc(strlen(taskDescriptor->overlayDir) + OVERLAY_EXT_LEN + 6);
  if (overlayPath == NULL) {
    // Fail.
    printString("ERROR: malloc failure for overlayPath.\n");
    releaseConsole();
    schedulerCloseAllFileDescriptors();
    return (void*) ((intptr_t) -1);
  }
  strcpy(overlayPath, taskDescriptor->overlayDir);
  strcat(overlayPath, "/main");
  strcat(overlayPath, OVERLAY_EXT);

  FileBlockMetadata *overlay
    = (FileBlockMetadata*) malloc(sizeof(FileBlockMetadata));
  if (overlay == NULL) {
    // Fail.
    printString("ERROR: malloc failure for overlayPath.\n");
    free(overlayPath);
    releaseConsole();
    schedulerCloseAllFileDescriptors();
    return (void*) ((intptr_t) -1);
  }

  // Get the overlay information we need.
  if (getFileBlockMetadataFromPath(overlayPath, overlay) != 0) {
    // We can't proceed
    printString("ERROR: malloc failure for overlayPath.\n");
    free(overlay);
    free(overlayPath);
    releaseConsole();
    schedulerCloseAllFileDescriptors();
    return (void*) ((intptr_t) -1);
  }
  printDebugString("Loading overlay prior to task start\n");

  // Do the copy of the overlay block information.
  HAL->timerHal->cancelTimer(SCHEDULER_STATE->preemptionTimer);
  taskDescriptor->overlay.blockDevice = overlay->blockDevice;
  taskDescriptor->overlay.startBlock  = overlay->startBlock;
  taskDescriptor->overlay.numBlocks   = overlay->numBlocks;

  free(overlay); overlay = NULL;
  free(overlayPath); overlayPath = NULL;

  // Yield so that the scheduler will load the overlay.
  taskYield();
  
  // Call the task function.
  int returnValue = runOverlayCommand(pathname, argc, argv);

  if (taskDescriptor->userId != NO_USER_ID) {
    // If this command was a shell task then we need to clear out the user ID
    // and environment variables.  Check.
    char *passwdStringBuffer = NULL;
    struct passwd *pwd = NULL;
    do {
      passwdStringBuffer = (char*) malloc(NANO_OS_PASSWD_STRING_BUF_SIZE);
      if (passwdStringBuffer == NULL) {
        fprintf(stderr,
          "ERROR! Could not allocate space for passwdStringBuffer in %s.\n",
          argv[0]);
        break;
      }
      
      pwd = (struct passwd*) malloc(sizeof(struct passwd));
      if (pwd == NULL) {
        fprintf(stderr,
          "ERROR! Could not allocate space for pwd in %s.\n", argv[0]);
        break;
      }
      
      struct passwd *result = NULL;
      nanoOsGetpwuid_r(taskDescriptor->userId, pwd,
        passwdStringBuffer, NANO_OS_PASSWD_STRING_BUF_SIZE, &result);
      if (result == NULL) {
        fprintf(stderr,
          "Could not find passwd info for uid %d\n", taskDescriptor->userId);
        break;
      }
      
      if (strcmp(pwd->pw_shell, taskDescriptor->overlayDir) != 0) {
        // The task exiting is not the user's shell.  We want to keep the
        // environment variables from being destroyed.
        if (envp != NULL) {
          execArgs->envp = NULL;
          if (schedulerAssignMemory(envp) != 0) {
            fprintf(stderr, "WARNING: Could not assign envp to scheduler\n");
            fprintf(stderr, "Undefined behavior\n");
          }

          for (int ii = 0; envp[ii] != NULL; ii++) {
            if (schedulerAssignMemory(envp[ii]) != 0) {
              fprintf(stderr,
                "WARNING: Could not assign envp[%d] to scheduler\n", ii);
              fprintf(stderr, "Undefined behavior\n");
            }
          }
        }
      } else {
        // This is the user's shell exiting.  Clear the tasks's user ID.
        taskDescriptor->userId = NO_USER_ID;
      }
    } while (0);
    free(pwd);
    free(passwdStringBuffer);
  }

  releaseConsole();

  schedulerCloseAllFileDescriptors();

  // Gracefully clear out our message queue.  We have to do this after closing
  // our file descriptors (which is a blocking call) because some other task
  // may be in the middle of sending us data and if we were to do this first,
  // it could turn around and send us more data again.
  msg_t *msg = taskMessageQueuePop();
  while (msg != NULL) {
    taskMessageSetDone(msg);
    msg = taskMessageQueuePop();
  }

  // ***DO NOT*** attempt to free the ExecArgs that were passed in, period.
  // The memory will be cleaned up by the scheduler after we exit.  Freeing
  // that memory here can result in nasty consequences if we get preempted
  // between freeing the memory and returning from this function.
  return (void*) ((intptr_t) returnValue);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/////////// NOTHING BELOW THIS LINE MAY CALL sendNanoOsMessageTo*: ///////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

/// @fn int sendTaskMessageToTask(
///   TaskDescriptor *taskDescriptor, TaskMessage *taskMessage)
///
/// @brief Get an available TaskMessage, populate it with the specified data,
/// and push it onto a destination task's queue.
///
/// @param taskDescriptor A pointer to the destination task to send the
///   message to.
/// @param taskMessage A pointer to the message to send to the destination
///   task.
///
/// @return Returns taskSuccess on success, taskError on failure.
int sendTaskMessageToTask(
  TaskDescriptor *taskDescriptor, TaskMessage *taskMessage
) {
  int returnValue = taskSuccess;
  if ((taskDescriptor == NULL) || (taskDescriptor->taskHandle == NULL)
    || (taskMessage == NULL)
  ) {
    // Invalid.
    returnValue = taskError;
    return returnValue;
  }

  returnValue = taskMessageQueuePush(taskDescriptor, taskMessage);

  return returnValue;
}

/// @fn int sendTaskMessageToTaskId(unsigned int taskId,
///   TaskMessage *taskMessage)
///
/// @brief Look up a task by its PID and send a message to it.
///
/// @param taskId The ID of the task to send the message to.
/// @param taskMessage A pointer to the message to send to the destination
///   task.
///
/// @return Returns taskSuccess on success, taskError on failure.
int sendTaskMessageToTaskId(unsigned int taskId, TaskMessage *taskMessage) {
  TaskDescriptor *taskDescriptor = schedulerGetTaskById(taskId);

  // If taskDescriptoris NULL, it will be detected as not running by
  // sendTaskMessageToTask, so there's no real point in checking for NULL
  // here.
  return sendTaskMessageToTask(taskDescriptor, taskMessage);
}

/// TaskMessage* getAvailableMessage(void)
///
/// @brief Get a message from the messages array that is not in use.
///
/// @return Returns a pointer to the available message on success, NULL if there
/// was no available message in the array.
TaskMessage* getAvailableMessage(void) {
  TaskMessage *availableMessage = NULL;

  for (int ii = 0; ii < NANO_OS_NUM_MESSAGES; ii++) {
    if (msg_in_use(&messages[ii]) == false) {
      availableMessage = &messages[ii];
      taskMessageInit(availableMessage, 0,
        &nanoOsMessages[ii], sizeof(nanoOsMessages[ii]), false);
      break;
    }
  }

  return availableMessage;
}

/// @fn TaskMessage* sendNanoOsMessageToTask(
///   TaskDescriptor *taskDescriptor, int type,
///   NanoOsMessageData func, NanoOsMessageData data, bool waiting)
///
/// @brief Send a NanoOsMessage to another task identified by its
/// TaskDescriptor.
///
/// @param task A pointer to the TaskDescriptor for the task.
/// @param type The type of the message to send to the destination task.
/// @param func The function information to send to the destination task,
///   cast to a NanoOsMessageData.
/// @param data The data to send to the destination task, cast to a
///   NanoOsMessageData.
/// @param waiting Whether or not the sender is waiting on a response from the
///   destination task.
///
/// @return Returns a pointer to the sent TaskMessage on success, NULL on failure.
TaskMessage* sendNanoOsMessageToTask(
  TaskDescriptor *taskDescriptor, int type,
  NanoOsMessageData func, NanoOsMessageData data, bool waiting
) {
  TaskMessage *taskMessage = NULL;
  if (taskDescriptor == NULL) {
    return taskMessage; // NULL
  } else if (!taskRunning(taskDescriptor)) {
    // Can't send to a non-running task.
    printString("ERROR: Could not send message from task ");
    printInt(taskId(getRunningTask()));
    printString("\n");
    if (taskDescriptor->taskHandle == NULL) {
      printString("ERROR: taskHandle is NULL\n");
    } else {
      printString("ERROR: Task ");
      printInt(taskId(taskDescriptor));
      printString(" is in state ");
      printInt(taskState(taskDescriptor));
      printString("\n");
    }
    return taskMessage; // NULL
  }

  taskMessage = getAvailableMessage();
  while (taskMessage == NULL) {
    taskYield();
    taskMessage = getAvailableMessage();
  }

  NanoOsMessage *nanoOsMessage
    = (NanoOsMessage*) taskMessageData(taskMessage);
  nanoOsMessage->func = func;
  nanoOsMessage->data = data;

  taskMessageInit(taskMessage, type,
    nanoOsMessage, sizeof(*nanoOsMessage), waiting);

  if (sendTaskMessageToTask(taskDescriptor, taskMessage)
    != taskSuccess
  ) {
    if (taskMessageRelease(taskMessage) != taskSuccess) {
      printString("ERROR: "
        "Could not release message from sendNanoOsMessageToTask.\n");
    }
    taskMessage = NULL;
  }

  return taskMessage;
}

/// @fn TaskMessage* sendNanoOsMessageToTaskId(int taskId, int type,
///   NanoOsMessageData func, NanoOsMessageData data, bool waiting)
///
/// @brief Send a NanoOsMessage to another task identified by its task ID. Looks
/// up the task's Coroutine by its PID and then calls
/// sendNanoOsMessageToTask.
///
/// @param taskId The task ID of the destination task.
/// @param type The type of the message to send to the destination task.
/// @param func The function information to send to the destination task,
///   cast to a NanoOsMessageData.
/// @param data The data to send to the destination task, cast to a
///   NanoOsMessageData.
/// @param waiting Whether or not the sender is waiting on a response from the
///   destination task.
///
/// @return Returns a pointer to the sent TaskMessage on success, NULL on
/// failure.
TaskMessage* sendNanoOsMessageToTaskId(int taskId, int type,
  NanoOsMessageData func, NanoOsMessageData data, bool waiting
) {
  TaskMessage *taskMessage = NULL;
  if (taskId >= NANO_OS_NUM_TASKS) {
    // Not a valid PID.  Fail.
    printString("ERROR: ");
    printInt(taskId);
    printString(" is not a valid PID.\n");
    return taskMessage; // NULL
  }

  TaskDescriptor *task = schedulerGetTaskById(taskId);
  taskMessage
    = sendNanoOsMessageToTask(task, type, func, data, waiting);
  if (taskMessage == NULL) {
    printString("ERROR: Could not send NanoOs message to task ");
    printInt(taskId);
    printString("\n");
  }
  return taskMessage;
}

/// @fn void* waitForDataMessage(
///   TaskMessage *sent, int type, const struct timespec *ts)
///
/// @brief Wait for a reply to a previously-sent message and get the data from
/// it.  The provided message will be released when the reply is received.
///
/// @param sent A pointer to a previously-sent TaskMessage the calling function is
///   waiting on a reply to.
/// @param type The type of message expected to be sent as a response.
/// @param ts A pointer to a struct timespec with the future time at which to
///   timeout if nothing is received by then.  If this parameter is NULL, an
///   infinite timeout will be used.
///
/// @return Returns a pointer to the data member of the received message on
/// success, NULL on failure.
void* waitForDataMessage(TaskMessage *sent, int type, const struct timespec *ts) {
  void *returnValue = NULL;

  TaskMessage *incoming = taskMessageWaitForReplyWithType(sent, true, type, ts);
  if (incoming != NULL)  {
    returnValue = nanoOsMessageDataPointer(incoming, void*);
    if (taskMessageRelease(incoming) != taskSuccess) {
      printString("ERROR: "
        "Could not release incoming message from waitForDataMessage.\n");
    }
  }

  return returnValue;
}

