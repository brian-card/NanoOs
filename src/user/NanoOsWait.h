///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              10.31.2026
///
/// @file              NanoOsWait.h
///
/// @brief             Kernel definitions for waiting on child processes exposed
///                    to userspace.
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

#ifndef NANO_OS_WAIT_H
#define NANO_OS_WAIT_H

/// @def WNOHANG
///
/// @brief waitpid option to return immediately if no child has exited.
#undef WNOHANG
#define WNOHANG 1

/// @def WIFEXITED
///
/// @brief Whether a wait status is for a child that returned from main.
#undef WIFEXITED
#define WIFEXITED(status) (((status) & 0x7f) == 0)

/// @def WEXITSTATUS
///
/// @brief The value a child returned from main, given its wait status.
#undef WEXITSTATUS
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)

/// @def WIFSIGNALED
///
/// @brief Whether a wait status is for a child that was killed by a signal.
#undef WIFSIGNALED
#define WIFSIGNALED(status) (((status) & 0x7f) != 0)

/// @def WTERMSIG
///
/// @brief The signal that killed a child, given its wait status.
#undef WTERMSIG
#define WTERMSIG(status) ((status) & 0x7f)

#endif // NANO_OS_WAIT_H
