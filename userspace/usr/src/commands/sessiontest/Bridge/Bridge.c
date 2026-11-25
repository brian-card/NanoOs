///////////////////////////////////////////////////////////////////////////////
///
/// @file              Bridge.c
///
/// @brief             sessiontest's bridge:  it opens a session and bridges
///                    it to the console port sessiontest runs on.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "ConsoleOutput.h"
#include "UserMessages.h"
#include "UserProcesses.h"
#include "SessionTest.h"

/// @def INPUT_QUEUE_SIZE
///
/// @brief How many bytes typed at the console the bridge holds for the
/// session.  It asks the console for more only while a whole buffer of input
/// fits, so that nothing typed is dropped.
#define INPUT_QUEUE_SIZE 256

/// @def INPUT_WAIT_NS
///
/// @brief How long, in nanoseconds, the bridge waits for a message while the
/// session is still reading the input it was last sent.
#define INPUT_WAIT_NS 10000000L

/// @struct Bridge
///
/// @brief The state of the bridge between the console port and a session.
///
/// @param uart The ID of the UART the session's output is written to.
/// @param consolePid The console's ProcessId.
/// @param port The session's console port.
/// @param queue Bytes typed at the console that the session hasn't been sent.
/// @param head The index in queue of the next byte to send.
/// @param count The number of bytes in queue.
/// @param waiting Whether the console has been asked for input it hasn't
///   returned yet.
/// @param inputArgs The arguments of the input being sent to the session.
/// @param inputMessage The CONSOLE_INPUT_READY message the session is still
///   reading, or NULL.
/// @param winsizeArgs The arguments of the size being sent to the session.
/// @param winsizeMessage The CONSOLE_SET_WINSIZE message the console hasn't
///   finished, or NULL.
typedef struct Bridge {
  int32_t                uart;
  ProcessId              consolePid;
  uint8_t                port;
  uint8_t                queue[INPUT_QUEUE_SIZE];
  uint16_t               head;
  uint16_t               count;
  bool                   waiting;
  ConsoleInputReadyArgs  inputArgs;
  ProcessMessage        *inputMessage;
  ConsoleWinsizeArgs     winsizeArgs;
  ProcessMessage        *winsizeMessage;
} Bridge;

/// @fn static void writeString(int32_t uart, const char *string)
///
/// @brief Write a string to a UART, translating each newline to a carriage
/// return and a newline.
///
/// @param uart The ID of the UART to write to.
/// @param string The NUL-terminated string to write.
///
/// @return This function returns no value.
static void writeString(int32_t uart, const char *string) {
  const char *start = string;
  for (const char *cursor = string; ; cursor++) {
    if ((*cursor != '\n') && (*cursor != '\0')) {
      continue;
    }
    getHal()->uart->write(uart, (const uint8_t*) start,
      (ssize_t) (cursor - start), NULL);
    if (*cursor == '\0') {
      break;
    }
    getHal()->uart->write(uart, (const uint8_t*) "\r\n", 2, NULL);
    start = cursor + 1;
  }
}

/// @fn static void waitForInput(Bridge *bridge)
///
/// @brief Ask the console for the next input typed at this port, if there's
/// room for it, without waiting, since the console may be waiting on this
/// process to take the session's echo.
///
/// @param bridge A pointer to the Bridge.
///
/// @return This function returns no value.
static void waitForInput(Bridge *bridge) {
  if (bridge->waiting
    || (INPUT_QUEUE_SIZE - bridge->count < CONSOLE_BUFFER_SIZE)
  ) {
    return;
  }
  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return;
  }
  processMessageInit(message,
    CONSOLE_COMMAND_SIGNATURE | CONSOLE_WAIT_FOR_INPUT, NULL, 0,
    /* waiting= */ false);
  if (sendProcessMessageToPid(bridge->consolePid, message) != processSuccess) {
    processMessageRelease(message);
    return;
  }
  bridge->waiting = true;
}

/// @fn static void sendWinsize(Bridge *bridge, const char *report)
///
/// @brief Set the session's size from a resize report typed at the console,
/// as a terminal sets its session's size when its window is resized.  The
/// report itself still goes to the session as input.
///
/// @param bridge A pointer to the Bridge.
/// @param report The input read, which may be a report:  ESC [ 48 ; rows ;
///   columns ; ... t.
///
/// @return This function returns no value.
static void sendWinsize(Bridge *bridge, const char *report) {
  if ((strncmp(report, "\033[48;", 5) != 0)
    || (bridge->winsizeMessage != NULL)
  ) {
    return;
  }
  char *end = NULL;
  long rows = strtol(&report[5], &end, 10);
  if (*end != ';') {
    return;
  }
  long columns = strtol(end + 1, &end, 10);
  if ((*end != ';') || (rows <= 0) || (columns <= 0)) {
    return;
  }

  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return;
  }
  bridge->winsizeArgs.rows = (uint16_t) rows;
  bridge->winsizeArgs.columns = (uint16_t) columns;
  bridge->winsizeArgs.returnValue = 0;
  processMessageInit(message, CONSOLE_COMMAND_SIGNATURE | CONSOLE_SET_WINSIZE,
    &bridge->winsizeArgs, sizeof(bridge->winsizeArgs), /* waiting= */ true);
  if (sendProcessMessageToPid(bridge->consolePid, message) != processSuccess) {
    processMessageRelease(message);
    return;
  }
  bridge->winsizeMessage = message;
}

/// @fn static void queueInput(Bridge *bridge, const char *bytes)
///
/// @brief Hold bytes typed at the console for the session.
///
/// @param bridge A pointer to the Bridge.
/// @param bytes The NUL-terminated bytes.
///
/// @return This function returns no value.
static void queueInput(Bridge *bridge, const char *bytes) {
  for (; (*bytes != '\0') && (bridge->count < INPUT_QUEUE_SIZE); bytes++) {
    bridge->queue[(bridge->head + bridge->count) % INPUT_QUEUE_SIZE]
      = (uint8_t) *bytes;
    bridge->count++;
  }
}

/// @fn static void sendInput(Bridge *bridge)
///
/// @brief Send the session the next bytes held for it, once it has read the
/// last ones, and forget a size the console has finished setting.
///
/// @param bridge A pointer to the Bridge.
///
/// @return This function returns no value.
static void sendInput(Bridge *bridge) {
  if ((bridge->winsizeMessage != NULL)
    && processMessageDone(bridge->winsizeMessage)
  ) {
    processMessageRelease(bridge->winsizeMessage);
    bridge->winsizeMessage = NULL;
  }
  if (bridge->inputMessage != NULL) {
    if (processMessageDone(bridge->inputMessage) == false) {
      return;
    }
    processMessageRelease(bridge->inputMessage);
    bridge->inputMessage = NULL;
  }
  if (bridge->count == 0) {
    return;
  }

  ProcessMessage *message = getAvailableMessage();
  if (message == NULL) {
    return;
  }
  uint8_t numBytes = 0;
  while ((numBytes < CONSOLE_INPUT_READY_SIZE) && (bridge->count > 0)) {
    bridge->inputArgs.bytes[numBytes++] = bridge->queue[bridge->head];
    bridge->head = (bridge->head + 1) % INPUT_QUEUE_SIZE;
    bridge->count--;
  }
  bridge->inputArgs.portId = bridge->port;
  bridge->inputArgs.numBytes = numBytes;
  processMessageInit(message, CONSOLE_COMMAND_SIGNATURE | CONSOLE_INPUT_READY,
    &bridge->inputArgs, sizeof(bridge->inputArgs), /* waiting= */ true);
  if (sendProcessMessageToPid(bridge->consolePid, message) != processSuccess) {
    processMessageRelease(message);
    return;
  }
  bridge->inputMessage = message;
}

/// @fn static ProcessMessage* nextMessage(Bridge *bridge)
///
/// @brief Wait for the next message, but only briefly while the session is
/// still reading input or the console setting its size, so that the next
/// input goes as soon as it can.
///
/// @param bridge A pointer to the Bridge.
///
/// @return Returns the message, or NULL if the wait ended without one.
static ProcessMessage* nextMessage(Bridge *bridge) {
  if ((bridge->inputMessage == NULL) && (bridge->winsizeMessage == NULL)) {
    return processMessageQueueWait(NULL);
  }

  struct timespec deadline;
  timespec_get(&deadline, TIME_UTC);
  deadline.tv_nsec += INPUT_WAIT_NS;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec++;
    deadline.tv_nsec -= 1000000000L;
  }
  return processMessageQueueWait(&deadline);
}

/// @fn static int bridge(int32_t uart, uint8_t rows, uint8_t columns)
///
/// @brief Open a session and bridge it to this console port until it ends.
///
/// @param uart The ID of the UART to write the session's output to.
/// @param rows The number of rows of the session.
/// @param columns The number of columns of the session.
///
/// @return Returns 0 when the session has ended, 1 if it couldn't be opened.
static int bridge(int32_t uart, uint8_t rows, uint8_t columns) {
  // On the heap, since the overlay's own data doesn't survive another
  // process's overlay running, and the console reads the input arguments.
  Bridge *bridge = (Bridge*) calloc(1, sizeof(Bridge));
  if (bridge == NULL) {
    fputs("sessiontest: out of memory\n", stderr);
    return 1;
  }
  bridge->uart = uart;
  if (findProcessByName(CONSOLE_PROCESS_NAME, &bridge->consolePid) != 0) {
    fputs("sessiontest: no console\n", stderr);
    free(bridge);
    return 1;
  }

  struct termios original;
  struct termios raw;
  tcgetattr(STDIN_FILENO, &original);
  raw = original;
  raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);

  uint8_t shell = 0;
  if (windowServerOpenSession(rows, columns, getpid(), &bridge->port, &shell)
    != 0
  ) {
    int openErrno = errno;
    tcsetattr(STDIN_FILENO, TCSANOW, &original);
    fprintf(stderr, "sessiontest: open failed: %s\n", strerror(openErrno));
    free(bridge);
    return 1;
  }
  char line[64];
  snprintf(line, sizeof(line), "sessiontest: opened port %d shell %d\n",
    bridge->port, shell);
  writeString(uart, line);

  while (1) {
    sendInput(bridge);
    waitForInput(bridge);
    ProcessMessage *message = nextMessage(bridge);
    if (message == NULL) {
      continue;
    }

    int64_t type = processMessageType(message);
    if (type == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_RETURNING_INPUT)) {
      ConsoleBuffer *consoleBuffer
        = (ConsoleBuffer*) processMessageData(message);
      if (consoleBuffer != NULL) {
        sendWinsize(bridge, consoleBuffer->buffer);
        queueInput(bridge, consoleBuffer->buffer);
      }
      if (processMessageWaiting(message) == false) {
        processMessageRelease(message);
      } else {
        processMessageSetDone(message);
      }
      bridge->waiting = false;
    } else if (type == (CONSOLE_COMMAND_SIGNATURE | CONSOLE_SESSION_ENDED)) {
      uint8_t port = (uint8_t) (uintptr_t) processMessageData(message);
      processMessageRelease(message);
      if (port == bridge->port) {
        break;
      }
    } else {
      const char *text = consoleOutputText(message);
      if (text != NULL) {
        writeString(uart, text);
      }
      consoleOutputDone(message);
    }
  }

  if (bridge->inputMessage != NULL) {
    processMessageWaitForDone(bridge->inputMessage, NULL);
    processMessageRelease(bridge->inputMessage);
  }
  if (bridge->winsizeMessage != NULL) {
    processMessageWaitForDone(bridge->winsizeMessage, NULL);
    processMessageRelease(bridge->winsizeMessage);
  }
  tcsetattr(STDIN_FILENO, TCSANOW, &original);
  snprintf(line, sizeof(line), "sessiontest: session ended port %d\n",
    bridge->port);
  writeString(uart, line);
  free(bridge);
  return 0;
}

/// @fn void* bridgeSession(void *args)
///
/// @brief Open a session and bridge it to this console port until it ends.
///
/// @param args A pointer to the BridgeArgs.
///
/// @return Always returns the provided pointer.
void* bridgeSession(void *args) {
  BridgeArgs *bridgeArgs = (BridgeArgs*) args;
  bridgeArgs->status
    = bridge(bridgeArgs->uart, bridgeArgs->rows, bridgeArgs->columns);
  return args;
}
