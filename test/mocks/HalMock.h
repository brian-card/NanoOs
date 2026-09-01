///////////////////////////////////////////////////////////////////////////////
///
/// @file              HalMock.h
///
/// @brief             A layered mock HAL for the kernel test harness.
///
/// "Layered" means each subsystem can independently be the deterministic mock
/// or the real POSIX implementation the simulator uses:
///
///   - overlay RAM window + heap sizing : always reused from HalPosixImpl
///     (halPosixImplInit); there is no value in re-mocking mmap.
///   - clock   : virtual monotonic time (default) or CLOCK_REALTIME.
///   - timer   : synchronous, fire-on-demand (default) or pthread-backed.
///   - uart    : in-RAM rx/tx buffers (default) or real stdin/stdout.
///   - storage : none (default) or a POSIX SD-card process over a FAT32 image.
///
/// Defaults give a fully deterministic kernel: no wall-clock, no preemption
/// unless the test asks for it, console I/O the test drives byte by byte.
///
///////////////////////////////////////////////////////////////////////////////

#ifndef HAL_MOCK_H
#define HAL_MOCK_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum MockClockMode {
  MOCK_CLOCK_VIRTUAL,
  MOCK_CLOCK_POSIX,
} MockClockMode;

typedef enum MockTimerMode {
  MOCK_TIMER_SYNCHRONOUS,
  MOCK_TIMER_POSIX,
} MockTimerMode;

typedef enum MockUartMode {
  MOCK_UART_BUFFER,
  MOCK_UART_POSIX,
} MockUartMode;

typedef enum MockStorageMode {
  MOCK_STORAGE_NONE,
  MOCK_STORAGE_FILE,
} MockStorageMode;

/// @struct HalMockConfig
///
/// @brief Per-boot configuration for the mock HAL.
typedef struct HalMockConfig {
  MockClockMode   clock;
  MockTimerMode   timer;
  MockUartMode    uart;
  MockStorageMode storage;
  const char     *imagePath; ///< FAT32 image for MOCK_STORAGE_FILE.
} HalMockConfig;

/// @fn HalMockConfig halMockConfigDefault(void)
///
/// @brief The fully deterministic default configuration.
HalMockConfig halMockConfigDefault(void);

/// @typedef HalMockRestartShellFn
///
/// @brief Signature of the harness callback that fills the shell process slot.
/// The argument is really a ProcessDescriptor*; kept as void* here so HalMock.h
/// does not have to pull in the kernel types.
typedef int32_t (*HalMockRestartShellFn)(void *processDescriptor);

/// @fn void halMockSetRestartShell(HalMockRestartShellFn fn)
///
/// @brief Register the function installed as HAL->platform.restartShell.  Must
/// be called before halMockInit().
void halMockSetRestartShell(HalMockRestartShellFn fn);

/// @fn void halMockSetPowerHook(void (*hook)(void))
///
/// @brief Register a callback invoked whenever a test drives
/// HAL->power.enterMode() (any mode).  The harness uses this to end the child
/// process instead of exit()ing the whole run.  If no hook is set,
/// HAL->power.enterMode() is a no-op that returns 0.
void halMockSetPowerHook(void (*hook)(void));

/// @fn int halMockInit(const HalMockConfig *config, jmp_buf *powerReturn)
///
/// @brief Populate halImpl / halFunctions with the mock HAL and run the shared
/// HAL bring-up.  Equivalent to halPosixInit() for the sim.
///
/// @param config Configuration, or NULL for halMockConfigDefault().
/// @param powerReturn Where HAL->power.enterMode() longjmps to.  The harness
///   points this at a buffer it setjmp'd before calling halMockInit; any
///   power mode (OFF / SUSPEND / RESET) unwinds there instead of exit()ing.
///
/// @return 0 on success, -errno on failure.
int halMockInit(const HalMockConfig *config, jmp_buf *powerReturn);

// --- deterministic control surface, callable from a running test body -----

/// @fn uint64_t mockClockNowNs(void)
uint64_t mockClockNowNs(void);

/// @fn void mockClockAdvanceNs(uint64_t deltaNs)
void mockClockAdvanceNs(uint64_t deltaNs);

/// @fn int mockTimerFire(int32_t deviceId)
///
/// @brief Invoke the armed one-shot callback for deviceId and disarm it.
///
/// @return 0 if a callback fired, -1 if the timer was not armed.
int mockTimerFire(int32_t deviceId);

/// @fn size_t mockUartFeed(const char *bytes, size_t length)
///
/// @brief Push bytes into the console UART's receive buffer, as if typed.
///
/// @return The number of bytes accepted.
size_t mockUartFeed(const char *bytes, size_t length);

/// @fn size_t mockUartDrain(char *out, size_t max)
///
/// @brief Pull everything written to the console UART's transmit buffer.
///
/// @return The number of bytes copied into out.
size_t mockUartDrain(char *out, size_t max);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_MOCK_H
