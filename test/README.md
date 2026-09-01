# NanoOs kernel test harness

Phase 2 infrastructure: boot a **real** NanoOs kernel on the host, under a
mock HAL, and run test code as a process inside it.

```
make -C test              # build + run every suite
make -C test build        # build only
make -C test run          # run the built binary
make -C test clean
make -C test ASAN=1        # ASan/UBSan (plain suites only - see below)
./test/bin/nano-os-test [filter]   # filter = substring of "suite/name"
```

Output is [TAP](https://testanything.org/); pipe it through `prove` if you
like.

## It does not touch any other build

`test/` is a self-contained tree with its own object directory
(`test/obj/`, git-ignored). It compiles the kernel/user/HAL sources from
`../src` with the same flags `buildsim` uses, into `test/obj` — never
`sim/obj`. Nothing outside `test/` is written or modified.

The other builds cannot see `test/` either: `buildsim` uses explicit
object lists, and the AgonLight2 / Arduino makefiles discover sources with
`find src -name '*.c'`. Verified: `make -C sim` and the full AgonLight2
firmware build are byte-for-byte unaffected by anything here.

## Layout

```
framework/   NanoOsTest.{h,c}   xUnit-ish: TEST()/ASSERT_*, registry, TAP
             TestMain.c          entry point; installs the kernel runner
harness/     KernelTestHarness.{h,c}
                                 kernelTestRun(): fork -> boot kernel ->
                                 run body as the driver process -> report
mocks/       HalMock.{h,c}       layered mock HAL + control surface
             MockClock.c         virtual monotonic time
             MockTimer.c         synchronous, fire-on-demand one-shots
             MockUart.c          in-RAM console rx/tx buffers
             MockBlockDevice.c   (MOCK_STORAGE_NONE only, for now)
             MockSubsystems.h    internal glue
suites/      test_smoke.c        framework self-check + kernel bring-up
```

## Writing tests

```c
#include "NanoOsTest.h"

NANO_OS_TEST(mysuite, plain_thing) {
  NANO_OS_ASSERT_EQ_INT(4, 2 + 2);
}

#include "kernel/Scheduler.h"

NANO_OS_KERNEL_TEST(mysuite, runs_in_a_booted_kernel) {
  // This body executes as a real NanoOs process. SCHEDULER_STATE is live;
  // IPC, malloc, HAL calls all work.
  NANO_OS_ASSERT_NOT_NULL(SCHEDULER_STATE);
}
```

Add the `.c` file's basename to `SUITE_SRCS` in `test/makefile`.

- `NANO_OS_TEST` bodies run directly in the runner process.
- `NANO_OS_KERNEL_TEST` bodies run inside a **freshly booted kernel**, one
  forked child per test, so suites are fully isolated and a test that
  calls `shutdown()` is harmless.

Assertions are fatal (they abort the current body and mark it failed); the
run continues with the next test.

## The mock HAL (`HalMock.h`)

"Layered" — each subsystem is independently the deterministic mock or the
real POSIX implementation from `HalPosixImpl.c`:

| subsystem | default | alt |
|-----------|---------|-----|
| overlay RAM window, heap sizing | POSIX (`halPosixImplInit`) | — |
| clock | virtual, `mockClockAdvanceNs()` | `MOCK_CLOCK_POSIX` |
| timer | synchronous, `mockTimerFire()` | `MOCK_TIMER_POSIX` |
| uart | RAM buffers, `mockUartFeed/Drain()` | `MOCK_UART_POSIX` |
| storage | none (`rootFsPid == 0`) | `MOCK_STORAGE_FILE` *(stub)* |
| memory, dio, spi | POSIX | — |
| power | ends the child cleanly | — |

Defaults give a fully deterministic kernel: no wall clock, no preemption
unless a test calls `mockTimerFire()`, console I/O driven byte by byte.

## Current limitations (next increments)

- **No root filesystem yet.** `MOCK_STORAGE_NONE` boots with
  `SCHEDULER_STATE->rootFsPid == 0`. Filesystem / pipe / overlay-command
  tests need `MOCK_STORAGE_FILE` — a POSIX SD-card process over a real
  FAT32 image (reusing `buildsim`'s image build) — which is stubbed in
  `MockBlockDevice.c`.
- **The driver process has no NanoOs stdio FDs.** It reports through an
  out-of-band pipe, not `printf`. Wiring console/pipe FDs onto the driver
  needs `standardUserFileDescriptors` exported from `Scheduler.c` (a
  deliberate one-line change, deferred until a suite needs it).
- **`ASAN=1` skips kernel tests.** `halPosixImplInit`'s heap-sizing
  recursion overflows the stack under ASan's instrumentation. Plain
  (non-kernel) suites still get full ASan coverage.
