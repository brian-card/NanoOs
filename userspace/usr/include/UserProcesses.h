///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              03.08.2026
///
/// @file              UserProcesses.h
///
/// @brief             Exposed NanoOs kernel functionality related to user
///                    processes.
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

#ifndef USER_PROCESSES_H
#define USER_PROCESSES_H

#include "NanoOsUser.h"
#include "../../../src/kernel/NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

static inline ProcessInfo* getProcessInfo(void) {
  return overlayMap.header.osApi->userApi->getProcessInfo();
}

/// @fn static inline int windowServerRegister(void)
///
/// @brief Make the running process the window server.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
static inline int windowServerRegister(void) {
  return overlayMap.header.osApi->userApi->windowServerRegister();
}

/// @fn static inline int windowServerConnect(uint8_t *pid)
///
/// @brief Connect the running process to the window server.
///
/// @param pid Where to store the window server's ProcessId.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
static inline int windowServerConnect(uint8_t *pid) {
  return overlayMap.header.osApi->userApi->windowServerConnect(pid);
}

/// @fn static inline int windowServerOpenSession(uint8_t rows,
///   uint8_t columns, uint8_t display, uint8_t *port, uint8_t *shell)
///
/// @brief Open a session:  a console port whose input only the running
/// process, the window server, may send, whose output goes to display, and
/// on which the user's shell runs.
///
/// @param rows The number of rows of the session's port.
/// @param columns The number of columns of the session's port.
/// @param display The ProcessId of the process the session's output goes
///   to.  It must run as the window server's user, below executive level
///   unless it's the window server itself.
/// @param port Where to store the session's console port.
/// @param shell Where to store the ProcessId of the session's shell.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
static inline int windowServerOpenSession(uint8_t rows, uint8_t columns,
  uint8_t display, uint8_t *port, uint8_t *shell
) {
  return overlayMap.header.osApi->userApi->windowServerOpenSession(
    rows, columns, display, port, shell);
}

/// @fn static inline int windowServerCloseSession(uint8_t port)
///
/// @brief End a session the running process, the window server, opened.  Its
/// servicers are sent CONSOLE_SESSION_ENDED once it has ended.
///
/// @param port The session's console port.
///
/// @return Returns 0 on success, -1 and sets errno on failure.
static inline int windowServerCloseSession(uint8_t port) {
  return overlayMap.header.osApi->userApi->windowServerCloseSession(port);
}

#ifdef __cplusplus
}
#endif

#endif // USER_PROCESSES_H

