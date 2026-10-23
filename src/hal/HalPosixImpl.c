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

/// @file HalArduinoSamD21x18ASimImpl.c
///
/// @brief HAL implementation for a Posix simulator.

#ifdef __x86_64__

// Standard C includes from the compiler
#undef errno
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include </usr/include/time.h>
#include <termios.h>
#include <unistd.h>

#include "kernel/Hal.h"
#include "HalPosix.h"
#include "kernel/MemoryManager.h"
#include "kernel/NanoOs.h"
#include "kernel/Processes.h"

// Scheduler.h is deliberately *NOT* included here: it #define's FILE to
// NanoOsFile, which would break this file's legitimate use of the real
// system FILE (see posixConfigOneShotTimer et al. below). schedulerPid is
// just a plain ProcessId global; forward-declare it instead.  Same reason
// schedulerDeinitialize is forward-declared rather than pulled in via the
// header.
extern ProcessId schedulerPid;
void schedulerDeinitialize(void);

/// @def DEBUG_MULTIPLIER
///
/// @brief Multiplier to use for stack/heap size during debugging.  When not
/// debugging, this value should be 1.
#define DEBUG_MULTIPLIER 1

/// @def DEFAULT_MEMORY_SIZE
///
/// @brief The size, in bytes, of the simulated memory shared by the process
/// stacks and the heap when halPosixImplInit is given no size.
#define DEFAULT_MEMORY_SIZE (96 * 1024)

/// @def POSIX_FILE_CACHE_NODE_OVERHEAD
///
/// @brief Heap bytes allowed per overlay file cache node beyond the overlay
/// itself, for its node and its copied overlay ID.
#define POSIX_FILE_CACHE_NODE_OVERHEAD 128

/// @def PROCESS_STACK_SIZE
///
/// @brief The size, in bytes, of a regular process's stack.
#define PROCESS_STACK_SIZE (4 * 1024)

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief The size, in bytes, of the memory manager process's stack.
#define MEMORY_MANAGER_STACK_SIZE 1024

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

/// @def CONTIGUOUS_OVERLAY_BASE_ADDRESS
///
/// @brief The base address that we will use in our mmap call for the
/// contiguous filesystem overlay.  Unlike the regular, swappable overlay
/// region, this mapping stays resident for the life of the process, so it
/// has to live at a different address than OVERLAY_BASE_ADDRESS.  Matches
/// the ORIGIN in NanoOsSimContiguous.ld.  The address has to be page
/// aligned on Linux.
#define CONTIGUOUS_OVERLAY_BASE_ADDRESS 0x20010000

/// @def CONTIGUOUS_OVERLAY_SIZE
///
/// @brief The size of the contiguous filesystem overlay region.  Matches
/// the LENGTH in NanoOsSimContiguous.ld.
#define CONTIGUOUS_OVERLAY_SIZE          32768

/// @def ELAST
///
/// @brief The highest errno value defined.  Missing from Linux's implementation
/// of errno.h.  (It's a BSD thing...)
#define ELAST                  EHWPOISON

int (*realTcgetattr)(int fd, struct termios *termios_p) = NULL;
int (*realTcsetattr)(int fd, int optional_actions,
  const struct termios *termios_p) = NULL;

int posixProcessStackSize(int32_t deviceId, va_list args) {
  (void) deviceId;
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  (void) debug;
  *returnValue = PROCESS_STACK_SIZE * DEBUG_MULTIPLIER;
  return 0;
}

int posixMemoryManagerStackSize(int32_t deviceId, va_list args) {
  (void) deviceId;
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
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

int posixBottomOfHeap(int32_t deviceId, va_list args) {
  (void) deviceId;
  bool debug = (bool) va_arg(args, int);
  void **returnValue = va_arg(args, void**);
  (void) debug;
  *returnValue = _bottomOfHeap;
  return 0;
}

int posixNumExtraSchedulerStacks(int32_t deviceId, va_list args) {
  (void) deviceId;
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  *returnValue = 1;
  return 0;
}

int posixNumExtraConsoleStacks(int32_t deviceId, va_list args) {
  (void) deviceId;
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  *returnValue = 1;
  return 0;
}

/// @var NUM_UARTS
///
/// @brief The number of serial ports we support on the Arduinos.  Using that
/// as a simulation baseline.
#define NUM_UARTS 2

/// @var uarts
///
/// @brief Array of serial ports on the system.  Index 0 is the main port,
/// which is the USB serial port on the Arduinos.  We won't use that here in
/// the sim because we want to test being able to use a non-default port from
/// the Console process.
static FILE **uarts[NUM_UARTS] = {
  &stderr,
  &stderr,
};

/// @var halUartCallbacks
///
/// @brief This is the backing storage for the callback information to be used
/// when an interrupt for one of the UARTs fires.
HalUartCallback halUartCallbacks[NUM_UARTS];

/// @var halUartCallbackMessages
///
/// @brief Callback-specific message storage to be used when a UART interrupt
/// is triggered.
ProcessMessage halUartCallbackMessages[NUM_UARTS];

/// @var halUartCallbacksPending
///
/// @brief Callback-specific callback-pending storage to be used when a UART
/// interrupt is triggered.
volatile bool halUartCallbacksPending[NUM_UARTS];

int posixInitUart(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  memset((void*) halUartCallbacks, 0, sizeof(halUartCallbacks));
  memset((void*) halUartCallbackMessages, 0, sizeof(halUartCallbackMessages));
  memset((void*) halUartCallbacksPending, 0, sizeof(halUartCallbacksPending));
  return 0;
}

/// @var _getAttrErrorMessage
///
/// @brief Message printed when the current console terminal attributes
/// can't be retrieved.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _getAttrErrorMessage[] KEEP_IN_FLASH
  = "Could not get current attributes for console.\n";

/// @var _setAttrErrorMessage
///
/// @brief Message printed when the new console terminal attributes can't
/// be applied.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _setAttrErrorMessage[] KEEP_IN_FLASH
  = "Could not set new attributes for console.\n";

/// @var _mainThreadId
///
/// @brief The ID of the main thread that calls halPosixInit.
static pthread_t _mainThreadId = 0;

// HalCommon.h can't be included here; see the note on Scheduler.h above.
void halCommonUartMarkReady(int32_t deviceId);
int posixNanoOsEio(void);

/// @var _pollOnlyEnvironmentVariable
///
/// @brief Environment variable that makes the simulator report its console
/// UART as poll-only.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _pollOnlyEnvironmentVariable[] KEEP_IN_FLASH
  = "NANO_OS_SIM_POLL_ONLY";

/// @fn bool posixConsolePollOnlyRequested(void)
///
/// @brief Report whether the simulator was asked to treat its console UART as
/// poll-only.
///
/// @return Returns true if NANO_OS_SIM_POLL_ONLY is set, false otherwise.
bool posixConsolePollOnlyRequested(void) {
  return getenv(_pollOnlyEnvironmentVariable) != NULL;
}

/// @var _fileCacheEnvironmentVariable
///
/// @brief Environment variable that makes the simulator call file overlays
/// through the overlay file cache.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _fileCacheEnvironmentVariable[] KEEP_IN_FLASH
  = "NANO_OS_SIM_FILE_CACHE";

/// @fn bool posixFileCacheRequested(void)
///
/// @brief Report whether the simulator was asked to use the overlay file
/// cache.
///
/// @return Returns true if NANO_OS_SIM_FILE_CACHE is set, false otherwise.
bool posixFileCacheRequested(void) {
  return getenv(_fileCacheEnvironmentVariable) != NULL;
}

/// @def POSIX_UART_RX_BUFFER_SIZE
///
/// @brief Size of each UART's receive ring.  One slot stays empty to tell a
/// full ring from an empty one, so it holds one less than this.
#define POSIX_UART_RX_BUFFER_SIZE 64

/// @def POSIX_UART_RX_SIGNAL
///
/// @brief Signal the receive threads raise on the main thread for each byte
/// they buffer, standing in for a UART receive interrupt.
#define POSIX_UART_RX_SIGNAL SIGIO

/// @struct PosixUartRx
///
/// @brief The receive side of a simulated UART: a thread reading a host file
/// descriptor into a ring.
///
/// @param fd The host file descriptor the UART reads, or -1.
/// @param buffer The ring of bytes read from fd.
/// @param head The index of the next slot the receive thread writes.
/// @param tail The index of the next slot the UART's reader reads.
/// @param mutex Guards buffer, head, and tail.
/// @param notFull Signaled by the reader when it frees a slot in a full ring.
/// @param threadStarted Whether the receive thread has been started.
/// @param ready Set by the receive thread when it buffers a byte, and cleared
///   by the signal handler when it reports the UART ready.
/// @param disconnected Set by the receive thread when fd reaches its end.
typedef struct PosixUartRx {
  int                   fd;
  uint8_t               buffer[POSIX_UART_RX_BUFFER_SIZE];
  int                   head;
  int                   tail;
  pthread_mutex_t       mutex;
  pthread_cond_t        notFull;
  bool                  threadStarted;
  volatile sig_atomic_t ready;
  volatile sig_atomic_t disconnected;
} PosixUartRx;

/// @var _uartRx
///
/// @brief The receive side of each UART.  UART1 reads stdin.  UART0 reads the
/// socket named by NANO_OS_SIM_UART0, when it's set.
static PosixUartRx _uartRx[NUM_UARTS] = {
  { .fd = -1, .mutex = PTHREAD_MUTEX_INITIALIZER,
    .notFull = PTHREAD_COND_INITIALIZER },
  { .fd = STDIN_FILENO, .mutex = PTHREAD_MUTEX_INITIALIZER,
    .notFull = PTHREAD_COND_INITIALIZER },
};

/// @var _uartRxSignalInstalled
///
/// @brief Whether posixUartRxSignalHandler has been installed.
static bool _uartRxSignalInstalled = false;

/// @def POSIX_UART_RX_SIGNAL_STACK_SIZE
///
/// @brief Size of the stack posixUartRxSignalHandler runs on.
#define POSIX_UART_RX_SIGNAL_STACK_SIZE (32 * 1024)

/// @var _uartRxSignalStack
///
/// @brief Alternate stack for POSIX_UART_RX_SIGNAL.  Delivering a signal costs
/// over 3 KB of stack on x86-64, which a 4 KB coroutine stack can't absorb.
static uint8_t _uartRxSignalStack[POSIX_UART_RX_SIGNAL_STACK_SIZE];

/// @var _uart0EnvironmentVariable
///
/// @brief Environment variable naming a Unix socket for the simulator to
/// connect UART0 to.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _uart0EnvironmentVariable[] KEEP_IN_FLASH
  = "NANO_OS_SIM_UART0";

/// @fn bool posixUart0Requested(void)
///
/// @brief Report whether the simulator was asked to connect UART0 to a
/// socket.
///
/// @return Returns true if NANO_OS_SIM_UART0 is set, false otherwise.
bool posixUart0Requested(void) {
  return getenv(_uart0EnvironmentVariable) != NULL;
}

/// @fn bool posixUartRxPushLocked(PosixUartRx *rx, uint8_t byte)
///
/// @brief Append a byte to a receive ring.  The caller must hold rx->mutex.
///
/// @param rx A pointer to the PosixUartRx of the UART.
/// @param byte The byte to append.
///
/// @return Returns true if the byte was stored, false if the ring was full.
static bool posixUartRxPushLocked(PosixUartRx *rx, uint8_t byte) {
  int nextHead = rx->head + 1;
  if (nextHead == POSIX_UART_RX_BUFFER_SIZE) {
    nextHead = 0;
  }
  if (nextHead == rx->tail) {
    return false;
  }
  rx->buffer[rx->head] = byte;
  rx->head = nextHead;
  return true;
}

/// @fn void posixUartRxSignalHandler(int signal)
///
/// @brief Handle POSIX_UART_RX_SIGNAL on the main thread by reporting each
/// UART that has received bytes as ready.
///
/// @param signal The numeric value of the signal.  Ignored.
///
/// @return This function returns no value.
static void posixUartRxSignalHandler(int signal) {
  (void) signal;
  for (int ii = 0; ii < NUM_UARTS; ii++) {
    if (_uartRx[ii].ready) {
      _uartRx[ii].ready = 0;
      halCommonUartMarkReady(ii);
    }
  }
}

/// @fn void* posixUartRxThreadFunction(void *arg)
///
/// @brief Block reading a UART's file descriptor, move each byte into its
/// receive ring, and signal the main thread.  Waits for room rather than
/// dropping bytes when the ring is full, and exits at end of file.
///
/// @param arg A pointer to the PosixUartRx of the UART.
///
/// @return This function always returns NULL.
static void* posixUartRxThreadFunction(void *arg) {
  PosixUartRx *rx = (PosixUartRx*) arg;

  // read() rather than getc(): getc() holds stdin's stdio lock while it
  // blocks, and exit() waits on that lock to flush streams.
  uint8_t byte = 0;
  while (read(rx->fd, &byte, 1) == 1) {
    pthread_mutex_lock(&rx->mutex);
    while (!posixUartRxPushLocked(rx, byte)) {
      pthread_cond_wait(&rx->notFull, &rx->mutex);
    }
    pthread_mutex_unlock(&rx->mutex);
    rx->ready = 1;
    pthread_kill(_mainThreadId, POSIX_UART_RX_SIGNAL);
  }
  pthread_mutex_lock(&rx->mutex);
  rx->threadStarted = false;
  rx->disconnected = 1;
  pthread_mutex_unlock(&rx->mutex);
  rx->ready = 1;
  pthread_kill(_mainThreadId, POSIX_UART_RX_SIGNAL);

  return NULL;
}

/// @fn int posixStartUartRx(PosixUartRx *rx)
///
/// @brief Start the receive thread of a UART, installing the receive signal
/// handler first if no UART has yet.
///
/// @param rx A pointer to the PosixUartRx of the UART.
///
/// @return Returns 0 on success, -errno on failure.
static int posixStartUartRx(PosixUartRx *rx) {
  if (rx->threadStarted) {
    return 0;
  }

  if (!_uartRxSignalInstalled) {
    stack_t signalStack;
    signalStack.ss_sp = _uartRxSignalStack;
    signalStack.ss_size = sizeof(_uartRxSignalStack);
    signalStack.ss_flags = 0;
    if (sigaltstack(&signalStack, NULL) < 0) {
      return -errno;
    }

    // Blocking every signal keeps a preemption from switching coroutines
    // while a frame is live on the alternate stack.
    struct sigaction sa;
    sa.sa_handler = posixUartRxSignalHandler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_ONSTACK;
    if (sigaction(POSIX_UART_RX_SIGNAL, &sa, NULL) < 0) {
      return -errno;
    }
    _uartRxSignalInstalled = true;
  }

  // The thread inherits this mask, which keeps process-directed signals like
  // SIGINT on the main thread.
  sigset_t allSignals;
  sigset_t previousSignals;
  sigfillset(&allSignals);
  pthread_sigmask(SIG_SETMASK, &allSignals, &previousSignals);
  pthread_t rxThread;
  int status = pthread_create(&rxThread, NULL, posixUartRxThreadFunction, rx);
  pthread_sigmask(SIG_SETMASK, &previousSignals, NULL);
  if (status != 0) {
    return -status;
  }
  pthread_detach(rxThread);
  rx->threadStarted = true;

  return 0;
}

/// @fn int posixConnectUart0(void)
///
/// @brief Connect UART0 to the Unix socket named by NANO_OS_SIM_UART0 and
/// start its receive thread.
///
/// @return Returns 0 on success, -errno on failure.
static int posixConnectUart0(void) {
  const char *path = getenv(_uart0EnvironmentVariable);
  struct sockaddr_un address;
  if ((path == NULL) || (strlen(path) >= sizeof(address.sun_path))) {
    return -ENOTTY;
  }

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return -errno;
  }
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, path);
  if (connect(fd, (struct sockaddr*) &address, sizeof(address)) != 0) {
    int returnValue = -errno;
    close(fd);
    return returnValue;
  }
  _uartRx[0].disconnected = 0;
  _uartRx[0].fd = fd;

  return posixStartUartRx(&_uartRx[0]);
}

/// @fn int posixConfigureUart(int32_t deviceId, va_list args)
///
/// @brief Configure stdin to not echo and start UART1's receive thread, or
/// connect UART0 to its socket.
///
/// @param deviceId The device ID that callHal checked the call against.
/// @param args A va_list of the arguments that were passed to the call.  See
///   HalUart.configure for a description of the arguments.
///
/// @return Returns 0 on success, -errno on failure.
int posixConfigureUart(int32_t deviceId, va_list args) {
  uint32_t baud = va_arg(args, uint32_t);
  (void) baud;

  if (deviceId > 1) {
    return -ERANGE;
  } else if (deviceId == 0) {
    return (_uartRx[0].fd >= 0) ? 0 : posixConnectUart0();
  }

  // We manage all the prints to screen ourselves, so disable stdin echoing
  // as well.
  struct termios oldFlags = {0};
  struct termios newFlags = {0};

  // Get the current console flags.
  int stdinFileno = fileno(stdin);
  if (realTcgetattr(stdinFileno, &oldFlags) != 0) {
    fprintf(stderr, _getAttrErrorMessage);
    return -errno;
  }

  // Disable echo to the console.
  newFlags = oldFlags;
  newFlags.c_lflag |= ECHONL;
  newFlags.c_lflag &= ~(ECHO | ICANON);
  if (realTcsetattr(stdinFileno, TCSANOW, &newFlags) != 0) {
    fprintf(stderr, _setAttrErrorMessage);
    return -errno;
  }

  return posixStartUartRx(&_uartRx[1]);
}

/// @fn int posixUartReadable(int32_t deviceId)
///
/// @brief Determine whether a UART has a receive ring the kernel may read.
///
/// @param deviceId The ID of the UART.
///
/// @return Returns true if the UART is UART1, or UART0 connected to its
/// socket, false otherwise.
static bool posixUartReadable(int32_t deviceId) {
  return (deviceId == 1) || ((deviceId == 0) && (_uartRx[0].fd >= 0));
}

int posixPollUart(int32_t deviceId, va_list args) {
  (void) args;
  int serialData = -1;

  if (posixUartReadable(deviceId)) {
    PosixUartRx *rx = &_uartRx[deviceId];
    // A timer signal here could switch coroutines while the mutex is held.
    sigset_t allSignals;
    sigset_t previousSignals;
    sigfillset(&allSignals);
    pthread_sigmask(SIG_SETMASK, &allSignals, &previousSignals);
    pthread_mutex_lock(&rx->mutex);
    if (rx->tail != rx->head) {
      serialData = rx->buffer[rx->tail];
      rx->tail++;
      if (rx->tail == POSIX_UART_RX_BUFFER_SIZE) {
        rx->tail = 0;
      }
      pthread_cond_signal(&rx->notFull);
    }
    pthread_mutex_unlock(&rx->mutex);
    pthread_sigmask(SIG_SETMASK, &previousSignals, NULL);
  }

  return serialData;
}

/// @fn int posixReadUart(int32_t deviceId, va_list args)
///
/// @brief Copy up to length bytes from a UART's receive ring without
/// blocking.  Poll-only devices and devices with no receive ring report 0
/// bytes read.  Once UART0's socket has closed and its ring is empty, the
/// socket is released and the read fails with NanoOs's -EIO.
///
/// @param deviceId The device ID that callHal checked the call against.
/// @param args A va_list holding the int32_t deviceId, uint8_t *data,
///   ssize_t length, and ssize_t *returnValue arguments of HalUart.read.
///
/// @return Returns 0 on success, -errno on failure.
int posixReadUart(int32_t deviceId, va_list args) {
  uint8_t *data = va_arg(args, uint8_t*);
  ssize_t length = va_arg(args, ssize_t);
  ssize_t *returnValue = va_arg(args, ssize_t*);

  ssize_t bytesRead = 0;
  if (posixUartReadable(deviceId) && !pollOnly(HAL->uart, deviceId)) {
    PosixUartRx *rx = &_uartRx[deviceId];
    // A timer signal here could switch coroutines while the mutex is held.
    sigset_t allSignals;
    sigset_t previousSignals;
    sigfillset(&allSignals);
    pthread_sigmask(SIG_SETMASK, &allSignals, &previousSignals);
    pthread_mutex_lock(&rx->mutex);
    while ((bytesRead < length) && (rx->tail != rx->head)) {
      data[bytesRead++] = rx->buffer[rx->tail];
      rx->tail++;
      if (rx->tail == POSIX_UART_RX_BUFFER_SIZE) {
        rx->tail = 0;
      }
    }
    if (bytesRead > 0) {
      pthread_cond_signal(&rx->notFull);
    }
    bool lost = (deviceId == 0) && (bytesRead == 0)
      && (rx->disconnected != 0) && (rx->threadStarted == false);
    pthread_mutex_unlock(&rx->mutex);
    pthread_sigmask(SIG_SETMASK, &previousSignals, NULL);
    if (lost) {
      close(rx->fd);
      rx->fd = -1;
      if (returnValue != NULL) {
        *returnValue = 0;
      }
      return posixNanoOsEio();
    }
  }

  if (returnValue != NULL) {
    *returnValue = bytesRead;
  }
  return 0;
}

int posixWriteUart(int32_t deviceId, va_list args) {
  const uint8_t *data = va_arg(args, const uint8_t*);
  ssize_t length = va_arg(args, ssize_t);
  ssize_t *returnValue = va_arg(args, ssize_t*);

  ssize_t numBytesWritten = -ERANGE;

  if ((deviceId == 0) && (_uartRx[0].fd < 0) && posixUart0Requested()
    && (posixConnectUart0() != 0)
  ) {
    numBytesWritten = posixNanoOsEio();
  } else if ((deviceId == 0) && (_uartRx[0].fd >= 0) && (length >= 0)) {
    numBytesWritten = 0;
    while (numBytesWritten < length) {
      ssize_t written = send(_uartRx[0].fd, &data[numBytesWritten],
        length - numBytesWritten, MSG_NOSIGNAL);
      if (written <= 0) {
        numBytesWritten = posixNanoOsEio();
        break;
      }
      numBytesWritten += written;
    }
  } else if ((deviceId >= 0) && (deviceId < NUM_UARTS) && (length >= 0)) {
    numBytesWritten = fwrite(data, 1, length, *uarts[deviceId]);
    fflush(*uarts[deviceId]);
  }

  if (returnValue != NULL) {
    *returnValue = numBytesWritten;
  }
  return (numBytesWritten >= 0) ? 0 : (int) numBytesWritten;
}

int posixIsUartConsole(int32_t deviceId, va_list args) {
  bool *returnValue = va_arg(args, bool*);
  if (returnValue != NULL) {
    *returnValue = (deviceId == 1);
  }
  return 0;
}

int posixInitDio(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixConfigureDio(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixWriteDio(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixInitSpi(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixConfigureSpiDevice(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixSetSpiSpeed(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixStartSpiTransfer(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixEndSpiTransfer(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixSpiTransfer8(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixSpiTransferBytes(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixSpiTransferUntil(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENOSYS;
}

int posixTimeInit(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return 0;
}

int posixSetSystemTime(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return 0;
}

// posixGetElapsedNanosecondsImpl is used as the base implementation.
static int posixGetElapsedNanosecondsImpl(int64_t startTime,
  int64_t *returnValue
) {
  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  if (returnValue != NULL) {
    *returnValue = ((((int64_t) spec.tv_sec) * ((int64_t) 1000000000))
      + ((int64_t) spec.tv_nsec)) - startTime;
  }
  return 0;
}

int posixGetElapsedMilliseconds(int32_t deviceId, va_list args) {
  (void) deviceId;
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t nanoseconds = 0;
  int32_t rv = posixGetElapsedNanosecondsImpl(
    startTime * ((int64_t) 1000000), &nanoseconds);
  if (returnValue != NULL) {
    *returnValue = nanoseconds / ((int64_t) 1000000);
  }
  return rv;
}

int posixGetElapsedMicroseconds(int32_t deviceId, va_list args) {
  (void) deviceId;
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t nanoseconds = 0;
  int32_t rv = posixGetElapsedNanosecondsImpl(
    startTime * ((int64_t) 1000), &nanoseconds);
  if (returnValue != NULL) {
    *returnValue = nanoseconds / ((int64_t) 1000);
  }
  return rv;
}

int posixGetElapsedNanoseconds(int32_t deviceId, va_list args) {
  (void) deviceId;
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  return posixGetElapsedNanosecondsImpl(startTime, returnValue);
}

/// @var _resetBuffer
///
/// @brief Copy of the jmp_buf that was initialized prior to initializing the
/// HAL so that we can jump back to before hardware initialization in the
/// simulation.
static jmp_buf _resetBuffer;

/// @var _munmapErrorFormat
///
/// @brief printf-style format string used to report a failed munmap() of
/// the overlay region during a simulated reset.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _munmapErrorFormat[] KEEP_IN_FLASH
  = "ERROR: munmap returned: %s\n";

/// @var _exitingMessage
///
/// @brief Printed immediately before exiting due to a fatal munmap()
/// failure during a simulated reset.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _exitingMessage[] KEEP_IN_FLASH = "Exiting.\n";

/// @var _contiguousOverlayMap
///
/// @brief The address the contiguous filesystem overlay is mapped at, for
/// consumption by whatever loads and dispatches to it.  NULL until
/// halPosixImplInit has run.
void *_contiguousOverlayMap = NULL;

int posixEnterPowerMode(int32_t deviceId, va_list args) {
  (void) deviceId;
  HalPowerMode powerMode = (HalPowerMode) va_arg(args, int);
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
      fprintf(stderr, _munmapErrorFormat, strerror(errno));
      fprintf(stderr, _exitingMessage);
      exit(1);
    }

    // Unmap the contiguous filesystem overlay so that it can be mapped
    // again when we reset.
    size_t contiguousOverlayBaseSize
      = ((size_t) (CONTIGUOUS_OVERLAY_SIZE + (pageSize - 1)))
      & ~((size_t) (pageSize - 1));

    if (munmap((void*) CONTIGUOUS_OVERLAY_BASE_ADDRESS,
      contiguousOverlayBaseSize) < 0
    ) {
      fprintf(stderr, _munmapErrorFormat, strerror(errno));
      fprintf(stderr, _exitingMessage);
      exit(1);
    }
    _contiguousOverlayMap = NULL;

    // Reset the block storage device online map so that initialization works
    // properly on reset.
    HAL->blockDevice->online[0] = 0;
    schedulerDeinitialize();
    threadsDeconfig();
    longjmp(_resetBuffer, 1);
  }

  return 0;
}

// Timer support

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
/// @param threadRunning Whether timerThread has been created and not yet
///   joined.
/// @param fired Whether timerThread has sent its signal to the main thread.
typedef struct SoftwareTimer {
  pthread_t timerThread;
  int signal;
  void (*signalHandler)(int);
  bool initialized;
  void (*callback)(void);
  bool active;
  int64_t startTime;
  int64_t deadline;
  bool threadRunning;
  bool fired;
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

  SoftwareTimer *swTimer = &softwareTimers[timer];
  int64_t delay = swTimer->deadline - swTimer->startTime;
  struct timespec ts = {
    .tv_sec = delay / ((int64_t) 1000000000),
    .tv_nsec = delay % ((int64_t) 1000000000),
  };
  nanosleep(&ts, NULL);
  if (swTimer->active) {
    swTimer->fired = true;
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
  if (!swTimer->fired) {
    // Sent by a timer thread that has since been cancelled.
    return;
  }

  swTimer->fired = false;
  swTimer->active = false;
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
    .threadRunning = false,
    .fired = false,
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
    .threadRunning = false,
    .fired = false,
  },
};

/// @var _numTimers
///
/// @brief The number of timers returned by posixGetNumTimers.  This is
/// initialized to the number of timers supported, but may be overridden by a
/// call to posixSetNumTimers.
static int _numTimers = sizeof(softwareTimers) / sizeof(softwareTimers[0]);

int posixInitTimer(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return 0;
}

int posixInitTimerDevice(int32_t deviceId, va_list args) {
  (void) args;
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

/// @fn void reapTimerThread(SoftwareTimer *swTimer)
///
/// @brief Cancel and join a timer's thread, if it has one, and discard any
/// signal it has already sent.
///
/// @param swTimer The SoftwareTimer whose thread to reap.
///
/// @return This function returns no value.
static void reapTimerThread(SoftwareTimer *swTimer) {
  // A timer signal here could switch coroutines in the middle of the join.
  sigset_t allSignals;
  sigset_t previousSignals;
  sigfillset(&allSignals);
  pthread_sigmask(SIG_SETMASK, &allSignals, &previousSignals);
  if (swTimer->threadRunning) {
    pthread_cancel(swTimer->timerThread);
    pthread_join(swTimer->timerThread, NULL);
    swTimer->threadRunning = false;
  }
  swTimer->fired = false;
  pthread_sigmask(SIG_SETMASK, &previousSignals, NULL);
}

int posixConfigOneShotTimer(int32_t deviceId, va_list args) {
  uint64_t nanoseconds = va_arg(args, uint64_t);
  void (*callback)(void) = va_arg(args, void (*)(void));

  if (deviceId >= _numTimers) {
    return -ERANGE;
  }

  SoftwareTimer *swTimer = &softwareTimers[deviceId];
  if (!swTimer->initialized) {
    return -EINVAL;
  }

  swTimer->active = false;
  reapTimerThread(swTimer);
  swTimer->callback = callback;
  swTimer->active = true;
  posixGetElapsedNanosecondsImpl(0, &swTimer->startTime);
  swTimer->deadline = swTimer->startTime + nanoseconds;
  swTimer->threadRunning = (pthread_create(&swTimer->timerThread, NULL,
    timerThreadFunction, (void*) ((intptr_t) deviceId)) == 0);

  return 0;
}

int posixConfiguredTimerNanoseconds(int32_t deviceId, va_list args) {
  uint64_t *returnValue = va_arg(args, uint64_t*);

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

int posixRemainingTimerNanoseconds(int32_t deviceId, va_list args) {
  uint64_t *returnValue = va_arg(args, uint64_t*);

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
  posixGetElapsedNanosecondsImpl(0, &now);
  if (now > swTimer->deadline) {
    return 0;
  }

  if (returnValue != NULL) {
    *returnValue = swTimer->deadline - now;
  }
  return 0;
}

int posixCancelTimer(int32_t deviceId, va_list args) {
  (void) args;

  // Always validate capabilities first.  Only the scheduler (arming its own
  // preemption timer) gets an unconditional pass here.
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor != NULL) {
    if ((processDescriptor->processId != schedulerPid)
      && (findHalCapabilityWithDevice(processDescriptor->halCapabilities,
        processDescriptor->numHalCapabilities, HAL_TIMER, HAL_TIMER_CANCEL,
        deviceId) == NULL)
    ) {
      return -EACCES;
    }
  }

  if (deviceId >= _numTimers) {
    return -ERANGE;
  }

  SoftwareTimer *swTimer = &softwareTimers[deviceId];
  if (!swTimer->initialized) {
    return -EINVAL;
  }

  swTimer->active = false;
  reapTimerThread(swTimer);

  swTimer->startTime = 0;
  swTimer->deadline = 0;
  swTimer->callback = NULL;

  return 0;
}

int posixCancelAndGetTimer(int32_t deviceId, va_list args) {
  // We need to get `now` as close to the beginning of this function call as
  // possible so that any call to reconfigure the timer later is correct.
  int64_t now = 0;
  posixGetElapsedNanosecondsImpl(0, &now);

  uint64_t *configuredNanoseconds = va_arg(args, uint64_t*);
  uint64_t *remainingNanoseconds = va_arg(args, uint64_t*);
  void (**callback)(void) = va_arg(args, void (**)(void));

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

  swTimer->active = false;
  reapTimerThread(swTimer);

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
    pthread_mutex_lock(&_uartRx[1].mutex);
    posixUartRxPushLocked(&_uartRx[1], 0x03);
    pthread_mutex_unlock(&_uartRx[1].mutex);
    halCommonUartMarkReady(1);
  }
}

/// @var _resetBufferCopiedMessage
///
/// @brief Bootstrap trace message.  This function runs before HAL exists
/// (it computes *staticLogs itself), so it cannot use logError() - this is
/// intentional, not an oversight.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _resetBufferCopiedMessage[] KEEP_IN_FLASH
  = "resetBuffer copied.\n";

/// @var _topOfStackFormat
///
/// @brief Bootstrap trace format string.  See _resetBufferCopiedMessage
/// for why this stays a raw fprintf.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _topOfStackFormat[] KEEP_IN_FLASH
  = "Top of stack        = %p\n";

/// @var _bottomOfStackFormat
///
/// @brief Bootstrap trace format string.  See _resetBufferCopiedMessage
/// for why this stays a raw fprintf.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _bottomOfStackFormat[] KEEP_IN_FLASH
  = "Bottom of stack     = %p\n";

/// @var _globalStackAllocatedMessage
///
/// @brief Bootstrap trace message.  See _resetBufferCopiedMessage for why
/// this stays a raw fprintf.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _globalStackAllocatedMessage[] KEEP_IN_FLASH
  = "Global stack allocated\n";

/// @var _mmapErrorFormat
///
/// @brief Bootstrap error format string.  See _resetBufferCopiedMessage
/// for why this stays a raw fprintf.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _mmapErrorFormat[] KEEP_IN_FLASH
  = "mmap failed with error: %s\n";

/// @var _overlayMapFormat
///
/// @brief Bootstrap trace format string.  See _resetBufferCopiedMessage
/// for why this stays a raw fprintf.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _overlayMapFormat[] KEEP_IN_FLASH
  = "posixHal.overlayMap = %p\n";

/// @var _contiguousOverlayMapFormat
///
/// @brief Bootstrap trace format string.  See _resetBufferCopiedMessage
/// for why this stays a raw fprintf.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _contiguousOverlayMapFormat[] KEEP_IN_FLASH
  = "posixHal.contiguousOverlayMap = %p\n";

/// @var _newline
///
/// @brief A single newline character.  See _resetBufferCopiedMessage for
/// why this stays a raw fprintf.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _newline[] KEEP_IN_FLASH = "\n";

/// @var _tcgetattrSymbolName
///
/// @brief Symbol name looked up via dlsym() to get the real tcgetattr().
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _tcgetattrSymbolName[] KEEP_IN_FLASH = "tcgetattr";

/// @var _tcsetattrSymbolName
///
/// @brief Symbol name looked up via dlsym() to get the real tcsetattr().
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _tcsetattrSymbolName[] KEEP_IN_FLASH = "tcsetattr";

/// @fn void* posixCancelTimerWarmupThreadFunc(void *arg)
///
/// @brief No-op thread function used only to give
/// posixCancelTimerWarmupPthreadCancel something real to cancel.
static void* posixCancelTimerWarmupThreadFunc(void *arg) {
  (void) arg;
  return NULL;
}

/// @fn void posixCancelTimerWarmupPthreadCancel(void)
///
/// @brief Force glibc's one-time, lazy dlopen() of libgcc_s.so.1 (needed
/// internally by pthread_cancel() for stack-unwinding support) to happen
/// here, on the native thread's own large stack, instead of happening the
/// first time some coroutine calls HAL->timer->cancel() -> posixCancelTimer()
/// -> pthread_cancel() from deep inside its own tiny, recursively-carved
/// stack slice.  That dlopen recurses far enough into glibc's dynamic
/// loader to overflow a coroutine's slice; it's a one-time, process-wide
/// cost (cached by glibc after the first call), so paying it here, before
/// any coroutine stack exists, means no coroutine ever pays it.
///
/// @return This function returns no value.
static void posixCancelTimerWarmupPthreadCancel(void) {
  pthread_t warmupThread;
  if (pthread_create(
    &warmupThread, NULL, posixCancelTimerWarmupThreadFunc, NULL) != 0
  ) {
    return;
  }
  pthread_cancel(warmupThread);
  pthread_join(warmupThread, NULL);
}

/// @fn size_t posixDefaultMemorySize(uint8_t fileCacheDepth)
///
/// @brief Get the simulated memory size to use when none is given, grown to
/// hold an overlay file cache of the given depth.
///
/// @param fileCacheDepth The number of overlays the file cache will hold, or
///   0 for no file cache.
///
/// @return Returns the memory size in bytes.
size_t posixDefaultMemorySize(uint8_t fileCacheDepth) {
  return DEFAULT_MEMORY_SIZE
    + (fileCacheDepth * (OVERLAY_SIZE + POSIX_FILE_CACHE_NODE_OVERHEAD));
}

int halPosixImplInit(jmp_buf resetBuffer, size_t memorySize,
  NanoOsOverlayMap **overlayMap, size_t *overlaySize,
  NanoOsOverlayMap **contiguousFilesystem, size_t *contiguousFilesystemSize
) {
  // Set the handler for sigint so that it's passed to the running process in
  // NanoOs instead of the simulator.
  signal(SIGINT, sigintHandler);

  // See posixCancelTimerWarmupPthreadCancel's own comment for why this has
  // to happen here, before any coroutine's stack is carved out of the
  // global stack allocated below.
  posixCancelTimerWarmupPthreadCancel();

  // Save our reset context for later.
  memcpy(_resetBuffer, resetBuffer, sizeof(jmp_buf));
  fprintf(stdout, _resetBufferCopiedMessage);
  fflush(stdout);

  int topOfStack = 0;
  fprintf(stderr, _topOfStackFormat, (void*) &topOfStack);

  if (memorySize == 0) {
    memorySize = posixDefaultMemorySize(
      posixFileCacheRequested() ? POSIX_FILE_CACHE_DEPTH : 0);
  }
  _bottomOfHeap = (void*) (((uintptr_t) &topOfStack)
    - ((uintptr_t) (memorySize * DEBUG_MULTIPLIER)));
  fprintf(stderr, _bottomOfStackFormat, (void*) _bottomOfHeap);
  jmp_buf returnBuffer;
  if (setjmp(returnBuffer) == 0) {
    allocateGlobalStack(returnBuffer, NULL);
  }
  fprintf(stderr, _globalStackAllocatedMessage);

  // The size used in the mmap call has to be large enough to accommodate the
  // size used for the overlay, plus the offset into the overlay.  It also has
  // to be page aligned.  Do the appropriate math to get us what we need
  // without wasting too much space.
  long pageSize = sysconf(_SC_PAGESIZE);
  size_t overlayBaseSize
    = ((size_t) (OVERLAY_OFFSET + OVERLAY_SIZE + (pageSize - 1)))
    & ~((size_t) (pageSize - 1));

  void *mappedOverlay = mmap((void*) OVERLAY_BASE_ADDRESS,
    overlayBaseSize, PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
    -1, 0);
  if (mappedOverlay == MAP_FAILED) {
    fprintf(stderr, _mmapErrorFormat, strerror(errno));
    return -1;
  }

  // The address that the code is built around for both the Cortex-M0 and the
  // simulation code is OVERLAY_OFFSET bytes into the map we just made.
  *overlayMap = (NanoOsOverlayMap*) (OVERLAY_BASE_ADDRESS + OVERLAY_OFFSET);
  *overlaySize = OVERLAY_SIZE;

  fprintf(stderr, _overlayMapFormat, (void*) *overlayMap);
  fprintf(stderr, _newline);

  // Second, separate mapping for the contiguous filesystem overlay.  Unlike
  // the regular overlay region above, this one stays resident for the life
  // of the process instead of being swapped in and out per call, so it
  // can't share the same address.
  void *mappedContiguousOverlay = mmap((void*) CONTIGUOUS_OVERLAY_BASE_ADDRESS,
    CONTIGUOUS_OVERLAY_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
    -1, 0);
  if (mappedContiguousOverlay == MAP_FAILED) {
    fprintf(stderr, _mmapErrorFormat, strerror(errno));
    return -1;
  }
  _contiguousOverlayMap = mappedContiguousOverlay;
  *contiguousFilesystem = _contiguousOverlayMap;
  *contiguousFilesystemSize = CONTIGUOUS_OVERLAY_SIZE;

  fprintf(stderr, _contiguousOverlayMapFormat, _contiguousOverlayMap);
  fprintf(stderr, _newline);

  _mainThreadId = pthread_self();

  *((void**) &realTcgetattr) = dlsym(RTLD_NEXT, _tcgetattrSymbolName);
  *((void**) &realTcsetattr) = dlsym(RTLD_NEXT, _tcsetattrSymbolName);

  return 0;
}

#endif // __x86_64__
