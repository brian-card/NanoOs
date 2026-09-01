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

// Each of these matches the HalFunction signature: int32_t fn(va_list).

// --- clock (MockClock.c) -----------------------------------------------
void    mockClockReset(void);
int32_t mockClockInitFn(va_list args);
int32_t mockClockSetSystemTimeFn(va_list args);
int32_t mockClockGetElapsedMillisecondsFn(va_list args);
int32_t mockClockGetElapsedMicrosecondsFn(va_list args);
int32_t mockClockGetElapsedNanosecondsFn(va_list args);

// --- timer (MockTimer.c) ---------------------------------------------
void    mockTimerReset(void);
int32_t mockTimerInitFn(va_list args);
int32_t mockTimerInitDeviceFn(va_list args);
int32_t mockTimerConfigOneShotFn(va_list args);
int32_t mockTimerConfiguredNanosecondsFn(va_list args);
int32_t mockTimerRemainingNanosecondsFn(va_list args);
int32_t mockTimerCancelFn(va_list args);
int32_t mockTimerCancelAndGetFn(va_list args);

// --- uart (MockUart.c) ---------------------------------------------
void    mockUartReset(void);
int32_t mockUartInitFn(va_list args);
int32_t mockUartConfigureFn(va_list args);
int32_t mockUartPollFn(va_list args);
int32_t mockUartWriteFn(va_list args);
int32_t mockUartIsConsoleFn(va_list args);

// --- block device (MockBlockDevice.c) -------------------------------
void    mockBlockDeviceReset(void);
int32_t mockBlockDeviceInitFn(va_list args);
int32_t mockBlockDeviceGetFn(va_list args);
int32_t mockBlockDeviceRestartFn(va_list args);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MOCK_SUBSYSTEMS_H
