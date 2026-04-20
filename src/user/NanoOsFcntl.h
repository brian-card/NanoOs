///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.12.2026
///
/// @file              NanoOsFcntl.h
///
/// @brief             Definitions in support of the standard POSIX fcntl
///                    functionality.
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

#ifndef NANO_OS_USER_FCNTL_H
#define NANO_OS_USER_FCNTL_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C"
{
#endif

// File access modes
#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_RDWR          0x0002
#define O_ACCMODE       0x0003

// File creation flags
#define O_CREAT         0x0040
#define O_EXCL          0x0080
#define O_NOCTTY        0x0100
#define O_TRUNC         0x0200

// File status flags
#define O_APPEND        0x0400
#define O_NONBLOCK      0x0800
#define O_DSYNC         0x1000
#define O_SYNC          0x2000
#define O_RSYNC         0x4000

// Extended open flags
#define O_DIRECTORY     0x10000
#define O_NOFOLLOW      0x20000
#define O_CLOEXEC       0x80000

// fcntl() commands
#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_GETFL         3
#define F_SETFL         4
#define F_GETLK         5
#define F_SETLK         6
#define F_SETLKW        7
#define F_DUPFD_CLOEXEC 8

// File descriptor flags
#define FD_CLOEXEC      1

// Advisory lock types
#define F_RDLCK         0
#define F_WRLCK         1
#define F_UNLCK         2

// openat() / *at() special value
#define AT_FDCWD        (-100)

// flag for faccessat(), fstatat(), etc.
#define AT_SYMLINK_NOFOLLOW  0x0100
#define AT_REMOVEDIR         0x0200
#define AT_SYMLINK_FOLLOW    0x0400
#define AT_EACCESS           0x0200

// POSIX advisory locking via lockf()
#define F_ULOCK         0
#define F_LOCK          1
#define F_TLOCK         2
#define F_TEST          3

int nanoOsFcntl(int fd, int op, va_list arg);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_FCNTL_H

