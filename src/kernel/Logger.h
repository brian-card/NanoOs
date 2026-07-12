///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              05.16.2026
///
/// @file              Logger.h
///
/// @brief             Definitions for a stringless logging system.
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

#ifndef LOGGER_H
#define LOGGER_H

#include "NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def LOGGER_COMMAND_SIGNATURE
///
/// @brief The 64-bit signature used to validate that a command is a logger
/// command.  "\0LOGPROC" as a little-endian value.
#define LOGGER_COMMAND_SIGNATURE ((int64_t) 0x43524F50474F4C00)

/// @struct LogEntry
///
/// @brief Descriptor for a single, stringless log entry in memory.
///
/// @param timeStamp 64-bit nanoseconds since midnight, Jan 1, 1970.
/// @param logLevel The log level the message was logged at.
/// @param fileName The address offset of the name of the file the log message
///   comes from.
/// @param lineNumber The line number in the file the log message comes from.
/// @param pid The ProcessId of the process logging the message.
/// @param formatThe address offset of the format string for the log message.
/// @param args Up to four (4) 32-bit arguments provided for the log message.
typedef struct LogEntry {
  int64_t   timeStamp;
  uint16_t  logLevel;
  int16_t   fileName;
  uint16_t  lineNumber;
  ProcessId pid;
  int16_t   format;
  uint32_t  args[4];
} LogEntry;

/// @struct LogMessageCommandArgs
///
/// @brief Arguments and return value for the logMessage command handler.
///
/// @param logEntry The embedded LogEntry to display.
/// @param returnValue The integer returnValue from the handler.
typedef struct LogMessageCommandArgs {
  LogEntry logEntry;
  int returnValue;
} LogMessageCommandArgs;

/// @struct StaticLogs
///
/// @brief Metadata and log entries for messages logged before the logger
/// process was running.
///
/// @param metadata The metadata for the log entries.  The LogEntry member keeps
///   the metadata aligned to the size of a LogEntry message.  The numEntries
///   member tracks the number of messages logged statically before the logger
///   was running.
/// @param logEntries Array of LogEntry objects that is numMessages in size.
///   This is a variable-length array.  The size of one element is just to keep
///   some compilers from complaining.
typedef struct StaticLogs {
  union {
    LogEntry logEntry;
    unsigned int numEntries;
  } metadata;
  LogEntry logEntries[1];
} StaticLogs;

/// @enum LogLevel
///
/// @brief This defines the possible log levels for log messages.
typedef uint16_t LogLevel;
#define LOG_LEVEL_NEVER     0
#define LOG_LEVEL_FLOOD     1
#define LOG_LEVEL_TRACE     2
#define LOG_LEVEL_DEBUG     3
#define LOG_LEVEL_DETAIL    4
#define LOG_LEVEL_INFO      5
#define LOG_LEVEL_WARN      6
#define LOG_LEVEL_ERROR     7
#define LOG_LEVEL_CRITICAL  8
#define LOG_LEVEL_BOX       9
#define LOG_LEVEL_NONE     10
#define NUM_LOG_LEVELS     11

#ifndef LOG_THRESHOLD
#define LOG_THRESHOLD LOG_LEVEL_DETAIL
#endif // LOG_THRESHOLD

#define logNever(format, ...) {}

#if (LOG_THRESHOLD == LOG_LEVEL_FLOOD)
#define logFlood(format, ...) \
  logMessage(LOG_LEVEL_FLOOD, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define logFlood(format, ...) {}
#endif // (LOG_THRESHOLD == LOG_LEVEL_FLOOD)

#if (LOG_THRESHOLD <= LOG_LEVEL_TRACE)
#define logTrace(format, ...) \
  logMessage(LOG_LEVEL_TRACE, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define logTrace(format, ...) {}
#endif // (LOG_THRESHOLD <= LOG_LEVEL_TRACE)

#if (LOG_THRESHOLD <= LOG_LEVEL_DEBUG)
#define logDebug(format, ...) \
  logMessage(LOG_LEVEL_DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define logDebug(format, ...) {}
#endif // (LOG_THRESHOLD <= LOG_LEVEL_DEBUG)

#if (LOG_THRESHOLD <= LOG_LEVEL_DETAIL)
#define logDetail(format, ...) \
  logMessage(LOG_LEVEL_DETAIL, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define logDetail(format, ...) {}
#endif // (LOG_THRESHOLD <= LOG_LEVEL_DETAIL)

#if (LOG_THRESHOLD <= LOG_LEVEL_INFO)
#define logInfo(format, ...) \
  logMessage(LOG_LEVEL_INFO, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define logInfo(format, ...) {}
#endif // (LOG_THRESHOLD <= LOG_LEVEL_INFO)

#if (LOG_THRESHOLD <= LOG_LEVEL_WARN)
#define logWarn(format, ...) \
  logMessage(LOG_LEVEL_WARN, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define logWarn(format, ...) {}
#endif // (LOG_THRESHOLD <= LOG_LEVEL_WARN)

#if (LOG_THRESHOLD <= LOG_LEVEL_ERROR)
#define logError(format, ...) \
  logMessage(LOG_LEVEL_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define logError(format, ...) {}
#endif // (LOG_THRESHOLD <= LOG_LEVEL_ERROR)

#if (LOG_THRESHOLD <= LOG_LEVEL_CRITICAL)
#define logCritical(format, ...) \
  logMessage(LOG_LEVEL_CRITICAL, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define logCritical(format, ...) {}
#endif // (LOG_THRESHOLD <= LOG_LEVEL_CRITICAL)

#if (LOG_THRESHOLD <= LOG_LEVEL_BOX)
#define logBox(format, ...) \
  logMessage(LOG_LEVEL_BOX, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
#define logBox(format, ...) {}
#endif // (LOG_THRESHOLD <= LOG_LEVEL_BOX)

/// @enum LoggerCommandResponse
///
/// @brief Commands and responses understood by the logger inter-process message
/// handler.
typedef enum LoggerCommandResponse {
  // Commands:
  LOGGER_LOG_MESSAGE,
  NUM_LOGGER_COMMANDS,
} LoggerCommand;

// Exported functions.
int logMessage(LogLevel logLevel, const char *fileName, uint16_t lineNumber,
   const char *format, ...);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LOGGER_H
