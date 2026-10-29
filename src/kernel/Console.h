///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              11.21.2024
///
/// @file              Console.h
///
/// @brief             Console library for NanoOs.
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

#ifndef CONSOLE_H
#define CONSOLE_H

#include "stdbool.h"
#include "stdint.h"

#include "NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

// ASCII characters that we need in our logic.
#define ASCII_BACKSPACE 8
#define ASCII_NEWLINE  10
#define ASCII_RETURN   13
#define ASCII_ESCAPE   27
#define ASCII_SPACE    32
#define ASCII_DELETE  127

/// @def CONSOLE_PROCESS_NAME
///
/// @brief The name the console process is registered under for
/// findProcessByName.
#define CONSOLE_PROCESS_NAME "console"

/// @def CONSOLE_ESCAPE_DELAY_MS
///
/// @brief How long, in milliseconds, an escape must go without a following
/// byte before it is taken to be the escape key by itself rather than the
/// start of an escape sequence.
#define CONSOLE_ESCAPE_DELAY_MS 50

/// @def CONSOLE_COMMAND_SIGNATURE
///
/// @brief The 64-bit signature used to validate that a command is a console
/// command.  "\0CONSOLE" as a little-endian value.
#define CONSOLE_COMMAND_SIGNATURE ((int64_t) 0x454C4F534E4F4300)

// Forward declarations.
typedef struct ConsoleBuffer ConsoleBuffer;

/// @struct ConsoleGetBufferArgs
///
/// @brief Arguments and return value for the CONSOLE_GET_BUFFER command.
///
/// @param consoleBuffer Pointer to the ConsoleBuffer provided by the console
///   process.
typedef struct ConsoleGetBufferArgs {
  ConsoleBuffer *consoleBuffer;
} ConsoleGetBufferArgs;

/// @struct ConsoleGetOwnedPortArgs
///
/// @brief Arguments and return value for the CONSOLE_GET_OWNED_PORT command.
///
/// @param ownedPort The port that's owned by the caller process.
typedef struct ConsoleGetOwnedPortArgs {
  int ownedPort;
} ConsoleGetOwnedPortArgs;

/// @struct ConsoleModesArgs
///
/// @brief Arguments and return value for the CONSOLE_GET_MODES and
/// CONSOLE_SET_MODES commands.
///
/// @param modes The CONSOLE_MODE_* flags to set, or that are in effect.
/// @param returnValue 0 on success, -1 if the process owns no console port.
typedef struct ConsoleModesArgs {
  uint8_t modes;
  int     returnValue;
} ConsoleModesArgs;

/// @struct ConsoleReleasePidPortArgs
///
/// @param Arguments and return value for the CONSOLE_RELEASE_PID_PORT command.
///
/// @param processId Process ID of the process to release the port of.
typedef struct ConsoleReleasePidPortArgs {
  ProcessId processId;
} ConsoleReleasePidPortArgs;

/// @struct ConsoleGetNumPortsArgs
///
/// @brief Arguments and return value for the CONSOLE_GET_NUM_PORTS command.
///
/// @param numPorts The number of ports returned from the command.
typedef struct ConsoleGetNumPortsArgs {
  int numPorts;
} ConsoleGetNumPortsArgs;

/// @struct ConsoleAddForwardingPortArgs
///
/// @brief Arguments and return value for the CONSOLE_ADD_FORWARDING_PORT
/// command.
///
/// @param forwardingPid The ProcessId of the process to redirect
///   CONSOLE_WRITE_BUFFER commands to.
/// @param portId The index of the port that gets assigned for the process.
/// @param returnValue 0 on success, -errno on failure.
typedef struct ConsoleAddForwardingPortArgs {
  ProcessId     forwardingPid;
  unsigned char portId;
  int           returnValue;
} ConsoleAddForwardingPortArgs;

/// @struct ConsoleGetUartPortArgs
///
/// @brief Arguments and return value for the CONSOLE_GET_UART_PORT command.
///
/// @param uartId The ID of the HAL UART to find the port of.
/// @param portId The index of the port the UART services, or -1 if no port
///   does.
typedef struct ConsoleGetUartPortArgs {
  unsigned char uartId;
  int           portId;
} ConsoleGetUartPortArgs;

/// @enum ConsoleCommandResponse
///
/// @brief The commands and responses that the console understands via
/// inter-process messages.
typedef enum ConsoleCommandResponse {
  // Commands:
  CONSOLE_WRITE_VALUE,
  CONSOLE_GET_BUFFER,
  CONSOLE_WRITE_BUFFER,
  CONSOLE_SET_PORT_SHELL,
  CONSOLE_ASSIGN_PORT,
  CONSOLE_RELEASE_PORT,
  CONSOLE_GET_OWNED_PORT,
  CONSOLE_GET_MODES,
  CONSOLE_SET_MODES,
  CONSOLE_WAIT_FOR_INPUT,
  CONSOLE_RELEASE_PID_PORT,
  CONSOLE_RELEASE_BUFFER,
  CONSOLE_GET_NUM_PORTS,
  CONSOLE_INPUT_READY,
  CONSOLE_ADD_FORWARDING_PORT,
  CONSOLE_GET_UART_PORT,
  NUM_CONSOLE_COMMANDS,
  // Responses:
  CONSOLE_RETURNING_INPUT,
  // Sent to the process that services a forwarding port:
  CONSOLE_FORWARD_STRING,
} ConsoleCommand;

/// @enum ConsoleInputEvent
///
/// @brief What event a byte of input completed, according to
/// consoleProcessByte.
typedef enum ConsoleInputEvent {
  CONSOLE_INPUT_NONE,
  CONSOLE_INPUT_LINE,
  CONSOLE_INPUT_ESCAPE,
  CONSOLE_INPUT_INTERRUPT,
  CONSOLE_INPUT_UNHANDLED,
} ConsoleInputEvent;

/// @enum ConsoleEcho
///
/// @brief What, if anything, a byte of input should echo back to its port.
typedef enum ConsoleEcho {
  CONSOLE_ECHO_NONE,
  CONSOLE_ECHO_CHAR,
  CONSOLE_ECHO_NEWLINE,
  CONSOLE_ECHO_ERASE,
} ConsoleEcho;

/// @enum ConsoleInputState
///
/// @brief Per-port line-editing state, kept in ConsolePort.inputState so that
/// a multi-byte sequence can arrive split across reads.
typedef enum ConsoleInputState {
  CONSOLE_INPUT_STATE_NORMAL,
  CONSOLE_INPUT_STATE_AFTER_RETURN,
  CONSOLE_INPUT_STATE_ESCAPE,
  CONSOLE_INPUT_STATE_ESCAPE_CTRL_SEQ,
  CONSOLE_INPUT_STATE_ESCAPE_SINGLE_SHIFT,
} ConsoleInputState;

/// @enum ConsoleValueType
///
/// @brief Types to be used with the CONSOLE_WRITE_VALUE command.
typedef enum ConsoleValueType {
  CONSOLE_VALUE_CHAR,
  CONSOLE_VALUE_UCHAR,
  CONSOLE_VALUE_INT,
  CONSOLE_VALUE_UINT,
  CONSOLE_VALUE_LONG_INT,
  CONSOLE_VALUE_LONG_UINT,
  CONSOLE_VALUE_FLOAT,
  CONSOLE_VALUE_DOUBLE,
  CONSOLE_VALUE_STRING,
  NUM_CONSOLE_VALUES
} ConsoleValueType;

// Support functions
void releaseConsole(void);
int getOwnedConsolePort(void);
int getConsoleModes(void);
int setConsoleModes(uint8_t modes);

// Exported for unit tests to use
ConsoleInputEvent consoleProcessByte(
  ConsolePort *consolePort, uint8_t byte, ConsoleEcho *echo);
int consoleReadForwardedByte(ConsolePort *consolePort);
bool consoleServicePort(ConsoleState *consoleState, ConsolePort *consolePort);
void handleConsoleMessages(ConsoleState *consoleState, bool block,
  const struct timespec *deadline);
void consoleSetModesCommandHandler(
  ConsoleState *consoleState, ProcessMessage *inputMessage);
ConsoleBuffer* getAvailableConsoleBuffer(
  ConsoleState *consoleState, ProcessId pid);
int releaseConsoleBuffer(
  ConsoleState *consoleState, ConsoleBuffer *consoleBuffer);
int consoleForwardString(ConsolePort *consolePort, const char *string);
void consoleEchoInput(
  ConsolePort *consolePort, ConsoleEcho echo, uint8_t byte);
void consoleInputReadyCommandHandler(
  ConsoleState *consoleState, ProcessMessage *inputMessage);
void consoleAddForwardingPort(
  ConsoleState *consoleState, ProcessMessage *inputMessage);
void consoleReleasePidPortCommandHandler(
  ConsoleState *consoleState, ProcessMessage *inputMessage);
void consoleGetUartPortCommandHandler(
  ConsoleState *consoleState, ProcessMessage *inputMessage);

// Exported processes
void* runConsole(void *args);

// The well-known ProcessId of the console.  Set once, directly, when the
// console process is created; see Console.c for details.
extern ProcessId consolePid;

int printConsoleChar(char message);
int printConsoleUChar(unsigned char message);
int printConsoleInt(int message);
int printConsoleUInt(unsigned int message);
int printConsoleLong(long int message);
int printConsoleULong(long unsigned int message);
int printConsoleFloat(float message);
int printConsoleDouble(double message);
int printConsoleString(const char *message);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CONSOLE_H
