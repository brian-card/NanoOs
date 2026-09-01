///////////////////////////////////////////////////////////////////////////////
///
/// @file              HalMock.c
///
/// @brief             Layered mock HAL implementation.  See HalMock.h.
///
///////////////////////////////////////////////////////////////////////////////

#ifdef __x86_64__

#include <setjmp.h>
#include <string.h>

#include "HalMock.h"
#include "MockSubsystems.h"

// NanoOs HAL wiring - the same headers, and the same forward-declaration
// dodges, HalPosix.c uses.  NanoOsApi.h / OverlayFunctions.h are deliberately
// NOT included: they pull user/../include/sys/types.h, whose pid_t/uid_t
// clash with the system <sys/types.h> that <stdlib.h> brings in earlier.
// Everything is addressed through the sim/ symlink dir so src/kernel never
// lands on the include path.
#include "hal/HalCommon.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"
#include "kernel/Scheduler.h"
#include "user/NanoOsErrno.h"

// Must come last
#include "user/NanoOsStdio.h"

// --- symbols reused from other translation units (see the note above) -----

typedef struct NanoOsApi NanoOsApi;
extern NanoOsApi  nanoOsApi;
extern NanoOsApi *NANO_OS_API;

void* callOverlayFunctionFromFile(const void *overlayDir, const void *overlay,
  const char *function, void *args);

int32_t posixProcessStackSize(va_list args);
int32_t posixMemoryManagerStackSize(va_list args);
int32_t posixBottomOfHeap(va_list args);
int32_t posixNumExtraSchedulerStacks(va_list args);
int32_t posixNumExtraConsoleStacks(va_list args);

int32_t posixInitDio(va_list args);
int32_t posixConfigureDio(va_list args);
int32_t posixWriteDio(va_list args);

int32_t posixInitSpi(va_list args);
int32_t posixConfigureSpiDevice(va_list args);
int32_t posixSetSpiSpeed(va_list args);
int32_t posixStartSpiTransfer(va_list args);
int32_t posixEndSpiTransfer(va_list args);
int32_t posixSpiTransfer8(va_list args);
int32_t posixSpiTransferBytes(va_list args);

int32_t posixTimeInit(va_list args);
int32_t posixSetSystemTime(va_list args);
int32_t posixGetElapsedMilliseconds(va_list args);
int32_t posixGetElapsedMicroseconds(va_list args);
int32_t posixGetElapsedNanoseconds(va_list args);

int32_t posixInitTimer(va_list args);
int32_t posixInitTimerDevice(va_list args);
int32_t posixConfigOneShotTimer(va_list args);
int32_t posixConfiguredTimerNanoseconds(va_list args);
int32_t posixRemainingTimerNanoseconds(va_list args);
int32_t posixCancelTimer(va_list args);
int32_t posixCancelAndGetTimer(va_list args);

int32_t posixInitUart(va_list args);
int32_t posixConfigureUart(va_list args);
int32_t posixPollUart(va_list args);
int32_t posixWriteUart(va_list args);
int32_t posixIsUartConsole(va_list args);

int32_t halPosixImplInit(jmp_buf resetBuffer,
  NanoOsOverlayMap **overlayMap, size_t *overlaySize, StaticLogs **staticLogs,
  NanoOsOverlayMap **contiguousFilesystem, size_t *contiguousFilesystemSize);

// --- state -----------------------------------------------------------

/// @var _powerHook
///
/// @brief Invoked by mockEnterPowerModeFn on any power mode.  Set by the
/// harness via halMockSetPowerHook.
static void (*_powerHook)(void) = NULL;

/// @var _restartShell
///
/// @brief Harness-provided function used to populate the shell process slot.
static HalMockRestartShellFn _restartShell = NULL;

// online bitmasks (pointed at from halImpl at init time)
static uint32_t _uartsOnline[]        = { 0x00000002 };
static uint32_t _diosOnline[]         = { 0x00000000 };
static uint32_t _spisOnline[]         = { 0x00000000 };
static uint32_t _timersOnline[]       = { 0x00000003 };
static uint32_t _blockDevicesOnline[] = { 0x00000000 };

static char _logBuffer[128];

// --- dispatch tables -------------------------------------------------

static HalFunction _memoryFunctions[HAL_MEMORY_NUM_FNS];
static HalFunction _uartFunctions[HAL_UART_NUM_FNS];
static HalFunction _dioFunctions[HAL_DIO_NUM_FNS];
static HalFunction _spiFunctions[HAL_SPI_NUM_FNS];
static HalFunction _clockFunctions[HAL_CLOCK_NUM_FNS];
static HalFunction _powerFunctions[HAL_POWER_NUM_FNS];
static HalFunction _timerFunctions[HAL_TIMER_NUM_FNS];
static HalFunction _blockDeviceFunctions[HAL_BLOCK_DEVICE_NUM_FNS];

// --- power -----------------------------------------------------------

/// @fn static int32_t mockEnterPowerModeFn(va_list args)
///
/// @brief Any power mode hands off to the harness' power hook (which ends the
/// child) instead of exit()ing the whole test run.
static int32_t mockEnterPowerModeFn(va_list args) {
  (void) args;
  if (_powerHook != NULL) {
    _powerHook();
  }
  return 0;
}

// --- root storage --------------------------------------------------

/// @fn static int32_t mockInitRootStorageNone(void)
///
/// @brief MOCK_STORAGE_NONE: leave rootFsPid == 0.  schedFopen and friends
/// already treat that as "no filesystem" and return NULL/ENOENT.
static int32_t mockInitRootStorageNone(void) {
  return 0;
}

// --- config ------------------------------------------------------

HalMockConfig halMockConfigDefault(void) {
  HalMockConfig config;
  memset(&config, 0, sizeof(config));
  config.clock     = MOCK_CLOCK_VIRTUAL;
  config.timer     = MOCK_TIMER_SYNCHRONOUS;
  config.uart      = MOCK_UART_BUFFER;
  config.storage   = MOCK_STORAGE_NONE;
  config.imagePath = NULL;
  return config;
}

void halMockSetRestartShell(HalMockRestartShellFn fn) {
  _restartShell = fn;
}

void halMockSetPowerHook(void (*hook)(void)) {
  _powerHook = hook;
}

// --- init --------------------------------------------------------

int halMockInit(const HalMockConfig *config, jmp_buf *powerReturn) {
  (void) powerReturn; // reserved; teardown is via halMockSetPowerHook now
  HalMockConfig cfg = (config != NULL) ? *config : halMockConfigDefault();

  mockClockReset();
  mockTimerReset();
  mockUartReset();
  mockBlockDeviceReset();

  // Memory + DIO + SPI: reuse the proven POSIX implementations.
  _memoryFunctions[HAL_MEMORY_PROCESS_STACK_SIZE]         = posixProcessStackSize;
  _memoryFunctions[HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = posixMemoryManagerStackSize;
  _memoryFunctions[HAL_MEMORY_BOTTOM_OF_HEAP]             = posixBottomOfHeap;
  _memoryFunctions[HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = posixNumExtraSchedulerStacks;
  _memoryFunctions[HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = posixNumExtraConsoleStacks;

  _dioFunctions[HAL_DIO_INIT]      = posixInitDio;
  _dioFunctions[HAL_DIO_CONFIGURE] = posixConfigureDio;
  _dioFunctions[HAL_DIO_WRITE]     = posixWriteDio;

  _spiFunctions[HAL_SPI_INIT]           = posixInitSpi;
  _spiFunctions[HAL_SPI_CONFIGURE]      = posixConfigureSpiDevice;
  _spiFunctions[HAL_SPI_START_TRANSFER] = posixStartSpiTransfer;
  _spiFunctions[HAL_SPI_END_TRANSFER]   = posixEndSpiTransfer;
  _spiFunctions[HAL_SPI_TRANSFER8]      = posixSpiTransfer8;
  _spiFunctions[HAL_SPI_TRANSFER_BYTES] = posixSpiTransferBytes;
  _spiFunctions[HAL_SPI_SET_SPEED]      = posixSetSpiSpeed;

  // Clock.
  if (cfg.clock == MOCK_CLOCK_POSIX) {
    _clockFunctions[HAL_CLOCK_INIT]                     = posixTimeInit;
    _clockFunctions[HAL_CLOCK_SET_SYSTEM_TIME]          = posixSetSystemTime;
    _clockFunctions[HAL_CLOCK_GET_ELAPSED_MILLISECONDS] = posixGetElapsedMilliseconds;
    _clockFunctions[HAL_CLOCK_GET_ELAPSED_MICROSECONDS] = posixGetElapsedMicroseconds;
    _clockFunctions[HAL_CLOCK_GET_ELAPSED_NANOSECONDS]  = posixGetElapsedNanoseconds;
  } else {
    _clockFunctions[HAL_CLOCK_INIT]                     = mockClockInitFn;
    _clockFunctions[HAL_CLOCK_SET_SYSTEM_TIME]          = mockClockSetSystemTimeFn;
    _clockFunctions[HAL_CLOCK_GET_ELAPSED_MILLISECONDS] = mockClockGetElapsedMillisecondsFn;
    _clockFunctions[HAL_CLOCK_GET_ELAPSED_MICROSECONDS] = mockClockGetElapsedMicrosecondsFn;
    _clockFunctions[HAL_CLOCK_GET_ELAPSED_NANOSECONDS]  = mockClockGetElapsedNanosecondsFn;
  }

  // Timer.
  if (cfg.timer == MOCK_TIMER_POSIX) {
    _timerFunctions[HAL_TIMER_INIT]                   = posixInitTimer;
    _timerFunctions[HAL_TIMER_INIT_DEVICE]            = posixInitTimerDevice;
    _timerFunctions[HAL_TIMER_CONFIG_ONE_SHOT]        = posixConfigOneShotTimer;
    _timerFunctions[HAL_TIMER_CONFIGURED_NANOSECONDS] = posixConfiguredTimerNanoseconds;
    _timerFunctions[HAL_TIMER_REMAINING_NANOSECONDS]  = posixRemainingTimerNanoseconds;
    _timerFunctions[HAL_TIMER_CANCEL]                 = posixCancelTimer;
    _timerFunctions[HAL_TIMER_CANCEL_AND_GET]         = posixCancelAndGetTimer;
  } else {
    _timerFunctions[HAL_TIMER_INIT]                   = mockTimerInitFn;
    _timerFunctions[HAL_TIMER_INIT_DEVICE]            = mockTimerInitDeviceFn;
    _timerFunctions[HAL_TIMER_CONFIG_ONE_SHOT]        = mockTimerConfigOneShotFn;
    _timerFunctions[HAL_TIMER_CONFIGURED_NANOSECONDS] = mockTimerConfiguredNanosecondsFn;
    _timerFunctions[HAL_TIMER_REMAINING_NANOSECONDS]  = mockTimerRemainingNanosecondsFn;
    _timerFunctions[HAL_TIMER_CANCEL]                 = mockTimerCancelFn;
    _timerFunctions[HAL_TIMER_CANCEL_AND_GET]         = mockTimerCancelAndGetFn;
  }

  // UART.
  if (cfg.uart == MOCK_UART_POSIX) {
    _uartFunctions[HAL_UART_INIT]       = posixInitUart;
    _uartFunctions[HAL_UART_CONFIGURE]  = posixConfigureUart;
    _uartFunctions[HAL_UART_POLL]       = posixPollUart;
    _uartFunctions[HAL_UART_WRITE]      = posixWriteUart;
    _uartFunctions[HAL_UART_IS_CONSOLE] = posixIsUartConsole;
  } else {
    _uartFunctions[HAL_UART_INIT]       = mockUartInitFn;
    _uartFunctions[HAL_UART_CONFIGURE]  = mockUartConfigureFn;
    _uartFunctions[HAL_UART_POLL]       = mockUartPollFn;
    _uartFunctions[HAL_UART_WRITE]      = mockUartWriteFn;
    _uartFunctions[HAL_UART_IS_CONSOLE] = mockUartIsConsoleFn;
  }

  _powerFunctions[HAL_POWER_ENTER_MODE] = mockEnterPowerModeFn;

  _blockDeviceFunctions[HAL_BLOCK_DEVICE_INIT]    = mockBlockDeviceInitFn;
  _blockDeviceFunctions[HAL_BLOCK_DEVICE_GET]     = mockBlockDeviceGetFn;
  _blockDeviceFunctions[HAL_BLOCK_DEVICE_RESTART] = mockBlockDeviceRestartFn;

  halFunctions[HAL_MEMORY]       = _memoryFunctions;
  halFunctions[HAL_UART]         = _uartFunctions;
  halFunctions[HAL_DIO]          = _dioFunctions;
  halFunctions[HAL_SPI]          = _spiFunctions;
  halFunctions[HAL_CLOCK]        = _clockFunctions;
  halFunctions[HAL_POWER]        = _powerFunctions;
  halFunctions[HAL_TIMER]        = _timerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = _blockDeviceFunctions;

  // Platform hooks.
  halImpl.platform.callFileOverlay       = callOverlayFunctionFromFile;
  halImpl.platform.execCommand           = NULL;
  halImpl.platform.restartRootFilesystem = NULL;
  halImpl.platform.restartShell          = (int32_t (*)(ProcessDescriptor*)) _restartShell;
  if (cfg.storage == MOCK_STORAGE_FILE) {
    // TODO: POSIX SD-card process over cfg.imagePath.
    halImpl.platform.initRootStorage = halCommonInitRootFilesystem;
  } else {
    halImpl.platform.initRootStorage = mockInitRootStorageNone;
  }

  // Subsystem counts / online bitmasks.
  halImpl.uart.numSupported        = 2;
  halImpl.uart.online              = _uartsOnline;
  halImpl.dio.numSupported         = 0;
  halImpl.dio.online              = _diosOnline;
  halImpl.spi.numSupported         = 0;
  halImpl.spi.online              = _spisOnline;
  halImpl.timer.numSupported       = 2;
  halImpl.timer.online            = _timersOnline;
  halImpl.blockDevice.numSupported = 1;
  halImpl.blockDevice.online      = _blockDevicesOnline;

  halImpl.memory.logBuffer      = _logBuffer;
  halImpl.memory.logBufferSize  = sizeof(_logBuffer);
  halImpl.memory.stringsPresent = true;

  jmp_buf implResetBuffer;
  memset(implResetBuffer, 0, sizeof(implResetBuffer));
  int32_t result = halPosixImplInit(implResetBuffer,
    &halImpl.memory.overlayMap,
    &halImpl.memory.overlaySize,
    &halImpl.memory.staticLogs,
    &halImpl.memory.contiguousFilesystem,
    &halImpl.memory.contiguousFilesystemSize);
  if (result != 0) {
    return result;
  }
  if (halImpl.memory.staticLogs != NULL) {
    memset(halImpl.memory.staticLogs, 0, sizeof(StaticLogs));
  }

  NANO_OS_API = &nanoOsApi;

  return halCommonInit();
}

#endif // __x86_64__
