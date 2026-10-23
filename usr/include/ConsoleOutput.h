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


/// @file ConsoleOutput.h
///
/// @brief Handling of the console output that the processes servicing
/// forwarding console ports are sent.

#ifndef CONSOLE_OUTPUT_H
#define CONSOLE_OUTPUT_H

#include "ExecutiveProcesses.h"
#include "../../src/kernel/Console.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @fn static inline const char* consoleOutputText(
///   ProcessMessage *processMessage)
///
/// @brief Get the text a console output message carries:  the string of a
/// CONSOLE_FORWARD_STRING message from the console, or the buffer of a
/// CONSOLE_WRITE_BUFFER message from a process writing its output directly.
///
/// @param processMessage A pointer to the received ProcessMessage.
///
/// @return Returns a pointer to the NUL-terminated text, or NULL if the
/// message is not console output.
static inline const char* consoleOutputText(ProcessMessage *processMessage) {
  int64_t type = processMessageType(processMessage);
  if (type == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_FORWARD_STRING)) {
    return (const char*) processMessageData(processMessage);
  } else if (type == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_WRITE_BUFFER)) {
    ConsoleBuffer *consoleBuffer
      = (ConsoleBuffer*) processMessageData(processMessage);
    return (consoleBuffer != NULL) ? consoleBuffer->buffer : NULL;
  }

  return NULL;
}

/// @fn static inline void consoleOutputDone(ProcessMessage *processMessage)
///
/// @brief Finish handling a message:  return the ConsoleBuffer of a
/// CONSOLE_WRITE_BUFFER message to the console, which is the receiver's job
/// as it is the console's, then mark the message done and release it if its
/// sender isn't waiting for it.
///
/// @param processMessage A pointer to the handled ProcessMessage.
///
/// @return This function returns no value.
static inline void consoleOutputDone(ProcessMessage *processMessage) {
  ProcessId consolePid = PROCESS_ID_NOT_SET;
  if ((processMessageType(processMessage)
      == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_WRITE_BUFFER))
    && (processMessageData(processMessage) != NULL)
    && (findProcessByName(CONSOLE_PROCESS_NAME, &consolePid) == 0)
  ) {
    // The release has to reach the console before the writer can ask for a
    // buffer again, so it's sent before the message is marked done.
    ProcessMessage *release = getAvailableMessage();
    if (release != NULL) {
      processMessageInit(release,
        CONSOLE_COMMAND_SIGNATURE | CONSOLE_RELEASE_BUFFER,
        processMessageData(processMessage), sizeof(ConsoleBuffer),
        /* waiting= */ false);
      if (sendProcessMessageToPid(consolePid, release) != processSuccess) {
        processMessageRelease(release);
      }
    }
  }

  processMessageSetDone(processMessage);
  if (processMessageWaiting(processMessage) == false) {
    processMessageRelease(processMessage);
  }
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CONSOLE_OUTPUT_H
