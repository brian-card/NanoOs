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

#ifdef __cplusplus
}
#endif

#endif // USER_PROCESSES_H

