///////////////////////////////////////////////////////////////////////////////
///
/// @file              MockSubsystems.h
///
/// @brief             Internal interfaces shared between the mock HAL subsystem
///                    implementations (MockClock/MockTimer/MockUart/
///                    MockBlockDevice) and HalMock.c.  Not part of the public
///                    harness API - test code includes HalMock.h.
///
///////////////////////////////////////////////////////////////////////////////

#ifndef MOCK_SUBSYSTEMS_H
#define MOCK_SUBSYSTEMS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Each of these matches the HalFunction signature: int fn(va_list).

// --- clock (MockClock.c) -----------------------------------------------
void    mockClockReset(void);
int mockClockInitFn(int32_t deviceId, va_list args);
int mockClockSetSystemTimeFn(int32_t deviceId, va_list args);
int mockClockGetElapsedMillisecondsFn(int32_t deviceId, va_list args);
int mockClockGetElapsedMicrosecondsFn(int32_t deviceId, va_list args);
int mockClockGetElapsedNanosecondsFn(int32_t deviceId, va_list args);

// --- timer (MockTimer.c) ---------------------------------------------
void    mockTimerReset(void);
int mockTimerInitFn(int32_t deviceId, va_list args);
int mockTimerInitDeviceFn(int32_t deviceId, va_list args);
int mockTimerConfigOneShotFn(int32_t deviceId, va_list args);
int mockTimerConfiguredNanosecondsFn(int32_t deviceId, va_list args);
int mockTimerRemainingNanosecondsFn(int32_t deviceId, va_list args);
int mockTimerCancelFn(int32_t deviceId, va_list args);
int mockTimerCancelAndGetFn(int32_t deviceId, va_list args);

// --- uart (MockUart.c) ---------------------------------------------
void    mockUartReset(void);
int mockUartInitFn(int32_t deviceId, va_list args);
int mockUartConfigureFn(int32_t deviceId, va_list args);
int mockUartPollFn(int32_t deviceId, va_list args);
int mockUartReadFn(int32_t deviceId, va_list args);
int mockUartWriteFn(int32_t deviceId, va_list args);
int mockUartIsConsoleFn(int32_t deviceId, va_list args);

// --- block device (MockBlockDevice.c) -------------------------------
void    mockBlockDeviceReset(void);
int mockBlockDeviceInitFn(int32_t deviceId, va_list args);
int mockBlockDeviceGetFn(int32_t deviceId, va_list args);
int mockBlockDeviceReadBlocksFn(int32_t deviceId, va_list args);
int mockBlockDeviceWriteBlocksFn(int32_t deviceId, va_list args);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MOCK_SUBSYSTEMS_H
