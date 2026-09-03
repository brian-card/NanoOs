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
#include <unistd.h>

#include "ExecutiveProcesses.h"
#include "UserspaceLogger.h"

/// @var _logLevelNames
///
/// @brief Names that are to be displayed in place of log level numeric values.
const char _logLevelNames[NUM_LOG_LEVELS][9] = {
  "NEVER",
  "FLOOD",
  "TRACE",
  "DEBUG",
  "DETAIL",
  "INFO",
  "WARN",
  "ERROR",
  "CRITICAL",
  "BOX",
  "NONE",
};

/// @var _logHeaderFormat
///
/// @brief printf-style format string used to build the header of a log
/// message.
const char _logHeaderFormat[] = "[%lld.%09lld %s:%u:%u %s:%s:%d %s] ";

/// @def LOG_HEADER_LENGTH
///
/// @brief Number of characters in _logHeaderFormat, minus the terminating NUL
/// byte.
#define LOG_HEADER_LENGTH 29

int printLogEntry(LoggerState *loggerState, LogEntry *logEntry) {
  loggerState->args = (void*) ((intptr_t) logEntry->functionName);
  if (callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    "GetString", "getString", loggerState) != loggerState
  ) {
    printString("logger: ERROR! Could not find function name at offset ");
    printInt(logEntry->functionName);
    printString("\n");
    goto printMessage;
  }
  strcpy(loggerState->formatBuffer, loggerState->buffer);
  
  loggerState->args = (void*) ((intptr_t) logEntry->fileName);
  if (callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    "GetString", "getString", loggerState) != loggerState
  ) {
    printString("logger: ERROR! Could not find file name at offset ");
    printInt(logEntry->fileName);
    printString("\n");
    goto printMessage;
  }
  
  char *fileName = loggerState->buffer;
  char *slashAt = strrchr(fileName, '/');
  if (slashAt != NULL) {
    fileName = slashAt + 1;
  }
  char *functionName = fileName + strlen(fileName) + 1;
  strncpy(functionName, loggerState->formatBuffer,
    sizeof(loggerState->buffer) - (functionName - loggerState->buffer));
  
  snprintf(loggerState->formatBuffer, loggerState->formatBufferSize,
    _logHeaderFormat,
    (long long int) (logEntry->timeStamp / ((int64_t) 1000000000)),
    (long long int) (logEntry->timeStamp % ((int64_t) 1000000000)),
    loggerState->hostname, logEntry->processId, logEntry->threadId,
    fileName, functionName, logEntry->lineNumber,
    _logLevelNames[logEntry->logLevel]);
  
printMessage:
  loggerState->args = (void*) ((intptr_t) logEntry->format);
  if (callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    "GetString", "getString", loggerState) != loggerState
  ) {
    printString("logger: ERROR! Could not find format string at offset ");
    printInt(logEntry->format);
    printString("\n");
    goto exit;
  }
  strncat(loggerState->formatBuffer, loggerState->buffer,
    loggerState->formatBufferSize - strlen(loggerState->formatBuffer) - 1);
  
  intptr_t args[4];
  memset(args, 0, sizeof(args));
  int increment = sizeof(intptr_t) / sizeof(uint32_t);
  for (int ii = 0, jj = 0; ii < 4; ii += increment, jj++) {
    memcpy(&args[jj], &logEntry->args[ii], sizeof(intptr_t));
  }
  snprintf(loggerState->buffer, sizeof(loggerState->buffer),
    loggerState->formatBuffer, args[0], args[1], args[2], args[3]);
  printString(loggerState->buffer);
  
exit:
  logEntry->inUse = false;
  return 0;
}

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
  LogEntry *logEntry = (LogEntry*) processMessageData(processMessage);
  printLogEntry(loggerState, logEntry);
  
  processMessageRelease(processMessage);
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
  
  if (callOverlayFunction(OVERLAY_SAME_NAMESPACE,
    "FindReferencePoint", "findReferencePoint", &loggerState) != &loggerState
  ) {
    printString("ERROR! Could not call findReferencePoint.  Halting logger.\n");
    fclose(loggerState.binaryFile);
    while (1) sched_yield();
  }
  if (loggerState.referenceOffset == -1) {
    printString(
      "ERROR! Could not locate reference point in binary.  Halting logger.\n");
    fclose(loggerState.binaryFile);
    while (1) sched_yield();
  }
  
  loggerState.formatBuffer = getHal()->memory.logBuffer;
  loggerState.formatBufferSize = getHal()->memory.logBufferSize;
  if (gethostname(loggerState.hostname, sizeof(loggerState.hostname)) != 0) {
    strcpy(loggerState.hostname, "localhost");
  }
  
  if (getHal()->memory.staticLogs != NULL) {
    for (uintptr_t ii = 0; ii < getHal()->memory.staticLogs->numEntries; ii++) {
      printLogEntry(&loggerState, getHal()->memory.staticLogs->logEntries[ii]);
    }
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

