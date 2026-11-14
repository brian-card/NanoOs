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

/// @file main.c
///
/// @brief Entrypoint into the loopback driver process: a console port
/// forwarding driver that writes the port's output to a UART.  It provides no
/// input, and exists to exercise forwarding ports on any platform.

#include "ExecutiveProcesses.h"
#include "Console.h"
#include "ConsoleOutput.h"

/// @fn static void loopbackWriteString(int32_t uart, const char *string)
///
/// @brief Write a string to a UART, translating each newline to a carriage
/// return and a newline.
///
/// @param uart The ID of the UART to write to.
/// @param string The NUL-terminated string to write.
///
/// @return This function returns no value.
static void loopbackWriteString(int32_t uart, const char *string) {
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

int main(int argc, char **argv) {
  int32_t uart = 1;
  if ((argc > 1) && (argv[1][0] >= '0') && (argv[1][0] <= '9')) {
    uart = argv[1][0] - '0';
  }

  while (1) {
    ProcessMessage *processMessage = processMessageQueueWait(NULL);
    const char *text = consoleOutputText(processMessage);
    if (text != NULL) {
      loopbackWriteString(uart, text);
    }
    consoleOutputDone(processMessage);
  }

  return 0;
}
