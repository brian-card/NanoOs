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

/// @file HalArduinoAvr.cpp
///
/// @brief HAL implementation for an AVR-based Arduino board.

#if defined(__AVR_ATmega4809__) || defined(__AVR_ATmega2560__)

// Base Arduino definitions
#define FILE  Arduino_FILE
#define gid_t Arduino_gid_t
#define uid_t Arduino_uid_t
#define pid_t Arduino_pid_t
#include <Arduino.h>
#undef FILE
#undef gid_t
#undef uid_t
#undef pid_t

// Basic SPI communication
#include <SPI.h>

// Standard C includes from the compiler
#include <limits.h>

#include "HalArduinoAvr.h"
#include "HalCommon.h"
// Deliberately *NOT* including MemoryManager.h here.  The HAL has to be
// operational prior to the memory manager and really should be completely
// independent of it.
#include "../kernel/Commands.h"
#include "../kernel/NanoOs.h"
#include "../kernel/Processes.h"
#include "../kernel/SdCardSpi.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsStdio.h"

/// @def MAX_SPI_DEVICES
///
/// @brief The maximum number of SPI devices the system can support.
#define MAX_SPI_DEVICES 2

/// @def PROCESS_STACK_SIZE
///
/// @brief The size, in bytes, of a regular process's stack.
#define PROCESS_STACK_SIZE 320

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief The size, in bytes, of the memory manager process's stack.
#define MEMORY_MANAGER_STACK_SIZE 128

/// @def DIO_PIN_UNDEFINED
///
/// @brief Value to indicate that the value of a specific pin is undefined.
#define DIO_PIN_UNDEFINED 255

/// @var _dioStart
///
/// @brief The first DIO pin number that's usable on the board.
static uint8_t _dioStart = 0;

/// @var _numDioPins
///
/// @brief The number of digital IO pins on the board.
static uint32_t _numDioPins = 0;

/// @var _spiCopiDio
///
/// @brief DIO pin used for SPI COPI.
static uint8_t _spiCopiDio = DIO_PIN_UNDEFINED;

/// @var _spiCipoDio
///
/// @brief DIO pin used for SPI CIPO.
static uint8_t _spiCipoDio = DIO_PIN_UNDEFINED;

/// @var _spiSckDio
///
/// @brief DIO pin used for SPI serial clock.
static uint8_t _spiSckDio = DIO_PIN_UNDEFINED;

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

// Sleep configuration
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

/// @def HAL_ARDUINO_AVR_NUM_PINS
///
/// @brief The total number of pins (digital and analog) on the board.  Used
/// to iterate over every pin when powering down.  The megaAVR-0 core
/// provides NUM_TOTAL_PINS; the classic AVR core does not, but its
/// NUM_DIGITAL_PINS already accounts for the analog pins being addressable
/// as digital pins.
#if defined(__AVR_ATmega4809__)
#define HAL_ARDUINO_AVR_NUM_PINS NUM_TOTAL_PINS
#elif defined(__AVR_ATmega2560__)
#define HAL_ARDUINO_AVR_NUM_PINS NUM_DIGITAL_PINS
#endif

int32_t arduinoAvrProcessStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = PROCESS_STACK_SIZE;
  }
  return 0;
}

int32_t arduinoAvrMemoryManagerStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  if (returnValue != NULL) {
    *returnValue = (debug == false)
      ? MEMORY_MANAGER_STACK_SIZE
      : MEMORY_MANAGER_DEBUG_STACK_SIZE;
  }
  return 0;
}

int32_t arduinoAvrBottomOfHeap(va_list args) {
  bool debug = (bool) va_arg(args, int);
  void **returnValue = va_arg(args, void**);
  (void) debug;
  if (returnValue != NULL) {
    extern int __heap_start;
    extern char *__brkval;
    *returnValue = (__brkval == NULL) ? (char*) &__heap_start : __brkval;
  }
  return 0;
}

int32_t arduinoAvrNumExtraSchedulerStacks(va_list args) {
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = 1;
  }
  return 0;
}

int32_t arduinoAvrNumExtraConsoleStacks(va_list args) {
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = 1;
  }
  return 0;
}

/// @var uarts
///
/// @brief Array of serial ports on the system.  Index 0 is the main port,
/// which is the USB serial port.
static HardwareSerial *uarts[] = {
  &Serial,
  &Serial1,
};

/// @var _numUarts
///
/// @brief The number of serial ports we support on this AVR board.
static const int _numUarts = sizeof(uarts) / sizeof(uarts[0]);

int32_t arduinoAvrInitUart(va_list args) {
  (void) args;
  return 0;
}

int32_t arduinoAvrConfigureUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint32_t baud = va_arg(args, uint32_t);
  int returnValue = -ERANGE;

  if ((deviceId >= 0) && (deviceId < _numUarts)) {
    uarts[deviceId]->begin(baud);
    while (!(*uarts[deviceId]));
    returnValue = 0;
  }

  return returnValue;
}

int32_t arduinoAvrPollUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  int serialData = -ERANGE;

  if ((deviceId >= 0) && (deviceId < _numUarts)) {
    serialData = uarts[deviceId]->read();
  }

  return serialData;
}

int32_t arduinoAvrWriteUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  const uint8_t *data = va_arg(args, const uint8_t*);
  ssize_t length = va_arg(args, ssize_t);
  ssize_t *returnValue = va_arg(args, ssize_t*);

  ssize_t numBytesWritten = -ERANGE;

  if ((deviceId >= 0) && (deviceId < _numUarts) && (length >= 0)) {
    numBytesWritten = uarts[deviceId]->write(data, length);
  }

  if (returnValue != NULL) {
    *returnValue = numBytesWritten;
  }
  return (numBytesWritten >= 0) ? 0 : (int32_t) numBytesWritten;
}

int32_t arduinoAvrIsUartConsole(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool *returnValue = va_arg(args, bool*);
  (void) deviceId;
  if (returnValue != NULL) {
    *returnValue = true;
  }
  return 0;
}

int32_t arduinoAvrInitDio(va_list args) {
  (void) args;
  return 0;
}

static int32_t arduinoAvrConfigureDioImpl(int32_t deviceId, bool output) {
  if ((deviceId < _dioStart) || (deviceId >= (int32_t) _numDioPins)) {
    return -ERANGE;
  }
  uint8_t modes[2] = { INPUT, OUTPUT };
  pinMode(deviceId, modes[output]);
  return 0;
}

int32_t arduinoAvrConfigureDio(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool output = (bool) va_arg(args, int);
  return arduinoAvrConfigureDioImpl(deviceId, output);
}

static int32_t arduinoAvrWriteDioImpl(int32_t deviceId, bool high) {
  if ((deviceId < _dioStart) || (deviceId >= (int32_t) _numDioPins)) {
    return -ERANGE;
  }
  uint8_t levels[2] = { LOW, HIGH };
  digitalWrite(deviceId, levels[high]);
  return 0;
}

int32_t arduinoAvrWriteDio(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool high = (bool) va_arg(args, int);
  return arduinoAvrWriteDioImpl(deviceId, high);
}

/// @var globalSpiConfigured
///
/// @brief Whether or not the Arduino's SPI interface has already been
/// configured.
static bool globalSpiConfigured = false;

/// @var globalSpiInUse
///
/// @brief Whether or not the Arduino's SPI interface is currently in use.
static bool globalSpiInUse = false;

/// @var arduinoAvrSpiDevices
///
/// @brief Array of structures that will hold the information about SPI
/// connections.
static struct ArduinoAvrSpi {
  bool     configured;         // Will default to false
  uint8_t  chipSelect;
  bool     transferInProgress; // Will default to false
  uint32_t baud;
} arduinoAvrSpiDevices[MAX_SPI_DEVICES] = {};

/// @var numArduinoSpis
///
/// @brief The number of devices we support in the arduinoAvrSpiDevices array.
static const int numArduinoSpis
  = sizeof(arduinoAvrSpiDevices) / sizeof(arduinoAvrSpiDevices[0]);

static int32_t arduinoAvrInitSpiImpl(void) {
  if (globalSpiConfigured == false) {
    globalSpiConfigured = true;
    SPI.begin();
  }
  return 0;
}

int32_t arduinoAvrInitSpi(va_list args) {
  (void) args;
  return arduinoAvrInitSpiImpl();
}

int32_t arduinoAvrConfigureSpiDevice(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t cs   = (uint8_t) va_arg(args, int);
  uint8_t sck  = (uint8_t) va_arg(args, int);
  uint8_t copi = (uint8_t) va_arg(args, int);
  uint8_t cipo = (uint8_t) va_arg(args, int);
  uint32_t baud = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)) {
    return -ENODEV;
  } else if ((cs < _dioStart) || (cs >= _numDioPins)) {
    return -ERANGE;
  } else if (
       (cs   == _spiSckDio)
    || (cs   == _spiCopiDio)
    || (cs   == _spiCipoDio)
    || (sck  != _spiSckDio)
    || (copi != _spiCopiDio)
    || (cipo != _spiCipoDio)
  ) {
    return -EINVAL;
  } else if (arduinoAvrSpiDevices[deviceId].configured == true) {
    return -EBUSY;
  }

  if (arduinoAvrInitSpiImpl() != 0) {
    return -ENODEV;
  }

  arduinoAvrConfigureDioImpl(cs, 1);
  arduinoAvrWriteDioImpl(cs, 1);

  arduinoAvrSpiDevices[deviceId].chipSelect = cs;
  arduinoAvrSpiDevices[deviceId].baud = baud;
  arduinoAvrSpiDevices[deviceId].configured = true;

  return 0;
}

static int32_t arduinoAvrStartSpiTransferImpl(int32_t deviceId) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoAvrSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  } else if (globalSpiInUse == true) {
    return -EBUSY;
  }

  globalSpiInUse = true;
  arduinoAvrWriteDioImpl(arduinoAvrSpiDevices[deviceId].chipSelect, 0);
  SPI.beginTransaction(SPISettings(arduinoAvrSpiDevices[deviceId].baud,
    MSBFIRST, SPI_MODE0));
  arduinoAvrSpiDevices[deviceId].transferInProgress = true;

  return 0;
}

int32_t arduinoAvrStartSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  return arduinoAvrStartSpiTransferImpl(deviceId);
}

int32_t arduinoAvrEndSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoAvrSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  }

  arduinoAvrSpiDevices[deviceId].transferInProgress = false;
  SPI.endTransaction();
  arduinoAvrWriteDioImpl(arduinoAvrSpiDevices[deviceId].chipSelect, 1);
  for (int ii = 0; ii < 8; ii++) {
    SPI.transfer(0xFF);
  }
  globalSpiInUse = false;

  return 0;
}

int32_t arduinoAvrSpiTransfer8(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t data = (uint8_t) va_arg(args, int);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoAvrSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  } else if (!arduinoAvrSpiDevices[deviceId].transferInProgress) {
    arduinoAvrStartSpiTransferImpl(deviceId);
  }

  return (int) SPI.transfer(data);
}

int32_t arduinoAvrSpiTransferBytes(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t *data = va_arg(args, uint8_t*);
  uint32_t length = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoAvrSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  } else if (!arduinoAvrSpiDevices[deviceId].transferInProgress) {
    arduinoAvrStartSpiTransferImpl(deviceId);
  }

  SPI.transfer(data, length);

  return 0;
}

/// @var halArduinoAvrSpisOnline
///
/// @brief Bitmask array of online SPIs.
static uint32_t halArduinoAvrSpisOnline[] = {
  0x00000003,
};

/// @var baseSystemTimeMs
///
/// @brief The time provided by the user or some other process as a baseline
/// time for the system.
static int64_t baseSystemTimeMs = 0;

int32_t arduinoAvrTimeInit(va_list args) {
  (void) args;
  return 0;
}

int32_t arduinoAvrSetSystemTime(va_list args) {
  struct timespec *now = va_arg(args, struct timespec*);
  if (now == NULL) {
    return -EINVAL;
  }

  baseSystemTimeMs
    = (((int64_t) now->tv_sec) * ((int64_t) 1000))
    + (((int64_t) now->tv_nsec) / ((int64_t) 1000000));

  return 0;
}

static int64_t arduinoAvrGetElapsedMillisecondsImpl(int64_t startTime) {
  int64_t now = baseSystemTimeMs + millis();
  if (now < startTime) {
    return -1;
  }
  return now - startTime;
}

int32_t arduinoAvrGetElapsedMilliseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t result = arduinoAvrGetElapsedMillisecondsImpl(startTime);
  if (returnValue != NULL) {
    *returnValue = result;
  }
  return (result >= 0) ? 0 : -EIO;
}

int32_t arduinoAvrGetElapsedMicroseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t result = arduinoAvrGetElapsedMillisecondsImpl(
    startTime / ((int64_t) 1000)) * ((int64_t) 1000);
  if (returnValue != NULL) {
    *returnValue = result;
  }
  return (result >= 0) ? 0 : -EIO;
}

int32_t arduinoAvrGetElapsedNanoseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t result = arduinoAvrGetElapsedMillisecondsImpl(
    startTime / ((int64_t) 1000000)) * ((int64_t) 1000000);
  if (returnValue != NULL) {
    *returnValue = result;
  }
  return (result >= 0) ? 0 : -EIO;
}

int32_t arduinoAvrEnterPowerMode(va_list args) {
  HalPowerMode powerMode = (HalPowerMode) va_arg(args, int);

  if ((powerMode == HAL_POWER_MODE_OFF)
    || (powerMode == HAL_POWER_MODE_SUSPEND)
  ) {
#if defined(__AVR_ATmega4809__)
    ADC0.CTRLA &= ~ADC_ENABLE_bm;
    SLPCTRL.CTRLA = SLPCTRL_SMODE_PDOWN_gc;
    _PROTECTED_WRITE(BOD.CTRLA, BOD_SLEEP_DIS_gc);
    USART0.CTRLB = 0;
    USART1.CTRLB = 0;
    USART2.CTRLB = 0;
    TWI0.MCTRLA = 0;
    SPI0.CTRLA = 0;
#elif defined(__AVR_ATmega2560__)
    ADCSRA &= ~_BV(ADEN);
    UCSR0B = 0;
    UCSR1B = 0;
    UCSR2B = 0;
    UCSR3B = 0;
    TWCR = 0;
    SPCR = 0;
#endif
    for (uint8_t pin = 0; pin < HAL_ARDUINO_AVR_NUM_PINS; pin++) {
      pinMode(pin, INPUT);
      digitalWrite(pin, LOW);
    }
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sei();
    sleep_cpu();
  } else if (powerMode == HAL_POWER_MODE_RESET) {
#if defined(__AVR_ATmega4809__)
    _PROTECTED_WRITE(RSTCTRL.SWRR, 1);
#elif defined(__AVR_ATmega2560__)
    wdt_enable(WDTO_15MS);
    while (1) {}
#endif
  }

  return 0;
}

/// @var blockDevices
///
/// @brief Array of BlockDevice pointers that are managed by the driver
/// processes.  *DON'T* mark this static.  The individual HAL implementations
/// reference it.
BlockDevice *blockDevices[] = {
  NULL,
};

/// @var _numBlockDevices
///
/// @brief Number of BlockDevices that can be managed by the HAL.
static const uint32_t _numBlockDevices
  = sizeof(blockDevices) / sizeof(blockDevices[0]);

/// @var arduinoAvrBlockDevicesOnline
///
/// @brief Bitmask array of online block devices.
static uint32_t arduinoAvrBlockDevicesOnline[] = {
  0x00000000,
};

// arduinoAvrInitBlockDevice, arduinoAvrGetBlockDevice, and
// arduinoAvrRestartBlockDevice are defined in the individual implementations.
#ifdef __cplusplus
extern "C"
{
#endif
int32_t arduinoAvrInitBlockDevice(va_list args);
int32_t arduinoAvrGetBlockDevice(va_list args);
int32_t arduinoAvrRestartBlockDevice(va_list args);
#ifdef __cplusplus
}
#endif

static HalFunction arduinoAvrMemoryFunctions[HAL_MEMORY_NUM_FNS] = {
  [HAL_MEMORY_PROCESS_STACK_SIZE]         = arduinoAvrProcessStackSize,
  [HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = arduinoAvrMemoryManagerStackSize,
  [HAL_MEMORY_BOTTOM_OF_HEAP]             = arduinoAvrBottomOfHeap,
  [HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = arduinoAvrNumExtraSchedulerStacks,
  [HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = arduinoAvrNumExtraConsoleStacks,
};

static HalFunction arduinoAvrUartFunctions[HAL_UART_NUM_FNS] = {
  [HAL_UART_INIT]       = arduinoAvrInitUart,
  [HAL_UART_CONFIGURE]  = arduinoAvrConfigureUart,
  [HAL_UART_POLL]       = arduinoAvrPollUart,
  [HAL_UART_WRITE]      = arduinoAvrWriteUart,
  [HAL_UART_IS_CONSOLE] = arduinoAvrIsUartConsole,
};

static HalFunction arduinoAvrDioFunctions[HAL_DIO_NUM_FNS] = {
  [HAL_DIO_INIT]      = arduinoAvrInitDio,
  [HAL_DIO_CONFIGURE] = arduinoAvrConfigureDio,
  [HAL_DIO_WRITE]     = arduinoAvrWriteDio,
};

static HalFunction arduinoAvrSpiFunctions[HAL_SPI_NUM_FNS] = {
  [HAL_SPI_INIT]           = arduinoAvrInitSpi,
  [HAL_SPI_CONFIGURE]      = arduinoAvrConfigureSpiDevice,
  [HAL_SPI_START_TRANSFER] = arduinoAvrStartSpiTransfer,
  [HAL_SPI_END_TRANSFER]   = arduinoAvrEndSpiTransfer,
  [HAL_SPI_TRANSFER8]      = arduinoAvrSpiTransfer8,
  [HAL_SPI_TRANSFER_BYTES] = arduinoAvrSpiTransferBytes,
};

static HalFunction arduinoAvrClockFunctions[HAL_CLOCK_NUM_FNS] = {
  [HAL_CLOCK_INIT]                     = arduinoAvrTimeInit,
  [HAL_CLOCK_SET_SYSTEM_TIME]          = arduinoAvrSetSystemTime,
  [HAL_CLOCK_GET_ELAPSED_MILLISECONDS] = arduinoAvrGetElapsedMilliseconds,
  [HAL_CLOCK_GET_ELAPSED_MICROSECONDS] = arduinoAvrGetElapsedMicroseconds,
  [HAL_CLOCK_GET_ELAPSED_NANOSECONDS]  = arduinoAvrGetElapsedNanoseconds,
};

static HalFunction arduinoAvrPowerFunctions[HAL_POWER_NUM_FNS] = {
  [HAL_POWER_ENTER_MODE] = arduinoAvrEnterPowerMode,
};

static HalFunction arduinoAvrBlockDeviceFunctions[HAL_BLOCK_DEVICE_NUM_FNS] = {
  [HAL_BLOCK_DEVICE_INIT]    = arduinoAvrInitBlockDevice,
  [HAL_BLOCK_DEVICE_GET]     = arduinoAvrGetBlockDevice,
  [HAL_BLOCK_DEVICE_RESTART] = arduinoAvrRestartBlockDevice,
};

/// @var _logBuffer
///
/// @brief Statically allocated buffer for formatting log messages.
static char _logBuffer[96];

int32_t halArduinoAvrInit(HalArduinoAvrInitArgs *args) {
  // Wire up per-subsystem function arrays.
  // HAL_TIMER is not supported on this platform — leave halFunctions[HAL_TIMER] NULL.
  halFunctions[HAL_MEMORY]       = arduinoAvrMemoryFunctions;
  halFunctions[HAL_UART]         = arduinoAvrUartFunctions;
  halFunctions[HAL_DIO]          = arduinoAvrDioFunctions;
  halFunctions[HAL_SPI]          = arduinoAvrSpiFunctions;
  halFunctions[HAL_CLOCK]        = arduinoAvrClockFunctions;
  halFunctions[HAL_POWER]        = arduinoAvrPowerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = arduinoAvrBlockDeviceFunctions;

  halCommonPlatform.execCommand = execBuiltinCommand;
  halCommonPlatform.restartShell = restartBuiltinShell;

  // Set per-platform data members from the init args.
  _dioStart   = args->dioStart;
  _numDioPins = args->numDiosSupported;
  _spiCopiDio = args->spiCopiDio;
  _spiCipoDio = args->spiCipoDio;
  _spiSckDio  = args->spiSckDio;

  halCommonUart.numSupported = args->numUartsSupported;
  halCommonUart.online       = args->uartsOnline;

  halCommonDio.numSupported = args->numDiosSupported;
  halCommonDio.online       = args->diosOnline;

  halCommonSpi.numSupported = MAX_SPI_DEVICES;
  halCommonSpi.online       = halArduinoAvrSpisOnline;

  halCommonTimer.numSupported = 0;
  halCommonTimer.online       = NULL;

  halCommonBlockDevice.numSupported = _numBlockDevices;
  halCommonBlockDevice.online       = arduinoAvrBlockDevicesOnline;

  halCommonMemory.logBuffer     = _logBuffer;
  halCommonMemory.logBufferSize = sizeof(_logBuffer);
  halCommonMemory.staticLogs    = NULL;

  return halCommonInit();
}

#endif // defined(__AVR_ATmega4809__) || defined(__AVR_ATmega2560__)
