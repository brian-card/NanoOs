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

/// @file HalAgonLight2.c
///
/// @brief Stub HAL implementation for the Agon Light 2 (eZ80F92).
///
/// All subsystems return "good status" so that the kernel compiles and links
/// cleanly for a binary-size estimate.  Real implementations are to be filled
/// in as each subsystem is brought up on the hardware.

#ifdef NANOOS_AGON

#include "HalAgonLight2.h"
#include "HalCommon.h"
#include "../user/NanoOsErrno.h"

// ---------------------------------------------------------------------------
// Memory layout constants
// ---------------------------------------------------------------------------

/// @def HEAP_START_ADDRESS
///
/// @brief Address of the start (bottom) of the heap.  On the AgonLight 2,
/// external RAM starts at address 0x40000 and since we're currently loading the
/// NanoOs binary at that address, we need to reserve 128 KB from there.
#define HEAP_START_ADDRESS 0x60000

/// @def PROCESS_STACK_SIZE
///
/// @brief Default process stack size in bytes.
#define PROCESS_STACK_SIZE 1024

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief Stack for the memory-manager process.
#define MEMORY_MANAGER_STACK_SIZE 512

/// @def OVERLAY_ADDRESS
///
/// @brief Address in RAM where overlays are loaded.
/// Placed at end of the 24-bit address space, which is where the internal RAM
/// is mapped by the boot initialization code.
#define OVERLAY_ADDRESS 0xFFE000

/// @def OVERLAY_SIZE
///
/// @brief Bytes reserved for the overlay region.
#define OVERLAY_SIZE 8192 // 8 KB - the size of the internal RAM area

// ---------------------------------------------------------------------------
// Memory subsystem stubs
// ---------------------------------------------------------------------------

int32_t agonLight2ProcessStackSize(va_list args) {
  bool    debug       = (bool)   va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = PROCESS_STACK_SIZE;
  }
  return 0;
}

int32_t agonLight2MemoryManagerStackSize(va_list args) {
  bool    debug       = (bool)   va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  if (returnValue != NULL) {
    *returnValue = debug ? MEMORY_MANAGER_DEBUG_STACK_SIZE
                         : MEMORY_MANAGER_STACK_SIZE;
  }
  return 0;
}

int32_t agonLight2BottomOfHeap(va_list args) {
  bool   debug       = (bool)  va_arg(args, int);
  void **returnValue = va_arg(args, void**);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = (void*) ((uintptr_t) HEAP_START_ADDRESS);
  }
  return 0;
}

int32_t agonLight2NumExtraSchedulerStacks(va_list args) {
  bool     debug       = (bool)    va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = 2;
  }
  return 0;
}

int32_t agonLight2NumExtraConsoleStacks(va_list args) {
  bool     debug       = (bool)    va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// UART subsystem stubs
// ---------------------------------------------------------------------------

int32_t agonLight2InitUart(va_list args) {
  (void) args;
  return 0;
}

int32_t agonLight2ConfigureUart(va_list args) {
  (void) args;
  return 0;
}

int32_t agonLight2PollUart(va_list args) {
  (void) va_arg(args, int32_t); // deviceId
  return -EAGAIN; // no data available yet
}

int32_t agonLight2WriteUart(va_list args) {
  (void)          va_arg(args, int32_t);        // deviceId
  (void)          va_arg(args, const uint8_t*); // data
  ssize_t  length      = va_arg(args, ssize_t);
  ssize_t *returnValue = va_arg(args, ssize_t*);
  if (returnValue != NULL) {
    *returnValue = length; // pretend every byte was written
  }
  return 0;
}

int32_t agonLight2IsUartConsole(va_list args) {
  (void) va_arg(args, int32_t); // deviceId
  bool *returnValue = va_arg(args, bool*);
  if (returnValue != NULL) {
    *returnValue = true;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// DIO subsystem stubs
// ---------------------------------------------------------------------------

int32_t agonLight2InitDio(va_list args)      { (void) args; return 0; }
int32_t agonLight2ConfigureDio(va_list args) { (void) args; return 0; }
int32_t agonLight2WriteDio(va_list args)     { (void) args; return 0; }

// ---------------------------------------------------------------------------
// SPI subsystem stubs (no SPI bus on the eZ80 side of the Agon)
// ---------------------------------------------------------------------------

int32_t agonLight2InitSpi(va_list args)          { (void) args; return -ENOTSUP; }
int32_t agonLight2ConfigureSpi(va_list args)     { (void) args; return -ENOTSUP; }
int32_t agonLight2StartSpiTransfer(va_list args) { (void) args; return -ENOTSUP; }
int32_t agonLight2EndSpiTransfer(va_list args)   { (void) args; return -ENOTSUP; }
int32_t agonLight2SpiTransfer8(va_list args)     { (void) args; return -ENOTSUP; }
int32_t agonLight2SpiTransferBytes(va_list args) { (void) args; return -ENOTSUP; }

// ---------------------------------------------------------------------------
// Clock subsystem stubs
// ---------------------------------------------------------------------------

/// @var _baseSystemTimeUs
///
/// @brief Epoch offset set via setSystemTime(), in microseconds.
static int64_t _baseSystemTimeUs = 0;

/// @var _uptimeUs
///
/// @brief Monotonic uptime counter in microseconds.
/// Incremented by real timer interrupt code once timers are implemented.
static int64_t _uptimeUs = 0;

int32_t agonLight2InitClock(va_list args) {
  (void) args;
  return 0;
}

int32_t agonLight2SetSystemTime(va_list args) {
  struct timespec *ts = va_arg(args, struct timespec*);
  if (ts == NULL) {
    return -EINVAL;
  }
  _baseSystemTimeUs
    = (((int64_t) ts->tv_sec)  * ((int64_t) 1000000))
    + (((int64_t) ts->tv_nsec) / ((int64_t) 1000));
  return 0;
}

int32_t agonLight2GetElapsedMilliseconds(va_list args) {
  int64_t  startTime   = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t  nowMs = (_baseSystemTimeUs + _uptimeUs) / (int64_t) 1000;
  if (returnValue != NULL) {
    *returnValue = nowMs - startTime;
  }
  return 0;
}

int32_t agonLight2GetElapsedMicroseconds(va_list args) {
  int64_t  startTime   = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t  nowUs = _baseSystemTimeUs + _uptimeUs;
  if (returnValue != NULL) {
    *returnValue = nowUs - startTime;
  }
  return 0;
}

int32_t agonLight2GetElapsedNanoseconds(va_list args) {
  int64_t  startTime   = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t  nowNs = (_baseSystemTimeUs + _uptimeUs) * (int64_t) 1000;
  if (returnValue != NULL) {
    *returnValue = nowNs - startTime;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Power subsystem stub
// ---------------------------------------------------------------------------

int32_t agonLight2EnterMode(va_list args) {
  (void) va_arg(args, int); // HalPowerMode
  // Halt the CPU in all cases until real power management is implemented.
  for (;;) {}
  return 0;
}

// ---------------------------------------------------------------------------
// Timer subsystem stubs (no hardware timer driver yet)
// ---------------------------------------------------------------------------

int32_t agonLight2InitTimer(va_list args) {
  (void) args;
  return 0;
}

int32_t agonLight2InitTimerDevice(va_list args) {
  (void) args;
  return -EINVAL; // no timer devices configured yet
}

int32_t agonLight2ConfigOneShotTimer(va_list args) {
  (void) args;
  return -EINVAL;
}

int32_t agonLight2ConfiguredTimerNanoseconds(va_list args) {
  (void)          va_arg(args, int32_t);  // deviceId
  uint64_t *returnValue = va_arg(args, uint64_t*);
  if (returnValue != NULL) {
    *returnValue = 0;
  }
  return -EINVAL;
}

int32_t agonLight2RemainingTimerNanoseconds(va_list args) {
  (void)          va_arg(args, int32_t);  // deviceId
  uint64_t *returnValue = va_arg(args, uint64_t*);
  if (returnValue != NULL) {
    *returnValue = 0;
  }
  return -EINVAL;
}

int32_t agonLight2CancelTimer(va_list args) {
  (void) args;
  return 0;
}

int32_t agonLight2CancelAndGetTimer(va_list args) {
  (void)              va_arg(args, int32_t);          // deviceId
  uint64_t           *cn = va_arg(args, uint64_t*);   // configuredNanoseconds
  uint64_t           *rn = va_arg(args, uint64_t*);   // remainingNanoseconds
  void             (**cb)(void) = va_arg(args, void (**)(void)); // callback
  if (cn != NULL) { *cn = 0; }
  if (rn != NULL) { *rn = 0; }
  if (cb != NULL) { *cb = NULL; }
  return -EINVAL;
}

// ---------------------------------------------------------------------------
// Block device subsystem stubs (no SD card driver for eZ80 side yet)
// ---------------------------------------------------------------------------

int32_t agonLight2InitBlockDevice(va_list args) {
  (void) args;
  return -ENODEV;
}

int32_t agonLight2GetBlockDevice(va_list args) {
  (void)              va_arg(args, int32_t);     // deviceId
  BlockDevice **returnValue = va_arg(args, BlockDevice**);
  if (returnValue != NULL) {
    *returnValue = NULL;
  }
  return -ENODEV;
}

int32_t agonLight2RestartBlockDevice(va_list args) {
  (void) va_arg(args, ProcessDescriptor*);
  return -ENODEV;
}

// ---------------------------------------------------------------------------
// Per-subsystem function tables
// ---------------------------------------------------------------------------

static HalFunction agonLight2MemoryFunctions[HAL_MEMORY_NUM_FNS] = {
  [HAL_MEMORY_PROCESS_STACK_SIZE]         = agonLight2ProcessStackSize,
  [HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = agonLight2MemoryManagerStackSize,
  [HAL_MEMORY_BOTTOM_OF_HEAP]             = agonLight2BottomOfHeap,
  [HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = agonLight2NumExtraSchedulerStacks,
  [HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = agonLight2NumExtraConsoleStacks,
};

static HalFunction agonLight2UartFunctions[HAL_UART_NUM_FNS] = {
  [HAL_UART_INIT]       = agonLight2InitUart,
  [HAL_UART_CONFIGURE]  = agonLight2ConfigureUart,
  [HAL_UART_POLL]       = agonLight2PollUart,
  [HAL_UART_WRITE]      = agonLight2WriteUart,
  [HAL_UART_IS_CONSOLE] = agonLight2IsUartConsole,
};

static HalFunction agonLight2DioFunctions[HAL_DIO_NUM_FNS] = {
  [HAL_DIO_INIT]      = agonLight2InitDio,
  [HAL_DIO_CONFIGURE] = agonLight2ConfigureDio,
  [HAL_DIO_WRITE]     = agonLight2WriteDio,
};

static HalFunction agonLight2SpiFunctions[HAL_SPI_NUM_FNS] = {
  [HAL_SPI_INIT]           = agonLight2InitSpi,
  [HAL_SPI_CONFIGURE]      = agonLight2ConfigureSpi,
  [HAL_SPI_START_TRANSFER] = agonLight2StartSpiTransfer,
  [HAL_SPI_END_TRANSFER]   = agonLight2EndSpiTransfer,
  [HAL_SPI_TRANSFER8]      = agonLight2SpiTransfer8,
  [HAL_SPI_TRANSFER_BYTES] = agonLight2SpiTransferBytes,
};

static HalFunction agonLight2ClockFunctions[HAL_CLOCK_NUM_FNS] = {
  [HAL_CLOCK_INIT]                     = agonLight2InitClock,
  [HAL_CLOCK_SET_SYSTEM_TIME]          = agonLight2SetSystemTime,
  [HAL_CLOCK_GET_ELAPSED_MILLISECONDS] = agonLight2GetElapsedMilliseconds,
  [HAL_CLOCK_GET_ELAPSED_MICROSECONDS] = agonLight2GetElapsedMicroseconds,
  [HAL_CLOCK_GET_ELAPSED_NANOSECONDS]  = agonLight2GetElapsedNanoseconds,
};

static HalFunction agonLight2PowerFunctions[HAL_POWER_NUM_FNS] = {
  [HAL_POWER_ENTER_MODE] = agonLight2EnterMode,
};

static HalFunction agonLight2TimerFunctions[HAL_TIMER_NUM_FNS] = {
  [HAL_TIMER_INIT]                   = agonLight2InitTimer,
  [HAL_TIMER_INIT_DEVICE]            = agonLight2InitTimerDevice,
  [HAL_TIMER_CONFIG_ONE_SHOT]        = agonLight2ConfigOneShotTimer,
  [HAL_TIMER_CONFIGURED_NANOSECONDS] = agonLight2ConfiguredTimerNanoseconds,
  [HAL_TIMER_REMAINING_NANOSECONDS]  = agonLight2RemainingTimerNanoseconds,
  [HAL_TIMER_CANCEL]                 = agonLight2CancelTimer,
  [HAL_TIMER_CANCEL_AND_GET]         = agonLight2CancelAndGetTimer,
};

static HalFunction agonLight2BlockDeviceFunctions[HAL_BLOCK_DEVICE_NUM_FNS] = {
  [HAL_BLOCK_DEVICE_INIT]    = agonLight2InitBlockDevice,
  [HAL_BLOCK_DEVICE_GET]     = agonLight2GetBlockDevice,
  [HAL_BLOCK_DEVICE_RESTART] = agonLight2RestartBlockDevice,
};

// ---------------------------------------------------------------------------
// Online-device bitmask arrays (all offline until hardware is brought up)
// ---------------------------------------------------------------------------

static uint32_t agonLight2UartsOnline[]        = { 0x00000000 };
static uint32_t agonLight2DiosOnline[]         = { 0x00000000 };
static uint32_t agonLight2SpisOnline[]         = { 0x00000000 };
static uint32_t agonLight2TimersOnline[]       = { 0x00000000 };
static uint32_t agonLight2BlockDevicesOnline[] = { 0x00000000 };

// ---------------------------------------------------------------------------
// Platform init
// ---------------------------------------------------------------------------

int32_t halAgonLight2Init(void) {
  halFunctions[HAL_MEMORY]       = agonLight2MemoryFunctions;
  halFunctions[HAL_UART]         = agonLight2UartFunctions;
  halFunctions[HAL_DIO]          = agonLight2DioFunctions;
  halFunctions[HAL_SPI]          = agonLight2SpiFunctions;
  halFunctions[HAL_CLOCK]        = agonLight2ClockFunctions;
  halFunctions[HAL_POWER]        = agonLight2PowerFunctions;
  halFunctions[HAL_TIMER]        = agonLight2TimerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = agonLight2BlockDeviceFunctions;

  halCommonMemory.overlayMap  = (NanoOsOverlayMap*) (uintptr_t) OVERLAY_ADDRESS;
  halCommonMemory.overlaySize = OVERLAY_SIZE;

  halCommonUart.numSupported        = 0;
  halCommonUart.online              = agonLight2UartsOnline;

  halCommonDio.numSupported         = 0;
  halCommonDio.online               = agonLight2DiosOnline;

  halCommonSpi.numSupported         = 0;
  halCommonSpi.online               = agonLight2SpisOnline;

  halCommonTimer.numSupported       = 0;
  halCommonTimer.online             = agonLight2TimersOnline;

  halCommonBlockDevice.numSupported = 0;
  halCommonBlockDevice.online       = agonLight2BlockDevicesOnline;

  return halCommonInit();
}

#endif // NANOOS_AGON
