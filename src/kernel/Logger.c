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
#include "string.h"

// NanoOs includes:
#include "Hal.h"
#include "Logger.h"

/// @var _referencePoint
///
/// @brief Variable that will be used to compute the relative offsets of string
/// parameters that are logged.
const char *_referencePoint = "4abc4abc4abc4abc4abc4abc4abc4abc";

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
  LogEntry logEntry;
  
  // Don't check the return value of getElapsedNanoseconds here.  A failure
  // isn't fatal.  Do this before anything else to get as accurate a timestamp
  // as possible.
  HAL->clock->getElapsedNanoseconds(0, &logEntry.timeStamp);
  
  // Get the rest of the fixed values.
  logEntry.logLevel = logLevel;
  logEntry.fileName = (int16_t) (((intptr_t) _referencePoint)
    - ((intptr_t) fileName));
  logEntry.lineNumber = lineNumber;
  logEntry.format = (int16_t) (((intptr_t) _referencePoint)
    - ((intptr_t) format));
  
  // Get the va_list values.
  va_list args;
  va_start(args, format);
  for (int ii = 0;
    ii < (int) ((sizeof(logEntry.args)) / (sizeof(logEntry.args[0])));
    ii++
  ) {
    logEntry.args[ii] = (uint32_t) va_arg(args, int);
  }
  va_end(args);
  
  
  memcpy(&HAL->memory->staticLogs->logEntries[
    HAL->memory->staticLogs->metadata.numEntries],
    &logEntry, sizeof(LogEntry));
  HAL->memory->staticLogs->metadata.numEntries++;
  
  return 0;
}

