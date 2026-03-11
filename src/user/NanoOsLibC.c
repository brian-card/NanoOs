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
#include "../kernel/Hal.h"
#include "../kernel/NanoOs.h"
#include "../kernel/NanoOsOverlayFunctions.h"
#include "../kernel/Scheduler.h"

/// @fn int timespec_get(struct timespec* spec, int base)
///
/// @brief Get the current time in the form of a struct timespec.
///
/// @param spec A pointer to the sturct timespec to populate.
/// @param base The base for the time (TIME_UTC).
///
/// @return Returns teh value of base on success, 0 on failure.
int timespec_get(struct timespec* spec, int base) {
  if (spec == NULL) {
    return 0;
  }
  
  int64_t now = HAL->getElapsedNanoseconds(0);
  spec->tv_sec = (time_t) (now / ((int64_t) 1000000000));
  spec->tv_nsec = now % ((int64_t) 1000000000);

  return base;
}

/// @var errorStrings
///
/// @brief Array of error messages arranged by error code.
const char *errorStrings[] = {
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
};

/// @var NUM_ERRORS
///
/// @brief Constant value to hold the number of errors defined in errorStrings.
const int NUM_ERRORS = sizeof(errorStrings) / sizeof(errorStrings[0]);

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
  int64_t start = HAL->getElapsedMilliseconds(0);
  while (HAL->getElapsedMilliseconds(start) < durationMs);
}

/// @fn time_t time(time_t *tloc)
///
/// @brief Implementation of standard C time function.
///
/// @param tloc Pointer to a time_t to store the current time in.  This
///   parameter may be NULL.
///
/// @return Returns the number of seconds since midnight, Jan 1, 1970 on
/// success, (time_t) -1 on error.  On error, the value of errno is also set.
time_t time(time_t *tloc) {
  time_t now = ((time_t) HAL->getElapsedMilliseconds(0)) / ((time_t) 1000);
  
  if (tloc != NULL) {
    *tloc = now;
  }
  
  return now;
}

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
  
  nptr = &nptr[strspn(nptr, " \t\r\n")];
  
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
      digit = c - 'a';
    } else if ((c >= 'A') && (c <= 'Z')) {
      digit = c - 'A';
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
      returnValue = 1 << ((sizeof(long long) << 3) - 1); // LLONG_MIN
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

