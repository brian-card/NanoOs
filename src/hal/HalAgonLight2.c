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

#ifdef NANO_OS_AGON_LIGHT_2

#include "HalAgonLight2.h"
#include "HalCommon.h"
#include "../kernel/Logger.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsStdio.h"

// Types and prototypes from files that we can't directly include.
typedef struct NanoOsApi NanoOsApi;
extern NanoOsApi nanoOsApi;
extern NanoOsApi *NANO_OS_API;

#ifdef __cplusplus
extern "C"
{
#endif
void* callOverlayFunctionFromFile(const void *overlayDir, const void *overlay,
  const char *function, void *args);
#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// Memory layout constants
// ---------------------------------------------------------------------------

/// @def RAM_START_ADDRESS
///
/// @brief Address of the start of external RAM.  The AgonLight 2 uses the
/// eZ80F92 CPU, which begins external RAM at address 0x40000.  (0x0 through
/// 0x1ffff are 128 KB of flash.  0x20000 through 0x3ffff is missing on the
/// eZ80F92.  It's an additional 128 KB of flash on the eZ80F91.)
#define RAM_START_ADDRESS 0x40000

/// @def NANO_OS_SIZE
///
/// @brief The size, in bytes, reserved for the NanoOs binary in memory.  We
/// will reserve 128 KB for this.
#define NANO_OS_SIZE (128 * 1024)

/// @def FILESYSTEM_DRIVER_ADDRESS
///
/// @brief The address of the start of the area reserved for the filesystem
/// driver binary.
#define FILESYSTEM_DRIVER_ADDRESS (RAM_START_ADDRESS + NANO_OS_SIZE)

/// @def FILESYSTEM_DRIVER_SIZE
///
/// @brief The size, in bytes, reserved for the filesystem driver in memory.  We
/// will reserve 32 KB for this.
#define FILESYSTEM_DRIVER_SIZE (32 * 1024)

/// @def STATIC_LOGS_ADDRESS
///
/// @brief The address of the start of the area reserved for the static log
/// metadata and entries.
#define STATIC_LOGS_ADDRESS (FILESYSTEM_DRIVER_ADDRESS + FILESYSTEM_DRIVER_SIZE)

/// @def STATIC_LOGS_SIZE
///
/// @brief The size, in bytes, reserved for the static logs in memory.  We will
/// only reserve 1 KB for this.
#define STATIC_LOGS_SIZE 1024

/// @def HEAP_START_ADDRESS
///
/// @brief Address of the start (bottom) of the heap.  This is the first address
/// after all of the reserved space.
#define HEAP_START_ADDRESS (STATIC_LOGS_ADDRESS + STATIC_LOGS_SIZE)

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

/// @def DIO_PIN_UNDEFINED
///
/// @brief Value to indicate that the value of a specific pin is undefined.
#define DIO_PIN_UNDEFINED 255

/// @def MAX_SPI_DEVICES
///
/// @brief The maximum number of SPI devices the system can support.
#define MAX_SPI_DEVICES 2

/// @def BASE_BAUD
///
/// @brief The baud rate that's used to compute the divisor that actually
/// initializes a UART periheral.
#define BASE_BAUD 1152000

/// @var _spiCopiDio
///
/// @brief DIO pin used for SPI COPI.
static uint8_t _spiCopiDio = 95;

/// @var _spiCipoDio
///
/// @brief DIO pin used for SPI CIPO.
static uint8_t _spiCipoDio = 94;

/// @var _spiSckDio
///
/// @brief DIO pin used for SPI serial clock.
static uint8_t _spiSckDio = 91;

/// @var _sdCardPinChipSelect
///
/// @brief Pin to use for the MicroSD card reader's SPI chip select line.
static uint8_t _sdCardPinChipSelect = 90;

// The fact that we've included Arduino.h in this file means that the memory
// management functions from its library are available in this file.  That's a
// problem.  (a) We can't allow dynamic memory at the HAL level and (b) if we
// were to allocate memory from Arduino's memory manager, we'd run the risk
// of corrupting something elsewhere in memory.  Just in case we ever forget
// this and try to use memory management functions in the future, define them
// all to MEMORY_ERROR so that the build will fail.
#undef malloc
#define malloc  MEMORY_ERROR
#undef calloc
#define calloc  MEMORY_ERROR
#undef realloc
#define realloc MEMORY_ERROR
#undef free
#define free   MEMORY_ERROR

/// @var blockDevices
///
/// @brief Array of BlockDevice pointers that are managed by the driver
/// processes.
static BlockDevice *blockDevices[] = {
  NULL,
};

/// @def _numBlockDevices
///
/// @brief Number of BlockDevices that can be managed by the HAL.
///
/// @note This is a #define rather than a const uint32_t so that it doesn't
/// need its own KEEP_IN_FLASH treatment - it's folded into an immediate
/// value at each use site instead of occupying storage that could land in
/// .rodata.
#define _numBlockDevices \
  ((uint32_t) (sizeof(blockDevices) / sizeof(blockDevices[0])))

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

extern void uart0_init(uint16_t divisor);
extern void uart1_init(uint16_t divisor);

int32_t agonLight2ConfigureUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint32_t baud = va_arg(args, uint32_t);
  int returnValue = -ERANGE;

  switch (deviceId) {
    case 0:
      {
        uart0_init(BASE_BAUD / baud);
        returnValue = 0;
        break;
      }

    case 1:
      {
        uart1_init(BASE_BAUD / baud);
        returnValue = 0;
        break;
      }
  }

  return returnValue;
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
  if (SCHEDULER_STATE == NULL) {
    return -EBUSY;
  }

  // Create the SD card process.
  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio   = _sdCardPinChipSelect,
    .spiCopiDio = _spiCopiDio,
    .spiCipoDio = _spiCipoDio,
    .spiSckDio  = _spiSckDio,
  };

  blockDevices[0] = halCommonInitRootSdSpiStorage(&sdCardSpiArgs);
  if (blockDevices[0] == NULL) {
    return -ENODEV;
  }
  setOnline(HAL->blockDevice, 0);

  return 0;
}

int32_t agonLight2GetBlockDevice(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  BlockDevice **returnValue = va_arg(args, BlockDevice**);

  if (!online(HAL->blockDevice, deviceId)) {
    if (returnValue != NULL) {
      *returnValue = NULL;
    }
    return -ENODEV;
  }

  if (returnValue != NULL) {
    *returnValue = blockDevices[deviceId];
  }
  return 0;
}

/// @var _sdCardName
///
/// @brief Process name assigned to the SD card process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _sdCardName[] KEEP_IN_FLASH = "SD card";

int32_t agonLight2RestartBlockDevice(va_list args) {
  ProcessDescriptor *processDescriptor = va_arg(args, ProcessDescriptor*);
  int32_t deviceId = (int32_t) (intptr_t) processDescriptor->restartArgs;

  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio   = _sdCardPinChipSelect,
    .spiCopiDio = _spiCopiDio,
    .spiCipoDio = _spiCipoDio,
    .spiSckDio  = _spiSckDio,
  };

  if (processCreate(processDescriptor, runSdCardSpi, &sdCardSpiArgs)
    != processSuccess
  ) {
    logError("Could not restart SD card process\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = _sdCardName;
  processDescriptor->userId = ROOT_USER_ID;

  BlockDevice *sdDevice
    = (BlockDevice*) coroutineResume(processDescriptor->mainThread, NULL);
  if (sdDevice == NULL) {
    logError("SD card restart returned NULL\n");
    return -ENODEV;
  }
  sdDevice->partitionNumber = 1;
  blockDevices[deviceId] = sdDevice;
  setOnline(HAL->blockDevice, deviceId);

  return 0;
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

/// @var _logBuffer
///
/// @brief Statically allocated buffer for formatting log messages.
static char _logBuffer[128];

int32_t halAgonLight2Init(void) {
  halFunctions[HAL_MEMORY]       = agonLight2MemoryFunctions;
  halFunctions[HAL_UART]         = agonLight2UartFunctions;
  halFunctions[HAL_DIO]          = agonLight2DioFunctions;
  halFunctions[HAL_SPI]          = agonLight2SpiFunctions;
  halFunctions[HAL_CLOCK]        = agonLight2ClockFunctions;
  halFunctions[HAL_POWER]        = agonLight2PowerFunctions;
  halFunctions[HAL_TIMER]        = agonLight2TimerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = agonLight2BlockDeviceFunctions;

  halImpl.platform.callFileOverlay = callOverlayFunctionFromFile;
  halImpl.platform.execCommand = execOverlayCommand;
  //// halImpl.platform.restartRootFilesystem = restartContiguousFilesystem;
  //// halImpl.platform.initRootStorage = halCommonInitRootFilesystem,
  //// halImpl.platform.restartShell = restartOverlayShell;
  halImpl.platform.restartShell = restartBuiltinShell;

  halImpl.memory.contiguousFilesystem
    = (NanoOsOverlayMap*) FILESYSTEM_DRIVER_ADDRESS;
  halImpl.memory.contiguousFilesystemSize = FILESYSTEM_DRIVER_SIZE;

  halImpl.memory.overlayMap  = (NanoOsOverlayMap*) OVERLAY_ADDRESS;
  halImpl.memory.overlaySize = OVERLAY_SIZE;

  halImpl.memory.logBuffer      = _logBuffer;
  halImpl.memory.logBufferSize  = sizeof(_logBuffer);
#ifdef NANO_OS_STRINGS_STRIPPED
  halImpl.memory.stringsPresent = false;
#else
  halImpl.memory.stringsPresent = true;
#endif // NANO_OS_STRINGS_STRIPPED
  /*
   * TODO:
   *
   * Remove this next line once we're running a real image with a full HAL
   * implementation.  This is only here for HAL development.
   *
   * JBC 2026-08-06
   */
  halImpl.memory.stringsPresent = true;
//// #if LOG_THRESHOLD < LOG_LEVEL_DETAIL
  halImpl.memory.staticLogs     = NULL;
//// #else // LOG_THRESHOLD >= LOG_LEVEL_DETAIL
////   halImpl.memory.staticLogs     = (StaticLogs*) STATIC_LOGS_ADDRESS;
////   memset(HAL->memory.staticLogs, 0, sizeof(*HAL->memory.staticLogs));
//// #endif // LOG_THRESHOLD < LOG_LEVEL_DETAIL

  halImpl.uart.numSupported        = 0;
  halImpl.uart.online              = agonLight2UartsOnline;

  halImpl.dio.numSupported         = 0;
  halImpl.dio.online               = agonLight2DiosOnline;

  halImpl.spi.numSupported         = 0;
  halImpl.spi.online               = agonLight2SpisOnline;

  halImpl.timer.numSupported       = 0;
  halImpl.timer.online             = agonLight2TimersOnline;

  halImpl.blockDevice.numSupported = _numBlockDevices;
  halImpl.blockDevice.online       = agonLight2BlockDevicesOnline;

  NANO_OS_API = &nanoOsApi;

  return halCommonInit();
}

#endif // NANO_OS_AGON_LIGHT_2
