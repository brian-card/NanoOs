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

uintptr_t arduinoNanoEveryProcessStackSize(bool debug) {
  (void) debug;
  return PROCESS_STACK_SIZE;
}

uintptr_t arduinoNanoEveryMemoryManagerStackSize(bool debug) {
  if (debug == false) {
    // This is the expected case, so list it first.
    return MEMORY_MANAGER_STACK_SIZE;
  } else {
    return MEMORY_MANAGER_DEBUG_STACK_SIZE;
  }
}

void* arduinoNanoEveryBottomOfHeap(bool debug) {
  (void) debug;
  extern int __heap_start;
  extern char *__brkval;
  return (__brkval == NULL) ? (char*) &__heap_start : __brkval;
}

uint8_t arduinoNanoEveryNumExtraSchedulerStacks(bool debug) {
  (void) debug;
  return 1;
}

uint8_t arduinoNanoEveryNumExtraConsoleStacks(bool debug) {
  (void) debug;
  return 1;
}

static HalMemory arduinoNanoEveryMemoryHal = {
  .processStackSize = arduinoNanoEveryProcessStackSize,
  .memoryManagerStackSize = arduinoNanoEveryMemoryManagerStackSize,
  .bottomOfHeap = arduinoNanoEveryBottomOfHeap,
  .numExtraSchedulerStacks = arduinoNanoEveryNumExtraSchedulerStacks,
  .numExtraConsoleStacks = arduinoNanoEveryNumExtraConsoleStacks,
  .overlayMap = NULL,
  .overlaySize = 0,
};

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

int32_t arduinoNanoEveryInitUart(void) {
  return 0;
}

int32_t arduinoNanoEveryConfigureUart(int32_t deviceId, uint32_t baud) {
  int returnValue = -ERANGE;
  
  if ((deviceId >= 0) && (deviceId < _numUarts)) {
    uarts[deviceId]->begin(baud);
    // wait for serial deviceId to connect.
    while (!(*uarts[deviceId]));
    returnValue = 0;
  }
  
  return returnValue;
}

int32_t arduinoNanoEveryPollUart(int32_t deviceId) {
  int serialData = -ERANGE;
  
  if ((deviceId >= 0) && (deviceId < _numUarts)) {
    serialData = uarts[deviceId]->read();
  }
  
  return serialData;
}

ssize_t arduinoNanoEveryWriteUart(int32_t deviceId,
  const uint8_t *data, ssize_t length
) {
  ssize_t numBytesWritten = -ERANGE;
  
  if ((deviceId >= 0) && (deviceId < _numUarts) && (length >= 0)) {
    numBytesWritten = uarts[deviceId]->write(data, length);
  }
  
  return numBytesWritten;
}

bool arduinoNanoEveryIsUartConsole(int32_t deviceId) {
  (void) deviceId;
  return true;
}

/// @var halArduinoNanoEveryUartsOnline
///
/// @brief Bitmask array of online UARTs.
static uint32_t halArduinoNanoEveryUartsOnline[] = {
  0x00000003,
};

static HalUart arduinoNanoEveryUartHal = {
  .numSupported = _numUarts,
  .online = halArduinoNanoEveryUartsOnline,
  .init = arduinoNanoEveryInitUart,
  .configure = arduinoNanoEveryConfigureUart,
  .poll = arduinoNanoEveryPollUart,
  .write = arduinoNanoEveryWriteUart,
  .isConsole = arduinoNanoEveryIsUartConsole,
};

int32_t arduinoNanoEveryInitDio(void) {
  return 0;
};

int32_t arduinoNanoEveryConfigureDio(int32_t deviceId, bool output) {
  int32_t returnValue = -ERANGE;
  
  if ((deviceId >= DIO_START) && (deviceId < NUM_DIO_PINS)) {
    uint8_t modes[2] = { INPUT, OUTPUT };
    pinMode(deviceId, modes[output]);
    
    returnValue = 0;
  }
  
  return returnValue;
}

int32_t arduinoNanoEveryWriteDio(int32_t deviceId, bool high) {
  int32_t returnValue = -ERANGE;
  
  if ((deviceId >= DIO_START) && (deviceId < NUM_DIO_PINS)) {
    uint8_t levels[2] = { LOW, HIGH };
    digitalWrite(deviceId, levels[high]);
    
    returnValue = 0;
  }
  
  return returnValue;
}

/// @var halArduinoSamD21x18AImplDiosOnline
///
/// @brief Bitmask array of online UARTs.
static uint32_t halArduinoNanoEveryDiosOnline[] = {
  0x00003fff,
};

static HalDio arduinoNanoEveryDioHal = {
  .numSupported = NUM_DIO_PINS,
  .online = halArduinoNanoEveryDiosOnline,
  .init = arduinoNanoEveryInitDio,
  .configure = arduinoNanoEveryConfigureDio,
  .write = arduinoNanoEveryWriteDio,
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
///
/// @details
/// On the Arduino Nano Every, 5 DIO pins are reserved:
/// - UART RX
/// - UART TX
/// - SPI SCK
/// - SPI COPI
/// - SPI CIPO
/// So, the maximum number of devcies we can support is the number of DIO pins
/// minus 5.
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

int32_t arduinoNanoEveryInitSpi(void) {
  if (globalSpiConfigured == false) {
    // Set up SPI at the default speed.
    globalSpiConfigured = true;
    SPI.begin();
  }
  
  return 0;
}

int32_t arduinoNanoEveryConfigureSpiDevice(int32_t deviceId,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud
) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if ((cs < DIO_START) || (cs >= NUM_DIO_PINS)) {
    // No such DIO pin to configure.
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
  
  if (arduinoNanoEveryInitSpi() != 0) {
    return -ENODEV;
  }
  
  // Configure the chip select DIO for output.
  arduinoNanoEveryConfigureDio(cs, 1);
  // Deselect the chip select pin.
  arduinoNanoEveryWriteDio(cs, 1);
  
  // Configure our internal metadata for the device.
  arduinoSpiDevices[deviceId].chipSelect = cs;
  arduinoSpiDevices[deviceId].baud = baud;
  arduinoSpiDevices[deviceId].configured = true;
  
  return 0;
}

int32_t arduinoNanoEveryStartSpiTransfer(int32_t deviceId) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (globalSpiInUse == true) {
    return -EBUSY;
  }
  
  // Mark the interface in use.
  globalSpiInUse = true;
  
  // Select the chip select pin.
  arduinoNanoEveryWriteDio(arduinoSpiDevices[deviceId].chipSelect, 0);
  
  // Begin the transaction
  SPI.beginTransaction(SPISettings(arduinoSpiDevices[deviceId].baud,
    MSBFIRST, SPI_MODE0));
  
  arduinoSpiDevices[deviceId].transferInProgress = true;
  
  return 0;
}

int32_t arduinoNanoEveryEndSpiTransfer(int32_t deviceId) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  }
  
  arduinoSpiDevices[deviceId].transferInProgress = false;
  
  // End the transaction.
  SPI.endTransaction();
  
  // Deselect the chip select pin.
  arduinoNanoEveryWriteDio(arduinoSpiDevices[deviceId].chipSelect, 1);
  for (int ii = 0; ii < 8; ii++) {
    SPI.transfer(0xFF); // 8 clock pulses
  }
  
  // Mark the interface not in use.
  globalSpiInUse = false;
  
  return 0;
}

int32_t arduinoNanoEverySpiTransfer8(int32_t deviceId, uint8_t data) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSpiDevices[deviceId].transferInProgress) {
    // The only error that arduinoNanoEveryStartSpiTransfer can return is
    // ENODEV and we've already checked for that, so we don't need to check the
    // return value here.
    arduinoNanoEveryStartSpiTransfer(deviceId);
  }
  
  return (int) SPI.transfer(data);
}

int32_t arduinoNanoEverySpiTransferBytes(int32_t deviceId,
  uint8_t *data, uint32_t length
) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSpiDevices[deviceId].transferInProgress) {
    // The only error that arduinoNanoEveryStartSpiTransfer can return is
    // ENODEV and we've already checked for that, so we don't need to check the
    // return value here.
    arduinoNanoEveryStartSpiTransfer(deviceId);
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

static HalSpi arduinoNanoEverySpiHal = {
  .numSupported = MAX_SPI_DEVICES,
  .online = halArduinoNanoEverySpisOnline,
  .init = arduinoNanoEveryInitSpi,
  .configure = arduinoNanoEveryConfigureSpiDevice,
  .startTransfer = arduinoNanoEveryStartSpiTransfer,
  .endTransfer = arduinoNanoEveryEndSpiTransfer,
  .transfer8 = arduinoNanoEverySpiTransfer8,
  .transferBytes = arduinoNanoEverySpiTransferBytes,
};

/// @var baseSystemTimeMs
///
/// @brief The time provided by the user or some other process as a baseline
/// time for the system.
static int64_t baseSystemTimeMs = 0;

int32_t arduinoNanoEveryTimeInit(void) {
  return 0;
}

int32_t arduinoNanoEverySetSystemTime(struct timespec *now) {
  if (now == NULL) {
    return -EINVAL;
  }
  
  baseSystemTimeMs
    = (((int64_t) now->tv_sec) * ((int64_t) 1000))
    + (((int64_t) now->tv_nsec) / ((int64_t) 1000000));
  
  return 0;
}

int64_t arduinoNanoEveryGetElapsedMilliseconds(int64_t startTime) {
  int64_t now = baseSystemTimeMs + millis();

  if (now < startTime) {
    return -1;
  }

  return now - startTime;
}

int64_t arduinoNanoEveryGetElapsedMicroseconds(int64_t startTime) {
  return arduinoNanoEveryGetElapsedMilliseconds(
    startTime / ((int64_t) 1000)) * ((int64_t) 1000);
}

int64_t arduinoNanoEveryGetElapsedNanoseconds(int64_t startTime) {
  return arduinoNanoEveryGetElapsedMilliseconds(
    startTime / ((int64_t) 1000000)) * ((int64_t) 1000000);
}

static HalClock arduinoNanoEveryClockHal = {
  .init = arduinoNanoEveryTimeInit,
  .setSystemTime = arduinoNanoEverySetSystemTime,
  .getElapsedMilliseconds = arduinoNanoEveryGetElapsedMilliseconds,
  .getElapsedMicroseconds = arduinoNanoEveryGetElapsedMicroseconds,
  .getElapsedNanoseconds = arduinoNanoEveryGetElapsedNanoseconds,
};

int32_t arduinoNanoEveryEnterPowerMode(HalPowerMode powerMode) {
  // You can't completely turn off a Nano 33 IoT from software.  The best we
  // can do is put into a low power state, so do the same set of operations for
  // both off and suspend.
  if ((powerMode == HAL_POWER_MODE_OFF)
    || (powerMode == HAL_POWER_MODE_SUSPEND)
  ) {
    // 1. Disable ADC
    ADC0.CTRLA &= ~ADC_ENABLE_bm;
    
    // 2. Disable all peripherals via Power Reduction
    SLPCTRL.CTRLA = SLPCTRL_SMODE_PDOWN_gc;  // Power-down mode
    
    // 3. Disable Brown-Out Detection (BOD) during sleep
    //    This is critical for lowest power!
    _PROTECTED_WRITE(BOD.CTRLA, BOD_SLEEP_DIS_gc);
    
    // 4. Disable all unnecessary peripherals
    USART0.CTRLB = 0;   // Disable USART
    USART1.CTRLB = 0;
    USART2.CTRLB = 0;
    TWI0.MCTRLA = 0;    // Disable I2C
    SPI0.CTRLA = 0;     // Disable SPI
    
    // 5. Configure all pins to minimize leakage
    //    Set unused pins as inputs with pullups disabled
    for (uint8_t pin = 0; pin < NUM_TOTAL_PINS; pin++) {
      pinMode(pin, INPUT);
      digitalWrite(pin, LOW);  // Disable pullup
    }
    
    // 6. Enter sleep
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sei();  // Must enable interrupts for wake-up
    sleep_cpu();
  } else if (powerMode == HAL_POWER_MODE_RESET) {
    _PROTECTED_WRITE(RSTCTRL.SWRR, 1);
  }
  
  return 0;
}

static HalPower arduinoNanoEveryPowerHal = {
  .enterMode = arduinoNanoEveryEnterPowerMode,
};

int arduinoNanoEveryInitRootStorage(SchedulerState *schedulerState) {
  // Create the SD card process.
  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio = SD_CARD_PIN_CHIP_SELECT,
    .spiCopiDio = SPI_COPI_DIO,
    .spiCipoDio = SPI_CIPO_DIO,
    .spiSckDio = SPI_SCK_DIO,
  };

  return halCommonInitRootFilesystem(
    halCommonInitRootSdSpiStorage(&sdCardSpiArgs));
}

/// @var arduinoNanoEveryHal
///
/// @brief The implementation of the Hal interface for the Arduino Nano Every.
static Hal arduinoNanoEveryHal = {
  // Memory definitions.
  .memory = &arduinoNanoEveryMemoryHal,
  .uart = &arduinoNanoEveryUartHal,
  .dio = &arduinoNanoEveryDioHal,
  .spi = &arduinoNanoEverySpiHal,
  .clock = &arduinoNanoEveryClockHal,
  .power = &arduinoNanoEveryPowerHal,
  .timer = NULL,
  
  // Root storage configuration.
  .initRootStorage = arduinoNanoEveryInitRootStorage,
};

const Hal* halArduinoNanoEveryInit(void) {
  if (halCommonInit(&arduinoNanoEveryHal) != 0) {
    return NULL;
  }

  return &arduinoNanoEveryHal;
}

#endif // ARDUINO_AVR_NANO_EVERY

