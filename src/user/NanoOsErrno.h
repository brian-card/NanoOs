///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              12.23.2025
///
/// @file              NanoOsErrno.h
///
/// @brief             Definitions in support of the standard C errno
///                    functionality.
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

#ifndef NANO_OS_USER_ERRNO_H
#define NANO_OS_USER_ERRNO_H

#ifdef __cplusplus
extern "C"
{
#endif

// The errors defined by the compiler's version of errno.h are not helpful
// because most things are defined to be ENOERR.  So, we need to define some of
// our own.
#define ENOERR           0      /* Success */
#define EOTHER           1      /* Unspecified error (POSIX.1-2008) */
#define EBUSY            2      /* Device or resource busy */
#define ENOMEM           3      /* Out of memory */
#define EACCES           4      /* Permission denied */
#define EINVAL           5      /* Invalid argument */
#define EIO              6      /* I/O error */
#define ENOSPC           7      /* No space left on device */
#define ENOENT           8      /* No such entry found */
#define ENOTEMPTY        9      /* Directory not empty */
#define EOVERFLOW       10      /* Overflow detected */
#define EFAULT          11      /* Invalid address */
#define ENAMETOOLONG    12      /* Name too long */
#define EBADF           13      /* Bad file descriptor */
#define ENODEV          14      /* No such device */
#define ENOTTY          15      /* No such terminal device */
#define ERANGE          16      /* Parameter or result out of range */
#define ELOOP           17      /* Infinite loop detected */
#define ETIMEDOUT       18      /* Operation timed out */
#define ENOEXEC         19      /* Exec format error */
#define ENOTSUP         20      /* Operation not supported */
#define ENXIO           21      /* No such device or address */
#define ELAST           21      /* End of error codes */

int* errno_(void);
#define errno (*errno_())

#ifdef __cplusplus
} // extern "C"
#endif

#endif // NANO_OS_USER_ERRNO_H
