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

/// @struct LogEntry
///
/// @brief Descriptor for a single, stringless log entry in memory.
///
/// @param timeStamp 64-bit microseconds since midnight, Jan 1, 1970.
/// @param fileName The address of the name of the file the log message comes
///   from.
/// @param lineNumber The line number in the file the log message comes from.
/// @param formatString The address of the format string for the log message.
/// @param args Up to three (3) arguments provided for the log message.
typedef struct LogEntry {
  int64_t  timeStamp;
  uint32_t fileName;
  uint32_t lineNumber;
  uint32_t formatString;
  uint32_t args[3];
} LogEntry;

/// @struct StaticLogs
///
/// @brief Metadata and log entries for messages logged before the logger
/// process was running.
///
/// @param metadata The metadata for the log entries.  The LogEntry member keeps
///   the metadata aligned to the size of a LogEntry message.  The numMessages
///   member tracks the number of messages logged statically before the logger
///   was running.
/// @param logEntries Array of LogEntry objects that is numMessages in size.
///   This is a variable-length array.  The size of one element is just to keep
///   some compilers from complaining.
typedef struct StaticLogs {
  union {
    LogEntry logEntry;
    unsigned int numMessages;
  } metadata;
  LogEntry logEntries[1];
} StaticLogs;

// Exported functions.
void* runLogger(void *args);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LOGGER_H
