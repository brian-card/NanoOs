///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.12.2026
///
/// @file              fcntl.h
///
/// @brief             Functionality in the Unix fcntl library.
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

#ifndef SCHED_H
#define SCHED_H

#include "NanoOsUser.h"
#include "../../src/user/NanoOsFcntl.h"

#ifdef __cplusplus
extern "C"
{
#endif

static inline int fcntl(int fd, int op, ... /* arg */ ) {
  va_list arg;
  va_start(arg, op);
  int returnValue = overlayMap.header.osApi->fcntl(fd, op, arg);
  va_end(arg);
  return returnValue;
}

#ifdef __cplusplus
}
#endif

#endif // SCHED_H

