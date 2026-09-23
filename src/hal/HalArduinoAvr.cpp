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
// See src/include/sys/types.h: clear the guard flags Arduino.h's (renamed)
// typedefs above set, so that header's own gid_t/uid_t/pid_t, reached later
// in this file, don't wrongly defer to them.
#undef __gid_t_defined
#undef _GID_T_DECLARED
#undef __uid_t_defined
#undef _UID_T_DECLARED
#undef __pid_t_defined
#undef _PID_T_DECLARED

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

/// @def PROCESS_STACK_SIZE
///
/// @brief The size, in bytes, of a regular process's stack.
#define PROCESS_STACK_SIZE 320

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief The size, in bytes, of the memory manager process's stack.
#define MEMORY_MANAGER_STACK_SIZE 128

/// @def NUM_PROCESSES
///
/// @brief Value to indicate the maximum number of processes that can be run
/// concurrently.
#define NUM_PROCESSES 9

/// @var _dioStart
///
/// @brief The first DIO pin number that's usable on the board.
static uint8_t _dioStart = 0;

/// @var _numDioPins
///
/// @brief The number of digital IO pins on the board.
static uint32_t _numDioPins = 0;

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

/// @struct HalProcessQueue
///
/// @brief Structure to manage an individual process queue.  This is the
/// implementation that backs the ProcessQueue structure in the kernel.
///
/// @param name The string name of the queue for use in error messages.
/// @param head The index of the head of the queue.
/// @param tail The index of the tail of the queue.
/// @param numElements The number of elements currently in the queue.
/// @param processes The array of pointers to ProcessDescriptors from the
///   allProcesses array.  This is a variable-length array in the kernel.  It's
///   declared with definitive size here since this is the actual
///   implementationn backing.
typedef struct HalProcessQueue {
  const char        *name;
  uint8_t            head;
  uint8_t            tail;
  uint8_t            numElements;
  ProcessDescriptor *processes[NUM_PROCESSES];
} HalProcessQueue;

/// @var _kernelReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_KERNEL ready
/// queue.
static HalProcessQueue _kernelReadyQueue;

/// @var _executiveReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_EXECUTIVE ready
/// queue.
static HalProcessQueue _executiveReadyQueue;

/// @var _supervisorReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_SUPERVISOR ready
/// queue.
static HalProcessQueue _supervisorReadyQueue;

/// @var _userReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_USER ready queue.
static HalProcessQueue _userReadyQueue;

/// @var _readyQueues
///
/// @brief HAL implementation backing for the ready queues.
static ProcessQueue *_readyQueues[NUM_READY_QUEUES] = {
  (ProcessQueue*) &_kernelReadyQueue,
  (ProcessQueue*) &_executiveReadyQueue,
  (ProcessQueue*) &_supervisorReadyQueue,
  (ProcessQueue*) &_userReadyQueue,
};

/// @var _waitingQueue
///
/// @brief HAL implementation backing for the waiting queue.
static HalProcessQueue _waitingQueue;

/// @var _timedWaitingQueue
///
/// @brief HAL implementation backing for the timed waiting queue.
static HalProcessQueue _timedWaitingQueue;

/// @var _freeQueue
///
/// @brief HAL implementation backing for the free queue.
static HalProcessQueue _freeQueue;

/// @var _processErrorNumbers
///
/// @brief Process-specific storage for each process's errno value.
static int _processErrorNumbers[NUM_PROCESSES + 1];

/// @var _processStorageBase
///
/// @brief File-local, first-level variable to hold the per-process storage.
static void *_processStorageBase[NUM_PROCESSES][NUM_PROCESS_STORAGE_KEYS];

/// @var _processStorage
///
/// @brief File-local, second-level variable to hold the per-process storage.
static void **_processStorage[NUM_PROCESSES];

/// @var _logBuffer
///
/// @brief Statically allocated buffer for formatting log messages.
static char _logBuffer[96];

/// @def NUM_LOG_ENTRIES
///
/// @brief The number of LogEntry objects held in our local array.
#define NUM_LOG_ENTRIES 3

/// @var _logEntries
///
/// @brief Local array of LogEntry objects to use in communication with the
/// logger process.
static LogEntry _logEntries[NUM_LOG_ENTRIES];

/// @var _logMessages
///
/// @brief Private pool of ProcessMessage objects used to deliver log entries to
/// the logger process.  One per _logEntries slot.
static ProcessMessage _logMessages[NUM_LOG_ENTRIES];

/// @var _allProcesses
///
/// @brief Statically allocated buffer of ProcessDescriptors to hold the
/// metadata for all processes on the system, including the scheduler.
static ProcessDescriptor _allProcesses[NUM_PROCESSES];

/// @var _namedProcesses
///
/// @brief This platform's storage for HalCommon.c's named-process lookup
/// table.  Sized for just the filesystem: AVR boards always leave
/// stringsPresent true, so arduinoAvrDoStartProcesses never starts a
/// logger, and data-segment space here is scarce.
static NamedProcessEntry _namedProcesses[1];

/// @var _filesystemIpcCapabilities
///
/// @brief This platform's storage for the filesystem process's IPC
/// capabilities.  See FILESYSTEM_IPC_CAPABILITIES_INITIALIZER.  No
/// _loggerIpcCapabilities here: AVR boards always leave stringsPresent
/// true, so arduinoAvrDoStartProcesses never starts a logger.
static IpcCapability _filesystemIpcCapabilities[]
  = FILESYSTEM_IPC_CAPABILITIES_INITIALIZER;

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

int arduinoAvrProcessStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = PROCESS_STACK_SIZE;
  }
  return 0;
}

int arduinoAvrMemoryManagerStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  if (returnValue != NULL) {
    *returnValue = (debug == false)
      ? MEMORY_MANAGER_STACK_SIZE
      : MEMORY_MANAGER_DEBUG_STACK_SIZE;
  }
  return 0;
}

int arduinoAvrBottomOfHeap(va_list args) {
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

int arduinoAvrNumExtraSchedulerStacks(va_list args) {
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = 1;
  }
  return 0;
}

int arduinoAvrNumExtraConsoleStacks(va_list args) {
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

int arduinoAvrInitUart(va_list args) {
  (void) args;
  return 0;
}

int arduinoAvrConfigureUart(va_list args) {
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

int arduinoAvrPollUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  int serialData = -ERANGE;

  if ((deviceId >= 0) && (deviceId < _numUarts)) {
    serialData = uarts[deviceId]->read();
  }

  return serialData;
}

int arduinoAvrWriteUart(va_list args) {
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

int arduinoAvrIsUartConsole(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool *returnValue = va_arg(args, bool*);
  (void) deviceId;
  if (returnValue != NULL) {
    *returnValue = true;
  }
  return 0;
}

int arduinoAvrInitDio(va_list args) {
  (void) args;
  return 0;
}

// @fn int arduinoAvrConfigureDioImpl(int32_t deviceId, bool output)
//
// Not static: the Mega 2560's SPI implementation (HalArduinoMega2560.cpp)
// needs to drive its own chip-select line directly, bypassing the
// capability-gated HAL_DIO dispatch (SPI chip-select is treated as part of
// what "SPI" means at the hardware level, not a generic DIO operation a
// process needs its own HAL_DIO grant for).  Declared in HalArduinoAvr.h.
int arduinoAvrConfigureDioImpl(int32_t deviceId, bool output) {
  if ((deviceId < _dioStart) || (deviceId >= (int32_t) _numDioPins)) {
    return -ERANGE;
  }
  uint8_t modes[2] = { INPUT, OUTPUT };
  pinMode(deviceId, modes[output]);
  return 0;
}

int arduinoAvrConfigureDio(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool output = (bool) va_arg(args, int);
  return arduinoAvrConfigureDioImpl(deviceId, output);
}

// See arduinoAvrConfigureDioImpl above for why this isn't static.
int arduinoAvrWriteDioImpl(int32_t deviceId, bool high) {
  if ((deviceId < _dioStart) || (deviceId >= (int32_t) _numDioPins)) {
    return -ERANGE;
  }
  uint8_t levels[2] = { LOW, HIGH };
  digitalWrite(deviceId, levels[high]);
  return 0;
}

int arduinoAvrWriteDio(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool high = (bool) va_arg(args, int);
  return arduinoAvrWriteDioImpl(deviceId, high);
}

/// @var baseSystemTimeMs
///
/// @brief The time provided by the user or some other process as a baseline
/// time for the system.
static int64_t baseSystemTimeMs = 0;

int arduinoAvrTimeInit(va_list args) {
  (void) args;
  return 0;
}

int arduinoAvrSetSystemTime(va_list args) {
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

int arduinoAvrGetElapsedMilliseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t result = arduinoAvrGetElapsedMillisecondsImpl(startTime);
  if (returnValue != NULL) {
    *returnValue = result;
  }
  return (result >= 0) ? 0 : -EIO;
}

int arduinoAvrGetElapsedMicroseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t result = arduinoAvrGetElapsedMillisecondsImpl(
    startTime / ((int64_t) 1000)) * ((int64_t) 1000);
  if (returnValue != NULL) {
    if (result >= 0) {
      *returnValue = result;
    } else {
      *returnValue = -1;
    }
  }
  return (result >= 0) ? 0 : -EIO;
}

int arduinoAvrGetElapsedNanoseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t result = arduinoAvrGetElapsedMillisecondsImpl(
    startTime / ((int64_t) 1000000)) * ((int64_t) 1000000);
  if (returnValue != NULL) {
    if (result >= 0) {
      *returnValue = result;
    } else {
      *returnValue = -1;
    }
  }
  return (result >= 0) ? 0 : -EIO;
}

int arduinoAvrEnterPowerMode(va_list args) {
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
// arduinoAvrRestartBlockDevice are defined in the individual implementations
// (HalArduinoMega2560.cpp / HalArduinoNanoEvery.c); declared with C linkage
// in HalArduinoAvr.h.

/// @var _initRootStorage
///
/// @brief Board-specific initRootStorage implementation, set via
/// arduinoAvrSetRootStorageFunctions.  NULL on boards with no root storage.
static HalInitRootStorageFn _initRootStorage = NULL;

/// @var _restartRootFilesystem
///
/// @brief Board-specific restartRootFilesystem implementation, set via
/// arduinoAvrSetRootStorageFunctions.  NULL on boards with no root
/// filesystem.
static HalRestartRootFilesystemFn _restartRootFilesystem = NULL;

void arduinoAvrSetRootStorageFunctions(
  HalInitRootStorageFn initRootStorage,
  HalRestartRootFilesystemFn restartRootFilesystem
) {
  _initRootStorage = initRootStorage;
  _restartRootFilesystem = restartRootFilesystem;
}

int arduinoAvrExecCommand(va_list args) {
  HalExecCommandFn *returnValue = va_arg(args, HalExecCommandFn*);
  if (returnValue != NULL) {
    *returnValue = execBuiltinCommand;
  }
  return 0;
}

/// @fn static int arduinoAvrDoStartProcesses(void)
///
/// @brief Start every process specific to this AVR board: its root storage,
/// if the board set one via arduinoAvrSetRootStorageFunctions, plus the
/// logger if this build's .rodata was stripped.
///
/// @return Returns 0 on success, -errno on failure.
static int arduinoAvrDoStartProcesses(void) {
  int returnValue = 0;
  if (_initRootStorage != NULL) {
    returnValue = _initRootStorage();
  }
  if (HAL->memory->stringsPresent == false) {
    int loggerStatus = halCommonInitLogger();
    if ((returnValue == 0) && (loggerStatus != 0)) {
      returnValue = loggerStatus;
    }
  }
  return returnValue;
}

int arduinoAvrStartProcesses(va_list args) {
  HalStartProcessesFn *returnValue = va_arg(args, HalStartProcessesFn*);
  if (returnValue != NULL) {
    *returnValue = arduinoAvrDoStartProcesses;
  }
  return 0;
}

int arduinoAvrRestartRootFilesystem(va_list args) {
  HalRestartRootFilesystemFn *returnValue
    = va_arg(args, HalRestartRootFilesystemFn*);
  if (returnValue != NULL) {
    *returnValue = _restartRootFilesystem;
  }
  return 0;
}

int arduinoAvrRestartShell(va_list args) {
  HalRestartShellFn *returnValue = va_arg(args, HalRestartShellFn*);
  if (returnValue != NULL) {
    *returnValue = restartBuiltinShell;
  }
  return 0;
}

int arduinoAvrLogBuffer(va_list args) {
  char **returnValue = va_arg(args, char**);
  if (returnValue != NULL) {
    *returnValue = _logBuffer;
  }
  return 0;
}

int arduinoAvrLogEntries(va_list args) {
  LogEntry **returnValue = va_arg(args, LogEntry**);
  if (returnValue != NULL) {
    *returnValue = _logEntries;
  }
  return 0;
}

int arduinoAvrLogMessages(va_list args) {
  ProcessMessage **returnValue = va_arg(args, ProcessMessage**);
  if (returnValue != NULL) {
    *returnValue = _logMessages;
  }
  return 0;
}

int arduinoAvrAllProcesses(va_list args) {
  ProcessDescriptor **returnValue = va_arg(args, ProcessDescriptor**);
  if (returnValue != NULL) {
    *returnValue = _allProcesses;
  }
  return 0;
}

int arduinoAvrReadyQueues(va_list args) {
  ProcessQueue ***returnValue = va_arg(args, ProcessQueue***);
  if (returnValue != NULL) {
    *returnValue = _readyQueues;
  }
  return 0;
}

int arduinoAvrWaitingQueue(va_list args) {
  ProcessQueue **returnValue = va_arg(args, ProcessQueue**);
  if (returnValue != NULL) {
    *returnValue = (ProcessQueue*) &_waitingQueue;
  }
  return 0;
}

int arduinoAvrTimedWaitingQueue(va_list args) {
  ProcessQueue **returnValue = va_arg(args, ProcessQueue**);
  if (returnValue != NULL) {
    *returnValue = (ProcessQueue*) &_timedWaitingQueue;
  }
  return 0;
}

int arduinoAvrFreeQueue(va_list args) {
  ProcessQueue **returnValue = va_arg(args, ProcessQueue**);
  if (returnValue != NULL) {
    *returnValue = (ProcessQueue*) &_freeQueue;
  }
  return 0;
}

int arduinoAvrProcessErrorNumbers(va_list args) {
  int **returnValue = va_arg(args, int**);
  if (returnValue != NULL) {
    *returnValue = _processErrorNumbers;
  }
  return 0;
}

int arduinoAvrProcessStorage(va_list args) {
  void ****returnValue = va_arg(args, void****);
  if (returnValue != NULL) {
    *returnValue = _processStorage;
  }
  return 0;
}

// NOTE: avr-g++ (unlike gcc) cannot compile a designated-initializer array
// with gaps ("sorry, unimplemented: non-trivial designated initializers not
// supported"), so every enum index must be listed in order, even the ones
// this platform leaves NULL.
static HalFunction arduinoAvrPlatformFunctions[HAL_PLATFORM_NUM_FNS] = {
  [HAL_PLATFORM_CALL_FILE_OVERLAY]       = NULL,
  [HAL_PLATFORM_EXEC_COMMAND]            = arduinoAvrExecCommand,
  [HAL_PLATFORM_RESTART_ROOT_FILESYSTEM] = arduinoAvrRestartRootFilesystem,
  [HAL_PLATFORM_RESTART_SHELL]           = arduinoAvrRestartShell,
  [HAL_PLATFORM_START_PROCESSES]         = arduinoAvrStartProcesses,
};

static HalFunction arduinoAvrMemoryFunctions[HAL_MEMORY_NUM_FNS] = {
  [HAL_MEMORY_PROCESS_STACK_SIZE]         = arduinoAvrProcessStackSize,
  [HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = arduinoAvrMemoryManagerStackSize,
  [HAL_MEMORY_BOTTOM_OF_HEAP]             = arduinoAvrBottomOfHeap,
  [HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = arduinoAvrNumExtraSchedulerStacks,
  [HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = arduinoAvrNumExtraConsoleStacks,
  [HAL_MEMORY_OVERLAY_MAP]                = NULL,
  [HAL_MEMORY_CONTIGUOUS_FILESYSTEM]      = NULL,
  [HAL_MEMORY_STATIC_LOGS]                = NULL,
  [HAL_MEMORY_LOG_BUFFER]                 = arduinoAvrLogBuffer,
  [HAL_MEMORY_LOG_ENTRIES]                = arduinoAvrLogEntries,
  [HAL_MEMORY_LOG_MESSAGES]               = arduinoAvrLogMessages,
  [HAL_MEMORY_ALL_PROCESSES]              = arduinoAvrAllProcesses,
  [HAL_MEMORY_READY_QUEUES]               = arduinoAvrReadyQueues,
  [HAL_MEMORY_WAITING_QUEUE]              = arduinoAvrWaitingQueue,
  [HAL_MEMORY_TIMED_WAITING_QUEUE]        = arduinoAvrTimedWaitingQueue,
  [HAL_MEMORY_FREE_QUEUE]                 = arduinoAvrFreeQueue,
  [HAL_MEMORY_PROCESS_ERROR_NUMBERS]      = arduinoAvrProcessErrorNumbers,
  [HAL_MEMORY_PROCESS_STORAGE]            = arduinoAvrProcessStorage,
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

// No arduinoAvrSpiFunctions table here: the SPI implementation (and its
// HAL_SPI dispatch table) lives entirely in HalArduinoMega2560.cpp now,
// since the Nano Every has no SPI bus wired to anything -- see
// HalArduinoNanoEvery.c's halArduinoInit(), which leaves halImpl.spi NULL.

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

// We want to link in the built-in filesystem and FAT32 implementation, so
// provide those declarations here.  Only the Mega 2560 (via
// HalArduinoMega2560.cpp) actually wires restartRootFilesystem to
// restartBuiltinFilesystem; the Nano Every never does, so these are simply
// unused there -- harmless, since NANO_OS_NO_BUILTIN_FILESYSTEM isn't
// defined for either AVR target (see NanoOsBuiltinFilesystem.h), so both
// still link the driver in.
#ifdef __cplusplus
extern "C"
{
#endif
extern FilesystemState* filesystemInitDriver(FilesystemState
 *filesystemState);
extern const FilesystemCommandHandler
  fat32CommandHandlers[NUM_FILESYSTEM_COMMANDS];
#ifdef __cplusplus
}
#endif

int halArduinoAvrInit(HalArduinoAvrInitArgs *args) {
  // Wire up per-subsystem function arrays.
  // HAL_TIMER is not supported on this platform — leave halFunctions[HAL_TIMER] NULL.
  // HAL_SPI is not wired here at all: only the Mega 2560 has an SPI bus
  // wired to anything, and it wires its own HAL_SPI table (and
  // halImpl.spi's data members) itself, in HalArduinoMega2560.cpp, before
  // calling this function.  The Nano Every leaves halImpl.spi NULL.
  halFunctions[HAL_PLATFORM]     = arduinoAvrPlatformFunctions;
  halFunctions[HAL_MEMORY]       = arduinoAvrMemoryFunctions;
  halFunctions[HAL_UART]         = arduinoAvrUartFunctions;
  halFunctions[HAL_DIO]          = arduinoAvrDioFunctions;
  halFunctions[HAL_CLOCK]        = arduinoAvrClockFunctions;
  halFunctions[HAL_POWER]        = arduinoAvrPowerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = arduinoAvrBlockDeviceFunctions;

  // Set per-platform data members from the init args.
  _dioStart   = args->dioStart;
  _numDioPins = args->numDiosSupported;

  halImpl.uart->numSupported = args->numUartsSupported;
  halImpl.uart->online       = args->uartsOnline;

  halImpl.dio->numSupported = args->numDiosSupported;
  halImpl.dio->online       = args->diosOnline;

  halImpl.timer->numSupported = 0;
  halImpl.timer->online       = NULL;

  halImpl.blockDevice->numSupported = _numBlockDevices;
  halImpl.blockDevice->online       = arduinoAvrBlockDevicesOnline;

  halImpl.memory->stringsPresent = true;
  halImpl.memory->logBufferSize  = sizeof(_logBuffer);
  halImpl.memory->numLogEntries  = NUM_LOG_ENTRIES;
  memset(&_logEntries, 0, sizeof(_logEntries));
  memset(&_logMessages, 0, sizeof(_logMessages));

  memset(_namedProcesses, 0, sizeof(_namedProcesses));
  namedProcessTable = _namedProcesses;
  namedProcessTableCapacity
    = sizeof(_namedProcesses) / sizeof(_namedProcesses[0]);

  filesystemIpcCapabilities = _filesystemIpcCapabilities;
  numFilesystemIpcCapabilities
    = sizeof(_filesystemIpcCapabilities) / sizeof(_filesystemIpcCapabilities[0]);

  memset(_allProcesses, 0, sizeof(_allProcesses));
  halImpl.memory->numProcesses   = NUM_PROCESSES;
  for (int ii = 0; ii < NUM_READY_QUEUES; ii++) {
    memset(_readyQueues[ii], 0, sizeof(HalProcessQueue));
  }
  memset(&_waitingQueue, 0, sizeof(HalProcessQueue));
  memset(&_timedWaitingQueue, 0, sizeof(HalProcessQueue));
  memset(&_freeQueue, 0, sizeof(HalProcessQueue));
  memset(_processStorageBase, 0,
    NUM_PROCESSES * NUM_PROCESS_STORAGE_KEYS * sizeof(void*));
  for (int ii = 0; ii < NUM_PROCESSES; ii++) {
    _processStorage[ii] = _processStorageBase[ii];
  }

  return halCommonInit(
    /* builtinFilesystemInitDriver= */ filesystemInitDriver,
    /* builtinFilesystemCommandHandlers= */ fat32CommandHandlers
  );
}

#endif // defined(__AVR_ATmega4809__) || defined(__AVR_ATmega2560__)
