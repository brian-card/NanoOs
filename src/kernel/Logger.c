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
// See src/include/sys/types.h: clear the guard flags the real library's
// (renamed) typedefs above set, so that header's own gid_t/uid_t/pid_t,
// reached later in this file, don't wrongly defer to them.
#undef __gid_t_defined
#undef _GID_T_DECLARED
#undef __uid_t_defined
#undef _UID_T_DECLARED
#undef __pid_t_defined
#undef _PID_T_DECLARED
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

/// @var loggerPid
///
/// @brief The well-known ProcessId of the logger, or 0 if none is running.
/// Set once, directly, when the logger process is created in HalCommon.c's
/// halCommonInitLogger.
ProcessId loggerPid = 0;

/// @var _cachedHostname
///
/// @brief Cached hostname, fetched via schedulerPeekHostname() the first
/// time it's needed and reused after that.  NULL means "not fetched yet".
/// A non-NULL, non-empty value doubles as this file's "is the scheduler
/// initialized" signal, so logMessage doesn't need a separate readiness
/// check once this is set: schedulerPeekHostname() only ever returns a
/// real (non-empty) hostname once the scheduler is initialized.
static const char *_cachedHostname = NULL;

/// @var _logLevelNames
///
/// @brief Names that are to be displayed in place of log level numeric values.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.  Entries are copied directly into this
/// array's own storage rather than being separate string-literal objects
/// pointed to from it, so tagging the array alone protects every entry.
static const char _logLevelNames[NUM_LOG_LEVELS][9] KEEP_IN_FLASH = {
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
  = "[%lld.%09lld %s:%u:%u %s:%s:%d %s] ";

/// @var _localhost
///
/// @brief Fallback hostname used in a log message header when the scheduler
/// hasn't set a real hostname yet.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _localhost[] KEEP_IN_FLASH = "localhost";

/// @var _noLogBufferMessage
///
/// @brief Message written to the console if we cant get logBuffer from the HAL.
static const char _noLogBufferMessage[] KEEP_IN_FLASH
  = "logMessage: Cannot get logBuffer from HAL.  Discarding message.\n";

/// @var _droppedReentrantLogMessage
///
/// @brief Message written to the console when a log raised from inside the
/// send to the logger process can't be written immediately either.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _droppedReentrantLogMessage[] KEEP_IN_FLASH
  = "logMessage: Discarding log raised while logging from line ";

/// @var _droppedReentrantLogInfix
///
/// @brief Infix component of message started by _droppedReentrantLogMessage.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _droppedReentrantLogInfix[] KEEP_IN_FLASH
  = " of the file at 0x";

/// @var _droppedReentrantLogSuffix
///
/// @brief Terminator for _droppedReentrantLogMessage.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _droppedReentrantLogSuffix[] KEEP_IN_FLASH = ".\n";

/// @var _sendingToLogger
///
/// @brief Whether or not a logMessage call is currently sending a message to
/// the logger process.  Sending re-enters the IPC path, which logs its own
/// failures, so this is what keeps such a log from recursing back through the
/// send and consuming the stack a second time.
///
/// @note Strictly speaking, this should be process-specific storage because
/// this is a condition that can affect each process's stack.  However, the only
/// processes that call logMessage are kernel processes and kernel processes
/// are cooperative, not preemptive.  So, there's no reason today (Oct 1, 2026)
/// to make this process-specific.  We may need to revisit this again in the
/// future if we decide to make kernel processes preemptive.
static bool _sendingToLogger = false;

/// @var numLogEntries
///
/// @brief The number of LogEntry objects held in the logEntries array and
/// number of ProcessMessage objects hel in the logMessages array.
size_t numLogEntries = 0;

/// @var logEntries
///
/// @brief Array of LogEntry objects to use in communication with the logger
/// process.
LogEntry *logEntries = NULL;

/// @var logMessages
///
/// @brief Pool of ProcessMessage objects used to deliver log entries to
/// the logger process.  One per logEntries slot.
ProcessMessage *logMessages = NULL;

/// @fn void logDroppedReentrantLog(int lineNumber, const char *fileName)
///
/// @brief Report a log message that was raised from inside the send to the
/// logger process and could not be written immediately either.  Keeping this
/// out of logMessage keeps its strings and its stack usage off that function's
/// hot path.
///
/// @param lineNumber The line number the discarded message was raised from.
/// @param fileName The name of the file the discarded message was raised from.
///   Only its address is printed:  this path only runs when the strings have
///   been removed from the binary, so the characters it points at aren't there
///   to print.
///
/// @return This function returns no value.
void logDroppedReentrantLog(int lineNumber, const char *fileName) {
  printString(_droppedReentrantLogMessage);
  printInt(lineNumber);
  printString(_droppedReentrantLogInfix);
  printHex((uintptr_t) fileName);
  printString(_droppedReentrantLogSuffix);
}

/// @fn int logMessage(LogLevel logLevel,
///   const char *fileName, const char *functionName, int lineNumber,
///   const char *format, ...)
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
int logMessage(LogLevel logLevel,
  const char *fileName, const char *functionName, int lineNumber,
   const char *format, ...
) {
  // Declare our variables upfront since we use gotos.
  va_list args;
  char *slashAt = NULL;
  union {
    int64_t i64Value;
    int     intValue;
  } temp;
  temp.i64Value = 0; // In case we fail to get a timestamp from the HAL.
  
  // Don't check the return value of getElapsedNanoseconds here.  A failure
  // isn't fatal.  Do this before anything else to get as accurate a timestamp
  // as possible.
  HAL->clock->getElapsedNanoseconds(0, &temp.i64Value);

  StaticLogs *staticLogs = NULL;
  char *logBuffer = NULL;

  LogEntry *logEntry = NULL;
  ProcessMessage *processMessage = NULL;
  HAL->memory->staticLogs(&staticLogs);

  // Once cached, a non-empty hostname proves the scheduler was initialized
  // at some point, which is all we need this check for.  Only pay for the
  // schedulerPeekHostname() call while that hasn't happened yet.
  if ((_cachedHostname == NULL) || (*_cachedHostname == '\0')) {
    const char *hostname = schedulerPeekHostname();
    if (*hostname != '\0') {
      _cachedHostname = hostname;
    }
  }
  bool schedulerReady
    = ((_cachedHostname != NULL) && (*_cachedHostname != '\0'));

  if ((schedulerReady && (loggerPid != 0))
    || (staticLogs == NULL)
  ) {
    // Select pointers from our statically-allocated arrays.
    for (size_t ii = 0; ii < numLogEntries; ii++) {
      if ((logEntries[ii].inUse == false)
        && (processMessageInUse(&logMessages[ii]) == false)
      ) {
        logEntry = &logEntries[ii];
        processMessage = &logMessages[ii];
        break;
      }
    }
  } else if (staticLogs->numEntries
    < (sizeof(staticLogs->logEntries) / sizeof(staticLogs->logEntries[0]))
  ) {
    // Select a LogEntry pointer from staticLogs.  No ProcessMessage pointer is
    // necessary.  The bound stops an overrun from running past the entries the
    // platform reserved, into heap the logger hasn't released yet.
    logEntry = &staticLogs->logEntries[staticLogs->numEntries];
  }
  if (logEntry == NULL) {
    // Nothing we can do.  The log message is just silently dropped.
    return -ENOMEM;
  }
  logEntry->inUse = true;
  
  // Get the rest of the fixed values.
  logEntry->timeStamp = temp.i64Value;
  logEntry->logLevel = logLevel;
  logEntry->fileName = (int) (((intptr_t) fileName)
    - ((intptr_t) _referencePoint));
  logEntry->functionName = (int) (((intptr_t) functionName)
    - ((intptr_t) _referencePoint));
  logEntry->lineNumber = lineNumber;
  logEntry->processId = getRunningPid();
  logEntry->threadId = 1;
  logEntry->format = (int) (((intptr_t) format)
    - ((intptr_t) _referencePoint));
  
  // Get the va_list values.
  temp.intValue = (int) ((sizeof(logEntry->args))
    / (sizeof(logEntry->args[0])));
  va_start(args, format);
  for (int ii = 0; ii < temp.intValue; ii++) {
    logEntry->args[ii] = va_arg(args, uintptr_t);
  }
  va_end(args);
  
  if ((schedulerReady == false) || (loggerPid == 0)) {
    if (staticLogs != NULL) {
      // Logger isn't up yet but will be.  Write to the staticLogs area.
      goto writeStaticLog;
    } else if (HAL->memory->stringsPresent == true) {
      // Write this entry immediately.
      goto writeImmediate;
    }
    
    // If we made it this far then we have no ability to log a static log for
    // the logger process to lookup AND strings are not compiled into the OS
    // image, so we can't print it as an immediate either.  This is a bug in
    // the HAL but there's nothing we can do at runtime, so just return to the
    // caller that this isn't supported.
    logEntry->inUse = false;
    return -ENOTSUP;
  }
  
  if (_sendingToLogger == true) {
    // This log was raised from inside the send below.  Going through the
    // logger again would recurse through the whole IPC path, so write it
    // immediately instead.
    if (HAL->memory->stringsPresent == true) {
      goto writeImmediate;
    }
    logDroppedReentrantLog(lineNumber, fileName);
    logEntry->inUse = false;
    return -EAGAIN;
  }
  
  if (processMessageInit(processMessage,
    LOGGER_COMMAND_SIGNATURE | LOGGER_LOG_MESSAGE,
    logEntry, sizeof(*logEntry), false) != processSuccess
  ) {
    processMessageRelease(processMessage);
    logEntry->inUse = false;
    return -EAGAIN;
  }
  
  _sendingToLogger = true;
  int sendStatus = sendProcessMessageToPid(loggerPid, processMessage);
  _sendingToLogger = false;
  if (sendStatus != 0) {
    processMessageRelease(processMessage);
    if (HAL->memory->stringsPresent == true) {
      // Write this entry immediately.
      goto writeImmediate;
    }
    logEntry->inUse = false;
    return -EAGAIN;
  }
  
  return 0;
  
writeImmediate:
  HAL->memory->logBuffer(&logBuffer);
  if (logBuffer == NULL) {
    // Either we lack the necessary HAL capability to get the logBuffer, or the
    // HAL on this system doesn't provide one.  Either way, we can't print the
    // user's message.  Use printString to alert of the problem and bail.
    printString(_noLogBufferMessage);
    logEntry->inUse = false;
    return -ENOMEM;
  }

  // Print the header.
  slashAt = strrchr(fileName, '/');
  if (slashAt != NULL) {
    fileName = slashAt + 1;
  }
  
  snprintf(logBuffer, HAL->memory->logBufferSize,
    _logHeaderFormat,
    (long long int) (logEntry->timeStamp / ((int64_t) 1000000000)),
    (long long int) (logEntry->timeStamp % ((int64_t) 1000000000)),
    schedulerReady ? _cachedHostname : _localhost,
    logEntry->processId, logEntry->threadId,
    fileName, functionName, lineNumber, _logLevelNames[logLevel]);
  int rv = printString(logBuffer);
  if (rv < 0) {
    logEntry->inUse = false;
    return rv;
  }

  // Print the log message.
  va_start(args, format);
  vsnprintf(logBuffer, HAL->memory->logBufferSize,
    format, args);
  va_end(args);
  rv += printString(logBuffer);
  logEntry->inUse = false;
  return rv;

writeStaticLog:
  // Increment numEntries in the static log area.
  staticLogs->numEntries++;
  
  return 0;
}

