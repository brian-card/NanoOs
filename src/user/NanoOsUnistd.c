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

// Standard C includes
#define FILE C_FILE
#include "stdio.h"
#undef FILE
#include "string.h"

#include "NanoOsLibC.h"
#include "NanoOsUnistd.h"
#include "../kernel/Console.h"
#include "../kernel/NanoOs.h"
#include "../kernel/Scheduler.h"

// Must come last
#include "NanoOsStdio.h"

/// @fn int nanoOsGethostname(char *name, size_t len)
///
/// @brief Implementation of the standard Unix nanoOsGethostname system call.
///
/// @param name Pointer to a character buffer to fill with the hostname.
/// @param len The number of bytes allocated to name.
///
/// @return Returns 0 on success, -1 on failure.  On failure, the value of
/// errno is also set.
int nanoOsGethostname(char *name, size_t len) {
  if (name == NULL) {
    errno = EFAULT;
    return -1;
  }
  
  const char *hostname = schedulerGetHostname();
  if (hostname == NULL) {
    hostname = "localhost";
  }
  size_t hostnameLen = strlen(hostname);
  
  int returnValue = 0;
  if (len < hostnameLen) {
    returnValue = -1;
    errno = ENAMETOOLONG;
  }
  strncpy(name, hostname, len);
  
  return returnValue;
}

/// @fn int nanoOsSethostname(const char *name, size_t len)
///
/// @brief Implementation of the standard Unix nanoOsSethostname system call.
///
/// @param name Pointer to a character buffer that contains the desired
///   hostname.
/// @param len The number of bytes allocated to name.
///
/// @return Returns 0 on success, -1 on failure.  On failure, the value of
/// errno is also set.
int nanoOsSethostname(const char *name, size_t len) {
  if (name == NULL) {
    errno = EFAULT;
    return -1;
  } else if (len > HOST_NAME_MAX) {
    errno = EINVAL;
    return -1;
  }
  
  FILE *hostnameFile = fopen("/etc/hostname", "r");
  if (hostnameFile == NULL) {
    printString("ERROR! fopen of hostname returned NULL!\n");
    return -1;
  }
  
  size_t bytesWritten = fwrite(name, 1, len, hostnameFile);
  if (bytesWritten != len) {
    printString("ERROR! Could not write hostname file.\n");
    fclose(hostnameFile);
    return -1;
  }
  
  fclose(hostnameFile);
  return 0;
}

/// @fd int nanoOsTtyname_r(int fd, char *buf, size_t buflen)
///
/// @brief Get the current tty name for a specified file descriptor in a
/// thread-safe way.
///
/// @param fd The integer file descriptor of the tty to get.
/// @param buf The character buffer to write the tty name into.
/// @param buflen The number of bytes available at the buf pointer.
///
/// @return Returns 0 on success, an error number on failure.
int nanoOsTtyname_r(int fd, char *buf, size_t buflen) {
  if (fd < 0) {
    return EBADF;
  } else if (fd > 2) {
    return ENOTTY;
  } else if (buflen < 10) {
    return ERANGE;
  }
  
  int consolePort = getOwnedConsolePort();
  if (consolePort < 0) {
    return ENOTTY;
  }
  
  snprintf(buf, buflen, "/dev/tty%d", consolePort);
  
  return 0;
}

