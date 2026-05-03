///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              11.21.2024
///
/// @file              NanoOs.h
///
/// @brief             Core nanokernel functionality for NanoOs.
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

#ifndef NANO_OS_H
#define NANO_OS_H

// Standard C includes
#include "time.h"

// Local headers
#include "MemoryManager.h"
#include "NanoOsTypes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def NANO_OS_NUM_MESSAGES
///
/// @brief The total number of inter-process messages that will be available
/// for use by processes.
#define NANO_OS_NUM_MESSAGES                              6

/// @def NANO_OS_MAX_NUM_SHELLS
///
/// @brief The maximum number of shell processes the system can run.
#define NANO_OS_MAX_NUM_SHELLS                            2

/// @def NANO_OS_VERSION
///
/// @brief The version string for NanoOs
#define NANO_OS_VERSION                             "0.1.0"

/// @def ROOT_USER_ID
///
/// @brief The numerical ID of the root user.
#define ROOT_USER_ID                                      0

/// @def NO_USER_ID
///
/// @brief The numerical value that indicates that a process is not owned.
#define NO_USER_ID                                       -1

/// @def NUM_TASK_STORAGE_KEYS
///
/// @brief The total number of keys supported by the per-process storage.
#define NUM_TASK_STORAGE_KEYS                             1

/// @def FGETS_CONSOLE_BUFFER_KEY
///
/// @brief Per-process storage key for the consoleBufer pointer in consoleFGets.
#define FGETS_CONSOLE_BUFFER_KEY                          0

/// @def MAX_GET_MESSAGE_RETRIES
///
/// @brief The maximum number of times we should attempt calling
/// getAvailableMessage before concluding that there are no more messages and
/// giving up.
#define MAX_GET_MESSAGE_RETRIES                           3

/// @def USE_HAL_MEMORY_DEBUG
///
/// @brief Constant to pass into HAL memory routines that vary their output
/// depending on whether or not we're in a debug build.
#ifdef NANO_OS_DEBUG
#define USE_HAL_MEMORY_DEBUG true
#else
#define USE_HAL_MEMORY_DEBUG false
#endif

/// @def floatToInts
///
/// @brief Break a floating-point number into two integer values that represent
/// its whole component and its decimal component to a specified level of
/// precision.
///
/// @param number The number to break into integers.
/// @param precision The desired number of decimal places in the decimal
///   component.
#define floatToInts(number, precision) \
  (int) number, \
  ABS((int) (number * raiseUInt(10, precision))) % raiseUInt(10, precision)

/// @def stringDestroy
///
/// @brief Convenience macro for the common operation of destroying a string.
#define stringDestroy(string) ((char*) (free((void*) string), NULL))

/// @def copyBytes
///
/// @brief Copy a specified number of bytes from a source to a destination one
/// byte at a time.  The source and destination may be at unaligned memory
/// addresses.
///
/// @param dst A pointer to the destination memory.
/// @param src A pointer to the source memory.
/// @param len The number of bytes to copy from the source to the destination.
#define copyBytes(dst, src, len) \
  do { \
    unsigned char *dstBytes = (unsigned char*) (dst); \
    unsigned char *srcBytes = (unsigned char*) (src); \
    size_t copyLength = (size_t) (len); \
    for (size_t ii = 0; ii < copyLength; ii++) { \
      dstBytes[ii] = srcBytes[ii]; \
    } \
  } while (0)

/// @def readBytes
///
/// @brief Read a value from a memory address that may be unaligned.
///
/// @param dst A pointer to the destination memory.  This is expected to be a
///   multi-byte type.
/// @param src A pointer to the source memory.
#define readBytes(dst, src) copyBytes(dst, src, sizeof(*(dst)))

/// @def writeBytes
///
/// @brief Write a value to a memory address that may be unaligned.
///
/// @param dst A pointer to the destination memory.
/// @param src A pointer to the source memory.  This is expected to be a
///   multi-byte type.
#define writeBytes(dst, src) copyBytes(dst, src, sizeof(*(src)))

// Support functions
ProcessId getNumPipes(const char *commandLine);
void timespecFromDelay(struct timespec *ts, long int delayMs);
unsigned int raiseUInt(unsigned int x, unsigned int y);
const char* getUsernameByUserId(UserId userId);
UserId getUserIdByUsername(const char *username);
void login(void);
void *getProcessStorage(uint8_t key);
int setProcessStorage_(uint8_t key, void *val, int pid, ...);
#define setProcessStorage(key, val, ...) \
  setProcessStorage_(key, val, ##__VA_ARGS__, -1)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NANO_OS_H
