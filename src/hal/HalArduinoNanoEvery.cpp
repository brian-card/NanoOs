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
#include "../kernel/ExFatTask.h"
#include "../kernel/NanoOs.h"
#include "../kernel/Tasks.h"
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

uintptr_t arduinoNanoEveryProcessStackSize(void) {
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

void* arduinoNanoEveryBottomOfHeap(void) {
  extern int __heap_start;
  extern char *__brkval;
  return (__brkval == NULL) ? (char*) &__heap_start : __brkval;
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
static int _numUarts = sizeof(uarts) / sizeof(uarts[0]);

int arduinoNanoEveryGetNumUarts(void) {
  return _numUarts;
}

int arduinoNanoEverySetNumUarts(int numUarts) {
  if (numUarts > ((int) (sizeof(uarts) / sizeof(uarts[0])))) {
    return -ERANGE;
  } else if (numUarts < -ELAST) {
    return -ERANGE;
  }
  
  _numUarts = numUarts;
  
  return 0;
}

int arduinoNanoEveryInitUart(int port, int32_t baud) {
  int returnValue = -ERANGE;
  
  if ((port >= 0) && (port < _numUarts)) {
    uarts[port]->begin(baud);
    // wait for serial port to connect.
    while (!(*uarts[port]));
    returnValue = 0;
  }
  
  return returnValue;
}

int arduinoNanoEveryPollUart(int port) {
  int serialData = -ERANGE;
  
  if ((port >= 0) && (port < _numUarts)) {
    serialData = uarts[port]->read();
  }
  
  return serialData;
}

ssize_t arduinoNanoEveryWriteUart(int port,
  const uint8_t *data, ssize_t length
) {
  ssize_t numBytesWritten = -ERANGE;
  
  if ((port >= 0) && (port < _numUarts) && (length >= 0)) {
    numBytesWritten = uarts[port]->write(data, length);
  }
  
  return numBytesWritten;
}

static HalUart arduinoNanoEveryUartHal = {
  .getNumUarts = arduinoNanoEveryGetNumUarts,
  .setNumUarts = arduinoNanoEverySetNumUarts,
  .initUart = arduinoNanoEveryInitUart,
  .pollUart = arduinoNanoEveryPollUart,
  .writeUart = arduinoNanoEveryWriteUart,
};

int arduinoNanoEveryGetNumDios(void) {
  return NUM_DIO_PINS;
}

int arduinoNanoEveryConfigureDio(int dio, bool output) {
  int returnValue = -ERANGE;
  
  if ((dio >= DIO_START) && (dio < NUM_DIO_PINS)) {
    uint8_t modes[2] = { INPUT, OUTPUT };
    pinMode(dio, modes[output]);
    
    returnValue = 0;
  }
  
  return returnValue;
}

int arduinoNanoEveryWriteDio(int dio, bool high) {
  int returnValue = -ERANGE;
  
  if ((dio >= DIO_START) && (dio < NUM_DIO_PINS)) {
    uint8_t levels[2] = { LOW, HIGH };
    digitalWrite(dio, levels[high]);
    
    returnValue = 0;
  }
  
  return returnValue;
}

static HalDio arduinoNanoEveryDioHal = {
  .getNumDios = arduinoNanoEveryGetNumDios,
  .configureDio = arduinoNanoEveryConfigureDio,
  .writeDio = arduinoNanoEveryWriteDio,
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
} arduinoSpiDevices[NUM_DIO_PINS - 5] = {};

/// @var numArduinoSpis
///
/// @brief The number of devices we support in the arduinoSpiDevices array.
static const int numArduinoSpis
  = sizeof(arduinoSpiDevices) / sizeof(arduinoSpiDevices[0]);

int arduinoNanoEveryInitSpiDevice(int spi,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud
) {
  if ((spi < 0) || (spi >= numArduinoSpis)) {
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
  } else if (arduinoSpiDevices[spi].configured == true) {
    return -EBUSY;
  }
  
  if (globalSpiConfigured == false) {
    // Set up SPI at the default speed.
    globalSpiConfigured = true;
    SPI.begin();
  }
  
  // Configure the chip select DIO for output.
  arduinoNanoEveryConfigureDio(cs, 1);
  // Deselect the chip select pin.
  arduinoNanoEveryWriteDio(cs, 1);
  
  // Configure our internal metadata for the device.
  arduinoSpiDevices[spi].chipSelect = cs;
  arduinoSpiDevices[spi].baud = baud;
  arduinoSpiDevices[spi].configured = true;
  
  return 0;
}

int arduinoNanoEveryStartSpiTransfer(int spi) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (arduinoSpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (globalSpiInUse == true) {
    return -EBUSY;
  }
  
  // Mark the interface in use.
  globalSpiInUse = true;
  
  // Select the chip select pin.
  arduinoNanoEveryWriteDio(arduinoSpiDevices[spi].chipSelect, 0);
  
  // Begin the transaction
  SPI.beginTransaction(SPISettings(arduinoSpiDevices[spi].baud,
    MSBFIRST, SPI_MODE0));
  
  arduinoSpiDevices[spi].transferInProgress = true;
  
  return 0;
}

int arduinoNanoEveryEndSpiTransfer(int spi) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (arduinoSpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  }
  
  arduinoSpiDevices[spi].transferInProgress = false;
  
  // End the transaction.
  SPI.endTransaction();
  
  // Deselect the chip select pin.
  arduinoNanoEveryWriteDio(arduinoSpiDevices[spi].chipSelect, 1);
  for (int ii = 0; ii < 8; ii++) {
    SPI.transfer(0xFF); // 8 clock pulses
  }
  
  // Mark the interface not in use.
  globalSpiInUse = false;
  
  return 0;
}

int arduinoNanoEverySpiTransfer8(int spi, uint8_t data) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (arduinoSpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSpiDevices[spi].transferInProgress) {
    // The only error that arduinoNanoEveryStartSpiTransfer can return is
    // ENODEV and we've already checked for that, so we don't need to check the
    // return value here.
    arduinoNanoEveryStartSpiTransfer(spi);
  }
  
  return (int) SPI.transfer(data);
}

int arduinoNanoEverySpiTransferBytes(int spi,
  uint8_t *data, uint32_t length
) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (arduinoSpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSpiDevices[spi].transferInProgress) {
    // The only error that arduinoNanoEveryStartSpiTransfer can return is
    // ENODEV and we've already checked for that, so we don't need to check the
    // return value here.
    arduinoNanoEveryStartSpiTransfer(spi);
  }
  
  SPI.transfer(data, length);
  
  return 0;
}

static HalSpi arduinoNanoEverySpiHal = {
  .initSpiDevice = arduinoNanoEveryInitSpiDevice,
  .startSpiTransfer = arduinoNanoEveryStartSpiTransfer,
  .endSpiTransfer = arduinoNanoEveryEndSpiTransfer,
  .spiTransfer8 = arduinoNanoEverySpiTransfer8,
  .spiTransferBytes = arduinoNanoEverySpiTransferBytes,
};

/// @var baseSystemTimeMs
///
/// @brief The time provided by the user or some other task as a baseline
/// time for the system.
static int64_t baseSystemTimeMs = 0;

int arduinoNanoEverySetSystemTime(struct timespec *now) {
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
  .setSystemTime = arduinoNanoEverySetSystemTime,
  .getElapsedMilliseconds = arduinoNanoEveryGetElapsedMilliseconds,
  .getElapsedMicroseconds = arduinoNanoEveryGetElapsedMicroseconds,
  .getElapsedNanoseconds = arduinoNanoEveryGetElapsedNanoseconds,
};

int arduinoNanoEveryShutdown(HalShutdownType shutdownType) {
  // You can't completely turn off a Nano 33 IoT from software.  The best we
  // can do is put into a low power state, so do the same set of operations for
  // both off and suspend.
  if ((shutdownType == HAL_SHUTDOWN_OFF)
    || (shutdownType == HAL_SHUTDOWN_SUSPEND)
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
  } else if (shutdownType == HAL_SHUTDOWN_RESET) {
    _PROTECTED_WRITE(RSTCTRL.SWRR, 1);
  }
  
  return 0;
}

static HalPower arduinoNanoEveryPowerHal = {
  .shutdown = arduinoNanoEveryShutdown,
};

int arduinoNanoEveryGetNumTimers(void) {
  return 0;
}
 
int arduinoNanoEverySetNumTimers(int numTimers) {
  (void) numTimers;
  
  return -ENOTSUP;
}

int arduinoNanoEveryInitTimer(int timer) {
  (void) timer;
  
  return -ENOTSUP;
}

int arduinoNanoEveryConfigOneShotTimer(int timer,
    uint64_t nanoseconds, void (*callback)(void)
) {
  (void) timer;
  (void) nanoseconds;
  (void) callback;
  
  return -ENOTSUP;
}

uint64_t arduinoNanoEveryConfiguredTimerNanoseconds(int timer) {
  (void) timer;
  
  return 0;
}

uint64_t arduinoNanoEveryRemainingTimerNanoseconds(int timer) {
  (void) timer;
  
  return 0;
}

int arduinoNanoEveryCancelTimer(int timer) {
  (void) timer;
  
  return -ENOTSUP;
}

int arduinoNanoEveryCancelAndGetTimer(int timer,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void)
) {
  (void) timer;
  (void) configuredNanoseconds;
  (void) remainingNanoseconds;
  (void) callback;
  
  return -ENOTSUP;
}

int arduinoNanoEveryInitRootStorage(SchedulerState *schedulerState) {
  // Create the SD card task.
  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio = SD_CARD_PIN_CHIP_SELECT,
    .spiCopiDio = SPI_COPI_DIO,
    .spiCipoDio = SPI_CIPO_DIO,
    .spiSckDio = SPI_SCK_DIO,
  };

  return halCommonInitRootSdSpiStorage(&sdCardSpiArgs);
}

/// @var arduinoNanoEveryHal
///
/// @brief The implementation of the Hal interface for the Arduino Nano Every.
static Hal arduinoNanoEveryHal = {
  // Memory definitions.
  .processStackSize = arduinoNanoEveryProcessStackSize,
  .memoryManagerStackSize = arduinoNanoEveryMemoryManagerStackSize,
  .bottomOfHeap = arduinoNanoEveryBottomOfHeap,
  
  // Overlay definitions.
  .overlayMap = NULL,
  .overlaySize = 0,
  
  .uartHal = &arduinoNanoEveryUartHal,
  .dioHal = &arduinoNanoEveryDioHal,
  .spiHal = &arduinoNanoEverySpiHal,
  .clockHal = &arduinoNanoEveryClockHal,
  .powerHal = &arduinoNanoEveryPowerHal,
  .timerHal = NULL,
  
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

