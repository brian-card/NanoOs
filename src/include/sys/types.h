///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              03.09.2026
///
/// @file              sys/types.h
///
/// @brief             Kernel-side header for sys/types.h defines exposed to
///                    userspace processes.
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

#ifndef SYS_TYPES_H
#define SYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Dual-guarded the same way ino_t/off_t are in NanoOsDirentTypes.h: struct
// stat (see NanoOsStatTypes.h, reached from src/kernel/Filesystem.h) needs
// gid_t/uid_t, and Filesystem.h ends up compiled into translation units
// that also reach a real libc's own, differently-shaped gid_t/uid_t/pid_t --
// the POSIX simulator build (src/hal/HalPosix.c, via kernel/Logger.h's
// stdlib.h chain) and the Arduino builds' own sketch file (NanoOs.ino is
// compiled with -include Arduino.h forced ahead of its own text, which this
// codebase has no chance to wrap the way every other file here that
// touches these names does -- see the next paragraph).
//
// Everywhere else in this codebase that risks the same collision
// (Commands.c, Console.c, Logger.c, NanoOsUnistd.c, HalCommon.c) already
// brackets its own #include of whatever pulls in the real names
// (typically "stdio.h") with "#define gid_t C_gid_t" / "#define uid_t
// C_uid_t" / "#define pid_t C_pid_t" ... "#undef", renaming the real
// library's typedefs out of the way so this header's own definitions
// (below) are the ones that end up bound to the real names. Those files
// also clear the guard macros below (immediately after their own #undef
// gid_t/uid_t/pid_t) so that dance doesn't leave a false "already
// declared" flag behind for this header to wrongly defer to.
//
// Guarding lets whichever definition is processed first in a given
// translation unit win, checking the same two conventions those libcs use
// to guard their own typedefs: glibc (__foo_t_defined) and newlib
// (_FOO_T_DECLARED).
#if !defined(__gid_t_defined) && !defined(_GID_T_DECLARED)
typedef unsigned int gid_t;
#define __gid_t_defined
#define _GID_T_DECLARED
#endif
#if !defined(__pid_t_defined) && !defined(_PID_T_DECLARED)
typedef uint8_t pid_t;
#define __pid_t_defined
#define _PID_T_DECLARED
#endif
#if !defined(__uid_t_defined) && !defined(_UID_T_DECLARED)
typedef int16_t uid_t;
#define __uid_t_defined
#define _UID_T_DECLARED
#endif

#ifdef __cplusplus
}
#endif

#endif // SYS_TYPES_H

