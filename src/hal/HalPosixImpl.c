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

/// @file HalPosix.cpp
///
/// @brief HAL implementation for a Posix simulator.

#ifdef __x86_64__

// Standard C includes from the compiler
#undef errno
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include </usr/include/time.h>
#include <termios.h>
#include <unistd.h>

#include "kernel/Hal.h"
#include "kernel/MemoryManager.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"

/// @def DEBUG_MULTIPLIER
///
/// @brief Multiplier to use for stack/heap size during debugging.  When not
/// debugging, this value should be 1.
#define DEBUG_MULTIPLIER 1

/// @def PROCESS_STACK_SIZE
///
/// @brief The size, in bytes, of a regular process's stack.
#define PROCESS_STACK_SIZE (4 * 1024)

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief The size, in bytes, of the memory manager process's stack.
#define MEMORY_MANAGER_STACK_SIZE 768

/// @def OVERLAY_BASE_ADDRESS
///
/// @brief This is the base address that we will use in our mmap call.  The
/// address has to be page aligned on Linux.  The address below should work
/// fine unless the host is using 1 GB pages.
#define OVERLAY_BASE_ADDRESS 0x20000000

/// @def OVERLAY_OFFSET
///
/// @brief This is the offset into the allocated and mapped memory that the
/// overlays will actually be loaded into.
#define OVERLAY_OFFSET           0x1800

/// @def OVERLAY_SIZE
///
/// @brief This is the size of the overlay that's permitted by the real
/// hardware.
#define OVERLAY_SIZE               8192

/// @def ELAST
///
/// @brief The highest errno value defined.  Missing from Linux's implementation
/// of errno.h.  (It's a BSD thing...)
#define ELAST                  EHWPOISON

// Defined in Scheduler.c
extern SchedulerState *SCHEDULER_STATE;

int (*realTcgetattr)(int fd, struct termios *termios_p) = NULL;
int (*realTcsetattr)(int fd, int optional_actions,
  const struct termios *termios_p) = NULL;

int32_t posixProcessStackSize(bool debug, size_t *returnValue) {
  (void) debug;
  *returnValue = PROCESS_STACK_SIZE * DEBUG_MULTIPLIER;
  return 0;
}

int32_t posixMemoryManagerStackSize(bool debug, size_t *returnValue) {
  if (debug == false) {
    // This is the expected case, so list it first.
    *returnValue = MEMORY_MANAGER_STACK_SIZE * DEBUG_MULTIPLIER;
  } else {
    *returnValue = MEMORY_MANAGER_DEBUG_STACK_SIZE * DEBUG_MULTIPLIER;
  }
  return 0;
}

/// @var _bottomOfHeap
///
/// @brief Where the bottom of the heap will be set to be in memory.
static void *_bottomOfHeap = NULL;

int32_t posixBottomOfHeap(bool debug, void **returnValue) {
  (void) debug;
  *returnValue = _bottomOfHeap;
  return 0;
}

int32_t posixNumExtraSchedulerStacks(bool debug, uint8_t *returnValue) {
  (void) debug;
  *returnValue = 0;
  return 0;
}

int32_t posixNumExtraConsoleStacks(bool debug, uint8_t *returnValue) {
  (void) debug;
  *returnValue = 1;
  return 0;
}

/// @var uarts
///
/// @brief Array of serial ports on the system.  Index 0 is the main port,
/// which is the USB serial port.
static FILE **uarts[] = {
  &stderr,
  &stderr,
};

/// @var _numUarts
///
/// @brief The number of serial ports we support on the Arduino Nano 33 IoT.
static int _numUarts = sizeof(uarts) / sizeof(uarts[0]);

int32_t posixInitUart(void) {
  return 0;
}

int32_t posixConfigureUart(int32_t deviceId, uint32_t baud) {
  (void) baud;
  
  if (deviceId > 1) {
    return -ERANGE;
  } else if (deviceId != 1) {
    return -ENOTTY;
  }
  
  // We don't actually need to do anything to stdout or stderr, but we do need
  // to configure stdin to be non-blocking.
  if (fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK) != 0) {
    return -errno;
  }
  
  // We manage all the prints to screen ourselves, so disable stdin echoing
  // as well.
  struct termios oldFlags = {0};
  struct termios newFlags = {0};
  
  // Get the current console flags.
  int stdinFileno = fileno(stdin);
  if (realTcgetattr(stdinFileno, &oldFlags) != 0) {
    fprintf(stderr, "Could not get current attributes for console.\n");
    return -errno;
  }

  // Disable echo to the console.
  newFlags = oldFlags;
  newFlags.c_lflag |= ECHONL;
  newFlags.c_lflag &= ~(ECHO | ICANON);
  if (realTcsetattr(stdinFileno, TCSANOW, &newFlags) != 0) {
    fprintf(stderr, "Could not set new attributes for console.\n");
    return -errno;
  }
  
  return 0;
}

int32_t posixPollUart(int32_t deviceId) {
  int serialData = -1;
  
  // While we'll support two outputs, we will only support one input to keep
  // things simple in the simulator.
  if (deviceId == 1) {
    serialData = getchar();
    if (serialData == EOF) {
      serialData = -1;
    }
  }
  
  return serialData;
}

int32_t posixWriteUart(int32_t deviceId,
  const uint8_t *data, ssize_t length, ssize_t *returnValue
) {
  ssize_t numBytesWritten = -ERANGE;

  if ((deviceId >= 0) && (deviceId < _numUarts) && (length >= 0)) {
    numBytesWritten = fwrite(data, 1, length, *uarts[deviceId]);
    fflush(*uarts[deviceId]);
  }

  if (returnValue != NULL) {
    *returnValue = numBytesWritten;
  }
  return (numBytesWritten >= 0) ? 0 : (int32_t) numBytesWritten;
}

int32_t posixIsUartConsole(int32_t deviceId, bool *returnValue) {
  if (returnValue != NULL) {
    *returnValue = (deviceId == 1);
  }
  return 0;
}

int posixGetNumDios(void) {
  return -ENOSYS;
}

int posixInitDio(void) {
  return -ENOSYS;
}

int32_t posixConfigureDio(int32_t deviceId, bool output) {
  (void) deviceId;
  (void) output;
  
  return -ENOSYS;
}

int32_t posixWriteDio(int32_t deviceId, bool high) {
  (void) deviceId;
  (void) high;
  
  return -ENOSYS;
}

int32_t posixInitSpi(void) {
  return -ENOSYS;
}

int32_t posixConfigureSpiDevice(int32_t deviceId,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud
) {
  (void) deviceId;
  (void) cs;
  (void) sck;
  (void) copi;
  (void) cipo;
  (void) baud;
  
  return -ENOSYS;
}

int32_t posixStartSpiTransfer(int32_t deviceId) {
  (void) deviceId;
  
  return -ENOSYS;
}

int32_t posixEndSpiTransfer(int32_t deviceId) {
  (void) deviceId;
  
  return -ENOSYS;
}

int32_t posixSpiTransfer8(int32_t deviceId, uint8_t data) {
  (void) deviceId;
  (void) data;
  
  return -ENOSYS;
}

int32_t posixSpiTransferBytes(int32_t deviceId,
  uint8_t *data, uint32_t length
) {
  (void) deviceId;
  (void) data;
  (void) length;
  
  return -ENOSYS;
}

int32_t posixTimeInit(void) {
  return 0;
}

int32_t posixSetSystemTime(struct timespec *now) {
  (void) now;
  
  return 0;
}

// posixGetElapsedNanoseconds is used as the base implementation, so declare
// its prototype here.
int32_t posixGetElapsedNanoseconds(int64_t startTime, int64_t *returnValue);

int32_t posixGetElapsedMilliseconds(int64_t startTime, int64_t *returnValue) {
  int64_t nanoseconds = 0;
  int32_t rv = posixGetElapsedNanoseconds(
    startTime * ((int64_t) 1000000), &nanoseconds);
  if (returnValue != NULL) {
    *returnValue = nanoseconds / ((int64_t) 1000000);
  }
  return rv;
}

int32_t posixGetElapsedMicroseconds(int64_t startTime, int64_t *returnValue) {
  int64_t nanoseconds = 0;
  int32_t rv = posixGetElapsedNanoseconds(
    startTime * ((int64_t) 1000), &nanoseconds);
  if (returnValue != NULL) {
    *returnValue = nanoseconds / ((int64_t) 1000);
  }
  return rv;
}

int32_t posixGetElapsedNanoseconds(int64_t startTime, int64_t *returnValue) {
  #include <time.h>
  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  if (returnValue != NULL) {
    *returnValue = ((((int64_t) spec.tv_sec) * ((int64_t) 1000000000))
      + ((int64_t) spec.tv_nsec)) - startTime;
  }
  return 0;
}

/// @var _resetBuffer
///
/// @brief Copy of the jmp_buf that was initialized prior to initializing the
/// HAL so that we can jump back to before hardware initialization in the
/// simulation.
static jmp_buf _resetBuffer;

int32_t posixEnterPowerMode(HalPowerMode powerMode) {
  // You can't completely turn off the hardware we're running on.  We're
  // simulating hardware, so do what the hardware would do, which is the same
  // set of operations for both off and suspend.
  if ((powerMode == HAL_POWER_MODE_OFF)
    || (powerMode == HAL_POWER_MODE_SUSPEND)
  ) {
    exit(0);
  } else if (powerMode == HAL_POWER_MODE_RESET) {
    // Unmap the overlay so that we can map it again when we reset.
    long pageSize = sysconf(_SC_PAGESIZE);
    size_t overlayBaseSize
      = ((size_t) (OVERLAY_OFFSET + OVERLAY_SIZE + (pageSize - 1)))
      & ~((size_t) (pageSize - 1));
    
    if (munmap((void*) OVERLAY_BASE_ADDRESS, overlayBaseSize) < 0) {
      fprintf(stderr, "ERROR: munmap returned: %s\n", strerror(errno));
      fprintf(stderr, "Exiting.\n");
      exit(1);
    }
    
    // Reset the block storage device online map so that initialization works
    // properly on reset.
    if (HAL->blockDevice != NULL) {
      HAL->blockDevice->online[0] = 0;
    }
    longjmp(_resetBuffer, 1);
  }
  
  return 0;
}

// Timer support

/// @var _mainThreadId
///
/// @brief The ID of the main thread that calls halPosixInit.
static pthread_t _mainThreadId = 0;

/// @struct SoftwareTimer
///
/// @brief Collection of variables needed to manage a software timer.
///
/// @param timerThread The pthread_t of the thread that is serving as the timer
///   if the timer is active.
/// @param signal The signal number that is to be sent to the main thread when
///   the timer expires.
/// @param signalHandler Function pointer to the signal handler function that
///   will be triggered on the main thread when the timer sends the signal.
/// @param initialized Whether or not the timer has been initialized yet.
/// @param callback The callback to call when the timer fires, if any.
/// @param active Whether or not the timer is currently active.
/// @param startTime The time, in nanoseconds, when the timer was configured.
/// @param deadline The time, in nanoseconds, when the timer expires.
typedef struct SoftwareTimer {
  pthread_t timerThread;
  int signal;
  void (*signalHandler)(int);
  bool initialized;
  void (*callback)(void);
  bool active;
  int64_t startTime;
  int64_t deadline;
} SoftwareTimer;

/// Forward declaration
extern SoftwareTimer softwareTimers[];

/// @fn void* timerThreadFunction(void *arg)
///
/// @brief pthread-compatible function to wait for a specified amount of time
/// before sending a signal to the main thread.  This function runs on its own
/// thread.
///
/// @param arg The index of the timer to use for configuration, cast to a
///   void*.
///
/// @return This function always succeeds if it completes and always returns
/// NULL.
void* timerThreadFunction(void *arg) {
  intptr_t timer = (intptr_t) arg;
  
  // We want to be able to kill this thread if we need to.
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  
  SoftwareTimer *swTimer = &softwareTimers[timer];
  int64_t delay = swTimer->deadline - swTimer->startTime;
  struct timespec ts = {
    .tv_sec = delay / ((int64_t) 1000000000),
    .tv_nsec = delay % ((int64_t) 1000000000),
  };
  nanosleep(&ts, NULL);
  if (swTimer->active) {
    // Send the specified signal to the main thread.
    pthread_kill(_mainThreadId, swTimer->signal);
  }
  
  return NULL;
}

/// @fn void timerSignalHandler(int timer)
///
/// @brief Main signal handler.  This function runs on the main thread.
///
/// @param timer Index of the timer in the softwareTimers array;
///
/// @return This function returns no value, but it does call the timer's
/// callback if one is set.
void timerSignalHandler(int timer) {
  SoftwareTimer *swTimer = &softwareTimers[timer];
  
  swTimer->active = false;
  swTimer->timerThread = 0;
  swTimer->startTime = 0;
  swTimer->deadline = 0;
  
  // Call callback if set
  if (swTimer->callback) {
    swTimer->callback();
  }
}

/// @fn void timer0SignalHandler(int signal)
///
/// @brief signal-compliant function to handle a signal serving as a timer
/// interrupt callback.
///
/// @param signal Integer value of the signal being raised.  Always SIGUSR1 in
///   this case.  The parameter is ignored by this function.
///
/// @return This function returns no value but invokes timerSignalHandler for
/// timer 0.
void timer0SignalHandler(int signal) {
  (void) signal; // We know this is SIGUSR1, so no need to check it.
  timerSignalHandler(0);
}

/// @fn void timer1SignalHandler(int signal)
///
/// @brief signal-compliant function to handle a signal serving as a timer
/// interrupt callback.
///
/// @param signal Integer value of the signal being raised.  Always SIGUSR2 in
///   this case.  The parameter is ignored by this function.
///
/// @return This function returns no value but invokes timerSignalHandler for
/// timer 1.
void timer1SignalHandler(int signal) {
  (void) signal; // We know this is SIGUSR2, so no need to check it.
  timerSignalHandler(1);
}

/// @var softwareTimers
///
/// @brief Array of SoftwareTimer objects managed by the HAL.
SoftwareTimer softwareTimers[] = {
  {
    .timerThread = 0,
    .signal = SIGUSR1,
    .signalHandler = timer0SignalHandler,
    .initialized = false,
    .callback = NULL,
    .active = false,
    .startTime = 0,
    .deadline = 0,
  },
  {
    .timerThread = 1,
    .signal = SIGUSR2,
    .signalHandler = timer1SignalHandler,
    .initialized = false,
    .callback = NULL,
    .active = false,
    .startTime = 0,
    .deadline = 0,
  },
};

/// @var _numTimers
///
/// @brief The number of timers returned by posixGetNumTimers.  This is
/// initialized to the number of timers supported, but may be overridden by a
/// call to posixSetNumTimers.
static int _numTimers = sizeof(softwareTimers) / sizeof(softwareTimers[0]);

int32_t posixInitTimer(void) {
  return 0;
}

int32_t posixInitTimerDevice(int32_t deviceId) {
  if (deviceId >= _numTimers) {
    return -ERANGE;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[deviceId];
  if (swTimer->initialized) {
    // Nothing to do
    return 0;
  }
  
  struct sigaction sa;
  sa.sa_handler = swTimer->signalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NODEFER | SA_RESTART;
  if (sigaction(swTimer->signal, &sa, NULL) < 0) {
    return -errno;
  }
  
  swTimer->initialized = true;
  
  return 0;
}

int32_t posixConfigOneShotTimer(int32_t deviceId,
    uint64_t nanoseconds, void (*callback)(void)
) {
  if (deviceId >= _numTimers) {
    return -ERANGE;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[deviceId];
  if (!swTimer->initialized) {
    return -EINVAL;
  }
  
  swTimer->callback = callback;
  swTimer->active = true;
  posixGetElapsedNanoseconds(0, &swTimer->startTime);
  swTimer->deadline = swTimer->startTime + nanoseconds;
  pthread_create(&swTimer->timerThread, NULL,
    timerThreadFunction, (void*) ((intptr_t) deviceId));
  pthread_detach(swTimer->timerThread);
  
  return 0;
}

int32_t posixConfiguredTimerNanoseconds(int32_t deviceId,
  uint64_t *returnValue
) {
  if (returnValue != NULL) {
    *returnValue = 0;
  }
  if (deviceId >= _numTimers) {
    return -ERANGE;
  }

  SoftwareTimer *swTimer = &softwareTimers[deviceId];
  if ((!swTimer->initialized) || (!swTimer->active)) {
    return -EINVAL;
  }

  if (returnValue != NULL) {
    *returnValue = swTimer->deadline - swTimer->startTime;
  }
  return 0;
}

int32_t posixRemainingTimerNanoseconds(int32_t deviceId,
  uint64_t *returnValue
) {
  if (returnValue != NULL) {
    *returnValue = 0;
  }
  if (deviceId >= _numTimers) {
    return -ERANGE;
  }

  SoftwareTimer *swTimer = &softwareTimers[deviceId];
  if ((!swTimer->initialized) || (!swTimer->active)) {
    return -EINVAL;
  }

  int64_t now = 0;
  posixGetElapsedNanoseconds(0, &now);
  if (now > swTimer->deadline) {
    return 0;
  }

  if (returnValue != NULL) {
    *returnValue = swTimer->deadline - now;
  }
  return 0;
}

int32_t posixCancelTimer(int32_t deviceId) {
  if (deviceId >= _numTimers) {
    return -ERANGE;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[deviceId];
  if (!swTimer->initialized) {
    return -EINVAL;
  }
  
  bool active = swTimer->active;
  swTimer->active = false;
  
  if (active) {
    pthread_cancel(swTimer->timerThread);
  }
  
  swTimer->timerThread = 0;
  swTimer->startTime = 0;
  swTimer->deadline = 0;
  swTimer->callback = NULL;
  
  return 0;
}

int32_t posixCancelAndGetTimer(int32_t deviceId,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void)
) {
  // We need to get `now` as close to the beginning of this function call as
  // possible so that any call to reconfigure the timer later is correct.
  int64_t now = 0;
  posixGetElapsedNanoseconds(0, &now);
  
  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[deviceId];
  if ((!swTimer->initialized) || (!swTimer->active)) {
    // We cannot populate the provided pointers, so we will error here.  This
    // also signals to the caller that there's no need to call configTimer
    // later.
    return -EINVAL;
  }
  
  // ***DO NOT*** call posixCancelTimer.  It's expected that this
  // function is in the critical path.  Time is of the essence, so inline the
  // logic.
  
  bool active = swTimer->active;
  swTimer->active = false;
  
  if (active) {
    pthread_cancel(swTimer->timerThread);
  }
  
  if (configuredNanoseconds != NULL) {
    if (swTimer->deadline > swTimer->startTime) {
      *configuredNanoseconds = swTimer->deadline - swTimer->startTime;
    } else {
      *configuredNanoseconds = 0;
    }
  }
  
  if (remainingNanoseconds != NULL) {
    if (now < swTimer->deadline) {
      *remainingNanoseconds = swTimer->deadline - now;
    } else {
      *remainingNanoseconds = 0;
    }
  }
  
  if (callback != NULL) {
    *callback = swTimer->callback;
  }
  
  swTimer->active = false;
  swTimer->startTime = 0;
  swTimer->deadline = 0;
  swTimer->callback = NULL;
  
  return 0;
}

/// @fn void returnToTop(jmp_buf returnBuffer, char *topOfStack)
///
/// @brief Return back to the top of the stack via a longjmp.  This function
/// is really pointless and exists only to make the compiler stop complaining
/// about infinite recursion.
///
/// @param returnBuffer A jmp_buf that contains the context we will jump back
///   to when the full stack has been allocated.
/// @param topOfStack A pointer to the beginning of the stack we allocate.
///
/// @return This function reeturns no value.
void returnToTop(jmp_buf returnBuffer, char *topOfStack) {
  longjmp(returnBuffer, (int) ((intptr_t) topOfStack));
}

/// @fn void allocateGlobalStack(jmp_buf returnBuffer, char *topOfStack)
///
/// @brief Allocate the full stack that the simulator will be able to use.
/// This forces the host OS's memory manager to allocate all the stack space
/// up front.
///
/// @param returnBuffer A jmp_buf that contains the context we will jump back
///   to when the full stack has been allocated.
/// @param topOfStack A pointer to the beginning of the stack we allocate.
///
/// @return This function reeturns no value.
void allocateGlobalStack(jmp_buf returnBuffer, char *topOfStack) {
  char stack[16384];
  stack[sizeof(stack) / 2] = '\0';
  
  if (topOfStack == NULL) {
    topOfStack = stack;
  }
  
  if (((uintptr_t) stack) > ((uintptr_t) _bottomOfHeap)) {
    allocateGlobalStack(returnBuffer, topOfStack);
  }
  
  returnToTop(returnBuffer, topOfStack);
}

/// @fn void sigintHandler(int signal)
///
/// @brief Handler for SIGINT.
///
/// @param signal The numeric value of the signal.  This should always be
/// SIGINT.
///
/// @return This function returns no value.
void sigintHandler(int signal) {
  if (signal == SIGINT) {
    ungetc(0x03, stdin);
  }
}

int halPosixImplInit(jmp_buf resetBuffer, Hal *hal) {
  // Set the handler for sigint so that it's passed to the running process in
  // NanoOs instead of the simulator.
  signal(SIGINT, sigintHandler);

  // Save our reset context for later.
  memcpy(_resetBuffer, resetBuffer, sizeof(jmp_buf));
  fprintf(stdout, "resetBuffer copied.\n");
  fflush(stdout);
  
  int topOfStack = 0;
  fprintf(stderr, "Top of stack        = %p\n", (void*) &topOfStack);
  
  // Simulate having a total of 64 KB available for dynamic memory.
  _bottomOfHeap = (void*) (((uintptr_t) &topOfStack)
    - ((uintptr_t) (((96 * 1024) * DEBUG_MULTIPLIER) - 0)));
  fprintf(stderr, "Bottom of stack     = %p\n", (void*) _bottomOfHeap);
  jmp_buf returnBuffer;
  if (setjmp(returnBuffer) == 0) {
    allocateGlobalStack(returnBuffer, NULL);
  }
  fprintf(stderr, "Global stack allocated\n");
  
  // The size used in the mmap call has to be large enough to accommodate the
  // size used for the overlay, plus the offset into the overlay.  It also has
  // to be page aligned.  Do the appropriate math to get us what we need
  // without wasting too much space.
  long pageSize = sysconf(_SC_PAGESIZE);
  size_t overlayBaseSize
    = ((size_t) (OVERLAY_OFFSET + OVERLAY_SIZE + (pageSize - 1)))
    & ~((size_t) (pageSize - 1));
  
  hal->memory->overlayMap = mmap((void*) OVERLAY_BASE_ADDRESS,
    overlayBaseSize, PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
    -1, 0);
  if (hal->memory->overlayMap == MAP_FAILED) {
    fprintf(stderr, "mmap failed with error: %s\n", strerror(errno));
    return -1;
  }
  
  // The address that the code is built around for both the Cortex-M0 and the
  // simulation code is OVERLAY_OFFSET bytes into the map we just made.
  hal->memory->overlayMap = (void*) (OVERLAY_BASE_ADDRESS + OVERLAY_OFFSET);
  hal->memory->overlaySize = OVERLAY_SIZE;
  
  fprintf(stderr, "posixHal.overlayMap = %p\n",
    (void*) hal->memory->overlayMap);
  fprintf(stderr, "\n");
  
  _mainThreadId = pthread_self();
  
  *((void**) &realTcgetattr) = dlsym(RTLD_NEXT, "tcgetattr");
  *((void**) &realTcsetattr) = dlsym(RTLD_NEXT, "tcsetattr");
  
  int32_t ii = 0;
  
  if (hal->uart != NULL) {
    if (hal->uart->init() < 0) {
      return -ENOTTY;
    }
    int32_t numUarts = hal->uart->numSupported;
    if (numUarts <= 0) {
      // Nothing we can do.
      return -ENOTTY;
    }
    
    for (ii = 0; ii < numUarts; ii++) {
      if (online(hal->uart, ii)) {
        if (hal->uart->configure(ii, 1000000) != 0) {
          setOffline(hal->uart, ii);
        }
      }
    }
  }

  if (hal->timer != NULL) {
    do {
      if (hal->timer->init() != 0) {
        fprintf(stderr, "WARNING: Could not initialize timer subsystem");
        break;
      }
      
      uint32_t online = hal->timer->online[0];
      for (ii = 0; ii < (int32_t) hal->timer->numSupported; ii++) {
        fprintf(stdout, "Initializing timer %u\n", ii);
        if (!online(hal->timer, ii)) {
          continue;
        }
        
        if (hal->timer->initDevice(ii) < 0) {
          setOffline(hal->timer, ii);
        }
      }
      if (hal->timer->online[0] != online) {
        fprintf(stderr, "WARNING: Did not initialize all timers\n");
      }
    } while (0);
  }

  return 0;
}

#endif // __x86_64__

