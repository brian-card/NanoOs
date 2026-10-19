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


/// @file test_kernel_hal_absent.c
///
/// @brief Kernel tests for HAL subsystems a platform doesn't provide.

#include "NanoOsTest.h"

#include <time.h>

#include "kernel/Hal.h"
#include "kernel/Logger.h"
#include "kernel/Processes.h"
#include "hal/HalCommon.h"
#include "user/NanoOsErrno.h"
#include "user/NanoOsLibC.h"
#include "user/NanoOsStdio.h"
#include "user/NanoOsTime.h"
#include "HalMock.h"

NANO_OS_KERNEL_TEST(hal_absent, root_filesystem_without_block_device) {
  halImpl.blockDevice = NULL;
  NANO_OS_ASSERT_EQ_INT(-ENODEV, halCommonInitRootFilesystem());
}

NANO_OS_KERNEL_TEST(hal_absent, restart_filesystem_without_block_device) {
  ProcessDescriptor processDescriptor;
  memset(&processDescriptor, 0, sizeof(processDescriptor));
  halImpl.blockDevice = NULL;
  NANO_OS_ASSERT_EQ_INT(-ENODEV, restartBuiltinFilesystem(&processDescriptor));
  NANO_OS_ASSERT_EQ_INT(-ENODEV, restartOverlayFilesystem(&processDescriptor));
  NANO_OS_ASSERT_EQ_INT(-ENODEV,
    restartContiguousFilesystem(&processDescriptor));
}

NANO_OS_KERNEL_TEST(hal_absent, sd_card_without_block_device) {
  halImpl.blockDevice = NULL;
  NANO_OS_ASSERT_NULL(halCommonInitRootSdSpiStorage(NULL));
}

NANO_OS_KERNEL_TEST(hal_absent, print_string_without_uart) {
  halImpl.uart = NULL;
  NANO_OS_ASSERT_EQ_INT(-ENODEV, printString_("hello\n"));
}

NANO_OS_KERNEL_TEST(hal_absent, time_without_clock) {
  mockClockAdvanceNs(5000000000ULL);
  halImpl.clock = NULL;
  NANO_OS_ASSERT_EQ_INT(0, (long long) nanoOsTime(NULL));
  msleep(10);
}

NANO_OS_KERNEL_TEST(hal_absent, log_without_memory) {
  halImpl.memory = NULL;
  NANO_OS_ASSERT_EQ_INT(-ENODEV, logMessage(LOG_LEVEL_ERROR,
    __FILE__, __func__, __LINE__, "dropped\n"));
}
