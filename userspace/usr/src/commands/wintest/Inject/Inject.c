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



/// @file Inject.c
///
/// @brief wintest inject:  trying to send the console input and a size for
/// a session, which only the window server may.

#include "Wintest.h"
#include "../../../src/kernel/Console.h"

/// @fn static int inject(int64_t command, void *args, size_t size)
///
/// @brief Try to send the console a message for a session, as only the
/// window server may.
///
/// @param command The ConsoleCommand.
/// @param args The arguments, from MESSAGE_ARGS_CREATE with
///   MESSAGE_ARGS_ONE_WAY.
/// @param size Their size.
///
/// @return Returns 0 if the send went through, or a negative errno value.
static int inject(int64_t command, void *args, size_t size) {
  ProcessId console = 0;
  ProcessMessage *message = getAvailableMessage();

  if ((args == NULL) || (message == NULL)
    || (findSystemProcess(CONSOLE_PROCESS_NAME, &console) != 0)
  ) {
    if (message != NULL) {
      processMessageRelease(message);
    }

    if (args != NULL) {
      MESSAGE_ARGS_DESTROY(args);
    }

    return -ENOMEM;
  }

  processMessageInit(message, CONSOLE_COMMAND_SIGNATURE | command, args,
    size, /* waiting= */ false);
  errno = 0;

  if (sendProcessMessageToPid(console, message) != processSuccess) {
    int rc = (errno != 0) ? -errno : -EIO;

    processMessageRelease(message);
    MESSAGE_ARGS_DESTROY(args);
    return rc;
  }

  return 0;
}

/// @def WINTEST_INJECTED_INPUT
///
/// @brief The input injectAll tries to send every console port.
#define WINTEST_INJECTED_INPUT "zz"

/// @def WINTEST_INJECTED_ROWS
///
/// @brief The rows of the size injectAll tries to set.
#define WINTEST_INJECTED_ROWS 3

/// @def WINTEST_INJECTED_COLUMNS
///
/// @brief The columns of the size injectAll tries to set.
#define WINTEST_INJECTED_COLUMNS 4

/// @fn static void injectAll(void)
///
/// @brief Try to send every console port the input "zz" and the size 3x4,
/// printing whether each kind of send went through.
///
/// @return This function returns no value.
static void injectAll(void) {
  int inputRc = 0;

  for (uint8_t port = 0; port < CONSOLE_NUM_PORTS; port++) {
    ConsoleInputReadyArgs *inputArgs = MESSAGE_ARGS_CREATE(
      ConsoleInputReadyArgs, MESSAGE_ARGS_ONE_WAY);
    if (inputArgs != NULL) {
      inputArgs->portId = port;
      inputArgs->numBytes = WINTEST_LITERAL_LENGTH(WINTEST_INJECTED_INPUT);
      memcpy(inputArgs->bytes, WINTEST_INJECTED_INPUT, inputArgs->numBytes);
    }

    int rc = inject(CONSOLE_INPUT_READY, inputArgs, sizeof(*inputArgs));
    if (rc < 0) {
      inputRc = rc;
    }
  }

  ConsoleWinsizeArgs *winsizeArgs = MESSAGE_ARGS_CREATE(ConsoleWinsizeArgs,
    MESSAGE_ARGS_ONE_WAY);
  if (winsizeArgs != NULL) {
    winsizeArgs->rows = WINTEST_INJECTED_ROWS;
    winsizeArgs->columns = WINTEST_INJECTED_COLUMNS;
    winsizeArgs->returnValue = 0;
  }

  int winsizeRc = inject(CONSOLE_SET_WINSIZE, winsizeArgs,
    sizeof(*winsizeArgs));

  printf("wintest: inject input %s\n",
    (inputRc < 0) ? strerror(-inputRc) : "sent");
  printf("wintest: inject size %s\n",
    (winsizeRc < 0) ? strerror(-winsizeRc) : "sent");
}

/// @fn void* wintestInject(void *args)
///
/// @brief Try to send every console port input and a size, printing
/// whether each kind of send went through.
///
/// @param args A pointer to the Wintest.
///
/// @return Always returns the provided pointer.
void* wintestInject(void *args) {
  injectAll();

  return args;
}
