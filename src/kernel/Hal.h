///////////////////////////////////////////////////////////////////////////////
///
/// @file              Hal.h
///
/// @brief             Definitions common to all hardware abstraction layer
///                    (HAL)implementations.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef HAL_H
#define HAL_H

// Standard C includes
#include "setjmp.h"
#include "stdbool.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @enum HalShutdownType
///
/// @brief Types of shutdowns that can be invoked in the HAL.
typedef enum HalShutdownType {
  HAL_SHUTDOWN_OFF,
  HAL_SHUTDOWN_SUSPEND,
  HAL_SHUTDOWN_RESET,
  HAL_SHUTDOWN_NUM_TYPES,
} HalShutdownType;

// Standard C types
typedef intptr_t ssize_t;
struct timespec;

// NanoOs types
typedef struct NanoOsOverlayMap NanoOsOverlayMap;
typedef struct SchedulerState SchedulerState;

typedef struct HalMemory {
  /// @fn uintptr_t processStackSize(bool debug)
  ///
  /// @brief The size of a regular process's stack.
  ///
  /// @param debug Whether or not the debug stack size for processes should
  ///   be used.
  ///
  /// Returns the size of the stack to use for all non-memory manager
  /// processes in bytes.  This function never fails.
  uintptr_t (*processStackSize)(bool debug);
  
  /// @fn uintptr_t memoryManagerStackSize(bool debug)
  ///
  /// @brief The size of the memory manager process's stack.
  ///
  /// @param debug Whether or not the memory manager's debug stack size should
  ///   be used so that debug prints can work correclty without corrupting the
  ///   stack.
  ///
  /// @return Returns the size of the stack to use for the memory manager.
  /// This call never fails.
  uintptr_t (*memoryManagerStackSize)(bool debug);
  
  /// @fn void* bottomOfHeap(void)
  ///
  /// @brief The memmory manager needs to know where the bottom of the heap is
  /// so that it knows where to start allocating memory.
  ///
  /// @param debug Whether or not the debug bottom of heap should be used.
  ///
  /// @return Returns the address of the bottom of the heap.   This call never
  /// fails.
  void* (*bottomOfHeap)(bool debug);
  
  /// @fn uint8_t numExtraSchedulerStacks(bool debug)
  ///
  /// @brief Get the number of extra scheduler stacks that need to be
  /// provisioned during startup.
  ///
  /// @param debug Whether or not the debug number of extra scheduler stacks
  ///   should be used.
  ///
  /// Returns the number of extra scheduler stacks that need to be provisioned
  /// when starting the scheduler.  This function never fails.
  uint8_t (*numExtraSchedulerStacks)(bool debug);
  
  /// @fn uint8_t numExtraConsoleStacks(bool debug)
  ///
  /// @brief Get the number of extra console stacks that need to be provisioned
  /// during startup.
  ///
  /// @param debug Whether or not the debug number of extra console stacks
  ///   should be used.
  ///
  /// Returns the number of extra console stacks that need to be provisioned
  /// when starting the scheduler.  This function never fails.
  uint8_t (*numExtraConsoleStacks)(bool debug);
  
  // Overlay definitions.
  
  /// @var overlayMap
  ///
  /// @brief Memory address where overlays will be loaded.
  NanoOsOverlayMap *overlayMap;
  
  /// @var overlaySize
  ///
  /// @brief The number of bytes available for the overlay.  This may be 0 on
  /// systems that don't support overlays.
  uintptr_t overlaySize;
} HalMemory;
  
typedef struct HalUart {
  /// @fn int getNum(void)
  ///
  /// @brief Get the number of addressable and configurable serial ports on the
  /// system.
  ///
  /// @return Returns the number of serial ports on the system (which may be 0)
  /// on success, -errno on failure.
  int (*getNum)(void);
  
  /// @fn int setNum(int numUarts)
  ///
  /// @brief Set the number of serial ports that is to be returned by
  /// getNumUarts.
  ///
  /// @param numUarts The value to be returned by getNumUarts.
  ///   This may be a non-negative value that is less-than or equal-to the
  ///   value initially returned by getNumUarts or a -errno value that
  ///   the function is to return.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*setNum)(int numUarts);
  
  /// @fn init(int port, int32_t baud)
  ///
  /// @brief Initialize a hardware serial port.
  ///
  /// @param port The zero-based index of the port to initialize.
  /// @param baud The desired baud rate of the port.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*init)(int port, int32_t baud);
  
  /// @fn int poll(int port)
  ///
  /// @brief Poll a serial port for a single byte of data.
  ///
  /// @param port The zero-based index of the port to read from.
  ///
  /// @return Returns the byte read, cast to an int, on success, -errno on
  /// failure.
  int (*poll)(int port);
  
  /// @fn ssize_t write(int port, const uint8_t *data, ssize_t length)
  ///
  /// @brief Write data to a serial port.
  ///
  /// @param port The zero-based index of the port to read from.
  /// @param data A pointer to arbitrary bytes of data to write to the serial
  ///   port.
  /// @param length The number of bytes to write to the serial port from the
  ///   data pointer.
  ///
  /// @return Returns the number of bytes written on success, -errno on failure.
  ssize_t (*write)(int port, const uint8_t *data, ssize_t length);
  
  /// @fn bool isConsole(int port);
  ///
  /// @brief Determine whether or not a given UART port functions as a console.
  ///
  /// @param port The zero-based index of the port to read from.
  ///
  /// @return Returns true if the UART is a console, false if not.
  bool (*isConsole)(int port);
} HalUart;

typedef struct HalDio {
  /// @fn int getNum(void)
  ///
  /// @brief Get the number of digial IO pins on the system.
  ///
  /// @return Returns the number of digital IO pins on success, -errno on
  /// failure.
  int (*getNum)(void);
  
  /// @fn int init(int dio, bool output)
  ///
  /// @brief Configure a DIO for either input or output.
  ///
  /// @param dio An integer indicating the DIO to configure.
  /// @param output Whether the DIO should be configured for output (true) or
  ///   input (false).
  ///
  /// @return Returns 0 on success, -errno onfailure.
  int (*init)(int dio, bool output);
  
  /// @fn int write(int dio, bool high)
  ///
  /// @brief Write either a high or low value to a DIO.  The DIO must be
  /// configured for output.
  ///
  /// @param dio An integer indicating the DIO to write the value to.
  /// @param high Whether the value to be written to the DIO should be high
  ///   (true) or low (false).
  ///
  /// @return Returns 0 on success, -errno onfailure.
  int (*write)(int dio, bool high);
} HalDio;

typedef struct HalSpi {
  /// @fn int initDevice(int spi,
  ///   uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo);
  ///
  /// @brief Initialize a SPI device on the system.
  ///
  /// @param spi The zero-based index of the SPI device to initialize.
  /// @param cs The DIO to use as the chip-select line.
  /// @param sck The DIO to use as the clock line.
  /// @param copi The DIO to use as the COPI line.
  /// @param cipo The DIO to use as the CIPO line.
  /// @param baud The baud rate the SPI is to run at.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*initDevice)(int spi,
    uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud);
  
  /// @fn int startTransfer(int spi)
  ///
  /// @brief Begin a transfer with a SPI device.
  ///
  /// @param spi The zero-based index of the SPI device to begin transferring
  /// data with.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*startTransfer)(int spi);
  
  /// @fn int endTransfer(int spi)
  ///
  /// @brief End a transfer with a SPI device.
  ///
  /// @param spi The zero-based index of the SPI device to halt transferring
  /// data with.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*endTransfer)(int spi);
  
  /// @fn int transfer8(int spi, uint8_t data)
  ///
  /// @brief Tranfer 8 bits (1 byte) between the SPI controller and a
  /// peripheral.
  ///
  /// @param spi The zero-based index of the SPI device to transfer data with.
  /// @param data The 8-bit value to transfer to the peripheral.
  ///
  /// @return Returns a value in the range 0x00000000 to 0x000000ff
  /// corresponding to the 8 bits transferred from the device on success,
  /// -errno on failure.
  int (*transfer8)(int spi, uint8_t data);
  
  /// @fn int transferBytes(int spi, uint8_t *data, uint32_t length)
  ///
  /// @brief Tranfer a buffer of 8-bit bytes between the SPI controller and a
  /// peripheral.
  ///
  /// @param spi The zero-based index of the SPI device to transfer data with.
  /// @param data The bufffer of 8-bit values to transfer to the peripheral.
  /// @param length The number of bytes in the buffer to transfer.
  ///
  /// @return On success, 0 is returned and length bytes in the data buffer are
  /// replaced with the bytes that were transferred from the SPI peripheral.
  /// -errno is returned and the contents of the data buffer are undefined on
  /// failure.
  int (*transferBytes)(int spi, uint8_t *data, uint32_t length);
} HalSpi;

typedef struct HalClock {
  /// @fn int setSystemTime(struct timespec *ts)
  ///
  /// @brief Set the current time on the system.
  ///
  /// @param ts A pointer to a struct timespec that contains the seconds and
  ///   nanoseconds since the epoch.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*setSystemTime)(struct timespec *ts);
  
  /// @fn int64_t getElapsedMilliseconds(int64_t startTime)
  ///
  /// @brief Get the number of milliseconds that have elapsed since the
  /// provided start time.
  ///
  /// @param startTime The initial number of milliseconds to measure against.
  ///   If this value is 0, then the value returned is the number of
  ///   milliseconds that have elapsed since the start of the epoch.  Note:  If
  ///   the system time has not yet been set then providing a startTime of 0
  ///   will yield the number of milliseconds that the system has been up
  ///   instead of the number of milliseconds since the start of the epoch.
  ///
  /// @return Returns the number of milliseconds that have elapsed since the
  /// provided start time on success, -1 on failure.
  int64_t (*getElapsedMilliseconds)(int64_t startTime);
  
  /// @fn int64_t getElapsedMicroseconds(int64_t startTime)
  ///
  /// @brief Get the number of microseconds that have elapsed since the
  /// provided start time.
  ///
  /// @param startTime The initial number of microseconds to measure against.
  ///   If this value is 0, then the value returned is the number of
  ///   microseconds that have elapsed since the start of the epoch.  Note:  If
  ///   the system time has not yet been set then providing a startTime of 0
  ///   will yield the number of microseconds that the system has been up
  ///   instead of the number of microseconds since the start of the epoch.
  ///
  /// @return Returns the number of microseconds that have elapsed since the
  /// provided start time on success, -1 on failure.
  int64_t (*getElapsedMicroseconds)(int64_t startTime);
  
  /// @fn int64_t getElapsedNanoseconds(int64_t startTime)
  ///
  /// @brief Get the number of nanoseconds that have elapsed since the
  /// provided start time.
  ///
  /// @param startTime The initial number of nanoseconds to measure against.
  ///   If this value is 0, then the value returned is the number of
  ///   nanoseconds that have elapsed since the start of the epoch.  Note:  If
  ///   the system time has not yet been set then providing a startTime of 0
  ///   will yield the number of nanoseconds that the system has been up
  ///   instead of the number of nanoseconds since the start of the epoch.
  ///
  /// @return Returns the number of nanoseconds that have elapsed since the
  /// provided start time on success, -1 on failure.
  int64_t (*getElapsedNanoseconds)(int64_t startTime);
} HalClock;

typedef struct HalPower {
  /// @fn int shutdown(HalShutdownType shutdownType)
  ///
  /// @brief Halt the OS and invoke the specified power action.
  ///
  /// @param shutdownType The power action to be invoked.
  ///
  /// @return Does not return or returns 0 on success.  On error, -errno will
  /// be returned.
  int (*shutdown)(HalShutdownType shutdownType);
} HalPower;

typedef struct HalTimer {
  /// @fn int getNum(void)
  ///
  /// @brief Get the number of available hardware timers on the system.
  ///
  /// @return Returns the number of available hardware timers on success,
  /// -errno on failure.
  int (*getNum)(void);
  
  /// @fn int setNum(int numTimers)
  ///
  /// @brief Set the number of hardware timers that is to be returned by
  /// getNumTimers.
  ///
  /// @param numTimers The value to be returned by getNumTimers.
  ///   This may be a non-negative value that is less-than or equal-to the
  ///   value initially returned by getNumTimers or a -errno value that
  ///   the function is to return.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*setNum)(int numTimers);
  
  /// @fn int init(int timer)
  ///
  /// @brief Initialize one of the system timers.
  ///
  /// @param timer The zero-based index of the timer to initialize.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*init)(int timer);
  
  /// @fn int configOneShot(int timer,
  ///   uint64_t nanoseconds, void (*callback)(void))
  ///
  /// @brief Configure a hardware timer to fire at some point in the future and
  /// call a callback.
  ///
  /// @param timer The zero-based index of the timer to configure.
  /// @param nanoseconds The number of nanoseconds in the future the timer
  ///   should fire.
  /// @param callback The function to call when the timer fires.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*configOneShot)(int timer,
    uint64_t nanoseconds, void (*callback)(void));
  
  /// @fn uint64_t configuredNanoseconds(int timer)
  ///
  /// @brief Get the number of nanoseconds a timer is configured to wait.
  ///
  /// @param timer The zero-based index of the timer to interrogate.
  ///
  /// @return Returns the number of nanoseconds configured for a timer.
  uint64_t (*configuredNanoseconds)(int timer);
  
  /// @fn uint64_t remainingNanoseconds(int timer)
  ///
  /// @brief Get the remaining number of nanoseconds before a timer fires.
  ///
  /// @param timer The zero-based index of the timer to interrogate.
  ///
  /// @return Returns the number of nanoseconds remaining for a timer.
  uint64_t (*remainingNanoseconds)(int timer);
  
  /// @fn int cancel(int timer)
  ///
  /// @brief Cancel a timer that's currently configured.
  ///
  /// @param timer The zero-based index of the timer to cancel.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*cancel)(int timer);
  
  /// @fn int cancelAndGet(int timer,
  ///   uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  ///   void (**callback)(void))
  ///
  /// @brief Cancel a timer and get its configuration.  It's expected that this
  /// is to be called in order to make sure that an operation is atomic and
  /// that the caller will call configTimer after doing its work.
  ///
  /// @param timer The zero-based index of the timer to cancel and retrieve.
  /// @param configuredNanoseconds A pointer to a uint64_t that will hold the
  ///   number of nanoseconds the timer is configured for, if any.
  /// @param remainingNanoseconds A pointer to a uint64_t that will hold the
  ///   number of nanoseconds remaining for the timer, if any.
  /// @param callback A pointer to a callback function pointer that will be
  ///   populated with the callback that the timer was going to call, if any.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*cancelAndGet)(int timer,
    uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
    void (**callback)(void));
} HalTimer;

typedef struct Hal {
  /// @var memory
  ///
  /// @brief Pointer to the HalMemory managed by the HAL, or NULL if there isn't
  /// one.  In the case of this HAL, there should always be one.
  HalMemory *memory;
  
  /// @var uart
  ///
  /// @brief Pointer to the HalUart managed by the HAL, or NULL if there
  /// isn't one.
  HalUart *uart;
  
  /// @var dio
  ///
  /// @brief Pointer to the HalDio managed by the HAL, or NULL if there isn't
  /// one.
  HalDio *dio;
  
  /// @var spi
  ///
  /// @brief Pointer to the HalSpi managed by the HAL, or NULL if there isn't
  /// one.
  HalSpi *spi;
  
  /// @var clock
  ///
  /// @brief Pointer to the HalClock managed by the HAL, or NULL if there isn't
  /// one.
  HalClock *clock;
  
  /// @var power
  ///
  /// @brief Pointer to the HalPower managed by the HAL, or NULL if there isn't
  /// one.
  HalPower *power;
  
  /// @var timer
  ///
  /// @brief Pointer to the HalTimer managed by HAL, or NULL if there isn't
  /// one.
  HalTimer *timer;
  
  // Root storage configuration.
  
  /// @fn int initRootStorage(SchedulerState *schedulerState)
  ///
  /// @brief Initialize the processes that operate the root storage system.
  ///
  /// @param schedulerState A pointer to the SchedulerState to use to initialize
  ///   the processes.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int (*initRootStorage)(SchedulerState *schedulerState);
} Hal;

extern const Hal *HAL;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_H

