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

/// @file GetString.c
///
/// @brief Userspace logger overlay for extracting a specified string out of a
/// binary.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ExecutiveProcesses.h"
#include "UserspaceLogger.h"

/// @fn void* getString(void *args)
///
/// @brief Find the location of the _referencePoint variable in the kernel's
/// logger.
///
/// @param args A pointer to the LoggerState maintained by the process.  The
///   args member variable will be the offset of the string to retrieve, cast
///   to a void*.
///
/// @return Sets the args member variable to the buffer pointer on success, sets
/// it to NULL on failure.  Always returns the LoggerState pointer passed in.
void* getString(void *args) {
  LoggerState *loggerState = (LoggerState*) args;
  intptr_t offset = loggerState->referenceOffset
    + ((intptr_t) loggerState->args);
  loggerState->args = NULL;
  if (fseek(loggerState->binaryFile, offset, SEEK_SET) != 0) {
    printString("logger: ERROR! Could not seek to offset ");
    printInt(offset);
    printString(" in binaryFile\n");
    goto exit; // loggerState->args remains NULL
  }
  if (fgets(loggerState->buffer, sizeof(loggerState->buffer),
    loggerState->binaryFile) == loggerState->buffer
  ) {
    loggerState->args = loggerState->buffer;
  }
  
exit:
  return loggerState;
}

