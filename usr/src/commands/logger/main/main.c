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

/// @file main.c
///
/// @brief Entrypoint into userspace logger command.

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ExecutiveProcesses.h"
#include "NanoOsExecutive.h"

#include "UserspaceLogger.h"

/// @fn int loggerLogMessageCommandHandler(
///   LoggerState *loggerState, ProcessMessage *processMessage)
///
/// @brief Command handler for the LOGGER_LOG_MESSAGE command.  Format and print
/// out a message that was logged by another process.
///
/// @param loggerState Pointer to the LoggerState managed by this process.
/// @param processMessage Pointer to the ProcessMessage received by this
///   process.
///
/// @return Returns 0 on success, -errno on failure.
int loggerLogMessageCommandHandler(
  LoggerState *loggerState, ProcessMessage *processMessage
) {
  (void) loggerState;
  (void) processMessage;
  return 0;
}

/// @typedef LoggerCommandHandler
///
/// @brief Signature of command handler for a logger command.
typedef int (*LoggerCommandHandler)(
  LoggerState *loggerState, ProcessMessage *incoming);

/// @var loggerCommandHandlers
///
/// @brief Array of function pointers for handlers for commands that are
/// understood by this process.
const LoggerCommandHandler loggerCommandHandlers[] = {
  loggerLogMessageCommandHandler,       // LOGGER_LOG_MESSAGE
};

int main(int argc, char **argv) {
  if (argc < 2) {
    printString("ERROR! No binary path provided to logger.  Halting.\n");
    while (1) sched_yield();
  }
  
  LoggerState loggerState;
  *loggerState.buffer = '\0';
  loggerState.binaryFile = fopen(argv[1], "r");
  if (loggerState.binaryFile == NULL) {
    printString("logger: Could not open binary file \"");
    printString(argv[1]);
    printString("\".  Halting.\n");
    while (1) sched_yield();
  }
  
  loggerState.referenceOffset
    = (intptr_t) callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    "FindReferencePoint", "findReferencePoint", loggerState.binaryFile);
  if (loggerState.referenceOffset == -1) {
    printString(
      "ERROR! Could not locate reference point in binary.  Halting logger.\n");
    fclose(loggerState.binaryFile);
    while (1) sched_yield();
  }
  
  while (1) {
    ProcessMessage *processMessage = processMessageQueueWait(NULL);
    while (processMessage != NULL) {
      if ((processMessageType(processMessage) & 0xffffffffffffff00)
        != LOGGER_COMMAND_SIGNATURE
      ) {
        printString("logger: Received unknown signature 0x");
        printHex(processMessageType(processMessage) & 0xffffffffffffff00);
        printString(" from process ");
        printInt(processPid(processMessageFrom(processMessage)));
        printString("\n");
        // Don't attempt to process this message further.
        processMessage = processMessageQueuePop();
        continue;
      }

      LoggerCommand messageType
        = (LoggerCommand) (processMessageType(processMessage) & 0xff);
      if (messageType >= NUM_LOGGER_COMMANDS) {
        printString("logger: Unrecognized message type ");
        printInt(messageType);
        printString("\n");

        processMessage = processMessageQueuePop();
        continue;
      }
      
      loggerCommandHandlers[messageType](&loggerState, processMessage);
      
      processMessage = processMessageQueuePop();
    }
  }
  
  printString("Gracefully exiting logger\n");
  return 0;
}

