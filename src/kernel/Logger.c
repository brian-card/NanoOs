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

/// @file Logger.c
///
/// @brief Custom logging support for NanoOs.

// C includes:
#include "stdarg.h"
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

// NanoOs includes:
#include "Hal.h"
#include "Logger.h"
#include "NanoOs.h"
#include "Processes.h"
#include "Scheduler.h"
#include "../user/NanoOsErrno.h"

// Must come last
#include "../user/NanoOsStdio.h"

/// @var _referencePoint
///
/// @brief Variable that will be used to compute the relative offsets of string
/// parameters that are logged.
const char *_referencePoint = REFERENCE_POINT_STRING;

/// @var _logLevelNames
///
/// @brief Names that are to be displayed in place of log level numeric values.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.  Entries are copied directly into this
/// array's own storage rather than being separate string-literal objects
/// pointed to from it, so tagging the array alone protects every entry.
const char _logLevelNames[NUM_LOG_LEVELS][9] KEEP_IN_FLASH = {
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
/// message printed immediately (before the logger process is up).
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _logHeaderFormat[] KEEP_IN_FLASH
  = "[%lld.%09lld %s:%u %s:%u %s] ";

/// @var _localhost
///
/// @brief Fallback hostname used in a log message header when the scheduler
/// hasn't set a real hostname yet.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _localhost[] KEEP_IN_FLASH = "localhost";

/// @fn int logMessage(LogLevel logLevel, const char *fileName,
///   uint16_t lineNumber, const char *format, ...)
///
/// @brief Log a message to be displayed.
///
/// @param logLevel The LogLevel of the message.
/// @param fileName The name of the file the message comes from.
/// @param lineNumber The line number within the file that the message comes
///   from.
/// @param format The standard printf-style format string for the message.
/// @param ... Up to four (4) integer parameters.
///
/// @return If logging to the logger process, returns 0 on success.  If logging
/// to the console, returns the number of bytes successfully written on success.
/// Returns -errno on failure.
int logMessage(LogLevel logLevel, const char *fileName, uint16_t lineNumber,
   const char *format, ...
) {
  va_list args;
  LogMessageCommandArgs commandArgs;
  char *slashAt = NULL;
  
  // Don't check the return value of getElapsedNanoseconds here.  A failure
  // isn't fatal.  Do this before anything else to get as accurate a timestamp
  // as possible.
  HAL->clock.getElapsedNanoseconds(0, &commandArgs.logEntry.timeStamp);
  
  // Get the rest of the fixed values.
  commandArgs.logEntry.logLevel = logLevel;
  commandArgs.logEntry.fileName = (int16_t) (((intptr_t) fileName)
    - ((intptr_t) _referencePoint));
  commandArgs.logEntry.lineNumber = lineNumber;
  commandArgs.logEntry.pid = getRunningPid();
  commandArgs.logEntry.format = (int16_t) (((intptr_t) format)
    - ((intptr_t) _referencePoint));
  
  // Get the va_list values.
  int increment = sizeof(intptr_t) / sizeof(uint32_t);
  va_start(args, format);
  for (int ii = 0;
    ii < (int) ((sizeof(commandArgs.logEntry.args))
      / (sizeof(commandArgs.logEntry.args[0])));
    ii += increment
  ) {
    intptr_t arg = va_arg(args, intptr_t);
    memcpy(&commandArgs.logEntry.args[ii], &arg, sizeof(arg));
  }
  va_end(args);
  
  if ((SCHEDULER_STATE == NULL) || (SCHEDULER_STATE->loggerPid == 0)) {
    if (HAL->memory.staticLogs != NULL) {
      // Logger isn't up yet but will be.  Write to the staticLogs area.
      goto writeStaticLog;
    } else if (HAL->memory.stringsPresent == true) {
      // Write this entry immediately.
      goto writeImmediate;
    }
    
    // If we made it this far then we have no ability to log a static log for
    // the logger process to lookup AND strings are not compiled into the OS
    // image, so we can't print it as an immediate either.  This is a bug in
    // the HAL but there's nothing we can do at runtime, so just return to the
    // caller that this isn't supported.
    return -ENOTSUP;
  }
  
  ProcessMessage *processMessage = getAvailableMessage();
  if (processMessage == NULL) {
    if (getRunningPid() != SCHEDULER_STATE->schedulerPid) {
      // This is the expected case.
      for (int ii = 0;
        (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
        ii++
      ) {
        processYield();
        processMessage = getAvailableMessage();
      }
      if (processMessage == NULL) {
        // There's something wrong with the system.  Try again later.
        return -EAGAIN;
      }
    } else {
      // We have to do things a little differently since the scheduler can't
      // yield.
      for (int ii = 0;
        (ii < MAX_GET_MESSAGE_RETRIES) && (processMessage == NULL);
        ii++
      ) {
        SCHEDULER_STATE->runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
        processMessage = getAvailableMessage();
      }
      if (processMessage == NULL) {
        // There's something wrong with the system.  Try again later.
        return -EAGAIN;
      }
    }
  }
  if (processMessageInit(processMessage,
    LOGGER_COMMAND_SIGNATURE | LOGGER_LOG_MESSAGE,
    &commandArgs.logEntry, sizeof(commandArgs.logEntry), true) != processSuccess
  ) {
    processMessageRelease(processMessage);
    return -EAGAIN;
  }
  
  if (sendProcessMessageToPid(SCHEDULER_STATE->loggerPid, processMessage)
    != 0
  ) {
    processMessageRelease(processMessage);
    if (HAL->memory.stringsPresent == true) {
      // Write this entry immediately.
      goto writeImmediate;
    }
    return -EAGAIN;
  }
  
  if (getRunningPid() != SCHEDULER_STATE->schedulerPid) {
    // This is the expected case.
    processMessageWaitForDone(processMessage, NULL);
  } else {
    while (processMessageDone(processMessage) == false) {
      SCHEDULER_STATE->runSchedulerQueues(PRIVILEGE_LEVEL_SUPERVISOR);
    }
  }
  processMessageRelease(processMessage);
  return commandArgs.returnValue;
  
writeImmediate:
  // Print the header.
  slashAt = strrchr(fileName, '/');
  if (slashAt != NULL) {
    fileName = slashAt + 1;
  }
  
  snprintf(HAL->memory.logBuffer, HAL->memory.logBufferSize,
    _logHeaderFormat,
    (long long int) (commandArgs.logEntry.timeStamp / ((int64_t) 1000000000)),
    (long long int) (commandArgs.logEntry.timeStamp % ((int64_t) 1000000000)),
    ((SCHEDULER_STATE != NULL) && (SCHEDULER_STATE->hostname != NULL))
      ? SCHEDULER_STATE->hostname
      : _localhost,
    (unsigned int) getRunningPid(),
    fileName, lineNumber, _logLevelNames[logLevel]);
  int rv = printString(HAL->memory.logBuffer);
  if (rv < 0) {
    return rv;
  }
  
  // Print the log message.
  va_start(args, format);
  vsnprintf(HAL->memory.logBuffer, HAL->memory.logBufferSize,
    format, args);
  va_end(args);
  rv += printString(HAL->memory.logBuffer);
  return rv;
  
writeStaticLog:
  // Copy the log entry to the static log area.
  memcpy(&HAL->memory.staticLogs->logEntries[
    HAL->memory.staticLogs->metadata.numEntries],
    &commandArgs.logEntry, sizeof(commandArgs.logEntry));
  HAL->memory.staticLogs->metadata.numEntries++;
  
  return 0;
}

