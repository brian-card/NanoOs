///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              10.22.2025
///
/// @file              NanoOsUnistd.h
///
/// @brief             Definitions in support of the standard POSIX unistd.h
///                    functionality to be exported to user programs.
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

#ifndef NANO_OS_USER_UNISTD_H
#define NANO_OS_USER_UNISTD_H

#ifdef __cplusplus
extern "C"
{
#endif

#define _POSIX_HOST_NAME_MAX 255

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif // HOST_NAME_MAX

#if HOST_NAME_MAX > _POSIX_HOST_NAME_MAX
#error "HOST_NAME_MAX cannot exceed _POSIX_HOST_NAME_MAX"
#endif // HOST_NAME_MAX > _POSIX_HOST_NAME_MAX

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int nanoOsClose(int fd);
int nanoOsDup2(int oldfd, int newfd);
int nanoOsGethostname(char *name, size_t len);
int nanoOsPipe(int pipefd[2]);
int nanoOsSethostname(const char *name, size_t len);
int nanoOsTtyname_r(int fd, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_UNISTD_H

