///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              11.21.2024
///
/// @file              Console.h
///
/// @brief             Console library for NanoOs.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
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
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef CONSOLE_H
#define CONSOLE_H

#include "stdbool.h"

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

/// @enum ConsoleCommandResponse
///
/// @brief The commands and responses that the console understands via
/// inter-task messages.
typedef enum ConsoleCommandResponse {
  // Commands:
  CONSOLE_WRITE_VALUE,
  CONSOLE_GET_BUFFER,
  CONSOLE_WRITE_BUFFER,
  CONSOLE_SET_PORT_SHELL,
  CONSOLE_ASSIGN_PORT,
  CONSOLE_ASSIGN_PORT_INPUT,
  CONSOLE_RELEASE_PORT,
  CONSOLE_GET_OWNED_PORT,
  CONSOLE_GET_ECHO_PORT,
  CONSOLE_SET_ECHO_PORT,
  CONSOLE_WAIT_FOR_INPUT,
  CONSOLE_RELEASE_PID_PORT,
  CONSOLE_RELEASE_BUFFER,
  CONSOLE_GET_NUM_PORTS,
  NUM_CONSOLE_COMMANDS,
  // Responses:
  CONSOLE_RETURNING_BUFFER,
  CONSOLE_RETURNING_PORT,
  CONSOLE_RETURNING_INPUT,
} ConsoleCommand;

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
bool getConsoleEcho(void);
int setConsoleEcho(bool desiredEchoState);
int getNumConsolePorts(void);

// Exported tasks
void* runConsole(void *args);

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
