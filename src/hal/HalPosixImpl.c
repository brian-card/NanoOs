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

#include "SdCardPosix.h"
#include "kernel/ExFatTask.h"
#include "kernel/Hal.h"
#include "kernel/MemoryManager.h"
#include "kernel/NanoOs.h"
#include "kernel/Tasks.h"

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
#define OVERLAY_SIZE               16384

/// @def ELAST
///
/// @brief The highest errno value defined.  Missing from Linux's implementation
/// of errno.h.  (It's a BSD thing...)
#define ELAST                  EHWPOISON

int (*realTcgetattr)(int fd, struct termios *termios_p) = NULL;
int (*realTcsetattr)(int fd, int optional_actions,
  const struct termios *termios_p) = NULL;

uintptr_t posixProcessStackSize(void) {
  return PROCESS_STACK_SIZE;
}

uintptr_t posixMemoryManagerStackSize(bool debug) {
  if (debug == false) {
    // This is the expected case, so list it first.
    return MEMORY_MANAGER_STACK_SIZE;
  } else {
    return MEMORY_MANAGER_DEBUG_STACK_SIZE;
  }
}

/// @var _bottomOfHeap
///
/// @brief Where the bottom of the heap will be set to be in memory.
static void *_bottomOfHeap = NULL;

void* posixBottomOfHeap(void) {
  return _bottomOfHeap;
}

/// @var serialPorts
///
/// @brief Array of serial ports on the system.  Index 0 is the main port,
/// which is the USB serial port.
static FILE **serialPorts[] = {
  &stderr,
};

/// @var _numSerialPorts
///
/// @brief The number of serial ports we support on the Arduino Nano 33 IoT.
static int _numSerialPorts = sizeof(serialPorts) / sizeof(serialPorts[0]);

int posixGetNumSerialPorts(void) {
  return _numSerialPorts;
}

int posixSetNumSerialPorts(int numSerialPorts) {
  if (numSerialPorts > ((int) (sizeof(serialPorts) / sizeof(serialPorts[0])))) {
    return -ERANGE;
  } else if (numSerialPorts < -ELAST) {
    return -ERANGE;
  }
  
  _numSerialPorts = numSerialPorts;
  
  return 0;
}

int posixInitSerialPort(int port, int32_t baud) {
  (void) baud;
  
  if (port != 0) {
    return -ERANGE;
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

int posixPollSerialPort(int port) {
  int serialData = -1;
  
  // While we'll support two outputs, we will only support one input to keep
  // things simple in the simulator.
  if (port == 0) {
    serialData = getchar();
    if (serialData == EOF) {
      serialData = -1;
    }
  }
  
  return serialData;
}

ssize_t posixWriteSerialPort(int port,
  const uint8_t *data, ssize_t length
) {
  ssize_t numBytesWritten = -ERANGE;
  
  if ((port >= 0) && (port < _numSerialPorts) && (length >= 0)) {
    numBytesWritten = fwrite(data, 1, length, *serialPorts[port]);
    fflush(*serialPorts[port]);
  }
  
  return numBytesWritten;
}

int posixGetNumDios(void) {
  return -ENOSYS;
}

int posixConfigureDio(int dio, bool output) {
  (void) dio;
  (void) output;
  
  return -ENOSYS;
}

int posixWriteDio(int dio, bool high) {
  (void) dio;
  (void) high;
  
  return -ENOSYS;
}

int posixInitSpiDevice(int spi,
  uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud
) {
  (void) spi;
  (void) cs;
  (void) sck;
  (void) copi;
  (void) cipo;
  (void) baud;
  
  return -ENOSYS;
}

int posixStartSpiTransfer(int spi) {
  (void) spi;
  
  return -ENOSYS;
}

int posixEndSpiTransfer(int spi) {
  (void) spi;
  
  return -ENOSYS;
}

int posixSpiTransfer8(int spi, uint8_t data) {
  (void) spi;
  (void) data;
  
  return -ENOSYS;
}

int posixSpiTransferBytes(int spi, uint8_t *data, uint32_t length) {
  (void) spi;
  (void) data;
  (void) length;
  
  return -ENOSYS;
}

int posixSetSystemTime(struct timespec *now) {
  (void) now;
  
  return 0;
}

// posixGetElapsedNanoseconds is used as the base implementation, so declare
// its prototype here.
int64_t posixGetElapsedNanoseconds(int64_t startTime);

int64_t posixGetElapsedMilliseconds(int64_t startTime) {
  return posixGetElapsedNanoseconds(
    startTime * ((int64_t) 1000000)) / ((int64_t) 1000000);
}

int64_t posixGetElapsedMicroseconds(int64_t startTime) {
  return posixGetElapsedNanoseconds(
    startTime * ((int64_t) 1000)) / ((int64_t) 1000);
}

int64_t posixGetElapsedNanoseconds(int64_t startTime) {
  #include <time.h>
  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  return ((((int64_t) spec.tv_sec) * ((int64_t) 1000000000))
    + ((int64_t) spec.tv_nsec)) - startTime;
}

/// @var _resetBuffer
///
/// @brief Copy of the jmp_buf that was initialized prior to initializing the
/// HAL so that we can jump back to before hardware initialization in the
/// simulation.
static jmp_buf _resetBuffer;

int posixShutdown(HalShutdownType shutdownType) {
  // You can't completely turn off the hardware we're running on.  We're
  // simulating hardware, so do what the hardware would do, which is the same
  // set of operations for both off and suspend.
  if ((shutdownType == HAL_SHUTDOWN_OFF)
    || (shutdownType == HAL_SHUTDOWN_SUSPEND)
  ) {
    exit(0);
  } else if (shutdownType == HAL_SHUTDOWN_RESET) {
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
    longjmp(_resetBuffer, 1);
  }
  
  return 0;
}

/// @var _sdCardDevicePath
///
/// @brief Path to the device node to connect to for the SdCardSim task.
static const char *_sdCardDevicePath = NULL;

int posixInitRootStorage(SchedulerState *schedulerState) {
  TaskDescriptor *allTasks = schedulerState->allTasks;
  
  // Create the SD card task.
  TaskDescriptor *taskDescriptor
    = &allTasks[NANO_OS_SD_CARD_TASK_ID - 1];
  if (taskCreate(
    taskDescriptor, runSdCardPosix, (void*) _sdCardDevicePath)
    != taskSuccess
  ) {
    fputs("Could not start SD card task.\n", stderr);
  }
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);
  taskDescriptor->taskId = NANO_OS_SD_CARD_TASK_ID;
  taskDescriptor->name = "SD card";
  taskDescriptor->userId = ROOT_USER_ID;
  BlockStorageDevice *sdDevice = (BlockStorageDevice*) coroutineResume(
    allTasks[NANO_OS_SD_CARD_TASK_ID - 1].taskHandle, NULL);
  sdDevice->partitionNumber = 1;
  
  // Create the filesystem task.
  taskDescriptor = &allTasks[NANO_OS_FILESYSTEM_TASK_ID - 1];
  if (taskCreate(taskDescriptor, runExFatFilesystem, sdDevice)
    != taskSuccess
  ) {
    fputs("Could not start filesystem task.\n", stderr);
  }
  taskHandleSetContext(taskDescriptor->taskHandle, taskDescriptor);
  taskDescriptor->taskId = NANO_OS_FILESYSTEM_TASK_ID;
  taskDescriptor->name = "filesystem";
  taskDescriptor->userId = ROOT_USER_ID;
  
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

int posixGetNumTimers(void) {
  return _numTimers;
}

int posixSetNumTimers(int numTimers) {
  if (numTimers
    > ((int) (sizeof(softwareTimers) / sizeof(softwareTimers[0])))
  ) {
    return -ERANGE;
  } else if (numTimers < -ELAST) {
    return -EINVAL;
  }
  
  _numTimers = numTimers;
  
  return 0;
}

int posixInitTimer(int timer) {
  if (timer >= _numTimers) {
    return -ERANGE;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[timer];
  if (swTimer->initialized) {
    // Nothing to do
    return 0;
  }
  
  signal(swTimer->signal, swTimer->signalHandler);
  swTimer->initialized = true;
  
  return 0;
}

int posixConfigOneShotTimer(int timer,
    uint64_t nanoseconds, void (*callback)(void)
) {
  if (timer >= _numTimers) {
    return -ERANGE;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[timer];
  if (!swTimer->initialized) {
    return -EINVAL;
  }
  
  swTimer->callback = callback;
  swTimer->active = true;
  swTimer->startTime = posixGetElapsedNanoseconds(0);
  swTimer->deadline = swTimer->startTime + nanoseconds;
  pthread_create(&swTimer->timerThread, NULL,
    timerThreadFunction, (void*) ((intptr_t) timer));
  pthread_detach(swTimer->timerThread);
  
  return 0;
}

uint64_t posixConfiguredTimerNanoseconds(int timer) {
  if (timer >= _numTimers) {
    return 0;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[timer];
  if ((!swTimer->initialized) || (!swTimer->active)) {
    return 0;
  }
  
  return swTimer->deadline - swTimer->startTime;
}

uint64_t posixRemainingTimerNanoseconds(int timer) {
  if (timer >= _numTimers) {
    return 0;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[timer];
  if ((!swTimer->initialized) || (!swTimer->active)) {
    return 0;
  }
  
  int64_t now = posixGetElapsedNanoseconds(0);
  if (now > swTimer->deadline) {
    return 0;
  }
  
  return swTimer->deadline - now;
}

int posixCancelTimer(int timer) {
  if (timer >= _numTimers) {
    return -ERANGE;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[timer];
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

int posixCancelAndGetTimer(int timer,
  uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  void (**callback)(void)
) {
  // We need to get `now` as close to the beginning of this function call as
  // possible so that any call to reconfigure the timer later is correct.
  int64_t now = posixGetElapsedNanoseconds(0);
  
  if ((timer < 0) || (timer >= _numTimers)) {
    return -ERANGE;
  }
  
  SoftwareTimer *swTimer = &softwareTimers[timer];
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

int halPosixImplInit(jmp_buf resetBuffer, const char *sdCardDevicePath,
  Hal *hal
) {
  fprintf(stdout, "Setting _sdCardDevicePath.\n");
  fflush(stdout);
  _sdCardDevicePath = sdCardDevicePath;
  fprintf(stdout, "_sdCardDevicePath set.\n");
  fflush(stdout);

  // Saver our reset context for later.
  memcpy(_resetBuffer, resetBuffer, sizeof(jmp_buf));
  fprintf(stdout, "resetBuffer copied.\n");
  fflush(stdout);
  
  int topOfStack = 0;
  fprintf(stderr, "Top of stack        = %p\n", (void*) &topOfStack);
  
  // Simulate having a total of 64 KB available for dynamic memory.
  _bottomOfHeap = (void*) (((uintptr_t) &topOfStack)
    - ((uintptr_t) (65536 - 11456)));
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
  
  hal->overlayMap = mmap((void*) OVERLAY_BASE_ADDRESS,
    overlayBaseSize, PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
    -1, 0);
  if (hal->overlayMap == MAP_FAILED) {
    fprintf(stderr, "mmap failed with error: %s\n", strerror(errno));
    return -1;
  }
  
  // The address that the code is built around for both the Cortex-M0 and the
  // simulation code is OVERLAY_OFFSET bytes into the map we just made.
  hal->overlayMap = (void*) (OVERLAY_BASE_ADDRESS + OVERLAY_OFFSET);
  hal->overlaySize = OVERLAY_SIZE;
  
  fprintf(stderr, "posixHal.overlayMap = %p\n", (void*) hal->overlayMap);
  fprintf(stderr, "\n");
  
  _mainThreadId = pthread_self();
  
  *((void**) &realTcgetattr) = dlsym(RTLD_NEXT, "tcgetattr");
  *((void**) &realTcsetattr) = dlsym(RTLD_NEXT, "tcsetattr");
  
  int numSerialPorts = hal->getNumSerialPorts();
  if (numSerialPorts <= 0) {
    // Nothing we can do.  Bail.
    fprintf(stderr, "hal->getNumSerialPorts() returned %d.\n", numSerialPorts);
    return -1;
  }
  
  // Set all the serial ports to run at 1000000 baud.
  if (hal->initSerialPort(0, 1000000) < 0) {
    // Nothing we can do.  Bail.
    fprintf(stderr, "Initializing serial port 0 failed.\n");
    return -1;
  }
  int ii = 0;
  for (ii = 1; ii < numSerialPorts; ii++) {
    if (hal->initSerialPort(ii, 1000000) < 0) {
      // We can't support more than the last serial port that was successfully
      // initialized.
      fprintf(stderr, "WARNING: Initializing serial port %d failed.\n", ii);
      break;
    }
  }
  hal->setNumSerialPorts(ii);

  int numTimers = hal->getNumTimers();
  for (ii = 0; ii < numTimers; ii++) {
    if (hal->initTimer(ii) < 0) {
      break;
    }
  }
  hal->setNumTimers(ii);
  if (ii != numTimers) {
    fprintf(stderr, "WARNING: Only initialized %d timers\n", ii);
  }

  return 0;
}

#endif // __x86_64__

