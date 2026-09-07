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
int mockClockInitFn(va_list args);
int mockClockSetSystemTimeFn(va_list args);
int mockClockGetElapsedMillisecondsFn(va_list args);
int mockClockGetElapsedMicrosecondsFn(va_list args);
int mockClockGetElapsedNanosecondsFn(va_list args);

// --- timer (MockTimer.c) ---------------------------------------------
void    mockTimerReset(void);
int mockTimerInitFn(va_list args);
int mockTimerInitDeviceFn(va_list args);
int mockTimerConfigOneShotFn(va_list args);
int mockTimerConfiguredNanosecondsFn(va_list args);
int mockTimerRemainingNanosecondsFn(va_list args);
int mockTimerCancelFn(va_list args);
int mockTimerCancelAndGetFn(va_list args);

// --- uart (MockUart.c) ---------------------------------------------
void    mockUartReset(void);
int mockUartInitFn(va_list args);
int mockUartConfigureFn(va_list args);
int mockUartPollFn(va_list args);
int mockUartWriteFn(va_list args);
int mockUartIsConsoleFn(va_list args);

// --- block device (MockBlockDevice.c) -------------------------------
void    mockBlockDeviceReset(void);
int mockBlockDeviceInitFn(va_list args);
int mockBlockDeviceGetFn(va_list args);
int mockBlockDeviceRestartFn(va_list args);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MOCK_SUBSYSTEMS_H
