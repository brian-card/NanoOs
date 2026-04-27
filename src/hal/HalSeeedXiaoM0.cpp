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

/// @file HalSeeedXiaoM0.cpp
///
/// @brief HAL implementation for a Seeed Xiao M0

#if defined(ARDUINO_SEEED_XIAO_M0)

// Base Arduino definitions
#include <Arduino.h>

// Basic SPI communication
#include <SPI.h>

#include "HalSeeedXiaoM0.h"
// Deliberately *NOT* including MemoryManager.h here.  The HAL has to be
// operational prior to the memory manager and really should be completely
// independent of it.
#include "../kernel/ExFatTask.h"
#include "../kernel/MemoryManager.h"
#include "../kernel/NanoOs.h"
#include "../kernel/Tasks.h"
#include "../kernel/Scheduler.h"
#include "../kernel/SdCardSpi.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsStdio.h"

/// @def NUM_DIO_PINS
///
/// @brief The number of digital IO pins on the board.
#define NUM_DIO_PINS 8

/// @def SPI_COPI_DIO
///
/// @brief DIO pin used for SPI COPI on the Adafruit Feather M0 WiFi.
#define SPI_COPI_DIO 10

/// @def SPI_CIPO_DIO
///
/// @brief DIO pin used for SPI CIPO on the Adafruit Feather M0 WiFi.
#define SPI_CIPO_DIO 9

/// @def SPI_SCK_DIO
///
/// @brief DIO pin used for SPI serial clock on the Adafruit Feather M0 WiFi.
#define SPI_SCK_DIO 8

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
/// @brief The size, in bytes, of the overlay supported by the board.
#define OVERLAY_SIZE 8192

/// @def SD_CARD_PIN_CHIP_SELECT
///
/// @brief Pin to use for the MicroSD card reader's SPI chip select line.
#define SD_CARD_PIN_CHIP_SELECT 7

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
    = (uint32_t) seeedXiaoM0TimerInterruptHandler ## handlerIndex; \
  return

uintptr_t seeedXiaoM0ProcessStackSize(void) {
  return PROCESS_STACK_SIZE;
}

uintptr_t seeedXiaoM0MemoryManagerStackSize(bool debug) {
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

void* seeedXiaoM0BottomOfHeap(void) {
  return _bottomOfHeap;
}

/// @def MAX_UARTS
///
/// @brief The maximum number of serial ports we can support on the board.
///
/// @note Due to the implementation of the Adafruit version of libraries, we
/// can't create two instances of the same base serial port class.  So, we'll
/// have to use switch statements throughout the serial port code.
#define MAX_UARTS 2

/// @def UART_USB
///
/// @brief The numerical ID that will correspond to the USB serial port in this
/// HAL.
#define UART_USB 0

/// @def UART_UART
///
/// @brief The numerical ID that will correspond to the UART serial port in
/// this HAL.
#define UART_UART 1

/// @var _numUarts
///
/// @brief The number of serial ports we support on the Adafruit Feather M0
/// WiFi.
static int _numUarts = MAX_UARTS;

int seeedXiaoM0GetNumUarts(void) {
  return _numUarts;
}

int seeedXiaoM0SetNumUarts(int numUarts) {
  if (numUarts > MAX_UARTS) {
    return -ERANGE;
  } else if (numUarts < -ELAST) {
    return -EINVAL;
  }
  
  _numUarts = numUarts;
  
  return 0;
}

int seeedXiaoM0InitUart(int port, int32_t baud) {
  int returnValue = -ERANGE;
  
  switch (port) {
    case UART_USB:
      {
        Serial.begin(baud);
        while (!Serial);
        returnValue = 0;
        break;
      }
    
    case UART_UART:
      {
        Serial1.begin(baud);
        while (!Serial1);
        returnValue = 0;
        break;
      }
  }
    
  return returnValue;
}

int seeedXiaoM0PollUart(int port) {
  int serialData = -ERANGE;
  
  switch (port) {
    case UART_USB:
      {
        serialData = Serial.read();
        break;
      }
    
    case UART_UART:
      {
        serialData = Serial1.read();
        break;
      }
  }
  
  return serialData;
}

ssize_t seeedXiaoM0WriteUart(int port,
  const uint8_t *data, ssize_t length
) {
  ssize_t numBytesWritten = -ERANGE;
  
  switch (port) {
    case UART_USB:
      {
        numBytesWritten = Serial.write(data, length);
        break;
      }
    
    case UART_UART:
      {
        numBytesWritten = Serial1.write(data, length);
        break;
      }
  }
  
  return numBytesWritten;
}

static HalUart seeedXiaoM0UartHal = {
  .getNumUarts = seeedXiaoM0GetNumUarts,
  .setNumUarts = seeedXiaoM0SetNumUarts,
  .initUart = seeedXiaoM0InitUart,
  .pollUart = seeedXiaoM0PollUart,
  .writeUart = seeedXiaoM0WriteUart,
};

int seeedXiaoM0GetNumDios(void) {
  return NUM_DIO_PINS;
}

int seeedXiaoM0ConfigureDio(int dio, bool output) {
  int returnValue = -ERANGE;
  
  uint8_t modes[2] = { INPUT, OUTPUT };
  pinMode(dio, modes[output]);
  
  returnValue = 0;
  
  return returnValue;
}

int seeedXiaoM0WriteDio(int dio, bool high) {
  int returnValue = -ERANGE;
  
  uint8_t levels[2] = { LOW, HIGH };
  digitalWrite(dio, levels[high]);
  
  returnValue = 0;
  
  return returnValue;
}

static HalDio seeedXiaoM0DioHal = {
  .getNumDios = seeedXiaoM0GetNumDios,
  .configureDio = seeedXiaoM0ConfigureDio,
  .writeDio = seeedXiaoM0WriteDio,
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

/// @var seeedXiaoM0SpiDevices
///
/// @brief Array of structures that will hold the information about SPI
/// connections.
static struct SeeedXiaoM0Spi {
  bool     configured;         // Will default to false
  uint8_t  chipSelect;
  bool     transferInProgress; // Will default to false
  uint32_t baud;
} seeedXiaoM0SpiDevices[NUM_DIO_PINS] = {};

/// @var numArduinoSpis
///
/// @brief The number of devices we support in the
/// seeedXiaoM0SpiDevices array.
static const int numArduinoSpis
  = sizeof(seeedXiaoM0SpiDevices)
  / sizeof(seeedXiaoM0SpiDevices[0]);

int seeedXiaoM0InitSpiDevice(int spi,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud
) {
  if ((spi < 0) || (spi >= numArduinoSpis)) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (
       (cs   == SPI_SCK_DIO)
    || (cs   == SPI_COPI_DIO)
    || (cs   == SPI_CIPO_DIO)
    || (sck  != SPI_SCK_DIO)
    || (copi != SPI_COPI_DIO)
    || (cipo != SPI_CIPO_DIO)
  ) {
    return -EINVAL;
  } else if (seeedXiaoM0SpiDevices[spi].configured == true) {
    return -EBUSY;
  }
  
  if (globalSpiConfigured == false) {
    // Set up SPI at the default speed.
    globalSpiConfigured = true;
    SPI.begin();
  }
  
  // Configure the chip select DIO for output.
  seeedXiaoM0ConfigureDio(cs, 1);
  // Deselect the chip select pin.
  seeedXiaoM0WriteDio(cs, 1);
  
  // Configure our internal metadata for the device.
  seeedXiaoM0SpiDevices[spi].chipSelect = cs;
  seeedXiaoM0SpiDevices[spi].baud = baud;
  seeedXiaoM0SpiDevices[spi].configured = true;
  
  return 0;
}

int seeedXiaoM0StartSpiTransfer(int spi) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (seeedXiaoM0SpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (globalSpiInUse == true) {
    return -EBUSY;
  }
  
  // Mark the interface in use.
  globalSpiInUse = true;
  
  // Select the chip select pin.
  seeedXiaoM0WriteDio(
    seeedXiaoM0SpiDevices[spi].chipSelect, 0);
  
  // Begin the transaction
  SPI.beginTransaction(SPISettings(seeedXiaoM0SpiDevices[spi].baud,
    MSBFIRST, SPI_MODE0));
  
  seeedXiaoM0SpiDevices[spi].transferInProgress = true;
  
  return 0;
}

int seeedXiaoM0EndSpiTransfer(int spi) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (seeedXiaoM0SpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  }
  
  seeedXiaoM0SpiDevices[spi].transferInProgress = false;
  
  // End the transaction.
  SPI.endTransaction();
  
  // Deselect the chip select pin.
  seeedXiaoM0WriteDio(
    seeedXiaoM0SpiDevices[spi].chipSelect, 1);
  for (int ii = 0; ii < 8; ii++) {
    SPI.transfer(0xFF); // 8 clock pulses
  }
  
  // Mark the interface not in use.
  globalSpiInUse = false;
  
  return 0;
}

int seeedXiaoM0SpiTransfer8(int spi, uint8_t data) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (seeedXiaoM0SpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!seeedXiaoM0SpiDevices[spi].transferInProgress) {
    // The only error that seeedXiaoM0StartSpiTransfer can return is
    // ENODEV and we've already checked for that, so we don't need to check the
    // return value here.
    seeedXiaoM0StartSpiTransfer(spi);
  }
  
  return (int) SPI.transfer(data);
}

int seeedXiaoM0SpiTransferBytes(int spi,
  uint8_t *data, uint32_t length
) {
  if ((spi < 0) || (spi >= numArduinoSpis)
    || (seeedXiaoM0SpiDevices[spi].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!seeedXiaoM0SpiDevices[spi].transferInProgress) {
    // The only error that seeedXiaoM0StartSpiTransfer can return is
    // ENODEV and we've already checked for that, so we don't need to check the
    // return value here.
    seeedXiaoM0StartSpiTransfer(spi);
  }
  
  SPI.transfer(data, length);
  
  return 0;
}

static HalSpi seeedXiaoM0SpiHal = {
  .initSpiDevice = seeedXiaoM0InitSpiDevice,
  .startSpiTransfer = seeedXiaoM0StartSpiTransfer,
  .endSpiTransfer = seeedXiaoM0EndSpiTransfer,
  .spiTransfer8 = seeedXiaoM0SpiTransfer8,
  .spiTransferBytes = seeedXiaoM0SpiTransferBytes,
};

/// @var baseSystemTimeUs
///
/// @brief The time provided by the user or some other task as a baseline
/// time for the system.
static int64_t baseSystemTimeUs = 0;

int seeedXiaoM0SetSystemTime(struct timespec *now) {
  if (now == NULL) {
    return -EINVAL;
  }
  
  baseSystemTimeUs
    = (((int64_t) now->tv_sec) * ((int64_t) 1000000))
    + (((int64_t) now->tv_nsec) / ((int64_t) 1000));
  
  return 0;
}

int64_t seeedXiaoM0GetElapsedMicroseconds(int64_t startTime);

int64_t seeedXiaoM0GetElapsedMilliseconds(int64_t startTime) {
  return seeedXiaoM0GetElapsedMicroseconds(
    startTime * ((int64_t) 1000)) / ((int64_t) 1000);
}

int64_t seeedXiaoM0GetElapsedMicroseconds(int64_t startTime) {
  int64_t now = baseSystemTimeUs + micros();

  if (now < startTime) {
    return -1;
  }

  return now - startTime;
}

int64_t seeedXiaoM0GetElapsedNanoseconds(int64_t startTime) {
  return seeedXiaoM0GetElapsedMicroseconds(
    startTime / ((int64_t) 1000)) * ((int64_t) 1000);
}

static HalClock seeedXiaoM0ClockHal = {
  .setSystemTime = seeedXiaoM0SetSystemTime,
  .getElapsedMilliseconds = seeedXiaoM0GetElapsedMilliseconds,
  .getElapsedMicroseconds = seeedXiaoM0GetElapsedMicroseconds,
  .getElapsedNanoseconds = seeedXiaoM0GetElapsedNanoseconds,
};

int seeedXiaoM0Shutdown(HalShutdownType shutdownType) {
  // You can't completely turn off the board from software.  The best we can
  // do is put into a low power state, so do the same set of operations for
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

static HalPower seeedXiaoM0PowerHal = {
  .shutdown = seeedXiaoM0Shutdown,
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
/// @brief The number of timers returned by seeedXiaoM0GetNumTimers.  This
/// is initialized to the number of timers supported, but may be overridden by
/// a call to seeedXiaoM0SetNumTimers.
static int _numTimers = sizeof(hardwareTimers) / sizeof(hardwareTimers[0]);

int seeedXiaoM0GetNumTimers(void) {
  return _numTimers;
}

int seeedXiaoM0SetNumTimers(int numTimers) {
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

int seeedXiaoM0InitTimer(int timer) {
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

int seeedXiaoM0ConfigOneShotTimer(int timer,
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
  int seeedXiaoM0CancelTimer(int);
  seeedXiaoM0CancelTimer(timer);
  
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
  hwTimer->startTime = seeedXiaoM0GetElapsedNanoseconds(0);
  hwTimer->deadline = hwTimer->startTime + (microseconds * ((uint64_t) 1000));
  
  return 0;
}

uint64_t seeedXiaoM0ConfiguredTimerNanoseconds(int timer) {
  if ((timer < 0) || (timer >= _numTimers)) {
    return 0;
  }
  
  HardwareTimer *hwTimer = &hardwareTimers[timer];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    return 0;
  }
  
  return hwTimer->deadline - hwTimer->startTime;
}

uint64_t seeedXiaoM0RemainingTimerNanoseconds(int timer) {
  if ((timer < 0) || (timer >= _numTimers)) {
    return 0;
  }
  
  HardwareTimer *hwTimer = &hardwareTimers[timer];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    return 0;
  }
  
  int64_t now = seeedXiaoM0GetElapsedNanoseconds(0);
  if (now > hwTimer->deadline) {
    return 0;
  }
  
  return hwTimer->deadline - now;
}

int seeedXiaoM0CancelTimer(int timer) {
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

int seeedXiaoM0CancelAndGetTimer(int timer,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void)
) {
  // We need to get `now` as close to the beginning of this function call as
  // possible so that any call to reconfigure the timer later is correct.
  // Don't call seeedXiaoM0GetElapsedNanoseconds, just compute
  // directly.
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
  
  // ***DO NOT*** call seeedXiaoM0CancelTimer.  It's expected that
  // this function is in the critical path.  Time is of the essence, so inline
  // the logic.
  
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

/// @fn void seeedXiaoM0TimerInterruptHandler(int timer)
///
/// @brief Base implementation for the Timer/Counter interrupt handlers.
///
/// @param timer The zero-based index of the timer to handle.
///
/// @return This function returns no value.
void seeedXiaoM0TimerInterruptHandler(int timer) {
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

/// @fn void seeedXiaoM0TimerInterruptHandler0()
///
/// @brief Call seeedXiaoM0TimerInterruptHandler with a value of 0.  This
/// effectively replaces TC3_Handler.
///
/// @return This function returns no value.
void seeedXiaoM0TimerInterruptHandler0() {
  SAVE_CONTEXT();
  seeedXiaoM0TimerInterruptHandler(0);
  RESTORE_CONTEXT();
}

/// @fn void seeedXiaoM0TimerInterruptHandler1()
///
/// @brief Call seeedXiaoM0TimerInterruptHandler with a value of 1.  This
/// effectively replaces TC4_Handler.
///
/// @return This function returns no value.
void seeedXiaoM0TimerInterruptHandler1() {
  SAVE_CONTEXT();
  seeedXiaoM0TimerInterruptHandler(1);
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

static HalTimer seeedXiaoM0TimerHal = {
  .getNumTimers = seeedXiaoM0GetNumTimers,
  .setNumTimers = seeedXiaoM0SetNumTimers,
  .initTimer = seeedXiaoM0InitTimer,
  .configOneShotTimer = seeedXiaoM0ConfigOneShotTimer,
  .configuredTimerNanoseconds = seeedXiaoM0ConfiguredTimerNanoseconds,
  .remainingTimerNanoseconds = seeedXiaoM0RemainingTimerNanoseconds,
  .cancelTimer = seeedXiaoM0CancelTimer,
  .cancelAndGetTimer = seeedXiaoM0CancelAndGetTimer,
};

int seeedXiaoM0InitRootStorage(SchedulerState *schedulerState) {
  TaskDescriptor *allTasks = schedulerState->allTasks;
  
  // Create the SD card task.
  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio = SD_CARD_PIN_CHIP_SELECT,
    .spiCopiDio = SPI_COPI_DIO,
    .spiCipoDio = SPI_CIPO_DIO,
    .spiSckDio = SPI_SCK_DIO,
  };

  // Create the SD card task.
  TaskDescriptor *taskDescriptor
    = &allTasks[schedulerState->firstUserTaskId - 1];
  if (taskCreate(
    taskDescriptor, runSdCardSpi, &sdCardSpiArgs)
    != taskSuccess
  ) {
    fputs("Could not start SD card task.\n", stderr);
  }
  printDebugString("Started SD card task.\n");
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);
  taskDescriptor->taskId = schedulerState->firstUserTaskId;
  taskDescriptor->name = "SD card";
  taskDescriptor->userId = ROOT_USER_ID;
  BlockStorageDevice *sdDevice = (BlockStorageDevice*) coroutineResume(
    allTasks[schedulerState->firstUserTaskId - 1].taskHandle, NULL);
  sdDevice->partitionNumber = 1;
  printDebugString("Configured SD card task.\n");
  
  // Create the filesystem task.
  schedulerState->rootFsTaskId = schedulerState->firstUserTaskId + 1;
  taskDescriptor = &allTasks[schedulerState->rootFsTaskId - 1];
  if (taskCreate(taskDescriptor, runExFatFilesystem, sdDevice)
    != taskSuccess
  ) {
    fputs("Could not start filesystem task.\n", stderr);
  }
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);
  taskDescriptor->taskId = schedulerState->rootFsTaskId;
  taskDescriptor->name = "filesystem";
  taskDescriptor->userId = ROOT_USER_ID;
  printDebugString("Created filesystem task.\n");
  
  schedulerState->firstUserTaskId = schedulerState->rootFsTaskId + 1;
  schedulerState->firstShellTaskId = schedulerState->firstUserTaskId;
  
  return 0;
}


/// @var seeedXiaoM0Hal
///
/// @brief The implementation of the Hal interface for the Adafruit Feather M0
/// WiFi.
static Hal seeedXiaoM0Hal = {
  // Memory definitions.
  .processStackSize = seeedXiaoM0ProcessStackSize,
  .memoryManagerStackSize = seeedXiaoM0MemoryManagerStackSize,
  .bottomOfHeap = seeedXiaoM0BottomOfHeap,
  
  // Overlay definitions.
  .overlayMap = (NanoOsOverlayMap*) OVERLAY_ADDRESS,
  .overlaySize = OVERLAY_SIZE,
  
  .uartHal = &seeedXiaoM0UartHal,
  .dioHal = &seeedXiaoM0DioHal,
  .spiHal = &seeedXiaoM0SpiHal,
  .clockHal = &seeedXiaoM0ClockHal,
  .powerHal = &seeedXiaoM0PowerHal,
  .timerHal = &seeedXiaoM0TimerHal,
  
  // Root storage configuration.
  .initRootStorage = seeedXiaoM0InitRootStorage,
};

const Hal* halSeeedXiaoM0Init(void) {
  extern char __bss_end__;
  if (((uintptr_t) &__bss_end__)
    > ((uintptr_t) seeedXiaoM0Hal.overlayMap)
  ) {
    int stackPosition = 0;
    Serial.begin(1000000);
    while (!Serial);
    Serial.print("ERROR!!! 0x");
    Serial.print((uintptr_t) &__bss_end__, HEX);
    Serial.print(" > 0x");
    Serial.print((uintptr_t) seeedXiaoM0Hal.overlayMap, HEX);
    Serial.print("\n");
    Serial.print("Stack position = 0x");
    Serial.print((uintptr_t) &stackPosition, HEX);
    Serial.print("\n");
    Serial.print("*******************************************************\n");
    Serial.print("* Running user programs will corrupt system memory!!! *\n");
    Serial.print("*******************************************************\n");
  }
  
  __enable_irq();  // Ensure global interrupts are enabled
  
  int ii = 0;
  Hal *hal = &seeedXiaoM0Hal;
  if (hal->uartHal != NULL) {
    int numUarts = hal->uartHal->getNumUarts();
    if (numUarts <= 0) {
      // Nothing we can do.
      return NULL;
    }
    
    // Set all the serial ports to run at 1000000 baud.
    if (hal->uartHal->initUart(0, 1000000) < 0) {
      // Nothing we can do.
      return NULL;
    }
    for (ii = 1; ii < numUarts; ii++) {
      if (hal->uartHal->initUart(ii, 1000000) < 0) {
        // We can't support more than the last serial port that was successfully
        // initialized.
        break;
      }
    }
    hal->uartHal->setNumUarts(ii);
    if (ii != numUarts) {
      Serial.begin(1000000);
      while (!Serial);
      Serial.print("WARNING: Only initialized ");
      Serial.print(ii);
      Serial.print(" serial ports\n");
    }
  }

  if (hal->timerHal != NULL)  {
    int numTimers = hal->timerHal->getNumTimers();
    for (ii = 0; ii < numTimers; ii++) {
      if (hal->timerHal->initTimer(ii) < 0) {
        break;
      }
    }
    hal->timerHal->setNumTimers(ii);
    if (ii != numTimers) {
      Serial.begin(1000000);
      while (!Serial);
      Serial.print("WARNING: Only initialized ");
      Serial.print(ii);
      Serial.print(" timers\n");
    }
  }
  
  return hal;
}

#endif // ARDUINO_SEEED_XIAO_M0
