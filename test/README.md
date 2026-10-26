# NanoOs test suite

Four layers:

| layer | what | where | needs |
|-------|------|-------|-------|
| **1 - unit** | pure/`static`-ish helpers, no kernel | `suites/test_unit_*.c` | gcc |
| **2 - kernel** | a **real kernel** booted on the host under a mock HAL; test bodies run as a process inside it | `suites/test_kernel_*.c` | gcc |
| **3 - e2e** | the **real simulator** driven over a PTY | `e2e/` | `python3` + `pexpect`, `mtools`, `sfdisk` |
| **4 - emulator** | a **real Agon Light 2 build** booted in fab-agon-emulator, driven over UART1; skipped when the tools are missing | `emulator/` | layer 3's needs + `socat`, the ez80 toolchain (`AGDEV_HOME`, default `~/agondev`), fab-agon-emulator (`NANO_OS_AGON_EMULATOR`, default `~/src/fab-agon-emulator`) |

```
test/run-all.sh                 # every layer, with a summary
test/run-all.sh --no-e2e --no-asan --no-emulator

make -C test                    # layers 1 + 2 (build + run)
make -C test ASAN=1             # layers 1 + 2 under ASan (kernel tests skip)
./test/bin/nano-os-test [filter] # filter = substring of "suite/name"

python3 test/e2e/run.py [-v] [-j N] [filter]   # layer 3 (standalone, TAP output)
                                # N parallel sims; default nproc, 1 with -v
python3 test/emulator/run.py [-v] [regex]     # layer 4 (one emulator boot)
```

All runners emit [TAP](https://testanything.org/).

## It does not touch any other build

Layers 1-2 build into `test/obj/` (git-ignored) with the same flags
`buildsim` uses — never `sim/obj/`. Layer 3's `mkimage.sh` *does* run
`make -C sim` and `make -C usr/src` (it needs the real artifacts), same as
`buildsim`; it writes its disk image into `test/e2e/.cache/` (git-ignored).

Layer 4 runs `builddev`, which rebuilds the kernel and `usr/src` for the
Agon Light 2. It boots a private copy of the emulator in a temporary
directory, so the copy's MOS image is the one `builddev` overwrites, and runs
it without a window.

Nothing outside `test/` is modified. The AgonLight2 / Arduino / `buildsim`
builds cannot see `test/` (they discover sources by explicit lists /
`find src`). Verified: `make -C sim` and the full AgonLight2 firmware build
are unaffected.

## Layout

```
framework/  NanoOsTest.{h,c}   xUnit-ish: TEST()/ASSERT_*, registry, TAP
            TestMain.c          entry point; installs the kernel runner
harness/    KernelTestHarness.{h,c}
                                kernelTestRun(): fork -> boot kernel ->
                                run body as the driver process -> report
mocks/      HalMock.{h,c}       layered mock HAL + control surface
            MockClock/Timer/Uart/BlockDevice.c, MockSubsystems.h
suites/     test_smoke.c            framework + kernel bring-up
            test_unit_misc.c       raiseUInt, user table, timespecFromDelay
            test_unit_strtoll.c    nanoOsStrtoll
            test_unit_console.c    consoleProcessByte (the console's line editor)
            test_kernel_memory.c   malloc/free/calloc/realloc via the mm process
            test_kernel_ipc.c      message pool + blocking request/response
            test_kernel_hal.c      clock / timer / uart HAL calls
            test_kernel_scheduler.c getpid, process table, process count
e2e/        run.py              standalone PTY-driven runner (pexpect, no pytest)
            fakevdp.py          a stand-in VDP on the simulator's UART0
            mkimage.sh          builds the FAT32 image (factored from buildsim)
            .cache/            built images (git-ignored)
emulator/   run.py              Agon Light 2 build in fab-agon-emulator
BUGS.txt    defects found while writing these tests
run-all.sh  runs every layer
```

## Writing tests

```c
#include "NanoOsTest.h"

NANO_OS_TEST(mysuite, plain_thing) {          // layer 1: runs in the runner
  NANO_OS_ASSERT_EQ_INT(4, 2 + 2);
}

#include "kernel/Scheduler.h"
NANO_OS_KERNEL_TEST(mysuite, in_a_kernel) {   // layer 2: fresh kernel, per test
  NANO_OS_ASSERT_TRUE(schedulerIsInitialized()); // IPC / malloc / HAL all live
}
```

Add the file's basename to `SUITE_SRCS` in `test/makefile`. Assertions are
fatal (abort the body, mark it failed, continue with the next test).
`NANO_OS_KERNEL_TEST` bodies each run in their own forked child, so suites
are isolated and a test that calls `shutdown()` is harmless.

Layer-3 tests are plain Python functions `test_*(session)` in
`e2e/run.py`; `session.login()` / `session.sh("cmd")` drive the console.

`session.restart_with_vdp()` boots with vdp.drv serving a console on a
`FakeVdp`: the simulator connects UART0 to the Unix socket named by
`NANO_OS_SIM_UART0`, and the fake answers general polls, records the VDU
stream as screen text, and sends keyboard packets (`vdp.type("ls\r")`,
`vdp.expect(regex)`). `vdp.disconnect()` drops the socket; the next UART0
read in the simulator then fails with `-EIO`, which makes vdp.drv exit and
be restarted.

Layer-4 tests are functions `test_*(emulator)` in `emulator/run.py`, run in
order against a single boot, so later tests see the files earlier ones made;
the last one halts the system and runs `fsck.vfat` on the image.
`emulator.sh("cmd")` strips the SD card log lines and `[boot]` trace lines
that can land in the middle of command output.

## The mock HAL (`HalMock.h`)

"Layered" — each subsystem is independently the deterministic mock or the
real POSIX implementation from `HalPosixImpl.c`:

| subsystem | default | alt |
|-----------|---------|-----|
| overlay RAM window, heap sizing | POSIX (`halPosixImplInit`) | — |
| clock | virtual, `mockClockAdvanceNs()` | `MOCK_CLOCK_POSIX` |
| timer | synchronous, `mockTimerFire()` | `MOCK_TIMER_POSIX` |
| uart | RAM buffers, `mockUartFeed/Drain()`, `halMockSetUartPollOnly()` | `MOCK_UART_POSIX` |
| storage | none (`rootFsPid == 0`) | `MOCK_STORAGE_FILE` *(stub)* |
| memory, dio, spi | POSIX | — |
| power | ends the child cleanly | — |

Defaults give a fully deterministic kernel: no wall clock, no preemption
unless a test calls `mockTimerFire()`, console I/O driven byte by byte.

## Bugs found (see `test/BUGS.txt`)

- **BUG-1** — `timespecFromDelay` double-counted seconds, never normalised
  `tv_nsec`, 32-bit multiply overflow. **Fixed**; regression test
  `unit_time/timespecFromDelay_splits_ms_into_s_and_ns`.
- **BUG-2** — `nanoOsStrtoll` valued letter digits 10 too low (every base >
  10 wrong, trailing letters eaten in base 10). **Fixed**; regression
  tests `strtoll/*`.
- **BUG-3** — `schedulerGetProcessInfo` built an invalid `timespec` ~10% of
  the time (same class as BUG-1, inlined at `Scheduler.c:1296`). **Fixed**
  by routing through `timespecFromDelay(&timeout, 100)`. Masked by the
  virtual clock, so no unit test — covered indirectly by the e2e `ps`
  tests.
- **BUG-4** — a pipeline of **3+ commands** (`echo a | grep a | grep a`)
  takes down the OS. **Partially fixed.** The original **SIGSEGV** was
  `logMessage` truncating every log vararg to 32 bits (`Logger.c:146`,
  `LogEntry.args` is `uint32_t[4]`) — lossless on the 32-bit MCUs, but on
  the 64-bit sim a `logError("...%s", heapPtr)` truncated the pointer and
  the logger process `strlen`'d a wild address. Fixed 2026-09-01 (both
  sides `memcpy` `sizeof(intptr_t)` with a matching stride; unused slots
  `memset` to 0). The 3-command pipe now **hangs** instead: the `schedF*`
  reentrancy guard is held during pipe-stage teardown, `SPAWN` returns
  `-EBUSY`, and `Scheduler.c:3565` re-queues it forever. 2-command pipes
  are fine; the `cmd &` path fails cleanly on slot exhaustion. **Open** —
  full analysis in `BUGS.txt`; xfail e2e test
  `test_pipe_three_stage_does_not_crash_the_os`.
- **BUG-5** — `ls`'s day-of-week column (`weekdays[tm.tm_wday]`) could read
  out of bounds and crash/hang. Root cause: `NanoOsTime.c`'s
  `_yearStartDay` lookup table (used by `nanoOsGmtime_r` to compute
  `tm_wday`) lacked `KEEP_IN_FLASH`, so on the **stripped** binary
  (`.rodata` removed — what actually ships to AgonLight2/ItsyBitsy)
  it held garbage instead of `{4,5,6,1,2,3,4,...}`, producing a wildly
  out-of-range `tm_wday` (e.g. `-4`) for perfectly ordinary dates. Latent
  since `nanoOsGmtime_r` was written — nothing read `tm_wday` until `ls`
  started printing a weekday column. **Fixed** 2026-09-17 (added
  `KEEP_IN_FLASH`); regression tests
  `test_ls_does_not_crash_on_populated_directory` (crash/hang) and
  `test_ls_shows_correct_weekday_for_mtime` (every printed weekday
  actually matches its printed date, not just "looks like a real
  abbreviation").

`make -C test run` is currently all-green. Known regressions, if any are
added later, use `NANO_OS_TEST_TODO` / `NANO_OS_KERNEL_TEST_TODO` so they
report red without failing the run.

## Limitations / next increments

- **No root filesystem in layer 2.** `MOCK_STORAGE_NONE` boots with
  `rootFsPid == 0`. Filesystem / pipe / overlay-command coverage lives in
  layer 3 today; `MOCK_STORAGE_FILE` (the POSIX SD card over a real
  image) is stubbed in `MockBlockDevice.c`.
- **The layer-2 driver process has no NanoOs stdio FDs** — it reports
  through a pipe, not `printf`. Wiring FDs needs
  `standardUserFileDescriptors` exported from `Scheduler.c` (a deliberate
  one-line change, deferred until a suite needs it).
- **`ASAN=1` can't run kernel tests** — `halPosixImplInit`'s heap-sizing
  stack recursion overflows under ASan, so `kernelTestRun()` returns SKIP
  for `NANO_OS_KERNEL_TEST`s. `run-all.sh` sets `NANO_OS_TEST_SKIP_KERNEL=1`
  for its ASan pass so they're omitted entirely instead of printed as
  skips; the plain (Layer-1) suites still get full ASan/UBSan coverage
  there, and the kernel tests ran for real in the non-ASan pass just above.
- **Layer 3 is wired for the `contiguous` block filesystem** (what the
  POSIX sim's `restartContiguousFilesystem` expects); building the image
  with `overlay` makes the sim fail to bring up its filesystem.
