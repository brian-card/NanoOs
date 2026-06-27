///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              05.16.2026
///
/// @file              Hal.h
///
/// @brief             Definitions common to all hardware abstraction layer
///                    (HAL)implementations.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
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
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef HAL_H
#define HAL_H

// Standard C includes
#include "setjmp.h"
#include "stdbool.h"
#include "stdint.h"

// NanoOs includes
#include "BlockDevice.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// @def online
///
/// @brief Function macro to determine whether or not an individual device
/// within a HAL subsystem is online.
///
/// @param hal Pointer to a HAL subsystem pointer.
/// @param deviceId The zero-based index of the device to check.
#define online(hal, deviceId) ( \
  ((deviceId >= 0) && (deviceId < ((int32_t) hal->numSupported))) \
  ? (((((uint32_t) 1) << (deviceId & 31)) & hal->online[deviceId >> 5]) != 0) \
  : false)

/// @def setOnline
///
/// @brief Mark a device ID as being online within a HAL subsystem.
///
/// @param hal Pointer to a HAL subsystem pointer.
/// @param deviceId The zero-based index of the device to mark online.
#define setOnline(hal, deviceId) \
  hal->online[deviceId >> 5] |= (((uint32_t) 1) << (deviceId & 31))

/// @def setOffline
///
/// @brief Mark a device ID as being offline within a HAL subsystem.
///
/// @param hal Pointer to a HAL subsystem pointer.
/// @param deviceId The zero-based index of the device to mark offline.
#define setOffline(hal, deviceId) \
  hal->online[deviceId >> 5] &= ~(((uint32_t) 1) << (deviceId & 31))
  
/// @enum HalPowerMode
///
/// @brief Power modes that can be entered by the HAL.
typedef enum HalPowerMode {
  HAL_POWER_MODE_OFF,
  HAL_POWER_MODE_SUSPEND,
  HAL_POWER_MODE_RESET,
  HAL_POWER_MODE_NUM_TYPES,
} HalPowerMode;

/// @enum HalSubsystem
///
/// @brief Identifiers for each subsystem in the HAL.
typedef enum HalSubsystem {
  HAL_MEMORY,
  HAL_UART,
  HAL_DIO,
  HAL_SPI,
  HAL_CLOCK,
  HAL_POWER,
  HAL_TIMER,
  HAL_BLOCK_DEVICE,
  HAL_NUM_SUBSYSTEMS,
} HalSubsystem;

/// @enum HalMemoryFunction
///
/// @brief Function indices for the HalMemory subsystem.
typedef enum HalMemoryFunction {
  HAL_MEMORY_PROCESS_STACK_SIZE,
  HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE,
  HAL_MEMORY_BOTTOM_OF_HEAP,
  HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS,
  HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS,
  HAL_MEMORY_NUM_FNS,
} HalMemoryFunction;

/// @enum HalUartFunction
///
/// @brief Function indices for the HalUart subsystem.
typedef enum HalUartFunction {
  HAL_UART_INIT,
  HAL_UART_CONFIGURE,
  HAL_UART_POLL,
  HAL_UART_WRITE,
  HAL_UART_IS_CONSOLE,
  HAL_UART_NUM_FNS,
} HalUartFunction;

/// @enum HalDioFunction
///
/// @brief Function indices for the HalDio subsystem.
typedef enum HalDioFunction {
  HAL_DIO_INIT,
  HAL_DIO_CONFIGURE,
  HAL_DIO_WRITE,
  HAL_DIO_NUM_FNS,
} HalDioFunction;

/// @enum HalSpiFunction
///
/// @brief Function indices for the HalSpi subsystem.
typedef enum HalSpiFunction {
  HAL_SPI_INIT,
  HAL_SPI_CONFIGURE,
  HAL_SPI_START_TRANSFER,
  HAL_SPI_END_TRANSFER,
  HAL_SPI_TRANSFER8,
  HAL_SPI_TRANSFER_BYTES,
  HAL_SPI_NUM_FNS,
} HalSpiFunction;

/// @enum HalClockFunction
///
/// @brief Function indices for the HalClock subsystem.
typedef enum HalClockFunction {
  HAL_CLOCK_INIT,
  HAL_CLOCK_SET_SYSTEM_TIME,
  HAL_CLOCK_GET_ELAPSED_MILLISECONDS,
  HAL_CLOCK_GET_ELAPSED_MICROSECONDS,
  HAL_CLOCK_GET_ELAPSED_NANOSECONDS,
  HAL_CLOCK_NUM_FNS,
} HalClockFunction;

/// @enum HalPowerFunction
///
/// @brief Function indices for the HalPower subsystem.
typedef enum HalPowerFunction {
  HAL_POWER_ENTER_MODE,
  HAL_POWER_NUM_FNS,
} HalPowerFunction;

/// @enum HalTimerFunction
///
/// @brief Function indices for the HalTimer subsystem.
typedef enum HalTimerFunction {
  HAL_TIMER_INIT,
  HAL_TIMER_INIT_DEVICE,
  HAL_TIMER_CONFIG_ONE_SHOT,
  HAL_TIMER_CONFIGURED_NANOSECONDS,
  HAL_TIMER_REMAINING_NANOSECONDS,
  HAL_TIMER_CANCEL,
  HAL_TIMER_CANCEL_AND_GET,
  HAL_TIMER_NUM_FNS,
} HalTimerFunction;

/// @enum HalBlockDeviceFunction
///
/// @brief Function indices for the HalBlockDevice subsystem.
typedef enum HalBlockDeviceFunction {
  HAL_BLOCK_DEVICE_INIT,
  HAL_BLOCK_DEVICE_GET,
  HAL_BLOCK_DEVICE_RESTART,
  HAL_BLOCK_DEVICE_NUM_FNS,
} HalBlockDeviceFunction;

// Standard C types
typedef intptr_t ssize_t;
typedef uintptr_t size_t;
struct timespec;

// NanoOs types
typedef struct NanoOsOverlayMap NanoOsOverlayMap;
typedef struct ProcessDescriptor ProcessDescriptor;
typedef struct SchedulerState SchedulerState;

/// @struct HalCapability
///
/// @brief Metadata to describe a HAL capability that a process has.
///
/// @param subsystemFunction A uint16_t with the HalSubsystem that the process
///   is authorized to use as the upper 8 bits and the function within that
///   subsystem that's authorized as the lower 8 bits.
/// @param deviceIds Bitmask of the device IDs that the process is authorized to
///   use the subsystem function with.
typedef struct HalCapability {
  uint16_t subsystemFunction;
  uint8_t deviceIds;
} HalCapability;

// HAL subsystems
typedef struct HalMemory {
  /// @fn int32_t processStackSize(bool debug, size_t *returnValue)
  ///
  /// @brief The size of a regular process's stack.
  ///
  /// @param debug Whether or not the debug stack size for processes should
  ///   be used.
  /// @param returnValue A pointer to a size_t that will hold the stack size
  ///   on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*processStackSize)(bool debug, size_t *returnValue);

  /// @fn int32_t memoryManagerStackSize(bool debug, size_t *returnValue)
  ///
  /// @brief The size of the memory manager process's stack.
  ///
  /// @param debug Whether or not the memory manager's debug stack size should
  ///   be used so that debug prints can work correclty without corrupting the
  ///   stack.
  /// @param returnValue A pointer to a size_t that will hold the stack size
  ///   on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*memoryManagerStackSize)(bool debug, size_t *returnValue);

  /// @fn int32_t bottomOfHeap(bool debug, void **returnValue)
  ///
  /// @brief The memmory manager needs to know where the bottom of the heap is
  /// so that it knows where to start allocating memory.
  ///
  /// @param debug Whether or not the debug bottom of heap should be used.
  /// @param returnValue A pointer to a void* that will hold the heap bottom
  ///   address on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*bottomOfHeap)(bool debug, void **returnValue);

  /// @fn int32_t numExtraSchedulerStacks(bool debug, uint8_t *returnValue)
  ///
  /// @brief Get the number of extra scheduler stacks that need to be
  /// provisioned during startup.
  ///
  /// @param debug Whether or not the debug number of extra scheduler stacks
  ///   should be used.
  /// @param returnValue A pointer to a uint8_t that will hold the count on
  ///   success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*numExtraSchedulerStacks)(bool debug, uint8_t *returnValue);

  /// @fn int32_t numExtraConsoleStacks(bool debug, uint8_t *returnValue)
  ///
  /// @brief Get the number of extra console stacks that need to be provisioned
  /// during startup.
  ///
  /// @param debug Whether or not the debug number of extra console stacks
  ///   should be used.
  /// @param returnValue A pointer to a uint8_t that will hold the count on
  ///   success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*numExtraConsoleStacks)(bool debug, uint8_t *returnValue);
  
  // Overlay definitions.
  
  /// @var overlayMap
  ///
  /// @brief Memory address where overlays will be loaded.
  NanoOsOverlayMap *overlayMap;
  
  /// @var overlaySize
  ///
  /// @brief The number of bytes available for the overlay.  This may be 0 on
  /// systems that don't support overlays.
  size_t overlaySize;
} HalMemory;

typedef struct HalUart {
  /// @var numSupported
  ///
  /// @brief The number of UART devices that are supported on the hardware.
  uint32_t numSupported;
  
  /// @var online
  ///
  /// @brief Bitmask array indicating which of the supported UARTs are online.
  /// Whether or not an individual UART is online can be found by:
  ///
  /// online(HAL->uart, deviceId)
  uint32_t *online;
  
  /// @fn int32_t init(void)
  ///
  /// @brief Initialize the UART subsystem.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*init)(void);
  
  /// @fn configure(int32_t deviceId, uint32_t baud)
  ///
  /// @brief Configure a UART device.
  ///
  /// @param device ID The zero-based ID of the UART to configure.
  /// @param baud The desired baud rate of the UART.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*configure)(int32_t deviceId, uint32_t baud);
  
  /// @fn int poll(int32_t deviceId)
  ///
  /// @brief Poll a UART for a single byte of data.
  ///
  /// @param deviceId The zero-based ID of the UART to read from.
  ///
  /// @return Returns the byte read, cast to an int32_t, on success, -errno on
  /// failure.
  int32_t (*poll)(int32_t deviceId);
  
  /// @fn int32_t write(int32_t deviceId, const uint8_t *data, ssize_t length,
  ///   ssize_t *returnValue)
  ///
  /// @brief Write data to a UART.
  ///
  /// @param deviceId The zero-based ID of the UART to read from.
  /// @param data A pointer to arbitrary bytes of data to write to the UART.
  /// @param length The number of bytes to write to the UART from the data
  ///   pointer.
  /// @param returnValue A pointer to a ssize_t that will hold the number of
  ///   bytes written on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*write)(int32_t deviceId, const uint8_t *data, ssize_t length,
    ssize_t *returnValue);

  /// @fn int32_t isConsole(int32_t deviceId, bool *returnValue)
  ///
  /// @brief Determine whether or not a given UART functions as a console.
  ///
  /// @param deviceId The zero-based ID of the UART to test.
  /// @param returnValue A pointer to a bool that will be set to true if the
  ///   UART is a console, false if not.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*isConsole)(int32_t deviceId, bool *returnValue);
} HalUart;

typedef struct HalDio {
  /// @var numSupported
  ///
  /// @brief The number of DIO devices that are supported on the hardware.
  uint32_t numSupported;
  
  /// @var online
  ///
  /// @brief Bitmask array indicating which of the supported DIOs are online.
  /// Whether or not an individual DIO is online can be found by:
  ///
  /// online(HAL->dio, deviceId)
  uint32_t *online;
  
  /// @fn int32_t init(void)
  ///
  /// @brief Initialize the DIO subsystem.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*init)(void);
  
  /// @fn int32_t configure(int32_t deviceId, bool output)
  ///
  /// @brief Configure a DIO for either input or output.
  ///
  /// @param deviceId An integer indicating the DIO to configure.
  /// @param output Whether the DIO should be configured for output (true) or
  ///   input (false).
  ///
  /// @return Returns 0 on success, -errno onfailure.
  int32_t (*configure)(int32_t deviceId, bool output);
  
  /// @fn int32_t write(int32_t deviceId, bool high)
  ///
  /// @brief Write either a high or low value to a DIO.  The DIO must be
  /// configured for output.
  ///
  /// @param deviceId An integer indicating the DIO to write the value to.
  /// @param high Whether the value to be written to the DIO should be high
  ///   (true) or low (false).
  ///
  /// @return Returns 0 on success, -errno onfailure.
  int32_t (*write)(int32_t deviceId, bool high);
} HalDio;

typedef struct HalSpi {
  /// @var numSupported
  ///
  /// @brief The number of SPI devices that are supported on the hardware.
  uint32_t numSupported;
  
  /// @var online
  ///
  /// @brief Bitmask array indicating which of the supported SPIs are online.
  /// Whether or not an individual SPI is online can be found by:
  ///
  /// online(HAL->spi, deviceId)
  uint32_t *online;
  
  /// @fn int32_t init(void)
  ///
  /// @brief Initialize the SPI subsystem.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*init)(void);
  
  /// @fn int32_t configure(int32_t deviceId,
  ///   uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo);
  ///
  /// @brief Initialize a SPI device on the system.
  ///
  /// @param deviceId The zero-based index of the SPI device to initialize.
  /// @param cs The DIO to use as the chip-select line.
  /// @param sck The DIO to use as the clock line.
  /// @param copi The DIO to use as the COPI line.
  /// @param cipo The DIO to use as the CIPO line.
  /// @param baud The baud rate the SPI is to run at.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*configure)(int32_t deviceId,
    uint8_t cs, uint8_t sck, uint8_t copi, uint8_t cipo, uint32_t baud);
  
  /// @fn int startTransfer(int deviceId)
  ///
  /// @brief Begin a transfer with a SPI device.
  ///
  /// @param deviceId The zero-based index of the SPI device to begin
  ///   transferring data with.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*startTransfer)(int32_t deviceId);
  
  /// @fn int32_t endTransfer(int32_t deviceId)
  ///
  /// @brief End a transfer with a SPI device.
  ///
  /// @param deviceId The zero-based index of the SPI device to halt
  ///   transferring data with.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*endTransfer)(int32_t deviceId);
  
  /// @fn int32_t transfer8(int32_t deviceId, uint8_t data)
  ///
  /// @brief Tranfer 8 bits (1 byte) between the SPI controller and a
  /// peripheral.
  ///
  /// @param deviceId The zero-based index of the SPI device to transfer data
  ///   with.
  /// @param data The 8-bit value to transfer to the peripheral.
  ///
  /// @return Returns a value in the range 0x00000000 to 0x000000ff
  /// corresponding to the 8 bits transferred from the device on success,
  /// -errno on failure.
  int32_t (*transfer8)(int32_t deviceId, uint8_t data);
  
  /// @fn int32_t transferBytes(int32_t deviceId,
  ///   uint8_t *data, uint32_t length)
  ///
  /// @brief Tranfer a buffer of 8-bit bytes between the SPI controller and a
  /// peripheral.
  ///
  /// @param deviceId The zero-based index of the SPI device to transfer data
  ///   with.
  /// @param data The bufffer of 8-bit values to transfer to the peripheral.
  /// @param length The number of bytes in the buffer to transfer.
  ///
  /// @return On success, 0 is returned and length bytes in the data buffer are
  /// replaced with the bytes that were transferred from the SPI peripheral.
  /// -errno is returned and the contents of the data buffer are undefined on
  /// failure.
  int32_t (*transferBytes)(int32_t deviceId, uint8_t *data, uint32_t length);
} HalSpi;

typedef struct HalClock {
  /// @fn int32_t init(void)
  ///
  /// @brief Initialize the time subsystem.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*init)(void);
  
  /// @fn int32_t setSystemTime(struct timespec *ts)
  ///
  /// @brief Set the current time on the system.
  ///
  /// @param ts A pointer to a struct timespec that contains the seconds and
  ///   nanoseconds since the epoch.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*setSystemTime)(struct timespec *ts);
  
  /// @fn int32_t getElapsedMilliseconds(int64_t startTime, int64_t *returnValue)
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
  /// @param returnValue A pointer to an int64_t that will hold the elapsed
  ///   milliseconds on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*getElapsedMilliseconds)(int64_t startTime, int64_t *returnValue);

  /// @fn int32_t getElapsedMicroseconds(int64_t startTime, int64_t *returnValue)
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
  /// @param returnValue A pointer to an int64_t that will hold the elapsed
  ///   microseconds on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*getElapsedMicroseconds)(int64_t startTime, int64_t *returnValue);

  /// @fn int32_t getElapsedNanoseconds(int64_t startTime, int64_t *returnValue)
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
  /// @param returnValue A pointer to an int64_t that will hold the elapsed
  ///   nanoseconds on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*getElapsedNanoseconds)(int64_t startTime, int64_t *returnValue);
} HalClock;

typedef struct HalPower {
  /// @fn int32_t enterMode(HalPowerMode powerMode)
  ///
  /// @brief Enter a given power mode for the system.
  ///
  /// @param powerMode The power mode to be entered.
  ///
  /// @return On success, either does not return or blocks until the mode is
  /// exited.  On error, -errno will be returned.
  int32_t (*enterMode)(HalPowerMode powerMode);
} HalPower;

typedef struct HalTimer {
  /// @var numSupported
  ///
  /// @brief The number of timer devices that are supported on the hardware.
  uint32_t numSupported;
  
  /// @var online
  ///
  /// @brief Bitmask array indicating which of the supported timers are online.
  /// Whether or not an individual timer is online can be found by:
  ///
  /// online(HAL->timer, deviceId)
  uint32_t *online;
  
  /// @fn int32_t init(void)
  ///
  /// @brief Initialize the timer subsystem.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*init)(void);
  
  /// @fn int32_t initDevice(int32_t deviceId)
  ///
  /// @brief Initialize one of the system timers.
  ///
  /// @param deviceId The zero-based ID of the timer to initialize.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*initDevice)(int32_t deviceId);
  
  /// @fn int32_t configOneShot(int32_t deviceId,
  ///   uint64_t nanoseconds, void (*callback)(void))
  ///
  /// @brief Configure a hardware timer to fire at some point in the future and
  /// call a callback.
  ///
  /// @param deviceId The zero-based ID of the timer to configure.
  /// @param nanoseconds The number of nanoseconds in the future the timer
  ///   should fire.
  /// @param callback The function to call when the timer fires.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*configOneShot)(int32_t deviceId,
    uint64_t nanoseconds, void (*callback)(void));
  
  /// @fn int32_t configuredNanoseconds(int32_t deviceId, uint64_t *returnValue)
  ///
  /// @brief Get the number of nanoseconds a timer is configured to wait.
  ///
  /// @param deviceId The zero-based ID of the timer to interrogate.
  /// @param returnValue A pointer to a uint64_t that will hold the configured
  ///   nanoseconds on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*configuredNanoseconds)(int32_t deviceId, uint64_t *returnValue);

  /// @fn int32_t remainingNanoseconds(int32_t deviceId, uint64_t *returnValue)
  ///
  /// @brief Get the remaining number of nanoseconds before a timer fires.
  ///
  /// @param deviceId The zero-based ID of the timer to interrogate.
  /// @param returnValue A pointer to a uint64_t that will hold the remaining
  ///   nanoseconds on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*remainingNanoseconds)(int32_t deviceId, uint64_t *returnValue);
  
  /// @fn int32_t cancel(int32_t deviceId)
  ///
  /// @brief Cancel a timer that's currently configured.
  ///
  /// @param deviceId The zero-based ID of the timer to cancel.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*cancel)(int32_t deviceId);
  
  /// @fn int32_t cancelAndGet(int32_t deviceId,
  ///   uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
  ///   void (**callback)(void))
  ///
  /// @brief Cancel a timer and get its configuration.  It's expected that this
  /// is to be called in order to make sure that an operation is atomic and
  /// that the caller will call configTimer after doing its work.
  ///
  /// @param deviceId The zero-based ID of the timer to cancel and retrieve.
  /// @param configuredNanoseconds A pointer to a uint64_t that will hold the
  ///   number of nanoseconds the timer is configured for, if any.
  /// @param remainingNanoseconds A pointer to a uint64_t that will hold the
  ///   number of nanoseconds remaining for the timer, if any.
  /// @param callback A pointer to a callback function pointer that will be
  ///   populated with the callback that the timer was going to call, if any.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*cancelAndGet)(int32_t deviceId,
    uint64_t *configuredNanoseconds, uint64_t *remainingNanoseconds,
    void (**callback)(void));
} HalTimer;

typedef struct HalBlockDevice {
  /// @var numSupported
  ///
  /// @brief The number of block devices that are supported on the hardware.
  uint32_t numSupported;
  
  /// @var online
  ///
  /// @brief Bitmask array indicating which of the supported block devices are
  /// online. Whether or not an individual block device is online can be found
  /// by:
  ///
  /// online(HAL->blockDevice, deviceId)
  uint32_t *online;
  
  /// @fn int32_t init(void)
  ///
  /// @brief Initialize the block device subsystem.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*init)(void);
  
  /// @fn int32_t get(int32_t deviceId, BlockDevice **returnValue)
  ///
  /// @brief Get one of the block devices managed by the HAL.
  ///
  /// @param deviceId The zero-based ID of the block device to get.
  /// @param returnValue A pointer to a BlockDevice* that will hold the device
  ///   pointer on success.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*get)(int32_t deviceId, BlockDevice **returnValue);

  /// @fn int restart(ProcessDescriptor *processDescriptor)
  ///
  /// @brief Restart the block device process associated with the given
  /// ProcessDescriptor.  The zero-based device ID to restart is taken from
  /// processDescriptor->restartArgs, cast to an intptr_t.
  ///
  /// @param processDescriptor A pointer to the ProcessDescriptor of the block
  ///   device process to restart.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*restart)(ProcessDescriptor *processDescriptor);
} HalBlockDevice;

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
  
  /// @var blockDevice
  ///
  /// @brief Pointer to the HalBlockDevice managed by the HAL, or NULL if there
  /// isn't one.
  HalBlockDevice *blockDevice;
  
  // Root storage configuration.
  
  /// @fn int initRootStorage(void)
  ///
  /// @brief Initialize the processes that operate the root storage system.
  ///
  /// @return Returns 0 on success, -errno on failure.
  int32_t (*initRootStorage)(void);
} Hal;

extern const Hal *HAL;

HalCapability* findHalCapability(
  HalCapability *capabilities, size_t numCapabilities,
  HalSubsystem subsystem, uint32_t function);
bool currentProcessHasHalCapability(
  HalSubsystem subsystem, uint32_t function);
HalCapability* findHalCapabilityWithDevice(
  HalCapability *capabilities, size_t numCapabilities,
  HalSubsystem subsystem, uint32_t function, int32_t deviceId);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_H

