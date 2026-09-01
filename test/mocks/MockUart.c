///////////////////////////////////////////////////////////////////////////////
///
/// @file              MockUart.c
///
/// @brief             In-RAM console UART for the mock HAL.  Device 1 is the
///                    console: its receive buffer is fed by mockUartFeed()
///                    (as if a user typed) and its transmit buffer is read by
///                    mockUartDrain().  Device 0 exists but sinks its output.
///
///////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include <stdbool.h>

#include "HalMock.h"

#include "MockSubsystems.h"

/// @def MOCK_UART_BUF_SIZE
#define MOCK_UART_BUF_SIZE 8192

/// @def MOCK_CONSOLE_UART
///
/// @brief Device id used as the console.  Matches posixUartsOnline = {0x2}.
#define MOCK_CONSOLE_UART 1

typedef struct MockUartRing {
  unsigned char data[MOCK_UART_BUF_SIZE];
  size_t        head; ///< read position
  size_t        tail; ///< write position
} MockUartRing;

static MockUartRing _rx;
static MockUartRing _tx;

static size_t ringCount(const MockUartRing *ring) {
  return ring->tail - ring->head;
}

static size_t ringPush(MockUartRing *ring, const unsigned char *bytes,
  size_t length
) {
  size_t accepted = 0;
  for (size_t ii = 0; ii < length; ii++) {
    if (ringCount(ring) >= MOCK_UART_BUF_SIZE) {
      break;
    }
    ring->data[ring->tail % MOCK_UART_BUF_SIZE] = bytes[ii];
    ring->tail++;
    accepted++;
  }
  return accepted;
}

static int ringPop(MockUartRing *ring) {
  if (ringCount(ring) == 0) {
    return -1;
  }
  int byte = ring->data[ring->head % MOCK_UART_BUF_SIZE];
  ring->head++;
  return byte;
}

void mockUartReset(void) {
  memset(&_rx, 0, sizeof(_rx));
  memset(&_tx, 0, sizeof(_tx));
}

size_t mockUartFeed(const char *bytes, size_t length) {
  return ringPush(&_rx, (const unsigned char*) bytes, length);
}

size_t mockUartDrain(char *out, size_t max) {
  size_t copied = 0;
  while ((copied < max) && (ringCount(&_tx) > 0)) {
    int byte = ringPop(&_tx);
    if (byte < 0) {
      break;
    }
    out[copied++] = (char) byte;
  }
  return copied;
}

int32_t mockUartInitFn(va_list args) {
  (void) args;
  mockUartReset();
  return 0;
}

int32_t mockUartConfigureFn(va_list args) {
  (void) args; // deviceId, baud - accepted and ignored
  return 0;
}

int32_t mockUartPollFn(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  if (deviceId != MOCK_CONSOLE_UART) {
    return -1;
  }
  return ringPop(&_rx);
}

int32_t mockUartWriteFn(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  const uint8_t *data = va_arg(args, const uint8_t*);
  intptr_t length = va_arg(args, intptr_t);
  intptr_t *returnValue = va_arg(args, intptr_t*);

  intptr_t written = length;
  if (length < 0) {
    written = -1;
  } else if (deviceId == MOCK_CONSOLE_UART) {
    written = (intptr_t) ringPush(&_tx, (const unsigned char*) data,
      (size_t) length);
  }
  // deviceId 0: accepted and dropped, written stays == length.

  if (returnValue != NULL) {
    *returnValue = written;
  }
  return (written >= 0) ? 0 : -1;
}

int32_t mockUartIsConsoleFn(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool *returnValue = va_arg(args, bool*);
  if (returnValue != NULL) {
    *returnValue = (deviceId == MOCK_CONSOLE_UART);
  }
  return 0;
}
