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

// Standard C includes
#define FILE C_FILE
#include "stdio.h"
#undef FILE

#include "Console.h"
#include "Hal.h"
#include "NanoOs.h"
#include "Tasks.h"
#include "Scheduler.h"
#include "../user/NanoOsLibC.h"

// Must come last
#include "../user/NanoOsStdio.h"

/// @fn int consolePrintMessage(ConsoleState *consoleState,
///   TaskMessage *inputMessage, const char *message)
///
/// @brief Print a message to all console ports that are owned by a task.
///
/// @param consoleState The ConsoleState being maintained by the runConsole
///   function.
/// @param inputMessage The message received from the task printing the
///   message.
/// @param message The formatted string message to print.
///
/// @return Returns taskSuccess on success, taskError on failure.
int consolePrintMessage(
  ConsoleState *consoleState, TaskMessage *inputMessage, const char *message
) {
  int returnValue = taskSuccess;
  TaskId owner = taskId(taskMessageFrom(inputMessage));
  ConsolePort *consolePorts = consoleState->consolePorts;

  bool portFound = false;
  for (int ii = 0; ii < consoleState->numConsolePorts; ii++) {
    if (consolePorts[ii].outputOwner == owner) {
      consolePorts[ii].consolePrintString(consolePorts[ii].portId, message);
      portFound = true;
    }
  }

  if (portFound == false) {
    printString("WARNING: Request to print message \"");
    printString(message);
    printString("\" from non-owning task ");
    printInt(owner);
    printString("\n");
    returnValue = taskError;
  }

  return returnValue;
}

/// @fn void consoleMessageCleanup(TaskMessage *inputMessage)
///
/// @brief Release an input TaskMessage if there are no waiters for the message.
///
/// @param inputMessage A pointer to the TaskMessage to cleanup.
///
/// @return This function returns no value.
void consoleMessageCleanup(TaskMessage *inputMessage) {
  if (taskMessageWaiting(inputMessage) == false) {
    if (taskMessageRelease(inputMessage) != taskSuccess) {
      // printSerialString is defined below.  Provide the prototype.
      int printSerialString(unsigned char serialPort, const char *string);
      printSerialString(0, "ERROR: Could not release inputMessage from ");
      printSerialString(0, __func__);
      printSerialString(0, "\n");
    }
  }
}

/// @fn ConsoleBuffer* getAvailableConsoleBuffer(
///   ConsoleState *consoleState, TaskId taskId)
///
/// @brief Get an available console buffer and mark it as being in use.
///
/// @param consoleState A pointer to the ConsoleState structure held by the
///   runConsole task.  The buffers in this state will be searched for
/// @param taskId The numerical ID of the task (PID) making the request
///   for a buffer (if any).
///
/// @return Returns a pointer to the available ConsoleBuffer on success, NULL
/// on failure.
ConsoleBuffer* getAvailableConsoleBuffer(
  ConsoleState *consoleState, TaskId taskId
) {
  ConsoleBuffer *consoleBuffers = consoleState->consoleBuffers;
  ConsoleBuffer *returnValue = NULL;

  // Check to see if the requesting task owns one of the ports for output.
  // Use the buffer for that port if so.
  ConsolePort *consolePorts = consoleState->consolePorts;
  for (int ii = 0; ii < consoleState->numConsolePorts; ii++) {
    if ((consolePorts[ii].outputOwner == taskId)
      || (consolePorts[ii].inputOwner == taskId)
    ) {
      returnValue = &consoleBuffers[ii];
      // returnValue->inUse is already set to true, so no need to set it.
      break;
    }
  }

  if (returnValue == NULL) {
    returnValue = (ConsoleBuffer*) malloc(sizeof(ConsoleBuffer));
  }

  return returnValue;
}

/// @fn void consoleWriteValueCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Command handler for the CONSOLE_WRITE_VALUE command.
///
/// @param consoleState A pointer to the ConsoleState structure held by the
///   runConsole task.  Unused by this function.
/// @param inputMessage A pointer to the TaskMessage that was received from
///   the task that sent the command.
///
/// @return This function returns no value but does set the inputMessage to
/// done so that the calling task knows that we've handled the message.
void consoleWriteValueCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  char staticBuffer[19]; // max length of a 64-bit value is 18 digits plus NULL.
  ConsoleValueType valueType = (ConsoleValueType) taskMessageSize(inputMessage);
  const char *message = NULL;

  switch (valueType) {
    case CONSOLE_VALUE_CHAR:
      {
        char value = (char) ((uintptr_t) taskMessageData(inputMessage));
        sprintf(staticBuffer, "%c", value);
        message = staticBuffer;
      }
      break;

    case CONSOLE_VALUE_UCHAR:
      {
        unsigned char value
          = (unsigned char) ((uintptr_t) taskMessageData(inputMessage));
        sprintf(staticBuffer, "%u", value);
        message = staticBuffer;
      }
      break;

    case CONSOLE_VALUE_INT:
      {
        int value = (int) ((uintptr_t) taskMessageData(inputMessage));
        sprintf(staticBuffer, "%d", value);
        message = staticBuffer;
      }
      break;

    case CONSOLE_VALUE_UINT:
      {
        unsigned int value
          = (unsigned int) ((uintptr_t) taskMessageData(inputMessage));
        sprintf(staticBuffer, "%u", value);
        message = staticBuffer;
      }
      break;

    case CONSOLE_VALUE_LONG_INT:
      {
        long int value = (long int) ((uintptr_t) taskMessageData(inputMessage));
        sprintf(staticBuffer, "%ld", value);
        message = staticBuffer;
      }
      break;

    case CONSOLE_VALUE_LONG_UINT:
      {
        long unsigned int value
          = (long unsigned int) ((uintptr_t) taskMessageData(inputMessage));
        sprintf(staticBuffer, "%lu", value);
        message = staticBuffer;
      }
      break;

    case CONSOLE_VALUE_FLOAT:
      {
        float value = (float) ((uintptr_t) taskMessageData(inputMessage));
        sprintf(staticBuffer, "%f", (double) value);
        message = staticBuffer;
      }
      break;

    case CONSOLE_VALUE_DOUBLE:
      {
        double value = (double) ((uintptr_t) taskMessageData(inputMessage));
        sprintf(staticBuffer, "%lf", value);
        message = staticBuffer;
      }
      break;

    case CONSOLE_VALUE_STRING:
      {
        message = (const char*) taskMessageData(inputMessage);
      }
      break;

    default:
      // Do nothing.
      break;

  }

  // It's possible we were passed a bad type that didn't result in the value of
  // message being set, so only attempt to print it if it was set.
  if (message != NULL) {
    consolePrintMessage(consoleState, inputMessage, message);
  }

  taskMessageSetDone(inputMessage);
  consoleMessageCleanup(inputMessage);

  return;
}

/// @fn void consoleGetBufferCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Command handler for the CONSOLE_GET_BUFFER command.  Gets a free
/// buffer from the provided ConsoleState and replies to the message sender with
/// a pointer to the buffer structure.
///
/// @param consoleState A pointer to the ConsoleState structure held by the
///   runConsole task.  The buffers in this state will be searched for
///   something available to return to the message sender.
/// @param inputMessage A pointer to the TaskMessage that was received from
///   the task that sent the command.
///
/// @return This function returns no value but does set the inputMessage to
/// done so that the calling task knows that we've handled the message.
/// Since this is a synchronous call, it also pushes a message onto the message
/// sender's queue with the free buffer on success.  On failure, the
/// inputMessage is marked as done but no response is sent.
void consoleGetBufferCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  // We're going to reuse the input message as the return message.
  TaskMessage *returnMessage = inputMessage;
  taskMessageData(returnMessage) = NULL;
  TaskId callingPid = taskId(taskMessageFrom(inputMessage));

  ConsoleBuffer *returnValue
    = getAvailableConsoleBuffer(consoleState, callingPid);
  if (returnValue != NULL) {
    // Send the buffer back to the caller via the message we allocated earlier.
    taskMessageInit(returnMessage, CONSOLE_RETURNING_BUFFER,
      returnValue, sizeof(*returnValue), true);
  }

  // Whether we were able to grab a buffer or not, we're now done with this
  // call, so mark the input message handled.  This is a synchronous call and
  // the caller is waiting on our response, so *DO NOT* release it.  The caller
  // is responsible for releasing it when they've received the response.
  taskMessageSetDone(inputMessage);
  return;
}

/// @fn void consoleWriteBufferCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Command handler for the CONSOLE_WRITE_BUFFER command.  Writes the
/// contents of the sent buffer to the console and then clears its inUse flag.
///
/// @param consoleState A pointer to the ConsoleState structure held by the
///   runConsole task.  Unused by this function.
/// @param inputMessage A pointer to the TaskMessage that was received from
///   the task that sent the command.
///
/// @return This function returns no value but does set the inputMessage to
/// done so that the calling task knows that we've handled the message.
void consoleWriteBufferCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  (void) consoleState;

  ConsoleBuffer *consoleBuffer = (ConsoleBuffer*) taskMessageData(inputMessage);
  if (consoleBuffer != NULL) {
    const char *message = consoleBuffer->buffer;
    if (message != NULL) {
      consolePrintMessage(consoleState, inputMessage, message);
    }
  }
  taskMessageSetDone(inputMessage);
  consoleMessageCleanup(inputMessage);

  return;
}

/// @fn void consoleSetPortShellCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Set the designated shell task ID for a port.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.  This contains a NanoOsMessage that contains a
///   ConsolePortPidAssociation that will associate the port with the task
///   if this function succeeds.
///
/// @return This function returns no value, but it marks the inputMessage as
/// being 'done' on success and does *NOT* mark it on failure.
void consoleSetPortShellCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  ConsolePortPidUnion consolePortPidUnion;
  consolePortPidUnion.nanoOsMessageData
    = (uintptr_t) taskMessageData(inputMessage);
  ConsolePortPidAssociation *consolePortPidAssociation
    = &consolePortPidUnion.consolePortPidAssociation;

  uint8_t consolePort = consolePortPidAssociation->consolePort;
  TaskId taskId = consolePortPidAssociation->taskId;

  if (consolePort < consoleState->numConsolePorts) {
    consoleState->consolePorts[consolePort].shell = taskId;
    taskMessageSetDone(inputMessage);
    consoleMessageCleanup(inputMessage);
  } else {
    printString("ERROR: Request to assign ownership of non-existent port ");
    printInt(consolePort);
    printString("\n");
    // *DON'T* call taskMessageRelease or taskMessageSetDone here.  The lack of
    // the message being done will indicate to the caller that there was a
    // problem servicing the command.
  }

  return;
}

/// @fn void consoleAssignPortHelper(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Assign a console port's input and possibly output to a running
/// task.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.  This contains a NanoOsMessage that contains a
///   ConsolePortPidAssociation that will associate the port's input with the
///   task if this function succeeds.
/// @param assignOutput Whether or not to assign the output as well as the
///   input.
///
/// @return This function returns no value, but it marks the inputMessage as
/// being 'done' on success and does *NOT* mark it on failure.
void consoleAssignPortHelper(
  ConsoleState *consoleState, TaskMessage *inputMessage, bool assignOutput
) {
  ConsolePortPidUnion consolePortPidUnion;
  consolePortPidUnion.nanoOsMessageData
    = (uintptr_t) taskMessageData(inputMessage);
  ConsolePortPidAssociation *consolePortPidAssociation
    = &consolePortPidUnion.consolePortPidAssociation;

  uint8_t consolePort = consolePortPidAssociation->consolePort;
  TaskId taskId = consolePortPidAssociation->taskId;

  if (consolePort < consoleState->numConsolePorts) {
    if (assignOutput == true) {
      consoleState->consolePorts[consolePort].outputOwner = taskId;
    }
    consoleState->consolePorts[consolePort].inputOwner = taskId;
    taskMessageSetDone(inputMessage);
    consoleMessageCleanup(inputMessage);
  } else {
    printString("ERROR: Request to assign ownership of non-existent port ");
    printInt(consolePort);
    printString("\n");
    // *DON'T* call taskMessageRelease or taskMessageSetDone here.  The
    // lack of the message being done will indicate to the caller that there
    // was a problem servicing the command.
  }

  return;
}

/// @fn void consoleAssignPortCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Assign a console port's input and output to a running task.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.  This contains a NanoOsMessage that contains a
///   ConsolePortPidAssociation that will associate the port's input and output
///   with the task if this function succeeds.
///
/// @return This function returns no value, but it marks the inputMessage as
/// being 'done' on success and does *NOT* mark it on failure.
void consoleAssignPortCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  consoleAssignPortHelper(consoleState, inputMessage, true);

  return;
}

/// @fn void consoleReleasePortCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Release all the ports currently owned by a task.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.
///
/// @return This function returns no value.
void consoleReleasePortCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  TaskId owner = taskId(taskMessageFrom(inputMessage));
  ConsolePort *consolePorts = consoleState->consolePorts;

  for (int ii = 0; ii < consoleState->numConsolePorts; ii++) {
    if (consolePorts[ii].outputOwner == owner) {
      consolePorts[ii].outputOwner = consolePorts[ii].shell;
    }
    if (consolePorts[ii].inputOwner == owner) {
      consolePorts[ii].inputOwner = consolePorts[ii].shell;
    }
  }

  // Since piped commands still attempt to release a port on completion, we
  // will not print a warning if we didn't successfully release anything.

  taskMessageSetDone(inputMessage);
  consoleMessageCleanup(inputMessage);

  return;
}

/// @fn void consoleGetOwnedPortCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Get the first port currently owned by a task.
///
/// @note While it is technically possible for a single task to own multiple
/// ports, the expectation here is that this call is made by a task that is
/// only expecting to own one.  This is mostly for the purposes of transferring
/// ownership of the port from one task to another.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.
///
/// @return This function returns no value.
void consoleGetOwnedPortCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  TaskId owner = taskId(taskMessageFrom(inputMessage));
  ConsolePort *consolePorts = consoleState->consolePorts;

  int ownedPort = -1;
  for (int ii = 0; ii < consoleState->numConsolePorts; ii++) {
    // inputOwner is assigned at the same as outputOwner, but inputOwner can be
    // set separately later if the commands are being piped together.
    // Therefore, checking inputOwner checks both of them.
    if (consolePorts[ii].inputOwner == owner) {
      ownedPort = ii;
      break;
    }
  }

  taskMessageData(inputMessage) = (void*) ((intptr_t) ownedPort);
  taskMessageSetDone(inputMessage);

  consoleMessageCleanup(inputMessage);

  return;
}

/// @fn void consoleGetEchoCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Get whether or not input is echoed back to any console ports owned
/// by a task.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.
///
/// @return This function returns no value.
void consoleGetEchoCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  TaskId owner = taskId(taskMessageFrom(inputMessage));
  ConsolePort *consolePorts = consoleState->consolePorts;
  TaskMessage *returnMessage = inputMessage;

  bool portFound = false;
  bool echoing = false;
  for (int ii = 0; ii < consoleState->numConsolePorts; ii++) {
    if (consolePorts[ii].outputOwner == owner) {
      echoing |= consolePorts[ii].echo;
      portFound = true;
    }
  }

  if (portFound == true) {
    taskMessageData(returnMessage) = (void*) ((uintptr_t) echoing);
  } else {
    printString("WARNING: Request to get echo from non-owning task ");
    printInt(owner);
    printString("\n");
  }

  taskMessageSetDone(inputMessage);

  return;
}

/// @fn void consoleSetEchoCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Set whether or not input is echoed back to all console ports owned
/// by a task.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.
///
/// @return This function returns no value.
void consoleSetEchoCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  TaskId owner = taskId(taskMessageFrom(inputMessage));
  ConsolePort *consolePorts = consoleState->consolePorts;
  bool desiredEchoState = (bool) ((uintptr_t) taskMessageData(inputMessage));

  bool portFound = false;
  for (int ii = 0; ii < consoleState->numConsolePorts; ii++) {
    if (consolePorts[ii].outputOwner == owner) {
      consolePorts[ii].echo = desiredEchoState;
      portFound = true;
    }
  }

  taskMessageData(inputMessage) = (void*) ((intptr_t) 0);
  if (portFound == false) {
    printString("WARNING: Request to set echo from non-owning task ");
    printInt(owner);
    printString("\n");
    taskMessageData(inputMessage) = (void*) ((intptr_t) -1);
  }

  taskMessageSetDone(inputMessage);
  consoleMessageCleanup(inputMessage);

  return;
}

/// @fn void consoleWaitForInputCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Wait for input from any of the console ports owned by a task.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.
///
/// @return This function returns no value.
void consoleWaitForInputCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  TaskId owner = taskId(taskMessageFrom(inputMessage));
  ConsolePort *consolePorts = consoleState->consolePorts;

  bool portFound = false;
  for (int ii = 0; ii < consoleState->numConsolePorts; ii++) {
    if (consolePorts[ii].inputOwner == owner) {
      consolePorts[ii].waitingForInput = true;
      portFound = true;
    }
  }

  if (portFound == false) {
    printString("WARNING: Request to wait for input from non-owning task ");
    printInt(owner);
    printString("\n");
  }

  taskMessageSetDone(inputMessage);
  consoleMessageCleanup(inputMessage);

  return;
}

/// @fn void consoleReleasePidPortCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Release all the ports currently owned by a task.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.
///
/// @return This function returns no value.
void consoleReleasePidPortCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  TaskId sender = taskId(taskMessageFrom(inputMessage));
  if (sender != SCHEDULER_STATE->schedulerTaskId) {
    // Sender is not the scheduler.  We will ignore this.
    taskMessageSetDone(inputMessage);
    consoleMessageCleanup(inputMessage);
    return;
  }

  TaskId owner = (TaskId) ((intptr_t) taskMessageData(inputMessage));
  ConsolePort *consolePorts = consoleState->consolePorts;

  for (int ii = 0; ii < consoleState->numConsolePorts; ii++) {
    if (consolePorts[ii].inputOwner == owner) {
      consolePorts[ii].inputOwner = consolePorts[ii].shell;
    }
    if (consolePorts[ii].outputOwner == owner) {
      consolePorts[ii].outputOwner = consolePorts[ii].shell;
    }
  }

  taskMessageSetDone(inputMessage);
  consoleMessageCleanup(inputMessage);

  return;
}

/// @fn void consoleReleaseBufferCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Command handler for the CONSOLE_RELEASE_BUFFER command.  Releases a
/// buffer that was previously returned by the CONSOLE_GET_BUFFER command.
/// a pointer to the buffer structure.
///
/// @param consoleState A pointer to the ConsoleState structure held by the
///   runConsole task.  One of the buffers in this state will have its inUse
///   member set to false.
/// @param inputMessage A pointer to the TaskMessage that was received from
///   the task that sent the command.
///
/// @return This function returns no value but does set the inputMessage to
/// done so that the calling task knows that we've handled the message.
/// Since this is a synchronous call, it also pushes a message onto the message
/// sender's queue with the free buffer on success.  On failure, the
/// inputMessage is marked as done but no response is sent.
void consoleReleaseBufferCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  // We don't need to examine consoleState since the caller has provided us
  // with the pointer we can use directly.
  (void) consoleState;

  ConsoleBuffer *consoleBuffers = consoleState->consoleBuffers;
  ConsoleBuffer *consoleBuffer = (ConsoleBuffer*) taskMessageData(inputMessage);
  if (consoleBuffer != NULL) {
    for (int ii = 0; ii < consoleState->numConsolePorts; ii++) {
      if (consoleBuffer == &consoleBuffers[ii]) {
        // The buffer being released is one of the buffers dedicated to a port.
        // *DO NOT* free it because it is always in use.  Just release the
        // message and return.
        taskMessageRelease(inputMessage);
        return;
      }
    }

    free(consoleBuffer); consoleBuffer = NULL;
  }

  // Whether we were able to grab a buffer or not, we're now done with this
  // call, so mark the input message handled.  This is a synchronous call and
  // the caller is waiting on our response, so *DO NOT* release it.  The caller
  // is responsible for releasing it when they've received the response.
  taskMessageRelease(inputMessage);
  return;
}

/// @fn void consoleGetNumPortsCommandHandler(
///   ConsoleState *consoleState, TaskMessage *inputMessage)
///
/// @brief Get the number of running console ports.
///
/// @param consoleState A pointer to the ConsoleState being maintained by the
///   runConsole function that's running.
/// @param inputMessage A pointer to the TaskMessage with the received
///   command.
///
/// @return This function returns no value.
void consoleGetNumPortsCommandHandler(
  ConsoleState *consoleState, TaskMessage *inputMessage
) {
  taskMessageData(inputMessage)
    = (void*) ((intptr_t) consoleState->numConsolePorts);
  taskMessageSetDone(inputMessage);

  return;
}

/// @typedef ConsoleCommandHandler
///
/// @brief Signature of command handler for a console command.
typedef void (*ConsoleCommandHandler)(ConsoleState*, TaskMessage*);

/// @var consoleCommandHandlers
///
/// @brief Array of handlers for console command messages.
const ConsoleCommandHandler consoleCommandHandlers[] = {
  consoleWriteValueCommandHandler,      // CONSOLE_WRITE_VALUE
  consoleGetBufferCommandHandler,       // CONSOLE_GET_BUFFER
  consoleWriteBufferCommandHandler,     // CONSOLE_WRITE_BUFFER
  consoleSetPortShellCommandHandler,    // CONSOLE_SET_PORT_SHELL
  consoleAssignPortCommandHandler,      // CONSOLE_ASSIGN_PORT
  consoleReleasePortCommandHandler,     // CONSOLE_RELEASE_PORT
  consoleGetOwnedPortCommandHandler,    // CONSOLE_GET_OWNED_PORT
  consoleGetEchoCommandHandler,         // CONSOLE_GET_ECHO_PORT,
  consoleSetEchoCommandHandler,         // CONSOLE_SET_ECHO_PORT
  consoleWaitForInputCommandHandler,    // CONSOLE_WAIT_FOR_INPUT
  consoleReleasePidPortCommandHandler,  // CONSOLE_RELEASE_PID_PORT
  consoleReleaseBufferCommandHandler,   // CONSOLE_RELEASE_BUFFER
  consoleGetNumPortsCommandHandler,     // CONSOLE_GET_NUM_PORTS
};

/// @fn void handleConsoleMessages(ConsoleState *consoleState)
///
/// @brief Handle all messages currently in the console task's message queue.
///
/// @param consoleState A pointer to the ConsoleState structure maintained by
///   the runConsole task.
///
/// @return This function returns no value.
void handleConsoleMessages(ConsoleState *consoleState) {
  TaskMessage *message = taskMessageQueuePop();
  while (message != NULL) {
    ConsoleCommand messageType = (ConsoleCommand) taskMessageType(message);
    if (messageType >= NUM_CONSOLE_COMMANDS) {
      // Invalid.
      printInt(getRunningTaskId());
      printString(": ");
      printString(__func__);
      printString(": ");
      printInt(__LINE__);
      printString(": Invalid message type ");
      printInt(messageType);
      printString("\n");

      message = taskMessageQueuePop();
      continue;
    }

    consoleCommandHandlers[messageType](consoleState, message);
    message = taskMessageQueuePop();
  }

  return;
}

/// @fn int readSerialByte(ConsolePort *consolePort)
///
/// @brief Do a non-blocking read of a serial port.
///
/// @param ConsolePort A pointer to the ConsolePort data structure that contains
///   the buffer information to use.
///
/// @return Returns the byte read, cast to an int, on success, -1 on failure.
int readSerialByte(ConsolePort *consolePort) {
  int serialData = -1;
  serialData = HAL->serialPortHal->pollSerialPort((int) consolePort->portId);
  if (serialData > -1) {
    ConsoleBuffer *consoleBuffer = consolePort->consoleBuffer;
    char *buffer = consoleBuffer->buffer;
    if (((serialData >= ASCII_SPACE) && (serialData < ASCII_DELETE))
      || (serialData == ASCII_RETURN) || (serialData == ASCII_NEWLINE)
    ) {
      // Data is a printable ASCII character.
      if (consolePort->echo == true) {
        if ((serialData != ASCII_RETURN) && (serialData != ASCII_NEWLINE)) {
          char serialChar = (char) serialData;
          HAL->serialPortHal->writeSerialPort((int) consolePort->portId,
            (uint8_t*) &serialChar, 1);
        } else {
          HAL->serialPortHal->writeSerialPort(
            (int) consolePort->portId, (uint8_t*) "\r\n", 2);
        }
      }
      
      if (serialData == ASCII_RETURN) {
        serialData = ASCII_NEWLINE;
        // Some terminals send \r\n.  Read one more character just in case.
        HAL->serialPortHal->pollSerialPort((int) consolePort->portId);
      }
      
      if (consolePort->consoleBufferIndex < (CONSOLE_BUFFER_SIZE - 1)) {
        buffer[consolePort->consoleBufferIndex] = (char) serialData;
        consolePort->consoleBufferIndex++;
      }
    } else if ((serialData == ASCII_BACKSPACE)
      || (serialData == ASCII_DELETE)
    ) {
      // Data is a backspace or delete ASCII control character.  Treat them
      // both like a backspace.
      if (consolePort->consoleBufferIndex > 0) {
        if (consolePort->echo == true) {
          uint8_t backspace = ASCII_BACKSPACE;
          uint8_t space = ASCII_SPACE;
          HAL->serialPortHal->writeSerialPort(
            (int) consolePort->portId, &backspace, 1);
          HAL->serialPortHal->writeSerialPort(
            (int) consolePort->portId, &space, 1);
          HAL->serialPortHal->writeSerialPort(
            (int) consolePort->portId, &backspace, 1);
        }
        
        consolePort->consoleBufferIndex--;
      }
    } else if (serialData == ASCII_ESCAPE) {
      // Data is the beginning of an escape sequence.  We need to fill the
      // buffer with the full sequence.
      do {
        if (consolePort->consoleBufferIndex < (CONSOLE_BUFFER_SIZE - 1)) {
          buffer[consolePort->consoleBufferIndex] = (char) serialData;
          consolePort->consoleBufferIndex++;
        }
        serialData = HAL->serialPortHal->pollSerialPort(
          (int) consolePort->portId);
      } while (serialData > -1);
      
      // In this case, we need to return ASCII_ESCAPE so that the main loop
      // knows to forward this to the waiting task.  Handling escape
      // sequences is task specific.
      serialData = ASCII_ESCAPE;
    } else {
      printDebugString("Received unhandled character ");
      printDebugInt(serialData);
      printDebugString("\n");
    }
  }

  return serialData;
}

/// @fn int printSerialString(unsigned char serialPort, const char *string)
///
/// @brief Print a string to the default serial port.
///
/// @param serialPort The numerical ID of the serial port to read a byte from.
/// @param string A pointer to the string to print.
///
/// @return Returns the number of bytes written to the serial port.
int printSerialString(unsigned char serialPort, const char *string) {
  int returnValue = 0;
  size_t numBytes = 0;

  char *newlineAt = strchr(string, '\n');
  newlineAt = strchr(string, '\n');
  if (newlineAt == NULL) {
    numBytes = strlen(string);
  } else {
    numBytes = (size_t) (((uintptr_t) newlineAt) - ((uintptr_t) string));
  }
  while (newlineAt != NULL) {
    returnValue += (int) HAL->serialPortHal->writeSerialPort(
      (int) serialPort, (uint8_t*) string, numBytes);
    returnValue += (int) HAL->serialPortHal->writeSerialPort(
      (int) serialPort, (uint8_t*) "\r\n", 2);
    string = newlineAt + 1;
    newlineAt = strchr(string, '\n');
    if (newlineAt == NULL) {
      numBytes = strlen(string);
    } else {
      numBytes = (size_t) (((uintptr_t) newlineAt) - ((uintptr_t) string));
    }
  }
  returnValue += (int) HAL->serialPortHal->writeSerialPort(
    (int) serialPort, (uint8_t*) string, numBytes);

  return returnValue;
}

/// @fn void* runConsole(void *args)
///
/// @brief Main task for managing console input and output.  Runs in an
/// infinite loop and never exits.  Every iteration, it checks the serial
/// connection for a byte and adds it to the buffer if there is anything,
/// handles the user command if the incoming byte is a newline, and handles any
/// messages that were sent to this task.
///
/// @param args Any arguments provided by the scheduler.  Ignored by this
///   task.
///
/// @return This function never returns, but would return NULL if it did.
void* runConsole(void *args) {
  (void) args;

  int byteRead = -1;
  ConsoleState consoleState;
  memset(&consoleState, 0, sizeof(ConsoleState));
  TaskMessage *schedulerMessage = NULL;

  if (HAL->serialPortHal != NULL) {
    consoleState.numConsolePorts
      = MIN(CONSOLE_NUM_PORTS, HAL->serialPortHal->getNumSerialPorts());
  }

  // For each console port, use the console buffer at the corresponding index.
  for (uint8_t ii = 0; ii < consoleState.numConsolePorts; ii++) {
    consoleState.consolePorts[ii].consoleBuffer
      = &consoleState.consoleBuffers[ii];
    consoleState.consolePorts[ii].consoleBuffer->inUse = true;
  }

  for (int ii = 0; ii < consoleState.numConsolePorts; ii++) {
    // Set the port-specific data.
    consoleState.consolePorts[ii].portId = ii;
    consoleState.consolePorts[ii].consoleBufferIndex = 0;
    consoleState.consolePorts[ii].inputOwner = TASK_ID_NOT_SET;
    consoleState.consolePorts[ii].outputOwner = TASK_ID_NOT_SET;
    consoleState.consolePorts[ii].shell = TASK_ID_NOT_SET;
    consoleState.consolePorts[ii].waitingForInput = false;
    consoleState.consolePorts[ii].readByte = readSerialByte;
    consoleState.consolePorts[ii].echo = true;
    consoleState.consolePorts[ii].consolePrintString = printSerialString;
  }

  while (1) {
    for (uint8_t ii = 0; ii < consoleState.numConsolePorts; ii++) {
      ConsolePort *consolePort = &consoleState.consolePorts[ii];
      byteRead = consolePort->readByte(consolePort);
      if ((byteRead == ASCII_NEWLINE) || (byteRead == ASCII_RETURN)
        || (byteRead == ASCII_ESCAPE)
      ) {
        if ((consolePort->inputOwner != TASK_ID_NOT_SET)
          && (consolePort->waitingForInput == true)
        ) {
          consolePort->consoleBuffer->buffer[consolePort->consoleBufferIndex]
            = '\0';
          consolePort->consoleBufferIndex = 0;
          if (initSendTaskMessageToTaskId(
            consolePort->inputOwner, CONSOLE_RETURNING_INPUT,
            consolePort->consoleBuffer, sizeof(ConsoleBuffer), false) == NULL
          ) {
            printString(
              "ERROR: Could not send CONSOLE_RETURNING_INPUT to task ID ");
            printInt(consolePort->inputOwner);
            printString("\n");
          }
          consolePort->waitingForInput = false;
        } else {
          // Console port is either not owned or owning task is not waiting
          // for input.  Reset our buffer and do nothing.
          consolePort->consoleBufferIndex = 0;
        }
      }
    }

    schedulerMessage = (TaskMessage*) taskYield();

    if (schedulerMessage != NULL) {
      // We have a message from the scheduler that we need to task.  This
      // is not the expected case, but it's the priority case, so we need to
      // list it first.
      ConsoleCommand messageType
        = (ConsoleCommand) taskMessageType(schedulerMessage);
      if (messageType < NUM_CONSOLE_COMMANDS) {
        consoleCommandHandlers[messageType](&consoleState, schedulerMessage);
      } else {
        printString("ERROR: Received unknown console command ");
        printInt(messageType);
        printString(" from scheduler.\n");
      }
    } else {
      // No message from the scheduler.  Handle any user task messages in
      // our message queue.
      handleConsoleMessages(&consoleState);
    }
  }

  return NULL;
}

/// @fn int printConsoleValue(
///   ConsoleValueType valueType, void *value, size_t length)
///
/// @brief Send a command to print a value to the console.
///
/// @param command The ConsoleCommand of the message to send, which will
///   determine the handler to use and therefore the type of the value that is
///   displayed.
/// @param value A pointer to the value to send as data.
/// @param length The length of the data at the address provided by the value
///   pointer.  Will be truncated to the size of uintptr_t if needed.
///
/// @return This function is non-blocking, always succeeds, and always returns
/// 0.
int printConsoleValue(ConsoleValueType valueType, void *value, size_t length) {
  printDebugString("Entering printConsoleValue.\n");
  uintptr_t message = 0;
  length = (length <= sizeof(message)) ? length : sizeof(message);
  memcpy(&message, value, length);

  printDebugString("Sending message to console task.\n");
  if (initSendTaskMessageToTaskId(SCHEDULER_STATE->consoleTaskId,
    CONSOLE_WRITE_VALUE, (void*) message, (size_t) valueType, false) == NULL
  ) {
    printString(
      "ERROR: Could not send CONSOLE_WRITE_VALUE message to console task\n");
  }

  printDebugString("Leaving printConsoleValue.\n");
  return 0;
}

/// @fn printConsole(<type> message)
///
/// @brief Print a message of an arbitrary type to the console.
///
/// @details
/// This is basically just a switch statement where the type is the switch
/// value.  They all call printConsole with the appropriate console command, a
/// pointer to the provided message, and the size of the message.
///
/// @param message The message to send to the console.
///
/// @return Returns the value returned by printConsoleValue.
int printConsoleChar(char message) {
  return printConsoleValue(CONSOLE_VALUE_CHAR, &message, sizeof(message));
}
int printConsoleUChar(unsigned char message) {
  return printConsoleValue(CONSOLE_VALUE_UCHAR, &message, sizeof(message));
}
int printConsoleInt(int message) {
  return printConsoleValue(CONSOLE_VALUE_INT, &message, sizeof(message));
}
int printConsoleUInt(unsigned int message) {
  return printConsoleValue(CONSOLE_VALUE_UINT, &message, sizeof(message));
}
int printConsoleLong(long int message) {
  return printConsoleValue(CONSOLE_VALUE_LONG_INT, &message, sizeof(message));
}
int printConsoleULong(long unsigned int message) {
  return printConsoleValue(CONSOLE_VALUE_LONG_UINT, &message, sizeof(message));
}
int printConsoleFloat(float message) {
  return printConsoleValue(CONSOLE_VALUE_FLOAT, &message, sizeof(message));
}
int printConsoleDouble(double message) {
  return printConsoleValue(CONSOLE_VALUE_DOUBLE, &message, sizeof(message));
}
int printConsoleString(const char *message) {
  return printConsoleValue(CONSOLE_VALUE_STRING, &message, sizeof(message));
}

// Console port support functions.

/// @fn void releaseConsole(void)
///
/// @brief Release the console and display the command prompt to the user again.
///
/// @return This function returns no value.
void releaseConsole(void) {
  // releaseConsole is sometimes called from within handleCommand, which runs
  // from within the console task.  That means we can't do blocking prints
  // from this function.  i.e. We can't use printf here.  Use printConsole
  // instead.
  if (initSendTaskMessageToTaskId(SCHEDULER_STATE->consoleTaskId,
    CONSOLE_RELEASE_PORT, /* data= */ 0, /* size= */ 0, false) == NULL
  ) {
    printString(
      "ERROR: Could not send CONSOLE_RELEASE_PORT message to console task\n");
  }
  taskYield();
}

/// @fn int getOwnedConsolePort(void)
///
/// @brief Get the first port owned by the running task.
///
/// @return Returns the numerical index of the console port the task owns on
/// success, -1 on failure.
int getOwnedConsolePort(void) {
  TaskMessage *sent = initSendTaskMessageToTaskId(
    SCHEDULER_STATE->consoleTaskId, CONSOLE_GET_OWNED_PORT,
    /* data= */ 0, /* size= */ 0, /* waiting= */ true);

  taskMessageWaitForDone(sent, NULL);

  int returnValue = (int) ((intptr_t) taskMessageData(sent));
  taskMessageRelease(sent);

  return returnValue;
}

/// @fn bool getConsoleEcho(void)
///
/// @brief Get the echo state for all ports owned by the current task.
///
/// @return Returns true if the console for the process is echoing, false
/// otherwise.
bool getConsoleEcho(void) {
  TaskMessage *sent = initSendTaskMessageToTaskId(
    SCHEDULER_STATE->consoleTaskId, CONSOLE_GET_ECHO_PORT,
    /* data= */ 0, /* size= */ 0, /* waiting= */ true);

  // The console will reuse the message we sent.
  taskMessageWaitForDone(sent, NULL);

  bool returnValue = (bool) ((uintptr_t) taskMessageData(sent));
  taskMessageRelease(sent);

  return returnValue;
}

/// @fn int setConsoleEcho(bool desiredEchoState)
///
/// @brief Set the echo state for all ports owned by the current task.
///
/// @return Returns 0 if the echo state was set for the current task's
/// ports, -1 on failure.
int setConsoleEcho(bool desiredEchoState) {
  TaskMessage *sent = initSendTaskMessageToTaskId(
    SCHEDULER_STATE->consoleTaskId, CONSOLE_SET_ECHO_PORT,
    /* data= */ (void*) ((uintptr_t) desiredEchoState), /* size= */ 0,
    /* waiting= */ true);

  taskMessageWaitForDone(sent, NULL);

  int returnValue = (int) ((intptr_t) taskMessageData(sent));
  taskMessageRelease(sent);

  return returnValue;
}

/// @fn int getNumConsolePorts(void)
///
/// @brief Get the number of ports the console is running.
///
/// @return Returns the number of ports running on success, -1 on failure.
int getNumConsolePorts(void) {
  TaskMessage *sent = initSendTaskMessageToTaskId(
    SCHEDULER_STATE->consoleTaskId, CONSOLE_GET_NUM_PORTS,
    /* data= */ 0, /* size= */ 0, /* waiting= */ true);
  if (sent == NULL) {
    return -1;
  }

  taskMessageWaitForDone(sent, NULL);
  int returnValue = (int) ((intptr_t) taskMessageData(sent));
  taskMessageRelease(sent);

  return returnValue;
}

