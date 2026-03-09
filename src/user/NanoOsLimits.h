///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              03.08.2026
///
/// @file              NanoOsLimits.h
///
/// @brief             Kernel limits definitions exposed to userspace.
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

#ifndef NANO_OS_USER_LIMITS_H
#define NANO_OS_USER_LIMITS_H

#ifdef __cplusplus
extern "C"
{
#endif

// POSIX definitions for the minimum values that must be supported
#define _POSIX_LOGIN_NAME_MAX 9 // 8 usable characters + null
#define _POSIX_NAME_MAX 14      // Filename length
#define _POSIX_OPEN_MAX 20      // Number of open files
#define _POSIX_PATH_MAX 256     // Path length

// Actual system values.
#define LOGIN_NAME_MAX _POSIX_LOGIN_NAME_MAX
#define NAME_MAX       32
#define OPEN_MAX       _POSIX_OPEN_MAX
#define PATH_MAX       _POSIX_PATH_MAX

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_LIMITS_H

