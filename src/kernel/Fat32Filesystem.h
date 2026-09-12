///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.28.2026
///
/// @file              Fat32Filesystem.h
///
/// @brief             Base FAT32 driver for NanoOs.
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

#ifndef FAT32_FILESYSTEM_H
#define FAT32_FILESYSTEM_H

// NanoOs's FILE type collides with the toolchain's own <stdio.h> FILE.
// Redirect the standard headers pulled in transitively below around that
// name before restoring NanoOs's own meaning of FILE.
#undef FILE

#define FILE C_FILE
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"
#undef FILE

#define FILE NanoOsFile

#ifdef __cplusplus
extern "C"
{
#endif

// This is the kernel-linked build's entry point into the FAT32 driver.
// Arduino IDE only discovers source files under src/kernel, so the actual
// implementation -- shared verbatim with the contiguous and overlay
// filesystem builds -- lives under usr/src/filesystems/drivers/fat32/ and is
// pulled in from Fat32Filesystem.c by #include rather than by symlink (the
// technique those other two builds use).  This header just forwards to the
// single canonical set of FAT32 type and constant definitions.
#define NANO_OS_KERNEL_BUILD
#include "../../usr/src/filesystems/drivers/fat32/Fat32.h"

#ifdef __cplusplus
} // extern "C"
#endif

#endif // FAT32_FILESYSTEM_H
