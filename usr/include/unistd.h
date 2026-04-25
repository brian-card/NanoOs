///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              10.23.2025
///
/// @file              unistd.h
///
/// @brief             Functionality in the Unix unistd library.
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

#ifndef UNISTD_H
#define UNISTD_H

#include "NanoOsUser.h"

#include "../../src/user/NanoOsUnistd.h"

#ifdef __cplusplus
extern "C"
{
#endif

static inline int close(int fd) {
  return overlayMap.header.osApi->close(fd);
}
static inline int dup2(int oldfd, int newfd) {
  return overlayMap.header.osApi->dup2(oldfd, newfd);
}
static inline int gethostname(char *name, size_t len) {
  return overlayMap.header.osApi->gethostname(name, len);
}
static inline int sethostname(const char *name, size_t len) {
  return overlayMap.header.osApi->sethostname(name, len);
}
static inline int ttyname_r(int fd, char buf[], size_t buflen) {
  return overlayMap.header.osApi->ttyname_r(fd, buf, buflen);
}
static inline int execve(const char *pathname,
  char *const argv[], char *const envp[]
) {
  return overlayMap.header.osApi->execve(pathname, argv, envp);
}
static inline int setuid(uid_t uid) {
  return overlayMap.header.osApi->setuid(uid);
}
static inline int pipe(int pipefd[2]) {
  return overlayMap.header.osApi->pipe(pipefd);
}

#ifdef __cplusplus
}
#endif

#endif // UNISTD_H

