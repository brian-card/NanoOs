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

#include <stdarg.h>
#include <string.h>

#include "../kernel/Hal.h"
// Deliberately *NOT* including MemoryManager.h here.  The HAL has to be
// operational prior to the memory manager and really should be completely
// independent of it.
#include "../kernel/Filesystem.h"
#include "../kernel/NanoOs.h"
#define FILE C_FILE
#define gid_t C_gid_t
#define uid_t C_uid_t
#define pid_t C_pid_t
#include "../kernel/Overlay.h"
#include "../kernel/OverlayFunctions.h"
#undef FILE
#undef gid_t
#undef uid_t
#undef pid_t
#include "../kernel/Processes.h"
#include "../kernel/Scheduler.h"
#include "../kernel/SdCardSpi.h"
#include "../user/NanoOsErrno.h"


/// @typedef HalFunction
///
/// @brief Type for all HAL implementation functions in the dispatch table.
typedef int32_t (*HalFunction)(va_list args);

#ifdef __cplusplus
extern "C"
{
#endif

extern HalFunction *halFunctions[HAL_NUM_SUBSYSTEMS];
extern HalMemory halCommonMemory;
extern HalUart halCommonUart;
extern HalDio halCommonDio;
extern HalSpi halCommonSpi;
extern HalClock halCommonClock;
extern HalPower halCommonPower;
extern HalTimer halCommonTimer;
extern HalBlockDevice halCommonBlockDevice;

int32_t callHal(HalSubsystem subsystem, uint32_t function, ...);
BlockDevice* halCommonInitRootSdSpiStorage(SdCardSpiArgs *sdCardSpiArgs);
int32_t halCommonInitRootFilesystem(void);
int32_t restartFilesystem(ProcessDescriptor *processDescriptor);
int32_t halCommonInit(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_COMMON_H

