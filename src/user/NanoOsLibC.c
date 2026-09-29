////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file

#include "NanoOsLibC.h"

#include "../kernel/Console.h"
#include "../kernel/Filesystem.h"
#include "../kernel/Logger.h"
#include "../kernel/Hal.h"
#include "../kernel/NanoOs.h"
#include "../kernel/OverlayFunctions.h"
#include "../kernel/Processes.h"
#include "../kernel/Scheduler.h"

// Must come last
#include "NanoOsStdio.h"

/// @var errorStrings
///
/// @brief Array of error messages arranged by error code.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.  Entries are copied directly into this
/// array's own storage rather than being separate string-literal objects
/// pointed to from it, so tagging the array alone protects every entry.
const char errorStrings[][33] KEEP_IN_FLASH = {
  "Success",                          // ENOERR
  "Unspecified error",                // EOTHER
  "Device or resource busy",          // EBUSY
  "Out of memory",                    // ENOMEM
  "Permission denied",                // EACCES
  "Invalid argument",                 // EINVAL
  "I/O error",                        // EIO
  "No space left on device",          // ENOSPC
  "No such entry found",              // ENOENT
  "Directory not empty",              // ENOTEMPTY
  "Overflow detected",                // EOVERFLOW
  "Invalid address",                  // EFAULT
  "Name too long",                    // ENAMETOOLONG
  "Bad file descriptor",              // EBADF
  "No such device",                   // ENODEV
  "No such terminal device",          // ENOTTY
  "Parameter or result out of range", // ERANGE
  "Infinite loop detected",           // ELOOP
  "Operation timed out",              // ETIMEDOUT
  "Exec format error",                // ENOEXEC
  "Operation not supported",          // ENOTSUP
  "No such device or address",        // ENXIO
  "Operation not permitted",          // EPERM
  "No such process",                  // ESRCH
  "Try again",                        // EAGAIN
  "Not a directory",                  // ENOTDIR
};

/// @def NUM_ERRORS
///
/// @brief The number of errors defined in errorStrings.
///
/// @note This is a #define rather than a const int so that it doesn't need
/// its own KEEP_IN_FLASH treatment - it's folded into an immediate value at
/// each use site instead of occupying storage that could land in .rodata.
#define NUM_ERRORS ((int) (sizeof(errorStrings) / sizeof(errorStrings[0])))

/// @fn char* nanoOsStrError(int errnum)
///
/// @brief nanoOsStrError implementation for NanoOs.
///
/// @param errnum The error code either set as the variable errno or returned
///   from a function.
///
/// @return This function always succeeds and returns the string corrsponding
/// to an error.  If the provided error number is outside the range of the
/// defined errors, the string "Unknown error" will be returned.
char* nanoOsStrError(int errnum) {
  if ((errnum < 0) || (errnum >= NUM_ERRORS)) {
    errnum = EOTHER;
  }

  return (char*) errorStrings[errnum];
}

/// @fn void msleep(int duration)
///
/// @brief Delay execution for a specified number of milliseconds.
///
/// @param durationMs The number of milliseconds to wait before contiuing
///   execution.
///
/// @return This function returns no value.
void msleep(int durationMs) {
  int64_t start = 0;
  HAL->clock->getElapsedMilliseconds(0, &start);
  int64_t elapsed = 0;
  do {
    HAL->clock->getElapsedMilliseconds(start, &elapsed);
  } while (elapsed < durationMs);
}

/// @var _whitespace
///
/// @brief Set of characters considered whitespace when skipping leading
/// whitespace in nanoOsStrtoll.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _whitespace[] KEEP_IN_FLASH = " \t\r\n";

/// @fn long long nanoOsStrtoll(const char *nptr, char **endptr, int base)
///
/// @brief NanoOs implementation of the standard C strtoll function.
///
/// @param nptr A pointer to the beginning of a string to convert to an integer
///   representation.
/// @param endptr A pointer to a char pointer that will be set to the first
///   character after the last valid numerical character if endptr is non-NULL.
/// @param base The base of the numeric string being passed in in thee range 2
///   through 36, inclusive, or the special value 0 which will determine the
///   base by the first few digits of the number.
///
/// @return On success, the converted number is returned.  If nptr is NULL, 0 is
/// returned and errno is set to EOTHER.  If base is an invalid value, 0 is
/// returned and errno is set to EINVAL.  On underflow or overflow, LLONG_MIN or
/// LLONG_MAX is returned, respectively, and errno is set to ERANGE.
long long nanoOsStrtoll(const char *nptr, char **endptr, int base) {
  long long returnValue = 0;
  long long multiplier = 1;
  
  if (nptr == NULL) {
    // Can't convert a NULL pointer, but there's no standard error for this.
    errno = EOTHER;
    return returnValue; // 0
  } else if ((base < 0) || (base == 1) || (base > 36)) {
    // Invalid baase.
    errno = EINVAL;
    return returnValue; // 0
  }
  
  nptr = &nptr[strspn(nptr, _whitespace)];
  
  if (*nptr == '-') {
    multiplier = -1;
    nptr++;
  } else if (*nptr == '+') {
    // No-op
    nptr++;
  }
  
  // We're at the first character of the number (supposedly).  If the base is 0
  // then we need to figure out the real base by looking at the fisrt few
  // digits.
  if (base == 0) {
    base = 10; // Until proven otherwise
    
    if (*nptr == '0') {
      // We need to evaluate the next character.
      char nextChar = nptr[1];
      if ((nextChar == 'x') || (nextChar == 'X')) {
        // Hexadecimal number
        base = 16;
        nptr = &nptr[2];
      } else if ((nextChar >= '1') && (nextChar <= '7')) {
        // Octal number
        base = 8;
        nptr = &nptr[1];
      }
    }
  }
  
  char c = *nptr;
  while (c != '\0') {
    char digit = 0;
    if ((c >= '0') && (c <= '9')) {
      digit = c - '0';
    } else if ((c >= 'a') && (c <= 'z')) {
      digit = 10 + c - 'a';
    } else if ((c >= 'A') && (c <= 'Z')) {
      digit = 10 + c - 'A';
    } else {
      // Not an alpha-numeric character
      break;
    }
    
    if (digit >= ((char) base)) {
      // Not a character that's in our base
      break;
    }
    
    returnValue *= (long long) base;
    returnValue += (long long) digit;
    
    if (returnValue < 0) {
      // Overflow or underflow.  We have to halt immediately and return the
      // corresponding value.
      returnValue = 1LL << ((sizeof(long long) << 3) - 1); // LLONG_MIN
      if (multiplier == 1) {
        returnValue--; // LLONG_MAX
      }
      errno = ERANGE;
      return returnValue;
    }
    
    nptr++;
    c = *nptr;
  }
  
  returnValue *= multiplier;
  
  if (endptr != NULL) {
    *endptr = (char*) nptr;
  }
  
  return returnValue;
}

/// @fn int nanoOsSetenv(const char *name, const char *value, int overwrite)
///
/// @brief Add an environment variable to the running process's context or
/// change the value of one that already exists.
///
/// @param name The name of the environment variable to change or add.
/// @param value The value to set for the environment variable.
/// @param overwrite Whether to overwrite an existing environment variable
////  (true/non-zero) or leave it unchanged (false/zero).
///
/// @return Returns 0 on success, sets the value of errno and returns -1 on
/// failure.
int nanoOsSetenv(const char *name, const char *value, int overwrite) {
  if ((name == NULL) || (*name == '\0') || (strchr(name, '=') != NULL)) {
    errno = EINVAL;
    return -1;
  }
  if (value == NULL) {
    value = "";
  }

  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor == NULL) {
    errno = EINVAL;
    return -1;
  }

  size_t nameLen = strlen(name);
  size_t valueLen = strlen(value);
  size_t newEntryLen = nameLen + valueLen + 2;

  char **envp = processDescriptor->envp;
  size_t numVariables = 0;
  size_t stringsBytes = 0;
  size_t entryOffset = 0;
  size_t oldEntryLen = 0;
  bool found = false;
  while ((envp != NULL) && (envp[numVariables] != NULL)) {
    size_t entryLen = strlen(envp[numVariables]) + 1;
    if ((found == false)
      && (strncmp(envp[numVariables], name, nameLen) == 0)
      && (envp[numVariables][nameLen] == '=')
    ) {
      if ((overwrite == 0)
        || (value == &envp[numVariables][nameLen + 1])
      ) {
        // Either we've been instructed to not overwrite the environment
        // variable we've found or someone has passed in `getenv(name)` as the
        // value, which would overlap and potentially cause problems with our
        // logic below.  Either way, this is a no-op.  Just return good status.
        return 0;
      }
      found = true;
      entryOffset = stringsBytes;
      oldEntryLen = entryLen;
    }
    stringsBytes += entryLen;
    numVariables++;
  }

  if (found == false) {
    entryOffset = stringsBytes;
  }
  size_t newNumVariables = numVariables + ((found == true) ? 0 : 1);
  size_t newStringsBytes = stringsBytes - oldEntryLen + newEntryLen;
  size_t paddedBytes
    = (newStringsBytes + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
  size_t newBytes = paddedBytes + ((newNumVariables + 1) * sizeof(char*));
  size_t tailBytes = stringsBytes - entryOffset - oldEntryLen;

  char *block = (envp != NULL) ? envp[0] : NULL;
  if (newEntryLen > oldEntryLen) {
    // Extend the allocated memory block and move all entries past the found
    // entry down in memory.
    void *check = realloc(block, newBytes);
    if (check == NULL) {
      errno = ENOMEM;
      return -1;
    }
    block = (char*) check;
    memmove(&block[entryOffset + newEntryLen],
      &block[entryOffset + oldEntryLen], tailBytes);
  } else {
    // Compact all the values past the found entry up in memory and then realloc
    // the memory block down in size.
    memmove(&block[entryOffset + newEntryLen],
      &block[entryOffset + oldEntryLen], tailBytes);
    void *check = realloc(block, newBytes);
    if (check == NULL) {
      errno = ENOMEM;
      return -1;
    }
    block = (char*) check;
  }

  memcpy(&block[entryOffset], name, nameLen);
  block[entryOffset + nameLen] = '=';
  memcpy(&block[entryOffset + nameLen + 1], value, valueLen + 1);

  char **newEnvp = (char**) &block[paddedBytes];
  char *nextString = block;
  for (size_t ii = 0; ii < newNumVariables; ii++) {
    newEnvp[ii] = nextString;
    nextString += strlen(nextString) + 1;
  }
  newEnvp[newNumVariables] = NULL;

  processDescriptor->envp = newEnvp;

  extern NanoOsOverlayMap *overlayMap;
  if (overlayMap != NULL) {
    overlayMap->header.env = newEnvp;
  }

  return 0;
}

