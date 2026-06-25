////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// @file HalArduinoNanoEvery.cpp
///
/// @brief HAL implementation for an Arduino Nano Every

#if defined(ARDUINO_AVR_NANO_EVERY)

// Base Arduino definitions
#include <Arduino.h>

// Basic SPI communication
#include <SPI.h>

// Standard C includes from the compiler
#include <limits.h>

#include "HalArduinoNanoEvery.h"
#include "HalCommon.h"
// Deliberately *NOT* including MemoryManager.h here.  The HAL has to be
// operational prior to the memory manager and really should be completely
// independent of it.
#include "../kernel/NanoOs.h"
#include "../kernel/Processes.h"
#include "../kernel/SdCardSpi.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsStdio.h"

/// @def DIO_START
///
/// @brief On the Arduino Nano Every, D0 is used for Serial1's RX and D1 is
/// used for Serial1's TX.  We use expect to use Serial1, so our first usable
/// DIO is 2.
#define DIO_START 2

/// @def NUM_DIO_PINS
///
/// @brief The number of digital IO pins on the board.  14 on an Arduino Nano.
#define NUM_DIO_PINS 14

/// @def MAX_SPI_DEVICES
///
/// @brief The maximum number of SPI devices the system can support.
#define MAX_SPI_DEVICES 2

/// @def SPI_COPI_DIO
///
/// @brief DIO pin used for SPI COPI on the Arduino Nano Every.
#define SPI_COPI_DIO 11

/// @def SPI_CIPO_DIO
///
/// @brief DIO pin used for SPI CIPO on the Arduino Nano Every.
#define SPI_CIPO_DIO 12

/// @def SPI_SCK_DIO
///
/// @brief DIO pin used for SPI serial clock on the Arduino Nano Every.
#define SPI_SCK_DIO 13

/// @def PROCESS_STACK_SIZE
///
/// @brief The size, in bytes, of a regular process's stack.
#define PROCESS_STACK_SIZE 320

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief The size, in bytes, of the memory manager process's stack.
#define MEMORY_MANAGER_STACK_SIZE 128

/// @def SD_CARD_PIN_CHIP_SELECT
///
/// @brief Pin to use for the MicroSD card reader's SPI chip select line.
#define SD_CARD_PIN_CHIP_SELECT 4

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

int32_t arduinoNanoEveryProcessStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = PROCESS_STACK_SIZE;
  }
  return 0;
}

int32_t arduinoNanoEveryMemoryManagerStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  if (returnValue != NULL) {
    *returnValue = (debug == false)
      ? MEMORY_MANAGER_STACK_SIZE
      : MEMORY_MANAGER_DEBUG_STACK_SIZE;
  }
  return 0;
}

int32_t arduinoNanoEveryBottomOfHeap(va_list args) {
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

int32_t arduinoNanoEveryNumExtraSchedulerStacks(va_list args) {
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = 1;
  }
  return 0;
}

int32_t arduinoNanoEveryNumExtraConsoleStacks(va_list args) {
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
/// @brief The number of serial ports we support on the Arduino Nano Every.
static const int _numUarts = sizeof(uarts) / sizeof(uarts[0]);

int32_t arduinoNanoEveryInitUart(va_list args) {
  (void) args;
  return 0;
}

int32_t arduinoNanoEveryConfigureUart(va_list args) {
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

int32_t arduinoNanoEveryPollUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  int serialData = -ERANGE;

  if ((deviceId >= 0) && (deviceId < _numUarts)) {
    serialData = uarts[deviceId]->read();
  }

  return serialData;
}

int32_t arduinoNanoEveryWriteUart(va_list args) {
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

int32_t arduinoNanoEveryIsUartConsole(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool *returnValue = va_arg(args, bool*);
  (void) deviceId;
  if (returnValue != NULL) {
    *returnValue = true;
  }
  return 0;
}

/// @var halArduinoNanoEveryUartsOnline
///
/// @brief Bitmask array of online UARTs.
static uint32_t halArduinoNanoEveryUartsOnline[] = {
  0x00000003,
};

int32_t arduinoNanoEveryInitDio(va_list args) {
  (void) args;
  return 0;
}

static int32_t arduinoNanoEveryConfigureDioImpl(int32_t deviceId, bool output) {
  if ((deviceId < DIO_START) || (deviceId >= NUM_DIO_PINS)) {
    return -ERANGE;
  }
  uint8_t modes[2] = { INPUT, OUTPUT };
  pinMode(deviceId, modes[output]);
  return 0;
}

int32_t arduinoNanoEveryConfigureDio(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool output = (bool) va_arg(args, int);
  return arduinoNanoEveryConfigureDioImpl(deviceId, output);
}

static int32_t arduinoNanoEveryWriteDioImpl(int32_t deviceId, bool high) {
  if ((deviceId < DIO_START) || (deviceId >= NUM_DIO_PINS)) {
    return -ERANGE;
  }
  uint8_t levels[2] = { LOW, HIGH };
  digitalWrite(deviceId, levels[high]);
  return 0;
}

int32_t arduinoNanoEveryWriteDio(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool high = (bool) va_arg(args, int);
  return arduinoNanoEveryWriteDioImpl(deviceId, high);
}

/// @var halArduinoNanoEveryDiosOnline
///
/// @brief Bitmask array of online DIOs.
static uint32_t halArduinoNanoEveryDiosOnline[] = {
  0x00003fff,
};

/// @var globalSpiConfigured
///
/// @brief Whether or not the Arduino's SPI interface has already been
/// configured.
static bool globalSpiConfigured = false;

/// @var globalSpiInUse
///
/// @brief Whether or not the Arduino's SPI interface is currently in use.
static bool globalSpiInUse = false;

/// @var arduinoSpiDevices
///
/// @brief Array of structures that will hold the information about SPI
/// connections.
static struct ArduinoNanoEverySpi {
  bool     configured;         // Will default to false
  uint8_t  chipSelect;
  bool     transferInProgress; // Will default to false
  uint32_t baud;
} arduinoSpiDevices[MAX_SPI_DEVICES] = {};

/// @var numArduinoSpis
///
/// @brief The number of devices we support in the arduinoSpiDevices array.
static const int numArduinoSpis
  = sizeof(arduinoSpiDevices) / sizeof(arduinoSpiDevices[0]);

static int32_t arduinoNanoEveryInitSpiImpl(void) {
  if (globalSpiConfigured == false) {
    globalSpiConfigured = true;
    SPI.begin();
  }
  return 0;
}

int32_t arduinoNanoEveryInitSpi(va_list args) {
  (void) args;
  return arduinoNanoEveryInitSpiImpl();
}

int32_t arduinoNanoEveryConfigureSpiDevice(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t cs   = (uint8_t) va_arg(args, int);
  uint8_t sck  = (uint8_t) va_arg(args, int);
  uint8_t copi = (uint8_t) va_arg(args, int);
  uint8_t cipo = (uint8_t) va_arg(args, int);
  uint32_t baud = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)) {
    return -ENODEV;
  } else if ((cs < DIO_START) || (cs >= NUM_DIO_PINS)) {
    return -ERANGE;
  } else if (
       (cs   == SPI_SCK_DIO)
    || (cs   == SPI_COPI_DIO)
    || (cs   == SPI_CIPO_DIO)
    || (sck  != SPI_SCK_DIO)
    || (copi != SPI_COPI_DIO)
    || (cipo != SPI_CIPO_DIO)
  ) {
    return -EINVAL;
  } else if (arduinoSpiDevices[deviceId].configured == true) {
    return -EBUSY;
  }

  if (arduinoNanoEveryInitSpiImpl() != 0) {
    return -ENODEV;
  }

  arduinoNanoEveryConfigureDioImpl(cs, 1);
  arduinoNanoEveryWriteDioImpl(cs, 1);

  arduinoSpiDevices[deviceId].chipSelect = cs;
  arduinoSpiDevices[deviceId].baud = baud;
  arduinoSpiDevices[deviceId].configured = true;

  return 0;
}

static int32_t arduinoNanoEveryStartSpiTransferImpl(int32_t deviceId) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  } else if (globalSpiInUse == true) {
    return -EBUSY;
  }

  globalSpiInUse = true;
  arduinoNanoEveryWriteDioImpl(arduinoSpiDevices[deviceId].chipSelect, 0);
  SPI.beginTransaction(SPISettings(arduinoSpiDevices[deviceId].baud,
    MSBFIRST, SPI_MODE0));
  arduinoSpiDevices[deviceId].transferInProgress = true;

  return 0;
}

int32_t arduinoNanoEveryStartSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  return arduinoNanoEveryStartSpiTransferImpl(deviceId);
}

int32_t arduinoNanoEveryEndSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  }

  arduinoSpiDevices[deviceId].transferInProgress = false;
  SPI.endTransaction();
  arduinoNanoEveryWriteDioImpl(arduinoSpiDevices[deviceId].chipSelect, 1);
  for (int ii = 0; ii < 8; ii++) {
    SPI.transfer(0xFF);
  }
  globalSpiInUse = false;

  return 0;
}

int32_t arduinoNanoEverySpiTransfer8(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t data = (uint8_t) va_arg(args, int);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  } else if (!arduinoSpiDevices[deviceId].transferInProgress) {
    arduinoNanoEveryStartSpiTransferImpl(deviceId);
  }

  return (int) SPI.transfer(data);
}

int32_t arduinoNanoEverySpiTransferBytes(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t *data = va_arg(args, uint8_t*);
  uint32_t length = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  } else if (!arduinoSpiDevices[deviceId].transferInProgress) {
    arduinoNanoEveryStartSpiTransferImpl(deviceId);
  }

  SPI.transfer(data, length);

  return 0;
}

/// @var halArduinoNanoEverySpisOnline
///
/// @brief Bitmask array of online SPIs.
static uint32_t halArduinoNanoEverySpisOnline[] = {
  0x00000003,
};

/// @var baseSystemTimeMs
///
/// @brief The time provided by the user or some other process as a baseline
/// time for the system.
static int64_t baseSystemTimeMs = 0;

int32_t arduinoNanoEveryTimeInit(va_list args) {
  (void) args;
  return 0;
}

int32_t arduinoNanoEverySetSystemTime(va_list args) {
  struct timespec *now = va_arg(args, struct timespec*);
  if (now == NULL) {
    return -EINVAL;
  }

  baseSystemTimeMs
    = (((int64_t) now->tv_sec) * ((int64_t) 1000))
    + (((int64_t) now->tv_nsec) / ((int64_t) 1000000));

  return 0;
}

static int64_t arduinoNanoEveryGetElapsedMillisecondsImpl(int64_t startTime) {
  int64_t now = baseSystemTimeMs + millis();
  if (now < startTime) {
    return -1;
  }
  return now - startTime;
}

int32_t arduinoNanoEveryGetElapsedMilliseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t result = arduinoNanoEveryGetElapsedMillisecondsImpl(startTime);
  if (returnValue != NULL) {
    *returnValue = result;
  }
  return (result >= 0) ? 0 : -EIO;
}

int32_t arduinoNanoEveryGetElapsedMicroseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t result = arduinoNanoEveryGetElapsedMillisecondsImpl(
    startTime / ((int64_t) 1000)) * ((int64_t) 1000);
  if (returnValue != NULL) {
    *returnValue = result;
  }
  return (result >= 0) ? 0 : -EIO;
}

int32_t arduinoNanoEveryGetElapsedNanoseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t result = arduinoNanoEveryGetElapsedMillisecondsImpl(
    startTime / ((int64_t) 1000000)) * ((int64_t) 1000000);
  if (returnValue != NULL) {
    *returnValue = result;
  }
  return (result >= 0) ? 0 : -EIO;
}

int32_t arduinoNanoEveryEnterPowerMode(va_list args) {
  HalPowerMode powerMode = (HalPowerMode) va_arg(args, int);

  if ((powerMode == HAL_POWER_MODE_OFF)
    || (powerMode == HAL_POWER_MODE_SUSPEND)
  ) {
    ADC0.CTRLA &= ~ADC_ENABLE_bm;
    SLPCTRL.CTRLA = SLPCTRL_SMODE_PDOWN_gc;
    _PROTECTED_WRITE(BOD.CTRLA, BOD_SLEEP_DIS_gc);
    USART0.CTRLB = 0;
    USART1.CTRLB = 0;
    USART2.CTRLB = 0;
    TWI0.MCTRLA = 0;
    SPI0.CTRLA = 0;
    for (uint8_t pin = 0; pin < NUM_TOTAL_PINS; pin++) {
      pinMode(pin, INPUT);
      digitalWrite(pin, LOW);
    }
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sei();
    sleep_cpu();
  } else if (powerMode == HAL_POWER_MODE_RESET) {
    _PROTECTED_WRITE(RSTCTRL.SWRR, 1);
  }

  return 0;
}

/// @var blockDevices
///
/// @brief Array of BlockDevice pointers that are managed by the driver
/// processes.
static BlockDevice *blockDevices[] = {
  NULL,
};

/// @var _numBlockDevices
///
/// @brief Number of BlockDevices that can be managed by the HAL.
static const uint32_t _numBlockDevices
  = sizeof(blockDevices) / sizeof(blockDevices[0]);

/// @var arduinoNanoEveryBlockDevicesOnline
///
/// @brief Bitmask array of online block devices.
static uint32_t arduinoNanoEveryBlockDevicesOnline[] = {
  0x00000000,
};

int32_t arduinoNanoEveryInitBlockDevice(va_list args) {
  (void) args;
  if (SCHEDULER_STATE == NULL) {
    return -EBUSY;
  }

  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio   = SD_CARD_PIN_CHIP_SELECT,
    .spiCopiDio = SPI_COPI_DIO,
    .spiCipoDio = SPI_CIPO_DIO,
    .spiSckDio  = SPI_SCK_DIO,
  };

  blockDevices[0] = halCommonInitRootSdSpiStorage(&sdCardSpiArgs);
  if (blockDevices[0] == NULL) {
    return -ENODEV;
  }
  setOnline(HAL->blockDevice, 0);

  return 0;
}

int32_t arduinoNanoEveryGetBlockDevice(va_list args) {
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

int32_t arduinoNanoEveryRestartBlockDevice(va_list args) {
  ProcessDescriptor *processDescriptor = va_arg(args, ProcessDescriptor*);
  int32_t deviceId = (int32_t) (intptr_t) processDescriptor->restartArgs;

  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio   = SD_CARD_PIN_CHIP_SELECT,
    .spiCopiDio = SPI_COPI_DIO,
    .spiCipoDio = SPI_CIPO_DIO,
    .spiSckDio  = SPI_SCK_DIO,
  };

  if (processCreate(processDescriptor, runSdCardSpi, &sdCardSpiArgs)
    != processSuccess
  ) {
    printString("Could not restart SD card process\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = "SD card";
  processDescriptor->userId = ROOT_USER_ID;

  BlockDevice *sdDevice
    = (BlockDevice*) coroutineResume(processDescriptor->mainThread, NULL);
  if (sdDevice == NULL) {
    printString("SD card restart returned NULL\n");
    return -ENODEV;
  }
  sdDevice->partitionNumber = 1;
  blockDevices[deviceId] = sdDevice;
  setOnline(HAL->blockDevice, deviceId);

  return 0;
}

static HalFunction arduinoNanoEveryMemoryFunctions[HAL_MEMORY_NUM_FNS] = {
  [HAL_MEMORY_PROCESS_STACK_SIZE]         = arduinoNanoEveryProcessStackSize,
  [HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = arduinoNanoEveryMemoryManagerStackSize,
  [HAL_MEMORY_BOTTOM_OF_HEAP]             = arduinoNanoEveryBottomOfHeap,
  [HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = arduinoNanoEveryNumExtraSchedulerStacks,
  [HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = arduinoNanoEveryNumExtraConsoleStacks,
};

static HalFunction arduinoNanoEveryUartFunctions[HAL_UART_NUM_FNS] = {
  [HAL_UART_INIT]       = arduinoNanoEveryInitUart,
  [HAL_UART_CONFIGURE]  = arduinoNanoEveryConfigureUart,
  [HAL_UART_POLL]       = arduinoNanoEveryPollUart,
  [HAL_UART_WRITE]      = arduinoNanoEveryWriteUart,
  [HAL_UART_IS_CONSOLE] = arduinoNanoEveryIsUartConsole,
};

static HalFunction arduinoNanoEveryDioFunctions[HAL_DIO_NUM_FNS] = {
  [HAL_DIO_INIT]      = arduinoNanoEveryInitDio,
  [HAL_DIO_CONFIGURE] = arduinoNanoEveryConfigureDio,
  [HAL_DIO_WRITE]     = arduinoNanoEveryWriteDio,
};

static HalFunction arduinoNanoEverySpiFunctions[HAL_SPI_NUM_FNS] = {
  [HAL_SPI_INIT]           = arduinoNanoEveryInitSpi,
  [HAL_SPI_CONFIGURE]      = arduinoNanoEveryConfigureSpiDevice,
  [HAL_SPI_START_TRANSFER] = arduinoNanoEveryStartSpiTransfer,
  [HAL_SPI_END_TRANSFER]   = arduinoNanoEveryEndSpiTransfer,
  [HAL_SPI_TRANSFER8]      = arduinoNanoEverySpiTransfer8,
  [HAL_SPI_TRANSFER_BYTES] = arduinoNanoEverySpiTransferBytes,
};

static HalFunction arduinoNanoEveryClockFunctions[HAL_CLOCK_NUM_FNS] = {
  [HAL_CLOCK_INIT]                     = arduinoNanoEveryTimeInit,
  [HAL_CLOCK_SET_SYSTEM_TIME]          = arduinoNanoEverySetSystemTime,
  [HAL_CLOCK_GET_ELAPSED_MILLISECONDS] = arduinoNanoEveryGetElapsedMilliseconds,
  [HAL_CLOCK_GET_ELAPSED_MICROSECONDS] = arduinoNanoEveryGetElapsedMicroseconds,
  [HAL_CLOCK_GET_ELAPSED_NANOSECONDS]  = arduinoNanoEveryGetElapsedNanoseconds,
};

static HalFunction arduinoNanoEveryPowerFunctions[HAL_POWER_NUM_FNS] = {
  [HAL_POWER_ENTER_MODE] = arduinoNanoEveryEnterPowerMode,
};

static HalFunction arduinoNanoEveryBlockDeviceFunctions[HAL_BLOCK_DEVICE_NUM_FNS] = {
  [HAL_BLOCK_DEVICE_INIT]    = arduinoNanoEveryInitBlockDevice,
  [HAL_BLOCK_DEVICE_GET]     = arduinoNanoEveryGetBlockDevice,
  [HAL_BLOCK_DEVICE_RESTART] = arduinoNanoEveryRestartBlockDevice,
};

int32_t halArduinoInit(void) {
  // Wire up per-subsystem function arrays.
  // HAL_TIMER is not supported on this platform — leave halFunctions[HAL_TIMER] NULL.
  halFunctions[HAL_MEMORY]       = arduinoNanoEveryMemoryFunctions;
  halFunctions[HAL_UART]         = arduinoNanoEveryUartFunctions;
  halFunctions[HAL_DIO]          = arduinoNanoEveryDioFunctions;
  halFunctions[HAL_SPI]          = arduinoNanoEverySpiFunctions;
  halFunctions[HAL_CLOCK]        = arduinoNanoEveryClockFunctions;
  halFunctions[HAL_POWER]        = arduinoNanoEveryPowerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = arduinoNanoEveryBlockDeviceFunctions;

  halCommonUart.numSupported = _numUarts;
  halCommonUart.online       = halArduinoNanoEveryUartsOnline;

  halCommonDio.numSupported = NUM_DIO_PINS;
  halCommonDio.online       = halArduinoNanoEveryDiosOnline;

  halCommonSpi.numSupported = MAX_SPI_DEVICES;
  halCommonSpi.online       = halArduinoNanoEverySpisOnline;

  halCommonTimer.numSupported = 0;
  halCommonTimer.online       = NULL;

  halCommonBlockDevice.numSupported = _numBlockDevices;
  halCommonBlockDevice.online       = arduinoNanoEveryBlockDevicesOnline;

  return halCommonInit();
}

#endif // ARDUINO_AVR_NANO_EVERY
