///////////////////////////////////////////////////////////////////////////////
///
/// @file              MockTimer.c
///
/// @brief             Synchronous one-shot timers for the mock HAL.  A timer
///                    never fires on its own; the test calls mockTimerFire()
///                    to invoke the pending callback.  This makes preemption
///                    points explicit and deterministic.
///
///////////////////////////////////////////////////////////////////////////////

#include <stdbool.h>

#include "HalMock.h"

#include "MockSubsystems.h"

/// @def MOCK_NUM_TIMERS
///
/// @brief Number of mock timer devices.  Matches the two the POSIX/SAMD21
/// HALs expose.
#define MOCK_NUM_TIMERS 2

/// @struct MockTimerDevice
typedef struct MockTimerDevice {
  bool     armed;
  uint64_t configuredNs;
  uint64_t deadlineNs;
  void   (*callback)(void);
} MockTimerDevice;

static MockTimerDevice _timers[MOCK_NUM_TIMERS];
static unsigned int _cancelCount = 0;

void mockTimerReset(void) {
  _cancelCount = 0;
  for (int ii = 0; ii < MOCK_NUM_TIMERS; ii++) {
    _timers[ii].armed        = false;
    _timers[ii].configuredNs = 0;
    _timers[ii].deadlineNs   = 0;
    _timers[ii].callback     = NULL;
  }
}

/// @fn static bool validDevice(int32_t deviceId)
static bool validDevice(int32_t deviceId) {
  return (deviceId >= 0) && (deviceId < MOCK_NUM_TIMERS);
}

int mockTimerFire(int32_t deviceId) {
  if (!validDevice(deviceId) || !_timers[deviceId].armed) {
    return -1;
  }
  void (*callback)(void) = _timers[deviceId].callback;
  _timers[deviceId].armed    = false;
  _timers[deviceId].callback = NULL;
  if (callback != NULL) {
    callback();
  }
  return 0;
}

int mockTimerInitFn(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  mockTimerReset();
  return 0;
}

int mockTimerInitDeviceFn(int32_t deviceId, va_list args) {
  (void) args;
  return validDevice(deviceId) ? 0 : -1;
}

int mockTimerConfigOneShotFn(int32_t deviceId, va_list args) {
  uint64_t nanoseconds = va_arg(args, uint64_t);
  void (*callback)(void) = va_arg(args, void (*)(void));
  if (!validDevice(deviceId)) {
    return -1;
  }
  _timers[deviceId].armed        = true;
  _timers[deviceId].configuredNs = nanoseconds;
  _timers[deviceId].deadlineNs   = mockClockNowNs() + nanoseconds;
  _timers[deviceId].callback     = callback;
  return 0;
}

int mockTimerConfiguredNanosecondsFn(int32_t deviceId, va_list args) {
  uint64_t *returnValue = va_arg(args, uint64_t*);
  if (!validDevice(deviceId) || (returnValue == NULL)) {
    return -1;
  }
  *returnValue = _timers[deviceId].configuredNs;
  return 0;
}

int mockTimerRemainingNanosecondsFn(int32_t deviceId, va_list args) {
  uint64_t *returnValue = va_arg(args, uint64_t*);
  if (!validDevice(deviceId) || (returnValue == NULL)) {
    return -1;
  }
  if (!_timers[deviceId].armed) {
    *returnValue = 0;
    return 0;
  }
  uint64_t now = mockClockNowNs();
  *returnValue = (_timers[deviceId].deadlineNs > now)
    ? (_timers[deviceId].deadlineNs - now) : 0;
  return 0;
}

bool mockTimerArmed(int32_t deviceId) {
  return validDevice(deviceId) && _timers[deviceId].armed;
}

unsigned int mockTimerCancelCount(void) {
  return _cancelCount;
}

int mockTimerCancelFn(int32_t deviceId, va_list args) {
  (void) args;
  _cancelCount++;
  if (!validDevice(deviceId)) {
    return -1;
  }
  _timers[deviceId].armed    = false;
  _timers[deviceId].callback = NULL;
  return 0;
}

int mockTimerCancelAndGetFn(int32_t deviceId, va_list args) {
  uint64_t *configuredNanoseconds = va_arg(args, uint64_t*);
  uint64_t *remainingNanoseconds = va_arg(args, uint64_t*);
  void (**callback)(void) = va_arg(args, void (**)(void));
  if (!validDevice(deviceId)) {
    return -1;
  }
  uint64_t now = mockClockNowNs();
  if (configuredNanoseconds != NULL) {
    *configuredNanoseconds = _timers[deviceId].configuredNs;
  }
  if (remainingNanoseconds != NULL) {
    *remainingNanoseconds = (_timers[deviceId].armed
      && (_timers[deviceId].deadlineNs > now))
      ? (_timers[deviceId].deadlineNs - now) : 0;
  }
  if (callback != NULL) {
    *callback = _timers[deviceId].callback;
  }
  _timers[deviceId].armed    = false;
  _timers[deviceId].callback = NULL;
  return 0;
}
