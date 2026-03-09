///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              03.08.2026
///
/// @file              NanoOsHardware.h
///
/// @brief             NanoOs-specific functionality that abstracts hardware
///                    for userspace processes.
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

#ifndef NANO_OS_HARDWARE_H
#define NANO_OS_HARDWARE_H

#ifdef __cplusplus
extern "C"
{
#endif

/// @enum NanoOsShutdownType
///
/// @brief Types of shutdowns that can be invoked from userspace.
typedef enum NanoOsShutdownType {
  NANO_OS_SHUTDOWN_OFF,
  NANO_OS_SHUTDOWN_HYBERNATE,
  NANO_OS_SHUTDOWN_SUSPEND,
  NANO_OS_SHUTDOWN_RESET,
  NANO_OS_SHUTDOWN_NUM_TYPES,
} NanoOsShutdownType;

int nanoOsShutdown(NanoOsShutdownType shutdownType);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_HARDWARE_H

