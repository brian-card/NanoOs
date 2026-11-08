////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
///
/// @file              NanoOsDirCap.h
///
/// @brief             Kernel-side implementation of the calls a process uses
///                    to see and narrow its own directory capabilities.
///
///////////////////////////////////////////////////////////////////////////////

#ifndef NANO_OS_USER_DIR_CAP_H
#define NANO_OS_USER_DIR_CAP_H

#include <stddef.h>
#include <stdint.h>
#include "../kernel/DirCapabilities.h"

#ifdef __cplusplus
extern "C"
{
#endif

int nanoOsDircapName(const char *directory, char **name, size_t *nameLength);
int nanoOsDircapAdd(const char *directory, uint8_t mask);
int nanoOsDircapDrop(const char *directory);
int nanoOsDircapGet(DirCapabilities *buffer, size_t bufferSize);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_DIR_CAP_H
