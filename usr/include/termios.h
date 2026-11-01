///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              01.18.2026
///
/// @file              termios.h
///
/// @brief             Functionality in the Unix unistd library.
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

#ifndef TERMIOS_H
#define TERMIOS_H

#include "NanoOsUser.h"

#include "../../src/user/NanoOsTermios.h"

#ifdef __cplusplus
extern "C"
{
#endif

static inline int tcgetattr(int fd, struct termios *termios_p) {
  return overlayMap.header.osApi->userApi->tcgetattr(fd, termios_p);
}
static inline int tcsetattr(int fd, int optional_actions,
  const struct termios *termios_p
) {
  return overlayMap.header.osApi->userApi->tcsetattr(
    fd, optional_actions, termios_p);
}

/// @fn static inline int tcgetwinsize(int fd, struct winsize *winsize)
///
/// @brief Get the size of the terminal a file descriptor refers to.
///
/// @param fd The file descriptor of the terminal.
/// @param winsize Set to the terminal's size.
///
/// @return Returns 0 on success, -1 with errno set on failure.
static inline int tcgetwinsize(int fd, struct winsize *winsize) {
  return overlayMap.header.osApi->userApi->tcgetwinsize(fd, winsize);
}

/// @fn static inline int tcsetwinsize(int fd, const struct winsize *winsize)
///
/// @brief Record the size of the terminal a file descriptor refers to.
///
/// @param fd The file descriptor of the terminal.
/// @param winsize The terminal's size.
///
/// @return Returns 0 on success, -1 with errno set on failure.
static inline int tcsetwinsize(int fd, const struct winsize *winsize) {
  return overlayMap.header.osApi->userApi->tcsetwinsize(fd, winsize);
}

#ifdef __cplusplus
}
#endif

#endif // TERMIOS_H

