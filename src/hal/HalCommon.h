///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              04.28.2026
///
/// @file              HalCommon.h
///
/// @brief             Header for routines that are common to multiple HAL
///                    implementations.
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

#ifndef HAL_COMMON_H
#define HAL_COMMON_H

#include <string.h>

#include "../kernel/Hal.h"
#include "../kernel/Filesystem.h"
// Deliberately *NOT* including MemoryManager.h here.  The HAL has to be
// operational prior to the memory manager and really should be completely
// independent of it.
#include "../kernel/NanoOs.h"
#include "../kernel/Tasks.h"
#include "../kernel/Scheduler.h"
#include "../kernel/SdCardSpi.h"
#include "../user/NanoOsErrno.h"


#ifdef __cplusplus
extern "C"
{
#endif

BlockStorageDevice* halCommonInitRootSdSpiStorage(SdCardSpiArgs *sdCardSpiArgs);
int halCommonInitRootFilesystem(BlockStorageDevice *blockDevice);
int halCommonInit(Hal *hal);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_COMMON_H

