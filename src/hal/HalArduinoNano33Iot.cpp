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

/// @file HalArduinoNano33Iot.cpp
///
/// @brief HAL implementation for an Arduino Nano 33 IoT

#if defined(ARDUINO_SAMD_NANO_33_IOT)

// Base Arduino definitions
#include <Arduino.h>

// Basic SPI communication
#include <SPI.h>

#include "HalArduinoNano33Iot.h"
#include "HalCommon.h"
// Deliberately *NOT* including MemoryManager.h here.  The HAL has to be
// operational prior to the memory manager and really should be completely
// independent of it.
#include "../kernel/NanoOs.h"
#include "../kernel/Tasks.h"
#include "../kernel/Scheduler.h"
#include "../kernel/SdCardSpi.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsStdio.h"

/// @def DIO_START
///
/// @brief On the Arduino Nano 33 IoT, D0 is used for Serial1's RX and D1 is
/// used for Serial1's TX.  We use expect to use Serial1, so our first usable
/// DIO is 2.
#define DIO_START 2

/// @def NUM_DIO_PINS
///
/// @brief The number of digital IO pins on the board.  14 on an Arduino Nano.
#define NUM_DIO_PINS 14

/// @def SPI_COPI_DIO
///
/// @brief DIO pin used for SPI COPI on the Arduino Nano 33 IoT.
#define SPI_COPI_DIO 11

/// @def SPI_CIPO_DIO
///
/// @brief DIO pin used for SPI CIPO on the Arduino Nano 33 IoT.
#define SPI_CIPO_DIO 12

/// @def SPI_SCK_DIO
///
/// @brief DIO pin used for SPI serial clock on the Arduino Nano 33 IoT.
#define SPI_SCK_DIO 13

/// @def PROCESS_STACK_SIZE
///
/// @brief The size, in bytes, of a regular process's stack.
#define PROCESS_STACK_SIZE 1024

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief The size, in bytes, of the memory manager process's stack.
#define MEMORY_MANAGER_STACK_SIZE 192

/// @def OVERLAY_ADDRESS
///
/// @brief The address of where the overlay will be placed in memory.
#define OVERLAY_ADDRESS 0x20001800

/// @def OVERLAY_SIZE
///
/// @brief The size, in bytes, of the overlay supported by the Nano 33 IoT.
#define OVERLAY_SIZE 8192

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

/// @struct SavedContext
///
/// @brief Function context that's saved during an interrupt handler.
///
/// @param r0 The value to load into the ARM r0 register.
/// @param r1 The value to load into the ARM r1 register.
/// @param r2 The value to load into the ARM r2 register.
/// @param r3 The value to load into the ARM r3 register.
/// @param r12 The value to load into the ARM r12 register.
/// @param lr The value to load into the ARM lr register.
/// @param pc The value to load into the ARM pc register.
/// @param sp The value to load into the ARM sp register.
typedef struct SavedContext {
  uint32_t r0, r1, r2, r3, r12, lr, pc, sp;
} SavedContext;

/// @var _savedContext
///
/// @brief Temporary storage for storing the context during a timer interrupt.
static SavedContext _savedContext;

/// @def THUMB_BIT
///
/// @brief A value that corresponds to the thumb bit in xPSR (bit 24).
#define THUMB_BIT 0x01000000

/// @def THUMB_BIT_MASK
///
/// @brief A mask of the 24 bits of xPSR.
#define THUMB_BIT_MASK 0x00ffffff

/// @def SAVE_CONTEXT
///
/// @brief Save the context of the stack frame we're using before proceeding.
#define SAVE_CONTEXT() \
  SavedContext savedContext = _savedContext

/// @def RESTORE_CONTEXT
///
/// @brief Restore the original caller's context and return to the address that
///  was found on the stack before it was modified by RETURN_TO_HANDLER.
#define RESTORE_CONTEXT() \
  /* Jump back to where we were interrupted */ \
  asm volatile( \
    "mov r4, %[ctx]\n\t"         /* Get address of context */ \
    "ldmia r4!, {r0-r3}\n\t"     /* Load r0-r3 */ \
    "ldr r5, [r4, #0]\n\t"       /* Load r12 */ \
    "mov r12, r5\n\t" \
    "ldr r5, [r4, #4]\n\t"       /* Load lr */ \
    "mov lr, r5\n\t" \
    "ldr r5, [r4, #8]\n\t"       /* Load pc */ \
    "ldr r4, [r4, #12]\n\t"      /* Load sp */ \
    "mov sp, r4\n\t"             /* Restore SP */ \
    "bx r5\n\t"                  /* Branch to PC */ \
    : \
    : [ctx] "l" (&savedContext) \
    : "r4", "r5", "memory" \
  ); \
  __builtin_unreachable()

/// @def RETURN_TO_HANDLER
///
/// @brief Modify the return address of the function to point to a custom
/// handler and return.
///
/// @details
/// In the Cortex M0, other interrupts cannot be processed while a timer
/// interrupt is being handled.  The timer interrupt cannot be marked as
/// complete any other way than by returning from it.  There is a special
/// return instruction that's generated by the compiler for this handler.
/// Thankfully, what we return to doesn't matter.  So, we modify the return
/// address to the real handler we want to run and then immediately return from
/// the root handler.  This macro will modify the return address so that a
/// custom function is called instead.  The called function will be responsible
/// for restoring the original return address we found by calling
/// RESTORE_CONTEXT().  It's critical that this is defined as an inline
/// macro and NOT a function.  We *MUST* return from the top-level handler to
/// clear the interrupt.  We have to modify the return address of that level,
/// no other.
#define RETURN_TO_HANDLER(handlerIndex) \
  uint32_t *returnAddressAt = (uint32_t*) &returnAddressAt; \
  HardwareTimer *hwTimer = &hardwareTimers[handlerIndex]; \
  if (hwTimer->tc->COUNT16.INTFLAG.bit.OVF) { \
    /* Clear interrupt flag */ \
    hwTimer->tc->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF; \
  } \
   \
  while (true) { \
    if (((*returnAddressAt & THUMB_BIT_MASK) == 0) \
      && (*returnAddressAt & THUMB_BIT) \
    ) { \
      break; \
    } \
    returnAddressAt++; \
  } \
  /* The return address should be immediately before the XPSR_VALUE on */ \
  /* the stack, so back up one index and we should be good. */ \
  returnAddressAt--; \
  _savedContext.r0 = returnAddressAt[-6]; \
  _savedContext.r1 = returnAddressAt[-5]; \
  _savedContext.r2 = returnAddressAt[-4]; \
  _savedContext.r3 = returnAddressAt[-3]; \
  _savedContext.r12 = returnAddressAt[-2]; \
  _savedContext.lr = returnAddressAt[-1]; \
  _savedContext.pc = returnAddressAt[0] | 1; \
  _savedContext.sp = (uint32_t) &returnAddressAt[2]; \
   \
  *returnAddressAt \
    = (uint32_t) arduinoNano33IotTimerInterruptHandler ## handlerIndex; \
  return

uintptr_t arduinoNano33IotProcessStackSize(void) {
  return PROCESS_STACK_SIZE;
}

uintptr_t arduinoNano33IotMemoryManagerStackSize(bool debug) {
  if (debug == false) {
    // This is the expected case, so list it first.
    return MEMORY_MANAGER_STACK_SIZE;
  } else {
    return MEMORY_MANAGER_DEBUG_STACK_SIZE;
  }
}

/// @var _bottomOfStack
///
/// @brief Where the bottom of the stack will be set to be in memory.
static void *_bottomOfHeap = (void*) (OVERLAY_ADDRESS + OVERLAY_SIZE);

void* arduinoNano33IotBottomOfHeap(void) {
  return _bottomOfHeap;
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
/// @brief The number of serial ports we support on the Arduino Nano 33 IoT.
static int _numUarts = sizeof(uarts) / sizeof(uarts[0]);

int arduinoNano33IotGetNumUarts(void) {
  return _numUarts;
}

int arduinoNano33IotSetNumUarts(int numUarts) {
  if (numUarts > ((int) (sizeof(uarts) / sizeof(uarts[0])))) {
    return -ERANGE;
  } else if (numUarts < -ELAST) {
    return -EINVAL;
  }
  
  _numUarts = numUarts;
  
  return 0;
}

int arduinoNano33IotInitUart(int port, int32_t baud) {
  int returnValue = -ERANGE;
  
  if ((port >= 0) && (port < _numUarts)) {
    uarts[port]->begin(baud);
    // wait for serial port to connect.
    while (!(*uarts[port]));
    returnValue = 0;
  }
  
  return returnValue;
}

int arduinoNano33IotPollUart(int port) {
  int serialData = -ERANGE;
  
  if ((port >= 0) && (port < _numUarts)) {
    serialData = uarts[port]->read();
  }
  
  return serialData;
}

ssize_t arduinoNano33IotWriteUart(int port,
  const uint8_t *data, ssize_t length
) {
  ssize_t numBytesWritten = -ERANGE;
  
  if ((port >= 0) && (port < _numUarts) && (length >= 0)) {
    numBytesWritten = uarts[port]->write(data, length);
  }
  
  return numBytesWritten;
}

static HalUart arduinoNano33IotUartHal = {
  .getNumUarts = arduinoNano33IotGetNumUarts,
  .setNumUarts = arduinoNano33IotSetNumUarts,
  .initUart = arduinoNano33IotInitUart,
  .pollUart = arduinoNano33IotPollUart,
  .writeUart = arduinoNano33IotWriteUart,
};

int arduinoNano33IotGetNumDios(void) {
  return NUM_DIO_PINS;
}

int arduinoNano33IotConfigureDio(int dio, bool output) {
  int returnValue = -ERANGE;
  
  if ((dio >= DIO_START) && (dio < NUM_DIO_PINS)) {
    uint8_t modes[2] = { INPUT, OUTPUT };
    pinMode(dio, modes[output]);
    
    returnValue = 0;
  }
  
  return returnValue;
}

int arduinoNano33IotWriteDio(int dio, bool high) {
  int returnValue = -ERANGE;
  
  if ((dio >= DIO_START) && (dio < NUM_DIO_PINS)) {
    uint8_t levels[2] = { LOW, HIGH };
    digitalWrite(dio, levels[high]);
    
    returnValue = 0;
  }
  
  return returnValue;
}

static HalDio arduinoNano33IotDioHal = {
  .getNumDios = arduinoNano33IotGetNumDios,
  .configureDio = arduinoNano33IotConfigureDio,
  .writeDio = arduinoNano33IotWriteDio,
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
/// On the Arduino Nano 33 IoT, 5 DIO pins are reserved:
/// - UART RX
/// - UART TX
/// - SPI SCK
/// - SPI COPI
/// - SPI CIPO
/// So, the maximum number of devcies we can support is the number of DIO pins
/// minus 5.
static struct ArduinoNano33IotSpi {
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

int arduinoNano33IotInitSpiDevice(int spi,
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
  arduinoNano33IotConfigureDio(cs, 1);
  // Deselect the chip select pin.
  arduinoNano33IotWriteDio(cs, 1);
  
  // Configure our internal metadata for the device.
  arduinoSpiDevices[spi].chipSelect = cs;
  arduinoSpiDevices[spi].baud = baud;
  arduinoSpiDevices[spi].configured = true;
  
  return 0;
}

int arduinoNano33IotStartSpiTransfer(int spi) {
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
  arduinoNano33IotWriteDio(arduinoSpiDevices[spi].chipSelect, 0);
  
  // Begin the transaction
  SPI.beginTransaction(SPISettings(arduinoSpiDevices[spi].baud,
    MSBFIRST, SPI_MODE0));
  
  arduinoSpiDevices[spi].transferInProgress = true;
  
  return 0;
}

int arduinoNano33IotEndSpiTransfer(int spi) {
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
  arduinoNano33IotWriteDio(arduinoSpiDevices[spi].chipSelect, 1);
  for (int ii = 0; ii < 8; ii++) {
    SPI.transfer(0xFF); // 8 clock pulses
  }
  
  // Mark the interface not in use.
  globalSpiInUse = false;
  
  return 0;
}

int arduinoNano33IotSpiTransfer8(int spi, uint8_t data) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (arduinoSpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSpiDevices[spi].transferInProgress) {
    // The only error that arduinoNano33IotStartSpiTransfer can return is
    // ENODEV and we've already checked for that, so we don't need to check the
    // return value here.
    arduinoNano33IotStartSpiTransfer(spi);
  }
  
  return (int) SPI.transfer(data);
}

int arduinoNano33IotSpiTransferBytes(int spi,
  uint8_t *data, uint32_t length
) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (arduinoSpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSpiDevices[spi].transferInProgress) {
    // The only error that arduinoNano33IotStartSpiTransfer can return is
    // ENODEV and we've already checked for that, so we don't need to check the
    // return value here.
    arduinoNano33IotStartSpiTransfer(spi);
  }
  
  SPI.transfer(data, length);
  
  return 0;
}

static HalSpi arduinoNano33IotSpiHal = {
  .initSpiDevice = arduinoNano33IotInitSpiDevice,
  .startSpiTransfer = arduinoNano33IotStartSpiTransfer,
  .endSpiTransfer = arduinoNano33IotEndSpiTransfer,
  .spiTransfer8 = arduinoNano33IotSpiTransfer8,
  .spiTransferBytes = arduinoNano33IotSpiTransferBytes,
};

/// @var baseSystemTimeUs
///
/// @brief The time provided by the user or some other task as a baseline
/// time for the system.
static int64_t baseSystemTimeUs = 0;

int arduinoNano33IotSetSystemTime(struct timespec *now) {
  if (now == NULL) {
    return -EINVAL;
  }
  
  baseSystemTimeUs
    = (((int64_t) now->tv_sec) * ((int64_t) 1000000))
    + (((int64_t) now->tv_nsec) / ((int64_t) 1000));
  
  return 0;
}

int64_t arduinoNano33IotGetElapsedMicroseconds(int64_t startTime);

int64_t arduinoNano33IotGetElapsedMilliseconds(int64_t startTime) {
  return arduinoNano33IotGetElapsedMicroseconds(
    startTime * ((int64_t) 1000)) / ((int64_t) 1000);
}

int64_t arduinoNano33IotGetElapsedMicroseconds(int64_t startTime) {
  int64_t now = baseSystemTimeUs + micros();

  if (now < startTime) {
    return -1;
  }

  return now - startTime;
}

int64_t arduinoNano33IotGetElapsedNanoseconds(int64_t startTime) {
  return arduinoNano33IotGetElapsedMicroseconds(
    startTime / ((int64_t) 1000)) * ((int64_t) 1000);
}

static HalClock arduinoNano33IotClockHal = {
  .setSystemTime = arduinoNano33IotSetSystemTime,
  .getElapsedMilliseconds = arduinoNano33IotGetElapsedMilliseconds,
  .getElapsedMicroseconds = arduinoNano33IotGetElapsedMicroseconds,
  .getElapsedNanoseconds = arduinoNano33IotGetElapsedNanoseconds,
};

int arduinoNano33IotShutdown(HalShutdownType shutdownType) {
  // You can't completely turn off a Nano 33 IoT from software.  The best we
  // can do is put into a low power state, so do the same set of operations for
  // both off and suspend.
  if ((shutdownType == HAL_SHUTDOWN_OFF)
    || (shutdownType == HAL_SHUTDOWN_SUSPEND)
  ) {
    // Configure for standby mode
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    
    // Set standby mode in Power Manager
    PM->SLEEP.reg = PM_SLEEP_IDLE_CPU;
    
    __DSB(); // Data Synchronization Barrier
    __WFI(); // Wait For Interrupt
  } else if (shutdownType == HAL_SHUTDOWN_RESET) {
    NVIC_SystemReset();
  }

  return 0;
}

static HalPower arduinoNano33IotPowerHal = {
  .shutdown = arduinoNano33IotShutdown,
};

/// @struct HardwareTimer
///
/// @brief Collection of variables needed to manage a single hardware timer.
///
/// @param tc A pointer to the timer/counter structure used for the tiemr.
/// @param irqType The IRQ used to manage the timer.
/// @param clockId The global clock identifier used to manage the timer.
/// @param initialized Whether or not the timer has been initialized yet.
/// @param callback The callback to call when the timer fires, if any.
/// @param active Whether or not the timer is currently active.
/// @param startTime The time, in nanoseconds, when the timer was configured.
/// @param deadline The time, in nanoseconds, when the timer expires.
typedef struct HardwareTimer {
  Tc *tc;
  IRQn_Type irqType;
  unsigned long clockId;
  bool initialized;
  void (*callback)(void);
  bool active;
  int64_t startTime;
  int64_t deadline;
} HardwareTimer;

/// @var hardwareTimers
///
/// @brief Array of HardwareTimer objects managed by the HAL.
static HardwareTimer hardwareTimers[] = {
  {
    .tc = TC3,
    .irqType = TC3_IRQn,
    .clockId = GCLK_CLKCTRL_ID_TCC2_TC3,
    .initialized = false,
    .callback = NULL,
    .active = false,
    .startTime = 0,
    .deadline = 0,
  },
  {
    .tc = TC4,
    .irqType = TC4_IRQn,
    .clockId = GCLK_CLKCTRL_ID_TC4_TC5,
    .initialized = false,
    .callback = NULL,
    .active = false,
    .startTime = 0,
    .deadline = 0,
  },
};

/// @var _numTimers
///
/// @brief The number of timers returned by arduinoNano33IotGetNumTimers.  This
/// is initialized to the number of timers supported, but may be overridden by
/// a call to arduinoNano33IotSetNumTimers.
static int _numTimers = sizeof(hardwareTimers) / sizeof(hardwareTimers[0]);

int arduinoNano33IotGetNumTimers(void) {
  return _numTimers;
}

int arduinoNano33IotSetNumTimers(int numTimers) {
  if (
    numTimers > ((int) (sizeof(hardwareTimers) / sizeof(hardwareTimers[0])))
  ) {
    return -ERANGE;
  } else if (numTimers < -ELAST) {
    return -EINVAL;
  }
  
  _numTimers = numTimers;
  
  return 0;
}

int arduinoNano33IotInitTimer(int timer) {
  if ((timer < 0) || (timer >= _numTimers)) {
    return -ERANGE;
  }
  
  HardwareTimer *hwTimer = &hardwareTimers[timer];
  if (hwTimer->initialized) {
    // Nothing to do
    return 0;
  }
  
  // Enable GCLK for the TC timer (48MHz)
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN |
                      GCLK_CLKCTRL_GEN_GCLK0 |
                      hwTimer->clockId;
  while (GCLK->STATUS.bit.SYNCBUSY);

  // Reset the TC timer
  hwTimer->tc->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Configure the TC timer in one-shot mode
  hwTimer->tc->COUNT16.CTRLA.reg
    = TC_CTRLA_MODE_COUNT16        // 16-bit counter
    | TC_CTRLA_WAVEGEN_NFRQ        // Normal frequency mode
    | TC_CTRLA_PRESCALER_DIV1;     // No prescaling (48MHz)
  
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Enable one-shot mode via CTRLBSET
  hwTimer->tc->COUNT16.CTRLBSET.reg = TC_CTRLBSET_ONESHOT;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Enable compare match interrupt
  hwTimer->tc->COUNT16.INTENSET.reg = TC_INTENSET_OVF;
  
  // Enable the TC timer interrupt in NVIC
  NVIC_SetPriority(hwTimer->irqType, 0);
  NVIC_EnableIRQ(hwTimer->irqType);
  
  hwTimer->initialized = true;
  
  return 0;
}

int arduinoNano33IotConfigOneShotTimer(int timer,
    uint64_t nanoseconds, void (*callback)(void)
) {
  if ((timer < 0) || (timer >= _numTimers)) {
    return -ERANGE;
  }
  
  HardwareTimer *hwTimer = &hardwareTimers[timer];
  if (!hwTimer->initialized) {
    return -EINVAL;
  }
  
  // Cancel any existing timer
  int arduinoNano33IotCancelTimer(int);
  arduinoNano33IotCancelTimer(timer);
  
  // We take a number of nanoseconds for HAL compatibility, but our timers
  // don't support that resolution.  Convert to microseconds.
  uint64_t microseconds = nanoseconds / ((uint64_t) 1000);
  
  // Make sure we don't overflow
  if (microseconds > 89478485) {
    microseconds = 89478485; // 0xffffffff / 48
  }
  
  // Calculate ticks (48 ticks per microsecond)
  uint32_t ticks = microseconds * 48;
  
  // Check if we need prescaling for longer delays
  uint16_t prescaler = TC_CTRLA_PRESCALER_DIV1;
  
  if (ticks > 65535) {
    // Use DIV8 for up to ~10.9ms
    prescaler = TC_CTRLA_PRESCALER_DIV8;
    ticks = (microseconds * 48) / 8;
    
    if (ticks > 65535) {
      // Use DIV64 for up to ~87ms
      prescaler = TC_CTRLA_PRESCALER_DIV64;
      ticks = (microseconds * 48) / 64;
      
      if (ticks > 65535) {
        // Use DIV256 for up to ~349ms
        prescaler = TC_CTRLA_PRESCALER_DIV256;
        ticks = (microseconds * 48) / 256;
        
        if (ticks > 65535) {
          ticks = 65535; // Clamp to max
        }
      }
    }
  }
  
  hwTimer->callback = callback;
  hwTimer->active = true;
  
  // Disable timer
  hwTimer->tc->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);
  
  // Update prescaler
  hwTimer->tc->COUNT16.CTRLA.bit.PRESCALER = prescaler;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);
  
  // Load counter with (65535 - ticks) so it overflows after ticks counts
  hwTimer->tc->COUNT16.COUNT.reg = 65535 - ticks;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);
  
  // Clear any pending interrupts
  hwTimer->tc->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF;
  
  // Enable timer
  hwTimer->tc->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);
  hwTimer->startTime = arduinoNano33IotGetElapsedNanoseconds(0);
  hwTimer->deadline = hwTimer->startTime + (microseconds * ((uint64_t) 1000));
  
  return 0;
}

uint64_t arduinoNano33IotConfiguredTimerNanoseconds(int timer) {
  if ((timer < 0) || (timer >= _numTimers)) {
    return 0;
  }
  
  HardwareTimer *hwTimer = &hardwareTimers[timer];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    return 0;
  }
  
  return hwTimer->deadline - hwTimer->startTime;
}

uint64_t arduinoNano33IotRemainingTimerNanoseconds(int timer) {
  if ((timer < 0) || (timer >= _numTimers)) {
    return 0;
  }
  
  HardwareTimer *hwTimer = &hardwareTimers[timer];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    return 0;
  }
  
  int64_t now = arduinoNano33IotGetElapsedNanoseconds(0);
  if (now > hwTimer->deadline) {
    return 0;
  }
  
  return hwTimer->deadline - now;
}

int arduinoNano33IotCancelTimer(int timer) {
  if ((timer < 0) || (timer >= _numTimers)) {
    return -ERANGE;
  }
  
  HardwareTimer *hwTimer = &hardwareTimers[timer];
  if (!hwTimer->initialized) {
    return -EINVAL;
  } else if (!hwTimer->active) {
    // Not an error but nothing to do.
    return 0;
  }
  
  // Disable timer
  hwTimer->tc->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);
  
  // Clear interrupt flag
  hwTimer->tc->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF;
  
  hwTimer->active = false;
  hwTimer->startTime = 0;
  hwTimer->deadline = 0;
  hwTimer->callback = nullptr;
  
  return 0;
}

int arduinoNano33IotCancelAndGetTimer(int timer,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void)
) {
  // We need to get `now` as close to the beginning of this function call as
  // possible so that any call to reconfigure the timer later is correct.
  // Don't call arduinoNano33IotGetElapsedNanoseconds, just compute directly.
  int64_t now = micros() * 1000;
  
  if ((timer < 0) || (timer >= _numTimers)) {
    return -ERANGE;
  }
  
  HardwareTimer *hwTimer = &hardwareTimers[timer];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    // We cannot populate the provided pointers, so we will error here.  This
    // also signals to the caller that there's no need to call configTimer
    // later.
    return -EINVAL;
  }
  
  // ***DO NOT*** call arduinoNano33IotCancelTimer.  It's expected that this
  // function is in the critical path.  Time is of the essence, so inline the
  // logic.
  
  // Disable timer
  hwTimer->tc->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);
  
  // Clear interrupt flag
  hwTimer->tc->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF;
  
  if (configuredNanoseconds != NULL) {
    if (hwTimer->deadline > hwTimer->startTime) {
      *configuredNanoseconds = hwTimer->deadline - hwTimer->startTime;
    } else {
      *configuredNanoseconds = 0;
    }
  }
  
  if (remainingNanoseconds != NULL) {
    if (now < hwTimer->deadline) {
      *remainingNanoseconds = hwTimer->deadline - now;
    } else {
      *remainingNanoseconds = 0;
    }
  }
  
  if (callback != NULL) {
    *callback = hwTimer->callback;
  }
  
  hwTimer->active = false;
  hwTimer->startTime = 0;
  hwTimer->deadline = 0;
  hwTimer->callback = nullptr;
  
  return 0;
}

/// @fn void arduinoNano33IotTimerInterruptHandler(int timer)
///
/// @brief Base implementation for the Timer/Counter interrupt handlers.
///
/// @param timer The zero-based index of the timer to handle.
///
/// @return This function returns no value.
void arduinoNano33IotTimerInterruptHandler(int timer) {
  // This function is only called from one of the real interrupt handlers, so
  // we're guaranteed that the timer parameter is good.  Skip validation.
  HardwareTimer *hwTimer = &hardwareTimers[timer];
  
  hwTimer->active = false;
  hwTimer->startTime = 0;
  hwTimer->deadline = 0;
  
  // Call callback if set
  if (hwTimer->callback) {
    hwTimer->callback();
  }
}

/// @fn void arduinoNano33IotTimerInterruptHandler0()
///
/// @brief Call arduinoNano33IotTimerInterruptHandler with a value of 0.  This
/// effectively replaces TC3_Handler.
///
/// @return This function returns no value.
void arduinoNano33IotTimerInterruptHandler0() {
  SAVE_CONTEXT();
  arduinoNano33IotTimerInterruptHandler(0);
  RESTORE_CONTEXT();
}

/// @fn void arduinoNano33IotTimerInterruptHandler1()
///
/// @brief Call arduinoNano33IotTimerInterruptHandler with a value of 1.  This
/// effectively replaces TC4_Handler.
///
/// @return This function returns no value.
void arduinoNano33IotTimerInterruptHandler1() {
  SAVE_CONTEXT();
  arduinoNano33IotTimerInterruptHandler(1);
  RESTORE_CONTEXT();
}

/// @fn void TC3_Handler(void)
///
/// @brief Interrupt handler for Timer/Counter 3.
///
/// @return This function returns no value.
void TC3_Handler(void) {
  RETURN_TO_HANDLER(0);
}

/// @fn void TC4_Handler(void)
///
/// @brief Interrupt handler for Timer/Counter 4.
///
/// @return This function returns no value.
void TC4_Handler(void) {
  RETURN_TO_HANDLER(1);
}

static HalTimer arduinoNano33IotTimerHal = {
  .getNumTimers = arduinoNano33IotGetNumTimers,
  .setNumTimers = arduinoNano33IotSetNumTimers,
  .initTimer = arduinoNano33IotInitTimer,
  .configOneShotTimer = arduinoNano33IotConfigOneShotTimer,
  .configuredTimerNanoseconds = arduinoNano33IotConfiguredTimerNanoseconds,
  .remainingTimerNanoseconds = arduinoNano33IotRemainingTimerNanoseconds,
  .cancelTimer = arduinoNano33IotCancelTimer,
  .cancelAndGetTimer = arduinoNano33IotCancelAndGetTimer,
};

int arduinoNano33IotInitRootStorage(SchedulerState *schedulerState) {
  // Create the SD card task.
  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio = SD_CARD_PIN_CHIP_SELECT,
    .spiCopiDio = SPI_COPI_DIO,
    .spiCipoDio = SPI_CIPO_DIO,
    .spiSckDio = SPI_SCK_DIO,
  };

  return halCommonInitRootFilesystem(
    halCommonInitRootSdSpiStorage(&sdCardSpiArgs));
}


/// @var arduinoNano33IotHal
///
/// @brief The implementation of the Hal interface for the Arduino Nano 33 Iot.
static Hal arduinoNano33IotHal = {
  // Memory definitions.
  .processStackSize = arduinoNano33IotProcessStackSize,
  .memoryManagerStackSize = arduinoNano33IotMemoryManagerStackSize,
  .bottomOfHeap = arduinoNano33IotBottomOfHeap,
  
  // Overlay definitions.
  .overlayMap = (NanoOsOverlayMap*) OVERLAY_ADDRESS,
  .overlaySize = OVERLAY_SIZE,
  
  .uartHal = &arduinoNano33IotUartHal,
  .dioHal = &arduinoNano33IotDioHal,
  .spiHal = &arduinoNano33IotSpiHal,
  .clockHal = &arduinoNano33IotClockHal,
  .powerHal = &arduinoNano33IotPowerHal,
  .timerHal = &arduinoNano33IotTimerHal,
  
  // Root storage configuration.
  .initRootStorage = arduinoNano33IotInitRootStorage,
};

const Hal* halArduinoNano33IotInit(void) {
  extern char __bss_end__;
  if (((uintptr_t) &__bss_end__)
    > ((uintptr_t) arduinoNano33IotHal.overlayMap)
  ) {
    int stackPosition = 0;
    Serial.begin(1000000);
    while (!Serial);
    Serial.print("ERROR!!! 0x");
    Serial.print((uintptr_t) &__bss_end__, HEX);
    Serial.print(" > 0x");
    Serial.print((uintptr_t) arduinoNano33IotHal.overlayMap, HEX);
    Serial.print("\n");
    Serial.print("Stack position = 0x");
    Serial.print((uintptr_t) &stackPosition, HEX);
    Serial.print("\n");
    Serial.print("*******************************************************\n");
    Serial.print("* Running user programs will corrupt system memory!!! *\n");
    Serial.print("*******************************************************\n");
  }
  
  __enable_irq();  // Ensure global interrupts are enabled
  
  if (halCommonInit(&arduinoNano33IotHal) != 0) {
    return NULL;
  }

  return &arduinoNano33IotHal;
}

#endif // ARDUINO_SAMD_NANO_33_IOT
