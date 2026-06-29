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

/// @file HalArduinoSamD21x18A.cpp
///
/// @brief HAL implementation for a SAMD21x18A Arduino-based board.

#if defined(__SAMD21G18A__) || defined(__SAMD21E18A__)

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

#include "HalArduinoSamD21x18A.h"
#include "HalCommon.h"
// Deliberately *NOT* including MemoryManager.h here.  The HAL has to be
// operational prior to the memory manager and really should be completely
// independent of it.
#include "../kernel/NanoOs.h"
#include "../kernel/Processes.h"
#include "../kernel/Scheduler.h"
#include "../kernel/SdCardSpi.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsStdio.h"

/// @def PROCESS_STACK_SIZE
///
/// @brief The size, in bytes, of a regular process's stack.
#define PROCESS_STACK_SIZE 800

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief The size, in bytes, of the memory manager process's stack.
#define MEMORY_MANAGER_STACK_SIZE 256

/// @def OVERLAY_ADDRESS
///
/// @brief The address of where the overlay will be placed in memory.
#define OVERLAY_ADDRESS 0x20001800

/// @def OVERLAY_SIZE
///
/// @brief The size, in bytes, of the overlay supported by the board.
#define OVERLAY_SIZE 4096

/// @def DIO_PIN_UNDEFINED
///
/// @brief Value to indicate that the value of a specific pin is undefined.
#define DIO_PIN_UNDEFINED 255

/// @def MAX_SPI_DEVICES
///
/// @brief The maximum number of SPI devices the system can support.
#define MAX_SPI_DEVICES 2

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

/// @var _sdCardPinChipSelect
///
/// @brief Pin to use for the MicroSD card reader's SPI chip select line.
static uint8_t _sdCardPinChipSelect = DIO_PIN_UNDEFINED;

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
    = (uint32_t) arduinoSamD21x18ATimerInterruptHandler ## handlerIndex; \
  return

int32_t arduinoSamD21x18AProcessStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  (void) debug;
  *returnValue = PROCESS_STACK_SIZE;
  return 0;
}

int32_t arduinoSamD21x18AMemoryManagerStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  if (debug == false) {
    // This is the expected case, so list it first.
    *returnValue = MEMORY_MANAGER_STACK_SIZE;
  } else {
    *returnValue = MEMORY_MANAGER_DEBUG_STACK_SIZE;
  }
  return 0;
}

int32_t arduinoSamD21x18ABottomOfHeap(va_list args) {
  bool debug = (bool) va_arg(args, int);
  void **returnValue = va_arg(args, void**);
  (void) debug;
  *returnValue = (void*) (OVERLAY_ADDRESS + OVERLAY_SIZE);
  return 0;
}

int32_t arduinoSamD21x18ANumExtraSchedulerStacks(va_list args) {
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  *returnValue = 2;
  return 0;
}

int32_t arduinoSamD21x18ANumExtraConsoleStacks(va_list args) {
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  *returnValue = 1;
  return 0;
}

/// @def MAX_UARTS
///
/// @brief The maximum number of serial ports we can support on the board.
///
/// @note Due to the implementation of some versions of the version of Arduino
/// libraries, we can't create two instances of the same base serial port class.
///  So, we'll have to use switch statements throughout the serial port code.
#define MAX_UARTS 2

/// @def UART_USB
///
/// @brief The numerical ID that will correspond to the USB serial port in this
/// HAL.
#define UART_USB 0

/// @def BOARD_UART
///
/// @brief The numerical ID that will correspond to the UART serial port in
/// this HAL.
#define BOARD_UART 1

int32_t arduinoSamD21x18AInitUart(va_list args) {
  (void) args;
  // Nothing really to do on this platform.  Just return.
  return 0;
}

int32_t arduinoSamD21x18AConfigureUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint32_t baud = va_arg(args, uint32_t);
  int returnValue = -ERANGE;

  switch (deviceId) {
    case UART_USB:
      {
        Serial.begin(baud);
        while (!Serial);
        returnValue = 0;
        break;
      }

    case BOARD_UART:
      {
        Serial1.begin(baud);
        while (!Serial1);
        returnValue = 0;
        break;
      }
  }

  return returnValue;
}

int32_t arduinoSamD21x18APollUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  int serialData = -ERANGE;

  switch (deviceId) {
    case UART_USB:
      {
        serialData = Serial.read();
        break;
      }

    case BOARD_UART:
      {
        serialData = Serial1.read();
        break;
      }
  }

  return serialData;
}

int32_t arduinoSamD21x18AWriteUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  const uint8_t *data = va_arg(args, const uint8_t*);
  ssize_t length = va_arg(args, ssize_t);
  ssize_t *returnValue = va_arg(args, ssize_t*);

  ssize_t numBytesWritten = -ERANGE;

  switch (deviceId) {
    case UART_USB:
      {
        numBytesWritten = Serial.write(data, length);
        break;
      }

    case BOARD_UART:
      {
        numBytesWritten = Serial1.write(data, length);
        break;
      }
  }

  if (returnValue != NULL) {
    *returnValue = numBytesWritten;
  }
  return (numBytesWritten >= 0) ? 0 : (int32_t) numBytesWritten;
}

int32_t arduinoSamD21x18AIsUartConsole(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool *returnValue = va_arg(args, bool*);
  (void) deviceId;
  if (returnValue != NULL) {
    *returnValue = true;
  }
  return 0;
}

int32_t arduinoSamD21x18AInitDio(va_list args) {
  (void) args;
  return 0;
}

static int32_t arduinoSamD21x18AConfigureDioImpl(int32_t dio, bool output) {
  uint8_t modes[2] = { INPUT, OUTPUT };
  pinMode(dio, modes[output]);
  return 0;
}

int32_t arduinoSamD21x18AConfigureDio(va_list args) {
  int32_t dio = va_arg(args, int32_t);
  bool output = (bool) va_arg(args, int);
  return arduinoSamD21x18AConfigureDioImpl(dio, output);
}

static int32_t arduinoSamD21x18AWriteDioImpl(int32_t dio, bool high) {
  uint8_t levels[2] = { LOW, HIGH };
  digitalWrite(dio, levels[high]);
  return 0;
}

int32_t arduinoSamD21x18AWriteDio(va_list args) {
  int32_t dio = va_arg(args, int32_t);
  bool high = (bool) va_arg(args, int);
  return arduinoSamD21x18AWriteDioImpl(dio, high);
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

/// @var arduinoSamD21x18ASpiDevices
///
/// @brief Array of structures that will hold the information about SPI
/// connections.
static struct ArduinoSamD21x18ASpi {
  bool     configured;         // Will default to false
  uint8_t  chipSelect;
  bool     transferInProgress; // Will default to false
  uint32_t baud;
} arduinoSamD21x18ASpiDevices[MAX_SPI_DEVICES] = {};

/// @var numArduinoSpis
///
/// @brief The number of devices we support in the
/// arduinoSamD21x18ASpiDevices array.
static const int numArduinoSpis
  = sizeof(arduinoSamD21x18ASpiDevices)
  / sizeof(arduinoSamD21x18ASpiDevices[0]);

static int32_t arduinoSamD21x18AInitSpiImpl(void) {
  if (globalSpiConfigured == false) {
    // Set up SPI at the default speed.
    globalSpiConfigured = true;
    SPI.begin();
  }
  return 0;
}

int32_t arduinoSamD21x18AInitSpi(va_list args) {
  (void) args;
  return arduinoSamD21x18AInitSpiImpl();
}

int32_t arduinoSamD21x18AConfigureSpi(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t cs   = (uint8_t) va_arg(args, int);
  uint8_t sck  = (uint8_t) va_arg(args, int);
  uint8_t copi = (uint8_t) va_arg(args, int);
  uint8_t cipo = (uint8_t) va_arg(args, int);
  uint32_t baud = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (
       (cs   == _spiSckDio)
    || (cs   == _spiCopiDio)
    || (cs   == _spiCipoDio)
    || (sck  != _spiSckDio)
    || (copi != _spiCopiDio)
    || (cipo != _spiCipoDio)
  ) {
    return -EINVAL;
  } else if (arduinoSamD21x18ASpiDevices[deviceId].configured == true) {
    return -EBUSY;
  }

  if (arduinoSamD21x18AInitSpiImpl() != 0) {
    return -ENODEV;
  }

  // Configure the chip select DIO for output.
  arduinoSamD21x18AConfigureDioImpl(cs, 1);
  // Deselect the chip select pin.
  arduinoSamD21x18AWriteDioImpl(cs, 1);

  // Configure our internal metadata for the device.
  arduinoSamD21x18ASpiDevices[deviceId].chipSelect = cs;
  arduinoSamD21x18ASpiDevices[deviceId].baud = baud;
  arduinoSamD21x18ASpiDevices[deviceId].configured = true;

  return 0;
}

static int32_t arduinoSamD21x18AStartSpiTransferImpl(int32_t deviceId) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSamD21x18ASpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (globalSpiInUse == true) {
    return -EBUSY;
  }

  // Mark the interface in use.
  globalSpiInUse = true;

  // Select the chip select pin.
  arduinoSamD21x18AWriteDioImpl(
    arduinoSamD21x18ASpiDevices[deviceId].chipSelect, 0);

  // Begin the transaction
  SPI.beginTransaction(SPISettings(arduinoSamD21x18ASpiDevices[deviceId].baud,
    MSBFIRST, SPI_MODE0));

  arduinoSamD21x18ASpiDevices[deviceId].transferInProgress = true;

  return 0;
}

int32_t arduinoSamD21x18AStartSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  return arduinoSamD21x18AStartSpiTransferImpl(deviceId);
}

int32_t arduinoSamD21x18AEndSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSamD21x18ASpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  }

  arduinoSamD21x18ASpiDevices[deviceId].transferInProgress = false;

  // End the transaction.
  SPI.endTransaction();

  // Deselect the chip select pin.
  arduinoSamD21x18AWriteDioImpl(
    arduinoSamD21x18ASpiDevices[deviceId].chipSelect, 1);
  for (int ii = 0; ii < 8; ii++) {
    SPI.transfer(0xFF); // 8 clock pulses
  }

  // Mark the interface not in use.
  globalSpiInUse = false;

  return 0;
}

int32_t arduinoSamD21x18ASpiTransfer8(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t data = (uint8_t) va_arg(args, int);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSamD21x18ASpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSamD21x18ASpiDevices[deviceId].transferInProgress) {
    arduinoSamD21x18AStartSpiTransferImpl(deviceId);
  }

  return (int32_t) SPI.transfer(data);
}

int32_t arduinoSamD21x18ASpiTransferBytes(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t *data = va_arg(args, uint8_t*);
  uint32_t length = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSamD21x18ASpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSamD21x18ASpiDevices[deviceId].transferInProgress) {
    arduinoSamD21x18AStartSpiTransferImpl(deviceId);
  }

  SPI.transfer(data, length);

  return 0;
}

/// @var baseSystemTimeUs
///
/// @brief The time provided by the user or some other process as a baseline
/// time for the system.
static int64_t baseSystemTimeUs = 0;

int32_t arduinoSamD21x18ATimeInit(va_list args) {
  (void) args;
  return 0;
}

int32_t arduinoSamD21x18ASetSystemTime(va_list args) {
  struct timespec *now = va_arg(args, struct timespec*);
  if (now == NULL) {
    return -EINVAL;
  }

  baseSystemTimeUs
    = (((int64_t) now->tv_sec) * ((int64_t) 1000000))
    + (((int64_t) now->tv_nsec) / ((int64_t) 1000));

  return 0;
}

static int32_t arduinoSamD21x18AGetElapsedMicrosecondsImpl(int64_t startTime,
  int64_t *returnValue
) {
  int64_t now = baseSystemTimeUs + micros();

  if (now < startTime) {
    if (returnValue != NULL) {
      *returnValue = -1;
    }
    return -EIO;
  }

  if (returnValue != NULL) {
    *returnValue = now - startTime;
  }
  return 0;
}

int32_t arduinoSamD21x18AGetElapsedMilliseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t microseconds = 0;
  int32_t rv = arduinoSamD21x18AGetElapsedMicrosecondsImpl(
    startTime * ((int64_t) 1000), &microseconds);
  if (returnValue != NULL) {
    *returnValue = microseconds / ((int64_t) 1000);
  }
  return rv;
}

int32_t arduinoSamD21x18AGetElapsedMicroseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  return arduinoSamD21x18AGetElapsedMicrosecondsImpl(startTime, returnValue);
}

int32_t arduinoSamD21x18AGetElapsedNanoseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t microseconds = 0;
  int32_t rv = arduinoSamD21x18AGetElapsedMicrosecondsImpl(
    startTime / ((int64_t) 1000), &microseconds);
  if (returnValue != NULL) {
    *returnValue = microseconds * ((int64_t) 1000);
  }
  return rv;
}

int32_t arduinoSamD21x18AEnterMode(va_list args) {
  HalPowerMode powerMode = (HalPowerMode) va_arg(args, int);
  // You can't completely turn off the board from software.  The best we can
  // do is put into a low power state, so do the same set of operations for
  // both off and suspend.
  if ((powerMode == HAL_POWER_MODE_OFF)
    || (powerMode == HAL_POWER_MODE_SUSPEND)
  ) {
    // Configure for standby mode
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    // Set standby mode in Power Manager
    PM->SLEEP.reg = PM_SLEEP_IDLE_CPU;

    __DSB(); // Data Synchronization Barrier
    __WFI(); // Wait For Interrupt
  } else if (powerMode == HAL_POWER_MODE_RESET) {
    NVIC_SystemReset();
  }

  return 0;
}

/// @struct HardwareTimer
///
/// @brief Collection of variables needed to manage a single hardware timer.
///
/// @param tc A pointer to the timer/counter structure used for the timer.
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
/// @brief The number of timers returned by HAL->timer->numSupported.
static const int _numTimers
  = sizeof(hardwareTimers) / sizeof(hardwareTimers[0]);

/// @var halArduinoSamD21x18ATimersOnline
///
/// @brief Bitmask array of online timers.
static uint32_t halArduinoSamD21x18ATimersOnline[] = {
  0x00000003,
};

int32_t arduinoSamD21x18AInitTimer(va_list args) {
  (void) args;
  return 0;
}

int32_t arduinoSamD21x18AInitTimerDevice(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);

  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
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

static int32_t arduinoSamD21x18ACancelTimerImpl(int32_t deviceId);

int32_t arduinoSamD21x18AConfigOneShotTimer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint64_t nanoseconds = va_arg(args, uint64_t);
  void (*callback)(void) = va_arg(args, void (*)(void));

  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if (!hwTimer->initialized) {
    return -EINVAL;
  }

  // Cancel any existing timer
  arduinoSamD21x18ACancelTimerImpl(deviceId);

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
  int64_t startTime = 0;
  arduinoSamD21x18AGetElapsedMicrosecondsImpl(0, &startTime);
  hwTimer->startTime = startTime * ((int64_t) 1000);
  hwTimer->deadline = hwTimer->startTime + (microseconds * ((uint64_t) 1000));

  return 0;
}

int32_t arduinoSamD21x18AConfiguredTimerNanoseconds(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint64_t *returnValue = va_arg(args, uint64_t*);

  if (returnValue != NULL) {
    *returnValue = 0;
  }
  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    return -EINVAL;
  }

  if (returnValue != NULL) {
    *returnValue = hwTimer->deadline - hwTimer->startTime;
  }
  return 0;
}

int32_t arduinoSamD21x18ARemainingTimerNanoseconds(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint64_t *returnValue = va_arg(args, uint64_t*);

  if (returnValue != NULL) {
    *returnValue = 0;
  }
  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    return -EINVAL;
  }

  int64_t nowUs = 0;
  arduinoSamD21x18AGetElapsedMicrosecondsImpl(0, &nowUs);
  int64_t now = nowUs * ((int64_t) 1000);
  if (now > hwTimer->deadline) {
    return 0;
  }

  if (returnValue != NULL) {
    *returnValue = hwTimer->deadline - now;
  }
  return 0;
}

static int32_t arduinoSamD21x18ACancelTimerImpl(int32_t deviceId) {
  // Always validate capabilities first.
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor != NULL) {
    if ((processDescriptor->privilegeLevel != PRIVILEGE_LEVEL_KERNEL)
      && (findHalCapabilityWithDevice(processDescriptor->halCapabilities,
        processDescriptor->numHalCapabilities, HAL_TIMER, HAL_TIMER_CANCEL,
        deviceId) == NULL)
    ) {
      return -EACCES;
    }
  }

  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
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

int32_t arduinoSamD21x18ACancelTimer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  return arduinoSamD21x18ACancelTimerImpl(deviceId);
}

int32_t arduinoSamD21x18ACancelAndGetTimer(va_list args) {
  // We need to get `now` as close to the beginning of this function call as
  // possible so that any call to reconfigure the timer later is correct.
  int64_t nowUs = micros();
  int64_t now = nowUs * ((int64_t) 1000);

  int32_t deviceId = va_arg(args, int32_t);
  uint64_t *configuredNanoseconds = va_arg(args, uint64_t*);
  uint64_t *remainingNanoseconds = va_arg(args, uint64_t*);
  void (**callback)(void) = va_arg(args, void (**)(void));

  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    // We cannot populate the provided pointers, so we will error here.  This
    // also signals to the caller that there's no need to call configTimer
    // later.
    return -EINVAL;
  }

  // ***DO NOT*** call arduinoSamD21x18ACancelTimer.  It's expected that
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

/// @fn void arduinoSamD21x18ATimerInterruptHandler(int32_t deviceId)
///
/// @brief Base implementation for the Timer/Counter interrupt handlers.
///
/// @param deviceId The zero-based ID of the timer to handle.
///
/// @return This function returns no value.
void arduinoSamD21x18ATimerInterruptHandler(int32_t deviceId) {
  // This function is only called from one of the real interrupt handlers, so
  // we're guaranteed that the timer parameter is good.  Skip validation.
  HardwareTimer *hwTimer = &hardwareTimers[deviceId];

  hwTimer->active = false;
  hwTimer->startTime = 0;
  hwTimer->deadline = 0;

  // Call callback if set
  if (hwTimer->callback) {
    hwTimer->callback();
  }
}

/// @fn void arduinoSamD21x18ATimerInterruptHandler0()
///
/// @brief Call arduinoSamD21x18ATimerInterruptHandler with a value of 0.  This
/// effectively replaces TC3_Handler.
///
/// @return This function returns no value.
void arduinoSamD21x18ATimerInterruptHandler0() {
  SAVE_CONTEXT();
  arduinoSamD21x18ATimerInterruptHandler(0);
  RESTORE_CONTEXT();
}

/// @fn void arduinoSamD21x18ATimerInterruptHandler1()
///
/// @brief Call arduinoSamD21x18ATimerInterruptHandler with a value of 1.  This
/// effectively replaces TC4_Handler.
///
/// @return This function returns no value.
void arduinoSamD21x18ATimerInterruptHandler1() {
  SAVE_CONTEXT();
  arduinoSamD21x18ATimerInterruptHandler(1);
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

/// @var halArduinoSamD21x18ABlockDevicesOnline
///
/// @brief Bitmask array of online block devices.
static uint32_t halArduinoSamD21x18ABlockDevicesOnline[] = {
  0x00000000,
};

/// @var halArduinoSamD21x18AUartsOnline
///
/// @brief Placeholder; actual online arrays come from the per-board init args.
static uint32_t *halArduinoSamD21x18AUartsOnline = NULL;

/// @var halArduinoSamD21x18ADiosOnline
///
/// @brief Placeholder; actual online arrays come from the per-board init args.
static uint32_t *halArduinoSamD21x18ADiosOnline = NULL;

/// @var halArduinoSamD21x18ASpisOnline
///
/// @brief Bitmask array of online SPIs.
static uint32_t halArduinoSamD21x18ASpisOnline[] = {
  0x00000003,
};

int32_t arduinoSamD21x18AInitBlockDevice(va_list args) {
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

int32_t arduinoSamD21x18AGetBlockDevice(va_list args) {
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

int32_t arduinoSamD21x18ARestartBlockDevice(va_list args) {
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

static HalFunction arduinoSamD21x18AMemoryFunctions[HAL_MEMORY_NUM_FNS] = {
  [HAL_MEMORY_PROCESS_STACK_SIZE]         = arduinoSamD21x18AProcessStackSize,
  [HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = arduinoSamD21x18AMemoryManagerStackSize,
  [HAL_MEMORY_BOTTOM_OF_HEAP]             = arduinoSamD21x18ABottomOfHeap,
  [HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = arduinoSamD21x18ANumExtraSchedulerStacks,
  [HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = arduinoSamD21x18ANumExtraConsoleStacks,
};

static HalFunction arduinoSamD21x18AUartFunctions[HAL_UART_NUM_FNS] = {
  [HAL_UART_INIT]       = arduinoSamD21x18AInitUart,
  [HAL_UART_CONFIGURE]  = arduinoSamD21x18AConfigureUart,
  [HAL_UART_POLL]       = arduinoSamD21x18APollUart,
  [HAL_UART_WRITE]      = arduinoSamD21x18AWriteUart,
  [HAL_UART_IS_CONSOLE] = arduinoSamD21x18AIsUartConsole,
};

static HalFunction arduinoSamD21x18ADioFunctions[HAL_DIO_NUM_FNS] = {
  [HAL_DIO_INIT]      = arduinoSamD21x18AInitDio,
  [HAL_DIO_CONFIGURE] = arduinoSamD21x18AConfigureDio,
  [HAL_DIO_WRITE]     = arduinoSamD21x18AWriteDio,
};

static HalFunction arduinoSamD21x18ASpiFunctions[HAL_SPI_NUM_FNS] = {
  [HAL_SPI_INIT]           = arduinoSamD21x18AInitSpi,
  [HAL_SPI_CONFIGURE]      = arduinoSamD21x18AConfigureSpi,
  [HAL_SPI_START_TRANSFER] = arduinoSamD21x18AStartSpiTransfer,
  [HAL_SPI_END_TRANSFER]   = arduinoSamD21x18AEndSpiTransfer,
  [HAL_SPI_TRANSFER8]      = arduinoSamD21x18ASpiTransfer8,
  [HAL_SPI_TRANSFER_BYTES] = arduinoSamD21x18ASpiTransferBytes,
};

static HalFunction arduinoSamD21x18AClockFunctions[HAL_CLOCK_NUM_FNS] = {
  [HAL_CLOCK_INIT]                     = arduinoSamD21x18ATimeInit,
  [HAL_CLOCK_SET_SYSTEM_TIME]          = arduinoSamD21x18ASetSystemTime,
  [HAL_CLOCK_GET_ELAPSED_MILLISECONDS] = arduinoSamD21x18AGetElapsedMilliseconds,
  [HAL_CLOCK_GET_ELAPSED_MICROSECONDS] = arduinoSamD21x18AGetElapsedMicroseconds,
  [HAL_CLOCK_GET_ELAPSED_NANOSECONDS]  = arduinoSamD21x18AGetElapsedNanoseconds,
};

static HalFunction arduinoSamD21x18APowerFunctions[HAL_POWER_NUM_FNS] = {
  [HAL_POWER_ENTER_MODE] = arduinoSamD21x18AEnterMode,
};

static HalFunction arduinoSamD21x18ATimerFunctions[HAL_TIMER_NUM_FNS] = {
  [HAL_TIMER_INIT]                   = arduinoSamD21x18AInitTimer,
  [HAL_TIMER_INIT_DEVICE]            = arduinoSamD21x18AInitTimerDevice,
  [HAL_TIMER_CONFIG_ONE_SHOT]        = arduinoSamD21x18AConfigOneShotTimer,
  [HAL_TIMER_CONFIGURED_NANOSECONDS] = arduinoSamD21x18AConfiguredTimerNanoseconds,
  [HAL_TIMER_REMAINING_NANOSECONDS]  = arduinoSamD21x18ARemainingTimerNanoseconds,
  [HAL_TIMER_CANCEL]                 = arduinoSamD21x18ACancelTimer,
  [HAL_TIMER_CANCEL_AND_GET]         = arduinoSamD21x18ACancelAndGetTimer,
};

static HalFunction arduinoSamD21x18ABlockDeviceFunctions[HAL_BLOCK_DEVICE_NUM_FNS] = {
  [HAL_BLOCK_DEVICE_INIT]    = arduinoSamD21x18AInitBlockDevice,
  [HAL_BLOCK_DEVICE_GET]     = arduinoSamD21x18AGetBlockDevice,
  [HAL_BLOCK_DEVICE_RESTART] = arduinoSamD21x18ARestartBlockDevice,
};

int32_t halArduinoSamD21x18AInit(HalArduinoSamD21x18AInitArgs *args) {
  // Wire up per-subsystem function arrays.
  halFunctions[HAL_MEMORY]       = arduinoSamD21x18AMemoryFunctions;
  halFunctions[HAL_UART]         = arduinoSamD21x18AUartFunctions;
  halFunctions[HAL_DIO]          = arduinoSamD21x18ADioFunctions;
  halFunctions[HAL_SPI]          = arduinoSamD21x18ASpiFunctions;
  halFunctions[HAL_CLOCK]        = arduinoSamD21x18AClockFunctions;
  halFunctions[HAL_POWER]        = arduinoSamD21x18APowerFunctions;
  halFunctions[HAL_TIMER]        = arduinoSamD21x18ATimerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = arduinoSamD21x18ABlockDeviceFunctions;

  // Set per-platform data members from the init args.
  _spiCopiDio          = args->spiCopiDio;
  _spiCipoDio          = args->spiCipoDio;
  _spiSckDio           = args->spiSckDio;
  _sdCardPinChipSelect = args->sdCardPinChipSelect;

  halArduinoSamD21x18AUartsOnline = args->uartsOnline;
  halArduinoSamD21x18ADiosOnline  = args->diosOnline;

  halCommonMemory.overlayMap  = (NanoOsOverlayMap*) OVERLAY_ADDRESS;
  halCommonMemory.overlaySize = OVERLAY_SIZE;

  halCommonUart.numSupported = args->numUartsSupported;
  halCommonUart.online       = args->uartsOnline;

  halCommonDio.numSupported = args->numDiosSupported;
  halCommonDio.online       = args->diosOnline;

  halCommonSpi.numSupported = MAX_SPI_DEVICES;
  halCommonSpi.online       = halArduinoSamD21x18ASpisOnline;

  halCommonTimer.numSupported = _numTimers;
  halCommonTimer.online       = halArduinoSamD21x18ATimersOnline;

  halCommonBlockDevice.numSupported = _numBlockDevices;
  halCommonBlockDevice.online       = halArduinoSamD21x18ABlockDevicesOnline;

  extern char __bss_end__;
  if (((uintptr_t) &__bss_end__)
    > ((uintptr_t) halCommonMemory.overlayMap)
  ) {
    int stackPosition = 0;
    Serial.begin(1000000);
    while (!Serial);
    Serial.print("ERROR!!! 0x");
    Serial.print((uintptr_t) &__bss_end__, HEX);
    Serial.print(" > 0x");
    Serial.print((uintptr_t) halCommonMemory.overlayMap, HEX);
    Serial.print("\n");
    Serial.print("Stack position = 0x");
    Serial.print((uintptr_t) &stackPosition, HEX);
    Serial.print("\n");
    Serial.print("*******************************************************\n");
    Serial.print("* Running user programs will corrupt system memory!!! *\n");
    Serial.print("*******************************************************\n");
  }

  __enable_irq();  // Ensure global interrupts are enabled

  return halCommonInit();
}

#endif // defined(__SAMD21G18A__) || defined(__SAMD21E18A__)
