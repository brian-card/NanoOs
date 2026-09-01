///////////////////////////////////////////////////////////////////////////////
///
/// @file              MockClock.c
///
/// @brief             Deterministic virtual clock for the mock HAL.  Time only
///                    moves when the test calls mockClockAdvanceNs().
///
///////////////////////////////////////////////////////////////////////////////

#include "HalMock.h"
#include "MockSubsystems.h"

/// @var _nowNs
///
/// @brief The current virtual time, in nanoseconds since boot.
static uint64_t _nowNs = 0;

void mockClockReset(void) {
  _nowNs = 0;
}

uint64_t mockClockNowNs(void) {
  return _nowNs;
}

void mockClockAdvanceNs(uint64_t deltaNs) {
  _nowNs += deltaNs;
}

int32_t mockClockInitFn(va_list args) {
  (void) args;
  return 0;
}

int32_t mockClockSetSystemTimeFn(va_list args) {
  (void) args;
  // The virtual clock is monotonic-from-boot; setting wall time is a no-op.
  return 0;
}

/// @fn static int32_t elapsed(va_list args, uint64_t divisor)
///
/// @brief Shared body for the getElapsed* functions.  Args are
/// (int64_t startTime, int64_t *returnValue); returnValue is set to
/// (now - startTime) scaled by divisor.
static int32_t elapsed(va_list args, uint64_t divisor) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  if (returnValue == NULL) {
    return -1;
  }
  int64_t scaledNow = (int64_t) (_nowNs / divisor);
  *returnValue = scaledNow - startTime;
  return 0;
}

int32_t mockClockGetElapsedMillisecondsFn(va_list args) {
  return elapsed(args, 1000000ULL);
}

int32_t mockClockGetElapsedMicrosecondsFn(va_list args) {
  return elapsed(args, 1000ULL);
}

int32_t mockClockGetElapsedNanosecondsFn(va_list args) {
  return elapsed(args, 1ULL);
}
