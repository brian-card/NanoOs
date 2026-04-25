///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              10.25.2025
///
/// @file              NanoOsSysUtsname.h
///
/// @brief             Functionality from the sys/ directory of include files.
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

#ifndef NANO_OS_USER_SYS_H
#define NANO_OS_USER_SYS_H

#include "NanoOsUnistd.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct utsname {
  char sysname[7];                /* Operating system name (e.g., "NanoOs") */
  char nodename[HOST_NAME_MAX];   /* Name within communications network
                                     to which the node is attached, if any */
  char release[16];               /* Operating system release
                                   (e.g., "2.6.28") */
  char version[32];               /* Operating system version */
  char machine[16];               /* Hardware type identifier */
};

int nanoOsUname(struct utsname *buf);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_SYS_H

