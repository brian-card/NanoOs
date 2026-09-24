////////////////////////////////////////////////////////////////////////////////
//
//                       Copyright (c) 2026 Brian Card
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
//                                 Brian Card
//                       https://github.com/brian-card
//
////////////////////////////////////////////////////////////////////////////////

/// @file HalArduinoSamD21x18A.cpp
///
/// @brief HAL implementation for a SAMD21x18A Arduino-based board.

#if defined(__SAMD21G18A__) || defined(__SAMD21E18A__)

// Base Arduino definitions
#define FILE  Arduino_FILE
#define gid_t Arduino_gid_t
#define uid_t Arduino_uid_t
#define pid_t Arduino_pid_t
#include <Arduino.h>
#undef FILE
#undef gid_t
#undef uid_t
#undef pid_t
// See src/include/sys/types.h: clear the guard flags Arduino.h's (renamed)
// typedefs above set, so that header's own gid_t/uid_t/pid_t, reached later
// in this file, don't wrongly defer to them.
#undef __gid_t_defined
#undef _GID_T_DECLARED
#undef __uid_t_defined
#undef _UID_T_DECLARED
#undef __pid_t_defined
#undef _PID_T_DECLARED

// Basic SPI communication
#include <SPI.h>

#include "HalArduinoSamD21x18A.h"
#include "HalCommon.h"
// Deliberately *NOT* including MemoryManager.h here.  The HAL has to be
// operational prior to the memory manager and really should be completely
// independent of it.
#include "../kernel/Logger.h"
#include "../kernel/NanoOs.h"
#include "../kernel/Overlay.h"
#include "../kernel/Processes.h"
#include "../kernel/Scheduler.h"
#include "../kernel/SdCardSpi.h"
#include "../user/NanoOsErrno.h"
#include "../user/NanoOsStdio.h"

// Types and prototypes from files that we can't directly include.
typedef struct NanoOsApi NanoOsApi;
extern NanoOsApi nanoOsApi;
extern NanoOsApi *NANO_OS_API;

#ifdef __cplusplus
extern "C"
{
#endif
void* callOverlayFunctionFromFile(const void *overlayDir, const void *overlay,
  const char *function, void *args);
#ifdef __cplusplus
}
#endif

/// @def PROCESS_STACK_SIZE
///
/// @brief The size, in bytes, of a regular process's stack.
#define PROCESS_STACK_SIZE 800

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief The size, in bytes, of the memory manager process's stack.
#define MEMORY_MANAGER_STACK_SIZE 448

/// @def OVERLAY_ADDRESS
///
/// @brief The address of where the overlay will be placed in memory.
///
/// @note This must clear .bss end on every SAMD21 board this makefile
/// supports, with enough headroom left over for whatever dynamic (heap)
/// allocations the Arduino core/libraries make during their own boot
/// sequence, before NanoOs takes over -- that memory starts allocating
/// right after .bss too, so a boundary set exactly at .bss end would
/// collide with it. Raising it to fit a board with more static RAM use
/// costs the same amount of heap everywhere else, which isn't worth it:
/// see "Supported boards" at the top of makefiles/ArduinoSamd21Makefile
/// for the boards this constant excludes and why.
///
/// Raised by 0x100 (256 bytes) from the ItsyBitsy M0's original
/// 0x20002400 to make room for the HAL_PLATFORM/HAL_MEMORY callHal
/// dispatch tables and capability entries added when HalPlatform's
/// function pointers and HalMemory's overlay/log/scheduler pointers
/// were converted to callHal-routed getters.
///
/// Tested raising this in small steps (0x20002600, 0x20002700, 0x20002900)
/// while root-causing a real-hardware boot hang, on the theory that Arduino
/// core/library boot-time dynamic (heap) allocations -- which start right
/// after .bss too -- were growing past the original ~524-byte margin here
/// and colliding with the overlay window.  None of those steps changed the
/// hang's behavior or location at all, conclusively ruling that theory out.
/// Reverted back to 0x20002500, then raised by another, unrelated 0x20
/// (32 bytes) to 0x20002520: adding the SD card process's HAL_TIMER_CANCEL
/// capability entry (see SD_CARD_HAL_CAPABILITIES_INITIALIZER in
/// HalCommon.h) grew .data by a measured 16 bytes, and the margin here was
/// already down to 12 bytes with nothing to do with the ruled-out theory
/// above.  32 bytes covers that need with 16 bytes of real slack left over.
///
/// Raised from 0x200025A0 to 0x20002800 after a HardFault at login was traced
/// to the Arduino core's newlib heap growing into this window.  That heap
/// starts at .bss end and grows up; USBCore/SPI/operator new had taken it to
/// a break of 0x2000269c, which is 252 bytes *inside* the old window.  The
/// USB CDC receive buffer ended exactly at the old base, so typing a password
/// wrote across the boundary and overwrote the overlay header's osApi (the
/// window's first word).  The overlay then called through osApi + 0xDC and
/// faulted.  The old reservation between __nanoos_overlay_window and this
/// address was 512 bytes; the core was measured needing 792.
///
/// Every byte given to the newlib heap below is a byte taken from the NanoOs
/// heap above (bottomOfHeap is OVERLAY_ADDRESS + OVERLAY_SIZE), so this is
/// not free headroom to pad.  Moving the window from 0x200025A0 to 0x20002800
/// cost the NanoOs heap 608 bytes and made `ps | grep p | grep t` run out of
/// memory.  0x20002700 leaves the newlib heap 892 bytes -- 100 over its
/// measured need -- and gives 256 of those bytes back.  The margin can be
/// this tight because _sbrk now enforces the boundary instead of trusting it:
/// overrunning fails the allocation rather than corrupting the window.
///
/// MUST be kept in sync with:
///   - __nanoos_overlay_window in ld/ArduinoSamd21FlashWithBootloader.ld
///   - OVERLAY_RAM ORIGIN      in usr/src/NanoOsArduinoSamd21.ld
#define OVERLAY_ADDRESS 0x20002700

/// @def OVERLAY_SIZE
///
/// @brief The size, in bytes, of the overlay supported by the board.
#define OVERLAY_SIZE 4096

/// @def STATIC_LOGS_ADDRESS
///
/// @brief The address where static logs will begin in memory.
#define STATIC_LOGS_ADDRESS 0x20004800

/// @def DIO_PIN_UNDEFINED
///
/// @brief Value to indicate that the value of a specific pin is undefined.
#define DIO_PIN_UNDEFINED 255

/// @def MAX_SPI_DEVICES
///
/// @brief The maximum number of SPI devices the system can support.
#define MAX_SPI_DEVICES 2

/// @def NUM_PROCESSES
///
/// @brief Value to indicate the maximum number of processes that can be run
/// concurrently.
#define NUM_PROCESSES 9

/// @var _spiCopiDio
///
/// @brief DIO pin used for SPI COPI.
static uint8_t _spiCopiDio = DIO_PIN_UNDEFINED;

/// @var _spiCipoDio
///
/// @brief DIO pin used for SPI CIPO.
static uint8_t _spiCipoDio = DIO_PIN_UNDEFINED;

/// @var _spiSckDio
///
/// @brief DIO pin used for SPI serial clock.
static uint8_t _spiSckDio = DIO_PIN_UNDEFINED;

/// @var _sdCardPinChipSelect
///
/// @brief Pin to use for the MicroSD card reader's SPI chip select line.
static uint8_t _sdCardPinChipSelect = DIO_PIN_UNDEFINED;

// The fact that we've included Arduino.h in this file means that the memory
// management functions from its library are available in this file.  That's a
// problem.  (a) We can't allow dynamic memory at the HAL level and (b) if we
// were to allocate memory from Arduino's memory manager, we'd run the risk
// of corrupting something elsewhere in memory.  Just in case we ever forget
// this and try to use memory management functions in the future, define them
// all to MEMORY_ERROR so that the build will fail.
#undef malloc
#define malloc  MEMORY_ERROR
#undef calloc
#define calloc  MEMORY_ERROR
#undef realloc
#define realloc MEMORY_ERROR
#undef free
#define free   MEMORY_ERROR

/// @struct HalProcessQueue
///
/// @brief Structure to manage an individual process queue.  This is the
/// implementation that backs the ProcessQueue structure in the kernel.
///
/// @param name The string name of the queue for use in error messages.
/// @param head The index of the head of the queue.
/// @param tail The index of the tail of the queue.
/// @param numElements The number of elements currently in the queue.
/// @param processes The array of pointers to ProcessDescriptors from the
///   allProcesses array.  This is a variable-length array in the kernel.  It's
///   declared with definitive size here since this is the actual
///   implementationn backing.
typedef struct HalProcessQueue {
  const char        *name;
  uint8_t            head;
  uint8_t            tail;
  uint8_t            numElements;
  ProcessDescriptor *processes[NUM_PROCESSES];
} HalProcessQueue;

/// @var _kernelReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_KERNEL ready
/// queue.
HalProcessQueue _kernelReadyQueue;

/// @var _executiveReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_EXECUTIVE ready
/// queue.
HalProcessQueue _executiveReadyQueue;

/// @var _supervisorReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_SUPERVISOR ready
/// queue.
HalProcessQueue _supervisorReadyQueue;

/// @var _userReadyQueue
///
/// @brief HAL implementation backing for the PRIVILEGE_LEVEL_USER ready queue.
HalProcessQueue _userReadyQueue;

/// @var _readyQueues
///
/// @brief HAL implementation backing for the ready queues.
ProcessQueue *_readyQueues[SCHEDULER_NUM_READY_QUEUES] = {
  (ProcessQueue*) &_kernelReadyQueue,
  (ProcessQueue*) &_executiveReadyQueue,
  (ProcessQueue*) &_supervisorReadyQueue,
  (ProcessQueue*) &_userReadyQueue,
};

/// @var _waitingQueue
///
/// @brief HAL implementation backing for the waiting queue.
static HalProcessQueue _waitingQueue;

/// @var _timedWaitingQueue
///
/// @brief HAL implementation backing for the timed waiting queue.
static HalProcessQueue _timedWaitingQueue;

/// @var _freeQueue
///
/// @brief HAL implementation backing for the free queue.
static HalProcessQueue _freeQueue;

/// @var _processErrorNumbers
///
/// @brief Process-specific storage for each process's errno value.
static int _processErrorNumbers[NUM_PROCESSES + 1];

/// @var _processStorageBase
///
/// @brief File-local, first-level variable to hold the per-process storage.
static void *_processStorageBase[NUM_PROCESSES][NUM_PROCESS_STORAGE_KEYS];

/// @var _processStorage
///
/// @brief File-local, second-level variable to hold the per-process storage.
static void **_processStorage[NUM_PROCESSES];

/// @struct SavedContext
///
/// @brief Function context that's saved during an interrupt handler.
///
/// @param r0 The value to load into the ARM r0 register.
/// @param r1 The value to load into the ARM r1 register.
/// @param r2 The value to load into the ARM r2 register.
/// @param r3 The value to load into the ARM r3 register.
/// @param r12 The value to load into the ARM r12 register.
/// @param lr The value to load into the ARM lr register.
/// @param pc The value to load into the ARM pc register.
/// @param sp The value to load into the ARM sp register.
typedef struct SavedContext {
  /// @param r4..r11 The callee-saved registers of the interrupted context.
  /// The hardware does not stack these, so they are captured by the naked
  /// exception stub before any C code runs.  Without them, interrupted code
  /// resumed with r4-r11 holding whatever the timer path left behind --
  /// observed as callHal computing halFunctionCounts[subsystem] from a
  /// clobbered r5 and taking an unaligned load fault.
  ///
  /// Kept first in the struct so TIMER_EXCEPTION_ENTRY can store straight to
  /// offset 0; the static_asserts below pin every offset the assembly uses.
  uint32_t r4, r5, r6, r7;
  uint32_t r8, r9, r10, r11;
  uint32_t r0, r1, r2, r3, r12, lr, pc, sp;
} SavedContext;

/// @var _savedContext
///
/// @brief Temporary storage for storing the context during a timer interrupt.
extern "C" {
/// @note Deliberately not static: TIMER_EXCEPTION_ENTRY is a naked function
/// and must reference this by its unmangled assembler name.
SavedContext _savedContext;
}

static_assert(offsetof(SavedContext, r4)  ==  0, "asm offset");
static_assert(offsetof(SavedContext, r8)  == 16, "asm offset");
static_assert(offsetof(SavedContext, r0)  == 32, "asm offset");
static_assert(offsetof(SavedContext, r12) == 48, "asm offset");
static_assert(offsetof(SavedContext, lr)  == 52, "asm offset");
static_assert(offsetof(SavedContext, pc)  == 56, "asm offset");
static_assert(offsetof(SavedContext, sp)  == 60, "asm offset");

/// @var _diagStaleTimer
///
/// @brief String reporting a preemption timer that fired while a process
/// that is never supposed to be preempted was running.
///
/// The scheduler arms the preemption timer as a one-shot, for 10ms, only for
/// processes with privilegeLevel > PRIVILEGE_LEVEL_EXECUTIVE, immediately
/// before resuming one (Scheduler.c, both the runScheduler path and
/// schedulerSendSignalCommandHandler's user-process path).  If that process
/// yields voluntarily before the 10ms is up and the timer is not cancelled,
/// the timer is still armed while the scheduler -- or any other
/// KERNEL/EXECUTIVE process -- runs.  When it then fires, this handler
/// hijacks whatever was executing at that instant and hands it to
/// RESTORE_CONTEXT, which is not valid for a context that was never set up
/// to be preempted.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _diagStaleTimer[] KEEP_IN_FLASH
  = "DIAG STALE TIMER fired, not preemptible: pid=";

/// @var _diagStaleTimerPrivilege
///
/// @brief Separator printed between the pid and the privilege level in a
/// stale preemption timer report.
static const char _diagStaleTimerPrivilege[] KEEP_IN_FLASH = " privilege=";

/// @var _diagStaleTimerPc
///
/// @brief Separator printed before the interrupted PC in
/// a stale preemption timer report.  This is the address the timer would have
/// hijacked, so it names the code that would have been corrupted.
static const char _diagStaleTimerPc[] KEEP_IN_FLASH = " interruptedPc=";

/// @var _diagStaleTimerNewline
///
/// @brief Line terminator for a stale timer report.
static const char _diagStaleTimerNewline[] KEEP_IN_FLASH = "\n";

/// @var _diagStaleTimerPrintsRemaining
///
/// @brief Budget capping how many stale preemption timer reports are
/// printed, so a repeating stale timer can't flood the console.
static int _diagStaleTimerPrintsRemaining = 20;

/// @def THUMB_BIT
///
/// @brief A value that corresponds to the thumb bit in xPSR (bit 24).
#define THUMB_BIT 0x01000000

/// @def THUMB_BIT_MASK
///
/// @brief A mask of the 24 bits of xPSR.
#define THUMB_BIT_MASK 0x00ffffff

/// @def SAVE_CONTEXT
///
/// @brief Save the context of the stack frame we're using before proceeding.
#define SAVE_CONTEXT() \
  SavedContext savedContext = _savedContext

/// @def RESTORE_CONTEXT
///
/// @brief Restore the original caller's context and return to the address that
///  was found on the stack before it was modified by RETURN_TO_HANDLER.
#define RESTORE_CONTEXT() \
  /* Jump back to where we were interrupted */ \
  asm volatile( \
    "mov  r7, %[ctx]     \n\t" /* r7 = &savedContext, base throughout */ \
    "ldr  r0, [r7, #60]  \n\t" /* sp first: needed for the push below */ \
    "mov  sp, r0         \n\t" \
    "ldr  r0, [r7, #16]  \n\t" /* r8 */ \
    "ldr  r1, [r7, #20]  \n\t" /* r9 */ \
    "ldr  r2, [r7, #24]  \n\t" /* r10 */ \
    "ldr  r3, [r7, #28]  \n\t" /* r11 */ \
    "mov  r8, r0         \n\t" \
    "mov  r9, r1         \n\t" \
    "mov  r10, r2        \n\t" \
    "mov  r11, r3        \n\t" \
    "ldr  r0, [r7, #48]  \n\t" /* r12 */ \
    "mov  r12, r0        \n\t" \
    "ldr  r0, [r7, #52]  \n\t" /* lr */ \
    "mov  lr, r0         \n\t" \
    "ldr  r0, [r7, #56]  \n\t" /* target pc */ \
    "push {r0}           \n\t" /* stage it so r0 can be restored below */ \
    "ldr  r0, [r7, #32]  \n\t" /* r0-r3 */ \
    "ldr  r1, [r7, #36]  \n\t" \
    "ldr  r2, [r7, #40]  \n\t" \
    "ldr  r3, [r7, #44]  \n\t" \
    "ldr  r4, [r7, #0]   \n\t" /* r4-r6 */ \
    "ldr  r5, [r7, #4]   \n\t" \
    "ldr  r6, [r7, #8]   \n\t" \
    "ldr  r7, [r7, #12]  \n\t" /* r7 last; base is dead after this */ \
    "pop  {pc}           \n\t" /* branch, leaving every register correct */ \
    : \
    : [ctx] "l" (&savedContext) \
    : "memory" \
  ); \
  __builtin_unreachable()

/// @def TIMER_EXCEPTION_ENTRY
///
/// @brief Body of a timer interrupt handler.
///
/// @details Captures the active stack pointer -- which, on exception entry,
/// is exactly the address of the hardware-stacked exception frame -- and
/// passes it to frameHandler.  Bit 2 of EXC_RETURN (in LR on entry) selects
/// MSP or PSP.  EXC_RETURN is preserved across the call and popped straight
/// into PC at the end, which is what performs the exception return.
///
/// Must be used only in a function declared naked, so that no compiler
/// prologue runs before the stack pointer is read.
#define TIMER_EXCEPTION_ENTRY(frameHandler) \
  __asm volatile ( \
    /* Capture the interrupted context's callee-saved registers first: the */ \
    /* hardware does not stack r4-r11, and any C code called below would */ \
    /* save and restore only its own copies. */ \
    "ldr  r2, =_savedContext \n" /* r2 = &_savedContext.r4 (offset 0) */ \
    "stmia r2!, {r4-r7}    \n" /* r4-r7; r2 advances to &r8 */ \
    "mov  r4, r8           \n" \
    "mov  r5, r9           \n" \
    "mov  r6, r10          \n" \
    "mov  r7, r11          \n" \
    "stmia r2!, {r4-r7}    \n" /* r8-r11 */ \
    /* Put r4-r7 back.  Saving r8-r11 above had to stage them through r4-r7 */ \
    /* (Thumb-1 stmia cannot encode high registers), which destroyed the */ \
    /* interrupted context's own r4-r7.  That used to be survivable only */ \
    /* because every path out of here ended in RESTORE_CONTEXT, which */ \
    /* rewrites all 16 registers.  frameHandler may now decline to retarget */ \
    /* the exception (a stale preemption timer), in which case the exception */ \
    /* returns straight to the interrupted code and these registers are */ \
    /* whatever we leave them.  r2 has advanced 32 bytes across the two */ \
    /* stmia's, so reload the base address rather than backing it up -- the */ \
    /* r4 field is at offset 0, so it needs no displacement.  r8-r11 were */ \
    /* only read, never written, so they need no restore. */ \
    "ldr  r2, =_savedContext \n" \
    "ldmia r2!, {r4-r7}    \n" \
    "movs r0, #4           \n" /* EXC_RETURN bit 2: 0 = MSP, 1 = PSP */ \
    "mov  r1, lr           \n" \
    "tst  r0, r1           \n" \
    "beq  1f               \n" \
    "mrs  r0, psp          \n" \
    "b    2f               \n" \
    "1:                    \n" \
    "mrs  r0, msp          \n" \
    "2:                    \n" \
    "push {r4, lr}         \n" /* EXC_RETURN preserved; pair keeps SP */ \
                               /* 8-byte aligned per AAPCS. */ \
    "ldr  r1, =" #frameHandler " \n" \
    "blx  r1               \n" \
    "pop  {r4, pc}         \n" /* Loading EXC_RETURN into PC returns */ \
    : \
    : \
    : "r0", "r1", "r2", "memory" \
  )

int arduinoSamD21x18AProcessStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  (void) debug;
  *returnValue = PROCESS_STACK_SIZE;
  return 0;
}

int arduinoSamD21x18AMemoryManagerStackSize(va_list args) {
  bool debug = (bool) va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  if (debug == false) {
    // This is the expected case, so list it first.
    *returnValue = MEMORY_MANAGER_STACK_SIZE;
  } else {
    *returnValue = MEMORY_MANAGER_DEBUG_STACK_SIZE;
  }
  return 0;
}

int arduinoSamD21x18ABottomOfHeap(va_list args) {
  bool debug = (bool) va_arg(args, int);
  void **returnValue = va_arg(args, void**);
  (void) debug;
  *returnValue = (void*) (OVERLAY_ADDRESS + OVERLAY_SIZE);
  return 0;
}

int arduinoSamD21x18ANumExtraSchedulerStacks(va_list args) {
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  *returnValue = 0;
  return 0;
}

int arduinoSamD21x18ANumExtraConsoleStacks(va_list args) {
  bool debug = (bool) va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  *returnValue = 1;
  return 0;
}

/// @def MAX_UARTS
///
/// @brief The maximum number of serial ports we can support on the board.
///
/// @note Due to the implementation of some versions of the version of Arduino
/// libraries, we can't create two instances of the same base serial port class.
///  So, we'll have to use switch statements throughout the serial port code.
#define MAX_UARTS 2

/// @def UART_USB
///
/// @brief The numerical ID that will correspond to the USB serial port in this
/// HAL.
#define UART_USB 0

/// @def BOARD_UART
///
/// @brief The numerical ID that will correspond to the UART serial port in
/// this HAL.
#define BOARD_UART 1

int arduinoSamD21x18AInitUart(va_list args) {
  (void) args;
  // Nothing really to do on this platform.  Just return.
  return 0;
}

int arduinoSamD21x18AConfigureUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint32_t baud = va_arg(args, uint32_t);
  int returnValue = -ERANGE;

  switch (deviceId) {
    case UART_USB:
      {
        Serial.begin(baud);
        while (!Serial);
        returnValue = 0;
        break;
      }

    case BOARD_UART:
      {
        Serial1.begin(baud);
        while (!Serial1);
        returnValue = 0;
        break;
      }
  }

  return returnValue;
}

int arduinoSamD21x18APollUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  int serialData = -ERANGE;

  switch (deviceId) {
    case UART_USB:
      {
        serialData = Serial.read();
        break;
      }

    case BOARD_UART:
      {
        serialData = Serial1.read();
        break;
      }
  }

  return serialData;
}

int arduinoSamD21x18AWriteUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  const uint8_t *data = va_arg(args, const uint8_t*);
  ssize_t length = va_arg(args, ssize_t);
  ssize_t *returnValue = va_arg(args, ssize_t*);

  ssize_t numBytesWritten = -ERANGE;

  switch (deviceId) {
    case UART_USB:
      {
        numBytesWritten = Serial.write(data, length);
        break;
      }

    case BOARD_UART:
      {
        numBytesWritten = Serial1.write(data, length);
        break;
      }
  }

  if (returnValue != NULL) {
    *returnValue = numBytesWritten;
  }
  return (numBytesWritten >= 0) ? 0 : (int32_t) numBytesWritten;
}

int arduinoSamD21x18AIsUartConsole(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool *returnValue = va_arg(args, bool*);
  (void) deviceId;
  if (returnValue != NULL) {
    *returnValue = true;
  }
  return 0;
}

int arduinoSamD21x18AInitDio(va_list args) {
  (void) args;
  return 0;
}

static int arduinoSamD21x18AConfigureDioImpl(int32_t dio, bool output) {
  uint8_t modes[2] = { INPUT, OUTPUT };
  pinMode(dio, modes[output]);
  return 0;
}

int arduinoSamD21x18AConfigureDio(va_list args) {
  int32_t dio = va_arg(args, int32_t);
  bool output = (bool) va_arg(args, int);
  return arduinoSamD21x18AConfigureDioImpl(dio, output);
}

static int arduinoSamD21x18AWriteDioImpl(int32_t dio, bool high) {
  uint8_t levels[2] = { LOW, HIGH };
  digitalWrite(dio, levels[high]);
  return 0;
}

int arduinoSamD21x18AWriteDio(va_list args) {
  int32_t dio = va_arg(args, int32_t);
  bool high = (bool) va_arg(args, int);
  return arduinoSamD21x18AWriteDioImpl(dio, high);
}

/// @var globalSpiConfigured
///
/// @brief Whether or not the Arduino's SPI interface has already been
/// configured.
static bool globalSpiConfigured = false;

/// @var globalSpiInUse
///
/// @brief Whether or not the Arduino's SPI interface is currently in use.
static bool globalSpiInUse = false;

/// @var arduinoSamD21x18ASpiDevices
///
/// @brief Array of structures that will hold the information about SPI
/// connections.
static struct ArduinoSamD21x18ASpi {
  bool     configured;         // Will default to false
  uint8_t  chipSelect;
  bool     transferInProgress; // Will default to false
  uint32_t baud;
} arduinoSamD21x18ASpiDevices[MAX_SPI_DEVICES] = {};

/// @def numArduinoSpis
///
/// @brief The number of devices we support in the
/// arduinoSamD21x18ASpiDevices array.
///
/// @note This is a #define rather than a const int so that it doesn't need
/// its own KEEP_IN_FLASH treatment - it's folded into an immediate value at
/// each use site instead of occupying storage that could land in .rodata.
#define numArduinoSpis \
  ((int) (sizeof(arduinoSamD21x18ASpiDevices) \
  / sizeof(arduinoSamD21x18ASpiDevices[0])))

/// @def SPI_POWER_UP_CLOCK_BYTES
///
/// @brief 0xFF bytes clocked out with chip select deasserted right after a
/// device is configured.  The SD physical spec wants >= 74 clock cycles (>= 10
/// bytes) with CS and DI high before the first command; harmless for anything
/// else on the bus.
#define SPI_POWER_UP_CLOCK_BYTES 10

static int arduinoSamD21x18AInitSpiImpl(void) {
  if (globalSpiConfigured == false) {
    // Set up SPI at the default speed.
    globalSpiConfigured = true;
    SPI.begin();
  }
  return 0;
}

int arduinoSamD21x18AInitSpi(va_list args) {
  (void) args;
  return arduinoSamD21x18AInitSpiImpl();
}

int arduinoSamD21x18AConfigureSpi(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t cs   = (uint8_t) va_arg(args, int);
  uint8_t sck  = (uint8_t) va_arg(args, int);
  uint8_t copi = (uint8_t) va_arg(args, int);
  uint8_t cipo = (uint8_t) va_arg(args, int);
  uint32_t baud = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (
       (cs   == _spiSckDio)
    || (cs   == _spiCopiDio)
    || (cs   == _spiCipoDio)
    || (sck  != _spiSckDio)
    || (copi != _spiCopiDio)
    || (cipo != _spiCipoDio)
  ) {
    return -EINVAL;
  } else if (arduinoSamD21x18ASpiDevices[deviceId].configured == true) {
    return -EBUSY;
  }

  if (arduinoSamD21x18AInitSpiImpl() != 0) {
    return -ENODEV;
  }

  // Configure the chip select DIO for output.
  arduinoSamD21x18AConfigureDioImpl(cs, 1);
  // Deselect the chip select pin.
  arduinoSamD21x18AWriteDioImpl(cs, 1);

  // SD physical-spec power-up: >= 74 clock cycles (>= 10 bytes) with CS and DI
  // held high before the device is ever selected for a command.  Harmless for
  // any other SPI peripheral.
  SPI.beginTransaction(SPISettings(baud, MSBFIRST, SPI_MODE0));
  for (uint8_t ii = 0; ii < SPI_POWER_UP_CLOCK_BYTES; ii++) {
    SPI.transfer(0xFF);
  }
  SPI.endTransaction();

  // Configure our internal metadata for the device.
  arduinoSamD21x18ASpiDevices[deviceId].chipSelect = cs;
  arduinoSamD21x18ASpiDevices[deviceId].baud = baud;
  arduinoSamD21x18ASpiDevices[deviceId].configured = true;

  return 0;
}

int arduinoSamD21x18ASetSpiSpeed(va_list args) {
  int32_t  deviceId = va_arg(args, int32_t);
  uint32_t baud     = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSamD21x18ASpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  }
  if (baud == 0) {
    return -EINVAL;
  }

  // Picked up by the SPISettings passed to the next SPI.beginTransaction() in
  // arduinoSamD21x18AStartSpiTransferImpl().
  arduinoSamD21x18ASpiDevices[deviceId].baud = baud;
  return 0;
}

static int arduinoSamD21x18AStartSpiTransferImpl(int32_t deviceId) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSamD21x18ASpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (globalSpiInUse == true) {
    return -EBUSY;
  }

  // Mark the interface in use.
  globalSpiInUse = true;

  // Select the chip select pin.
  arduinoSamD21x18AWriteDioImpl(
    arduinoSamD21x18ASpiDevices[deviceId].chipSelect, 0);

  // Begin the transaction
  SPI.beginTransaction(SPISettings(arduinoSamD21x18ASpiDevices[deviceId].baud,
    MSBFIRST, SPI_MODE0));

  arduinoSamD21x18ASpiDevices[deviceId].transferInProgress = true;

  return 0;
}

int arduinoSamD21x18AStartSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  return arduinoSamD21x18AStartSpiTransferImpl(deviceId);
}

int arduinoSamD21x18AEndSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSamD21x18ASpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  }

  arduinoSamD21x18ASpiDevices[deviceId].transferInProgress = false;

  // End the transaction.
  SPI.endTransaction();

  // Deselect the chip select pin.
  arduinoSamD21x18AWriteDioImpl(
    arduinoSamD21x18ASpiDevices[deviceId].chipSelect, 1);
  for (int ii = 0; ii < 8; ii++) {
    SPI.transfer(0xFF); // 8 clock pulses
  }

  // Mark the interface not in use.
  globalSpiInUse = false;

  return 0;
}

int arduinoSamD21x18ASpiTransfer8(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t data = (uint8_t) va_arg(args, int);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSamD21x18ASpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSamD21x18ASpiDevices[deviceId].transferInProgress) {
    arduinoSamD21x18AStartSpiTransferImpl(deviceId);
  }

  return (int32_t) SPI.transfer(data);
}

int arduinoSamD21x18ASpiTransferBytes(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t *data = va_arg(args, uint8_t*);
  uint32_t length = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoSamD21x18ASpiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!arduinoSamD21x18ASpiDevices[deviceId].transferInProgress) {
    arduinoSamD21x18AStartSpiTransferImpl(deviceId);
  }

  SPI.transfer(data, length);

  return 0;
}

/// @var baseSystemTimeUs
///
/// @brief The time provided by the user or some other process as a baseline
/// time for the system.
static int64_t baseSystemTimeUs = 0;

int arduinoSamD21x18ATimeInit(va_list args) {
  (void) args;
  return 0;
}

int arduinoSamD21x18ASetSystemTime(va_list args) {
  struct timespec *now = va_arg(args, struct timespec*);
  if (now == NULL) {
    return -EINVAL;
  }

  baseSystemTimeUs
    = (((int64_t) now->tv_sec) * ((int64_t) 1000000))
    + (((int64_t) now->tv_nsec) / ((int64_t) 1000));

  return 0;
}

static int arduinoSamD21x18AGetElapsedMicrosecondsImpl(int64_t startTime,
  int64_t *returnValue
) {
  int64_t now = baseSystemTimeUs + micros();

  if (now < startTime) {
    if (returnValue != NULL) {
      *returnValue = -1;
    }
    return -EIO;
  }

  if (returnValue != NULL) {
    *returnValue = now - startTime;
  }
  return 0;
}

int arduinoSamD21x18AGetElapsedMilliseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t microseconds = 0;
  int32_t rv = arduinoSamD21x18AGetElapsedMicrosecondsImpl(
    startTime * ((int64_t) 1000), &microseconds);
  if (returnValue != NULL) {
    if (rv == 0) {
      *returnValue = microseconds / ((int64_t) 1000);
    } else {
      *returnValue = -1;
    }
  }
  return rv;
}

int arduinoSamD21x18AGetElapsedMicroseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  return arduinoSamD21x18AGetElapsedMicrosecondsImpl(startTime, returnValue);
}

int arduinoSamD21x18AGetElapsedNanoseconds(va_list args) {
  int64_t startTime = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t microseconds = 0;
  int32_t rv = arduinoSamD21x18AGetElapsedMicrosecondsImpl(
    startTime / ((int64_t) 1000), &microseconds);
  if (returnValue != NULL) {
    if (rv == 0) {
      *returnValue = microseconds * ((int64_t) 1000);
    } else {
      *returnValue = -1;
    }
  }
  return rv;
}

int arduinoSamD21x18AEnterMode(va_list args) {
  HalPowerMode powerMode = (HalPowerMode) va_arg(args, int);
  // You can't completely turn off the board from software.  The best we can
  // do is put into a low power state, so do the same set of operations for
  // both off and suspend.
  if ((powerMode == HAL_POWER_MODE_OFF)
    || (powerMode == HAL_POWER_MODE_SUSPEND)
  ) {
    // Configure for standby mode
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    // Set standby mode in Power Manager
    PM->SLEEP.reg = PM_SLEEP_IDLE_CPU;

    __DSB(); // Data Synchronization Barrier
    __WFI(); // Wait For Interrupt
  } else if (powerMode == HAL_POWER_MODE_RESET) {
    NVIC_SystemReset();
  }

  return 0;
}

/// @struct HardwareTimer
///
/// @brief Collection of variables needed to manage a single hardware timer.
///
/// @param tc A pointer to the timer/counter structure used for the timer.
/// @param irqType The IRQ used to manage the timer.
/// @param clockId The global clock identifier used to manage the timer.
/// @param initialized Whether or not the timer has been initialized yet.
/// @param callback The callback to call when the timer fires, if any.
/// @param active Whether or not the timer is currently active.
/// @param startTime The time, in nanoseconds, when the timer was configured.
/// @param deadline The time, in nanoseconds, when the timer expires.
typedef struct HardwareTimer {
  Tc *tc;
  IRQn_Type irqType;
  unsigned long clockId;
  bool initialized;
  void (*callback)(void);
  bool active;
  int64_t startTime;
  int64_t deadline;
} HardwareTimer;

/// @var hardwareTimers
///
/// @brief Array of HardwareTimer objects managed by the HAL.
static HardwareTimer hardwareTimers[] = {
  {
    .tc = TC3,
    .irqType = TC3_IRQn,
    .clockId = GCLK_CLKCTRL_ID_TCC2_TC3,
    .initialized = false,
    .callback = NULL,
    .active = false,
    .startTime = 0,
    .deadline = 0,
  },
  {
    .tc = TC4,
    .irqType = TC4_IRQn,
    .clockId = GCLK_CLKCTRL_ID_TC4_TC5,
    .initialized = false,
    .callback = NULL,
    .active = false,
    .startTime = 0,
    .deadline = 0,
  },
};

/// @def _numTimers
///
/// @brief The number of timers returned by HAL->timer->numSupported.
///
/// @note This is a #define rather than a const int so that it doesn't need
/// its own KEEP_IN_FLASH treatment - it's folded into an immediate value at
/// each use site instead of occupying storage that could land in .rodata.
#define _numTimers \
  ((int) (sizeof(hardwareTimers) / sizeof(hardwareTimers[0])))

/// @var halArduinoSamD21x18ATimersOnline
///
/// @brief Bitmask array of online timers.
static uint32_t halArduinoSamD21x18ATimersOnline[] = {
  0x00000003,
};

int arduinoSamD21x18AInitTimer(va_list args) {
  (void) args;
  return 0;
}

int arduinoSamD21x18AInitTimerDevice(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);

  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if (hwTimer->initialized) {
    // Nothing to do
    return 0;
  }

  // Enable GCLK for the TC timer (48MHz)
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_CLKEN |
                      GCLK_CLKCTRL_GEN_GCLK0 |
                      hwTimer->clockId;
  while (GCLK->STATUS.bit.SYNCBUSY);

  // Reset the TC timer
  hwTimer->tc->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Configure the TC timer in one-shot mode
  hwTimer->tc->COUNT16.CTRLA.reg
    = TC_CTRLA_MODE_COUNT16        // 16-bit counter
    | TC_CTRLA_WAVEGEN_NFRQ        // Normal frequency mode
    | TC_CTRLA_PRESCALER_DIV1;     // No prescaling (48MHz)

  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Enable one-shot mode via CTRLBSET
  hwTimer->tc->COUNT16.CTRLBSET.reg = TC_CTRLBSET_ONESHOT;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Enable compare match interrupt
  hwTimer->tc->COUNT16.INTENSET.reg = TC_INTENSET_OVF;

  // Enable the TC timer interrupt in NVIC
  NVIC_SetPriority(hwTimer->irqType, 0);
  NVIC_EnableIRQ(hwTimer->irqType);

  hwTimer->initialized = true;

  return 0;
}

static int arduinoSamD21x18ACancelTimerImpl(int32_t deviceId);

int arduinoSamD21x18AConfigOneShotTimer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint64_t nanoseconds = va_arg(args, uint64_t);
  void (*callback)(void) = va_arg(args, void (*)(void));

  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if (!hwTimer->initialized) {
    return -EINVAL;
  }

  // Cancel any existing timer
  arduinoSamD21x18ACancelTimerImpl(deviceId);

  // We take a number of nanoseconds for HAL compatibility, but our timers
  // don't support that resolution.  Convert to microseconds.
  uint64_t microseconds = nanoseconds / ((uint64_t) 1000);

  // Make sure we don't overflow
  if (microseconds > 89478485) {
    microseconds = 89478485; // 0xffffffff / 48
  }

  // Calculate ticks (48 ticks per microsecond)
  uint32_t ticks = microseconds * 48;

  // Check if we need prescaling for longer delays
  uint16_t prescaler = TC_CTRLA_PRESCALER_DIV1;

  if (ticks > 65535) {
    // Use DIV8 for up to ~10.9ms
    prescaler = TC_CTRLA_PRESCALER_DIV8;
    ticks = (microseconds * 48) / 8;

    if (ticks > 65535) {
      // Use DIV64 for up to ~87ms
      prescaler = TC_CTRLA_PRESCALER_DIV64;
      ticks = (microseconds * 48) / 64;

      if (ticks > 65535) {
        // Use DIV256 for up to ~349ms
        prescaler = TC_CTRLA_PRESCALER_DIV256;
        ticks = (microseconds * 48) / 256;

        if (ticks > 65535) {
          ticks = 65535; // Clamp to max
        }
      }
    }
  }

  hwTimer->callback = callback;
  hwTimer->active = true;

  // Disable timer
  hwTimer->tc->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Update prescaler
  hwTimer->tc->COUNT16.CTRLA.bit.PRESCALER = prescaler;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Load counter with (65535 - ticks) so it overflows after ticks counts
  hwTimer->tc->COUNT16.COUNT.reg = 65535 - ticks;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Clear any pending interrupts
  hwTimer->tc->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF;

  // Enable timer
  hwTimer->tc->COUNT16.CTRLA.reg |= TC_CTRLA_ENABLE;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);
  int64_t startTime = 0;
  arduinoSamD21x18AGetElapsedMicrosecondsImpl(0, &startTime);
  hwTimer->startTime = startTime * ((int64_t) 1000);
  hwTimer->deadline = hwTimer->startTime + (microseconds * ((uint64_t) 1000));

  return 0;
}

int arduinoSamD21x18AConfiguredTimerNanoseconds(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint64_t *returnValue = va_arg(args, uint64_t*);

  if (returnValue != NULL) {
    *returnValue = 0;
  }
  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    return -EINVAL;
  }

  if (returnValue != NULL) {
    *returnValue = hwTimer->deadline - hwTimer->startTime;
  }
  return 0;
}

int arduinoSamD21x18ARemainingTimerNanoseconds(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint64_t *returnValue = va_arg(args, uint64_t*);

  if (returnValue != NULL) {
    *returnValue = 0;
  }
  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    return -EINVAL;
  }

  int64_t nowUs = 0;
  arduinoSamD21x18AGetElapsedMicrosecondsImpl(0, &nowUs);
  int64_t now = nowUs * ((int64_t) 1000);
  if (now > hwTimer->deadline) {
    return 0;
  }

  if (returnValue != NULL) {
    *returnValue = hwTimer->deadline - now;
  }
  return 0;
}

static int arduinoSamD21x18ACancelTimerImpl(int32_t deviceId) {
  // Always validate capabilities first.  Only the scheduler (arming its own
  // preemption timer) gets an unconditional pass here.
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if (processDescriptor != NULL) {
    if ((processDescriptor->processId != schedulerPid)
      && (findHalCapabilityWithDevice(processDescriptor->halCapabilities,
        processDescriptor->numHalCapabilities, HAL_TIMER, HAL_TIMER_CANCEL,
        deviceId) == NULL)
    ) {
      return -EACCES;
    }
  }

  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if (!hwTimer->initialized) {
    return -EINVAL;
  } else if (!hwTimer->active) {
    // Not an error but nothing to do.
    return 0;
  }

  // Disable timer
  hwTimer->tc->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Clear interrupt flag
  hwTimer->tc->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF;

  // Also drop any interrupt the NVIC has already latched for this timer.
  // The peripheral INTFLAG above and the NVIC's pending bit are separate:
  // if the timer overflowed before we got here, the NVIC has already latched
  // a pending interrupt, and clearing INTFLAG does not retract it.  Without
  // this, a cancelled timer still delivers one more interrupt -- with
  // INTFLAG now reading clear, so the handler cannot even tell it is
  // spurious -- and that interrupt lands on whatever is running next, which
  // is exactly the stale-timer preemption this cancel is meant to prevent.
  NVIC_ClearPendingIRQ(hwTimer->irqType);

  hwTimer->active = false;
  hwTimer->startTime = 0;
  hwTimer->deadline = 0;
  hwTimer->callback = nullptr;

  return 0;
}

int arduinoSamD21x18ACancelTimer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  return arduinoSamD21x18ACancelTimerImpl(deviceId);
}

int arduinoSamD21x18ACancelAndGetTimer(va_list args) {
  // We need to get `now` as close to the beginning of this function call as
  // possible so that any call to reconfigure the timer later is correct.
  int64_t nowUs = micros();
  int64_t now = nowUs * ((int64_t) 1000);

  int32_t deviceId = va_arg(args, int32_t);
  uint64_t *configuredNanoseconds = va_arg(args, uint64_t*);
  uint64_t *remainingNanoseconds = va_arg(args, uint64_t*);
  void (**callback)(void) = va_arg(args, void (**)(void));

  if ((deviceId < 0) || (deviceId >= _numTimers)) {
    return -ERANGE;
  }

  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if ((!hwTimer->initialized) || (!hwTimer->active)) {
    // We cannot populate the provided pointers, so we will error here.  This
    // also signals to the caller that there's no need to call configTimer
    // later.
    return -EINVAL;
  }

  // ***DO NOT*** call arduinoSamD21x18ACancelTimer.  It's expected that
  // this function is in the critical path.  Time is of the essence, so inline
  // the logic.

  // Disable timer
  hwTimer->tc->COUNT16.CTRLA.reg &= ~TC_CTRLA_ENABLE;
  while (hwTimer->tc->COUNT16.STATUS.bit.SYNCBUSY);

  // Clear interrupt flag
  hwTimer->tc->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF;

  if (configuredNanoseconds != NULL) {
    if (hwTimer->deadline > hwTimer->startTime) {
      *configuredNanoseconds = hwTimer->deadline - hwTimer->startTime;
    } else {
      *configuredNanoseconds = 0;
    }
  }

  if (remainingNanoseconds != NULL) {
    if (now < hwTimer->deadline) {
      *remainingNanoseconds = hwTimer->deadline - now;
    } else {
      *remainingNanoseconds = 0;
    }
  }

  if (callback != NULL) {
    *callback = hwTimer->callback;
  }

  hwTimer->active = false;
  hwTimer->startTime = 0;
  hwTimer->deadline = 0;
  hwTimer->callback = nullptr;

  return 0;
}

/// @fn void arduinoSamD21x18ATimerInterruptHandler(int32_t deviceId)
///
/// @brief Base implementation for the Timer/Counter interrupt handlers.
///
/// @param deviceId The zero-based ID of the timer to handle.
///
/// @return This function returns no value.
void arduinoSamD21x18ATimerInterruptHandler(int32_t deviceId) {
  // This function is only called from one of the real interrupt handlers, so
  // we're guaranteed that the timer parameter is good.  Skip validation.
  HardwareTimer *hwTimer = &hardwareTimers[deviceId];

  hwTimer->active = false;
  hwTimer->startTime = 0;
  hwTimer->deadline = 0;

  // Call callback if set
  if (hwTimer->callback) {
    hwTimer->callback();
  }
}

/// @fn void arduinoSamD21x18ATimerInterruptHandler0()
///
/// @brief Call arduinoSamD21x18ATimerInterruptHandler with a value of 0.  This
/// effectively replaces TC3_Handler.
///
/// @return This function returns no value.
void arduinoSamD21x18ATimerInterruptHandler0() {
  SAVE_CONTEXT();
  arduinoSamD21x18ATimerInterruptHandler(0);
  RESTORE_CONTEXT();
}

/// @fn void arduinoSamD21x18ATimerInterruptHandler1()
///
/// @brief Call arduinoSamD21x18ATimerInterruptHandler with a value of 1.  This
/// effectively replaces TC4_Handler.
///
/// @return This function returns no value.
void arduinoSamD21x18ATimerInterruptHandler1() {
  SAVE_CONTEXT();
  arduinoSamD21x18ATimerInterruptHandler(1);
  RESTORE_CONTEXT();
}

/// @fn void arduinoSamD21x18ATimerFrameHandler(
///   uint32_t *exceptionFrame, int32_t deviceId, void (*resumeHandler)(void))
///
/// @brief Retarget a timer exception so that it resumes into a custom handler
/// in Thread mode instead of returning to the interrupted code directly.
///
/// @details
/// On the Cortex-M0, other interrupts cannot be processed while a timer
/// interrupt is being handled, and the timer interrupt cannot be marked
/// complete any way other than by returning from it.  So rather than doing
/// the real work inside the handler, we rewrite the stacked PC to point at
/// resumeHandler and then return: the hardware performs a proper exception
/// return (clearing the interrupt), but resumes executing resumeHandler in
/// Thread mode, where it is safe to switch coroutines.  resumeHandler is
/// responsible for putting the original context back via RESTORE_CONTEXT().
///
/// The exception frame is located deterministically.  The architecture
/// guarantees the hardware stacks exactly R0, R1, R2, R3, R12, LR, PC and
/// xPSR at the active stack pointer, so the callers below simply hand us
/// that pointer (MSP or PSP, selected by bit 2 of EXC_RETURN).
///
/// This previously searched the stack for a word that looked like a stacked
/// xPSR (T-bit set, bits 0-23 clear) instead.  That was not reliable in two
/// distinct ways, both of which were observed on real hardware.  It could
/// fail to match at all -- stacked xPSR carries the interrupted context's
/// IPSR in bits 0-8, so preempting anything already in an exception context
/// left those bits nonzero -- and the unbounded search then ran off the top
/// of RAM and took a HardFault.  It could also match a false positive: any
/// unrelated stack word equal to 0x01000000 stopped the search early, after
/// which the offsets below read a garbage PC, LR and SP, and RESTORE_CONTEXT
/// would branch to a bogus address with a bogus stack pointer.
///
/// @param exceptionFrame A pointer to the hardware-stacked exception frame.
/// @param deviceId The zero-based ID of the timer being handled.
/// @param resumeHandler The function to resume into in Thread mode.
///
/// @return This function returns no value.
static void arduinoSamD21x18ATimerFrameHandler(
  uint32_t *exceptionFrame, int32_t deviceId, void (*resumeHandler)(void)
) {
  HardwareTimer *hwTimer = &hardwareTimers[deviceId];
  if (hwTimer->tc->COUNT16.INTFLAG.bit.OVF) {
    // Clear interrupt flag
    hwTimer->tc->COUNT16.INTFLAG.reg = TC_INTFLAG_OVF;
  } else {
    // The peripheral is not signalling an overflow, so this interrupt was
    // latched in the NVIC before something cleared INTFLAG underneath it --
    // a timer that was cancelled after it had already expired.  There is no
    // preemption being requested here, so returning without retargeting
    // exceptionFrame[6] resumes the interrupted code untouched.  (Cancel now
    // calls NVIC_ClearPendingIRQ, so this should no longer be reachable; it
    // stays because honouring a spurious interrupt means corrupting whatever
    // was running, which is too expensive a failure to leave to inference.)
    return;
  }

  // Refuse to hijack a context that was never eligible for preemption.  The
  // scheduler only ever arms this timer for a process with privilegeLevel >
  // PRIVILEGE_LEVEL_EXECUTIVE, so seeing it fire while anything at or below
  // EXECUTIVE is running means the one-shot outlived the process it was
  // armed for.  Hijacking here would hand an arbitrary instruction boundary
  // in the scheduler (or the filesystem, or the memory manager) to
  // RESTORE_CONTEXT, which is only valid for a context the scheduler is
  // prepared to resume.
  //
  // Returning without retargeting exceptionFrame[6] lets the exception
  // return normally to exactly where it interrupted, leaving the running
  // process untouched.  That is safe to do unconditionally because the timer
  // is a one-shot (arduinoSamD21x18AConfigOneShotTimer), so declining it does
  // not leave it re-firing.
  //
  // This should no longer fire: the scheduler now cancels the timer after
  // every processResume(), which closed the exit-without-yield path that
  // produced these.  It stays as an invariant check, because honouring such
  // an interrupt silently corrupts whichever kernel process was running.
  ProcessDescriptor *runningProcess = getRunningProcess();
  if ((runningProcess != NULL)
    && (runningProcess->privilegeLevel <= PRIVILEGE_LEVEL_EXECUTIVE)
  ) {
    if (_diagStaleTimerPrintsRemaining > 0) {
      _diagStaleTimerPrintsRemaining--;
      printString(_diagStaleTimer);
      printInt(runningProcess->processId);
      printString(_diagStaleTimerPrivilege);
      printInt((int) runningProcess->privilegeLevel);
      printString(_diagStaleTimerPc);
      printHex(exceptionFrame[6]);
      printString(_diagStaleTimerNewline);
    }
    return;
  }

  _savedContext.r0  = exceptionFrame[0];
  _savedContext.r1  = exceptionFrame[1];
  _savedContext.r2  = exceptionFrame[2];
  _savedContext.r3  = exceptionFrame[3];
  _savedContext.r12 = exceptionFrame[4];
  _savedContext.lr  = exceptionFrame[5];
  _savedContext.pc  = exceptionFrame[6] | 1;
  // The frame is eight words; the interrupted code's stack resumes above it.
  _savedContext.sp  = (uint32_t) &exceptionFrame[8];

  // Retarget the exception return to resumeHandler.
  exceptionFrame[6] = (uint32_t) resumeHandler;
}

/// @fn void arduinoSamD21x18ATimerFrameHandler0(uint32_t *exceptionFrame)
///
/// @brief Timer 0's exception-frame handler.  Named (rather than inlined)
/// because TC3_Handler below references it from assembly.
///
/// @param exceptionFrame A pointer to the hardware-stacked exception frame.
///
/// @return This function returns no value.
extern "C" void arduinoSamD21x18ATimerFrameHandler0(
  uint32_t *exceptionFrame
) {
  arduinoSamD21x18ATimerFrameHandler(
    exceptionFrame, 0, arduinoSamD21x18ATimerInterruptHandler0);
}

/// @fn void arduinoSamD21x18ATimerFrameHandler1(uint32_t *exceptionFrame)
///
/// @brief Timer 1's exception-frame handler.  Named (rather than inlined)
/// because TC4_Handler below references it from assembly.
///
/// @param exceptionFrame A pointer to the hardware-stacked exception frame.
///
/// @return This function returns no value.
extern "C" void arduinoSamD21x18ATimerFrameHandler1(
  uint32_t *exceptionFrame
) {
  arduinoSamD21x18ATimerFrameHandler(
    exceptionFrame, 1, arduinoSamD21x18ATimerInterruptHandler1);
}

/// @fn void TC3_Handler(void)
///
/// @brief Interrupt handler for Timer/Counter 3.
///
/// @return This function returns no value.
__attribute__((naked)) void TC3_Handler(void) {
  TIMER_EXCEPTION_ENTRY(arduinoSamD21x18ATimerFrameHandler0);
}

/// @fn void TC4_Handler(void)
///
/// @brief Interrupt handler for Timer/Counter 4.
///
/// @return This function returns no value.
__attribute__((naked)) void TC4_Handler(void) {
  TIMER_EXCEPTION_ENTRY(arduinoSamD21x18ATimerFrameHandler1);
}

/// @var blockDevices
///
/// @brief Array of BlockDevice pointers that are managed by the driver
/// processes.
static BlockDevice *blockDevices[] = {
  NULL,
};

/// @def _numBlockDevices
///
/// @brief Number of BlockDevices that can be managed by the HAL.
///
/// @note This is a #define rather than a const uint32_t so that it doesn't
/// need its own KEEP_IN_FLASH treatment - it's folded into an immediate
/// value at each use site instead of occupying storage that could land in
/// .rodata.
#define _numBlockDevices \
  ((uint32_t) (sizeof(blockDevices) / sizeof(blockDevices[0])))

/// @var halArduinoSamD21x18ABlockDevicesOnline
///
/// @brief Bitmask array of online block devices.
static uint32_t halArduinoSamD21x18ABlockDevicesOnline[] = {
  0x00000000,
};

/// @var halArduinoSamD21x18AUartsOnline
///
/// @brief Placeholder; actual online arrays come from the per-board init args.
static uint32_t *halArduinoSamD21x18AUartsOnline = NULL;

/// @var halArduinoSamD21x18ADiosOnline
///
/// @brief Placeholder; actual online arrays come from the per-board init args.
static uint32_t *halArduinoSamD21x18ADiosOnline = NULL;

/// @var halArduinoSamD21x18ASpisOnline
///
/// @brief Bitmask array of online SPIs.
static uint32_t halArduinoSamD21x18ASpisOnline[] = {
  0x00000003,
};

int arduinoSamD21x18AInitBlockDevice(va_list args) {
  (void) args;
  if (schedulerIsInitialized() == false) {
    return -EBUSY;
  }

  // Create the SD card process.
  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio   = _sdCardPinChipSelect,
    .spiCopiDio = _spiCopiDio,
    .spiCipoDio = _spiCipoDio,
    .spiSckDio  = _spiSckDio,
  };

  blockDevices[0] = halCommonInitRootSdSpiStorage(&sdCardSpiArgs);
  if (blockDevices[0] == NULL) {
    return -ENODEV;
  }
  setOnline(HAL->blockDevice, 0);

  return 0;
}

int arduinoSamD21x18AGetBlockDevice(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  BlockDevice **returnValue = va_arg(args, BlockDevice**);

  if (!online(HAL->blockDevice, deviceId)) {
    if (returnValue != NULL) {
      *returnValue = NULL;
    }
    return -ENODEV;
  }

  if (returnValue != NULL) {
    *returnValue = blockDevices[deviceId];
  }
  return 0;
}

/// @var _sdCardName
///
/// @brief Process name assigned to the SD card process.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _sdCardName[] KEEP_IN_FLASH = "SD card";

int arduinoSamD21x18ARestartBlockDevice(va_list args) {
  ProcessDescriptor *processDescriptor = va_arg(args, ProcessDescriptor*);
  int32_t deviceId = (int32_t) (intptr_t) processDescriptor->restartArgs;

  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio   = _sdCardPinChipSelect,
    .spiCopiDio = _spiCopiDio,
    .spiCipoDio = _spiCipoDio,
    .spiSckDio  = _spiSckDio,
  };

  if (processCreate(processDescriptor, runSdCardSpi, &sdCardSpiArgs)
    != processSuccess
  ) {
    logError("Could not restart SD card process\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = _sdCardName;
  processDescriptor->userId = ROOT_USER_ID;
  if (sdCardHalCapabilities != NULL) {
    processDescriptor->halCapabilities = sdCardHalCapabilities;
    processDescriptor->numHalCapabilities = numSdCardHalCapabilities;
  }

  BlockDevice *sdDevice
    = (BlockDevice*) coroutineResume(processDescriptor->mainThread, NULL);
  if (sdDevice == NULL) {
    logError("SD card restart returned NULL\n");
    return -ENODEV;
  }
  sdDevice->partitionNumber = 1;
  blockDevices[deviceId] = sdDevice;
  setOnline(HAL->blockDevice, deviceId);

  return 0;
}

/// @var _logBuffer
///
/// @brief Statically allocated buffer for formatting log messages.
static char _logBuffer[128];

/// @def NUM_LOG_ENTRIES
///
/// @brief The number of LogEntry objects held in our local array.
#define NUM_LOG_ENTRIES 3

/// @var _logEntries
///
/// @brief Local array of LogEntry objects to use in communication with the
/// logger process.
static LogEntry _logEntries[NUM_LOG_ENTRIES];

/// @var _logMessages
///
/// @brief Private pool of ProcessMessage objects used to deliver log entries to
/// the logger process.  One per _logEntries slot.
static ProcessMessage _logMessages[NUM_LOG_ENTRIES];

/// @var _allProcesses
///
/// @brief Statically allocated buffer of ProcessDescriptors to hold the
/// metadata for all processes on the system, including the scheduler.
static ProcessDescriptor _allProcesses[NUM_PROCESSES];

/// @var _namedProcesses
///
/// @brief This platform's storage for HalCommon.c's named-process lookup
/// table.  Sized for the processes arduinoSamD21x18ADoStartProcesses can
/// register (filesystem, logger).
static NamedProcessEntry _namedProcesses[2];

/// @var _filesystemIpcCapabilities
///
/// @brief This platform's storage for the filesystem process's IPC
/// capabilities.  See FILESYSTEM_IPC_CAPABILITIES_INITIALIZER.
static IpcCapability _filesystemIpcCapabilities[]
  = FILESYSTEM_IPC_CAPABILITIES_INITIALIZER;

/// @var _loggerIpcCapabilities
///
/// @brief This platform's storage for the logger process's IPC
/// capabilities.  See LOGGER_IPC_CAPABILITIES_INITIALIZER.
static IpcCapability _loggerIpcCapabilities[]
  = LOGGER_IPC_CAPABILITIES_INITIALIZER;

/// @var _sdCardHalCapabilities
///
/// @brief This platform's storage for the SD-over-SPI card process's HAL
/// capabilities.  See SD_CARD_HAL_CAPABILITIES_INITIALIZER.
static HalCapability _sdCardHalCapabilities[]
  = SD_CARD_HAL_CAPABILITIES_INITIALIZER;

int arduinoSamD21x18ACallFileOverlay(va_list args) {
  HalCallFileOverlayFn *returnValue = va_arg(args, HalCallFileOverlayFn*);
  if (returnValue != NULL) {
    *returnValue = callOverlayFunctionFromFile;
  }
  return 0;
}

int arduinoSamD21x18AExecCommand(va_list args) {
  HalExecCommandFn *returnValue = va_arg(args, HalExecCommandFn*);
  if (returnValue != NULL) {
    *returnValue = execOverlayCommand;
  }
  return 0;
}

/// @fn static int arduinoSamD21x18ADoStartProcesses(void)
///
/// @brief Start every process specific to the SAMD21 platform: the root
/// filesystem (and, transitively, the SD card process), plus the logger if
/// this build's .rodata was stripped.
///
/// @return Returns 0 on success, -errno on failure.
static int arduinoSamD21x18ADoStartProcesses(void) {
  int returnValue = halCommonInitRootFilesystem();
  if (HAL->memory->stringsPresent == false) {
    int loggerStatus = halCommonInitLogger();
    if ((returnValue == 0) && (loggerStatus != 0)) {
      returnValue = loggerStatus;
    }
  }
  return returnValue;
}

int arduinoSamD21x18AStartProcesses(va_list args) {
  HalStartProcessesFn *returnValue = va_arg(args, HalStartProcessesFn*);
  if (returnValue != NULL) {
    *returnValue = arduinoSamD21x18ADoStartProcesses;
  }
  return 0;
}

int arduinoSamD21x18ARestartRootFilesystem(va_list args) {
  HalRestartRootFilesystemFn *returnValue
    = va_arg(args, HalRestartRootFilesystemFn*);
  if (returnValue != NULL) {
    *returnValue = restartBuiltinFilesystem;
  }
  return 0;
}

int arduinoSamD21x18ARestartShell(va_list args) {
  HalRestartShellFn *returnValue = va_arg(args, HalRestartShellFn*);
  if (returnValue != NULL) {
    *returnValue = restartOverlayShell;
  }
  return 0;
}

int arduinoSamD21x18AOverlayMap(va_list args) {
  NanoOsOverlayMap **returnValue = va_arg(args, NanoOsOverlayMap**);
  if (returnValue != NULL) {
    *returnValue = (NanoOsOverlayMap*) OVERLAY_ADDRESS;
  }
  return 0;
}

int arduinoSamD21x18AStaticLogs(va_list args) {
  StaticLogs **returnValue = va_arg(args, StaticLogs**);
  if (returnValue != NULL) {
//// #if LOG_THRESHOLD < LOG_LEVEL_DETAIL
    *returnValue = NULL;
//// #else // LOG_THRESHOLD >= LOG_LEVEL_DETAIL
////     *returnValue = (StaticLogs*) STATIC_LOGS_ADDRESS;
//// #endif // LOG_THRESHOLD < LOG_LEVEL_DETAIL
  }
  return 0;
}

int arduinoSamD21x18ALogBuffer(va_list args) {
  char **returnValue = va_arg(args, char**);
  if (returnValue != NULL) {
    *returnValue = _logBuffer;
  }
  return 0;
}

int arduinoSamD21x18ALogEntries(va_list args) {
  LogEntry **returnValue = va_arg(args, LogEntry**);
  if (returnValue != NULL) {
    *returnValue = _logEntries;
  }
  return 0;
}

int arduinoSamD21x18ALogMessages(va_list args) {
  ProcessMessage **returnValue = va_arg(args, ProcessMessage**);
  if (returnValue != NULL) {
    *returnValue = _logMessages;
  }
  return 0;
}

int arduinoSamD21x18AAllProcesses(va_list args) {
  ProcessDescriptor **returnValue = va_arg(args, ProcessDescriptor**);
  if (returnValue != NULL) {
    *returnValue = _allProcesses;
  }
  return 0;
}

int arduinoSamD21x18AReadyQueues(va_list args) {
  ProcessQueue ***returnValue = va_arg(args, ProcessQueue***);
  if (returnValue != NULL) {
    *returnValue = _readyQueues;
  }
  return 0;
}

int arduinoSamD21x18AWaitingQueue(va_list args) {
  ProcessQueue **returnValue = va_arg(args, ProcessQueue**);
  if (returnValue != NULL) {
    *returnValue = (ProcessQueue*) &_waitingQueue;
  }
  return 0;
}

int arduinoSamD21x18ATimedWaitingQueue(va_list args) {
  ProcessQueue **returnValue = va_arg(args, ProcessQueue**);
  if (returnValue != NULL) {
    *returnValue = (ProcessQueue*) &_timedWaitingQueue;
  }
  return 0;
}

int arduinoSamD21x18AFreeQueue(va_list args) {
  ProcessQueue **returnValue = va_arg(args, ProcessQueue**);
  if (returnValue != NULL) {
    *returnValue = (ProcessQueue*) &_freeQueue;
  }
  return 0;
}

int arduinoSamD21x18AProcessErrorNumbers(va_list args) {
  int **returnValue = va_arg(args, int**);
  if (returnValue != NULL) {
    *returnValue = _processErrorNumbers;
  }
  return 0;
}

int arduinoSamD21x18AProcessStorage(va_list args) {
  void ****returnValue = va_arg(args, void****);
  if (returnValue != NULL) {
    *returnValue = _processStorage;
  }
  return 0;
}

static HalFunction arduinoSamD21x18APlatformFunctions[HAL_PLATFORM_NUM_FNS] = {
  [HAL_PLATFORM_CALL_FILE_OVERLAY]
    = arduinoSamD21x18ACallFileOverlay,
  [HAL_PLATFORM_EXEC_COMMAND]
    = arduinoSamD21x18AExecCommand,
  [HAL_PLATFORM_RESTART_ROOT_FILESYSTEM]
    = arduinoSamD21x18ARestartRootFilesystem,
  [HAL_PLATFORM_RESTART_SHELL]
    = arduinoSamD21x18ARestartShell,
  [HAL_PLATFORM_START_PROCESSES]
    = arduinoSamD21x18AStartProcesses,
};

// NOTE: avr-g++/arm-none-eabi-g++ cannot compile a designated-initializer
// array with gaps ("sorry, unimplemented: non-trivial designated
// initializers not supported"), so every enum index must be listed in
// order, even the ones this platform leaves NULL.
static HalFunction arduinoSamD21x18AMemoryFunctions[HAL_MEMORY_NUM_FNS] = {
  [HAL_MEMORY_PROCESS_STACK_SIZE]         = arduinoSamD21x18AProcessStackSize,
  [HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = arduinoSamD21x18AMemoryManagerStackSize,
  [HAL_MEMORY_BOTTOM_OF_HEAP]             = arduinoSamD21x18ABottomOfHeap,
  [HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = arduinoSamD21x18ANumExtraSchedulerStacks,
  [HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = arduinoSamD21x18ANumExtraConsoleStacks,
  [HAL_MEMORY_OVERLAY_MAP]                = arduinoSamD21x18AOverlayMap,
  [HAL_MEMORY_CONTIGUOUS_FILESYSTEM]      = NULL,
  [HAL_MEMORY_STATIC_LOGS]                = arduinoSamD21x18AStaticLogs,
  [HAL_MEMORY_LOG_BUFFER]                 = arduinoSamD21x18ALogBuffer,
  [HAL_MEMORY_LOG_ENTRIES]                = arduinoSamD21x18ALogEntries,
  [HAL_MEMORY_LOG_MESSAGES]               = arduinoSamD21x18ALogMessages,
  [HAL_MEMORY_ALL_PROCESSES]              = arduinoSamD21x18AAllProcesses,
  [HAL_MEMORY_READY_QUEUES]               = arduinoSamD21x18AReadyQueues,
  [HAL_MEMORY_WAITING_QUEUE]              = arduinoSamD21x18AWaitingQueue,
  [HAL_MEMORY_TIMED_WAITING_QUEUE]        = arduinoSamD21x18ATimedWaitingQueue,
  [HAL_MEMORY_FREE_QUEUE]                 = arduinoSamD21x18AFreeQueue,
  [HAL_MEMORY_PROCESS_ERROR_NUMBERS]      = arduinoSamD21x18AProcessErrorNumbers,
  [HAL_MEMORY_PROCESS_STORAGE]            = arduinoSamD21x18AProcessStorage,
};

static HalFunction arduinoSamD21x18AUartFunctions[HAL_UART_NUM_FNS] = {
  [HAL_UART_INIT]       = arduinoSamD21x18AInitUart,
  [HAL_UART_CONFIGURE]  = arduinoSamD21x18AConfigureUart,
  [HAL_UART_POLL]       = arduinoSamD21x18APollUart,
  [HAL_UART_WRITE]      = arduinoSamD21x18AWriteUart,
  [HAL_UART_IS_CONSOLE] = arduinoSamD21x18AIsUartConsole,
};

static HalFunction arduinoSamD21x18ADioFunctions[HAL_DIO_NUM_FNS] = {
  [HAL_DIO_INIT]      = arduinoSamD21x18AInitDio,
  [HAL_DIO_CONFIGURE] = arduinoSamD21x18AConfigureDio,
  [HAL_DIO_WRITE]     = arduinoSamD21x18AWriteDio,
};

static HalFunction arduinoSamD21x18ASpiFunctions[HAL_SPI_NUM_FNS] = {
  [HAL_SPI_INIT]           = arduinoSamD21x18AInitSpi,
  [HAL_SPI_CONFIGURE]      = arduinoSamD21x18AConfigureSpi,
  [HAL_SPI_START_TRANSFER] = arduinoSamD21x18AStartSpiTransfer,
  [HAL_SPI_END_TRANSFER]   = arduinoSamD21x18AEndSpiTransfer,
  [HAL_SPI_TRANSFER8]      = arduinoSamD21x18ASpiTransfer8,
  [HAL_SPI_TRANSFER_BYTES] = arduinoSamD21x18ASpiTransferBytes,
  [HAL_SPI_SET_SPEED]      = arduinoSamD21x18ASetSpiSpeed,
};

static HalFunction arduinoSamD21x18AClockFunctions[HAL_CLOCK_NUM_FNS] = {
  [HAL_CLOCK_INIT]                     = arduinoSamD21x18ATimeInit,
  [HAL_CLOCK_SET_SYSTEM_TIME]          = arduinoSamD21x18ASetSystemTime,
  [HAL_CLOCK_GET_ELAPSED_MILLISECONDS] = arduinoSamD21x18AGetElapsedMilliseconds,
  [HAL_CLOCK_GET_ELAPSED_MICROSECONDS] = arduinoSamD21x18AGetElapsedMicroseconds,
  [HAL_CLOCK_GET_ELAPSED_NANOSECONDS]  = arduinoSamD21x18AGetElapsedNanoseconds,
};

static HalFunction arduinoSamD21x18APowerFunctions[HAL_POWER_NUM_FNS] = {
  [HAL_POWER_ENTER_MODE] = arduinoSamD21x18AEnterMode,
};

static HalFunction arduinoSamD21x18ATimerFunctions[HAL_TIMER_NUM_FNS] = {
  [HAL_TIMER_INIT]                   = arduinoSamD21x18AInitTimer,
  [HAL_TIMER_INIT_DEVICE]            = arduinoSamD21x18AInitTimerDevice,
  [HAL_TIMER_CONFIG_ONE_SHOT]        = arduinoSamD21x18AConfigOneShotTimer,
  [HAL_TIMER_CONFIGURED_NANOSECONDS] = arduinoSamD21x18AConfiguredTimerNanoseconds,
  [HAL_TIMER_REMAINING_NANOSECONDS]  = arduinoSamD21x18ARemainingTimerNanoseconds,
  [HAL_TIMER_CANCEL]                 = arduinoSamD21x18ACancelTimer,
  [HAL_TIMER_CANCEL_AND_GET]         = arduinoSamD21x18ACancelAndGetTimer,
};

static HalFunction arduinoSamD21x18ABlockDeviceFunctions[HAL_BLOCK_DEVICE_NUM_FNS] = {
  [HAL_BLOCK_DEVICE_INIT]    = arduinoSamD21x18AInitBlockDevice,
  [HAL_BLOCK_DEVICE_GET]     = arduinoSamD21x18AGetBlockDevice,
  [HAL_BLOCK_DEVICE_RESTART] = arduinoSamD21x18ARestartBlockDevice,
};

/// @var _bssOverflowErrorPrefix
///
/// @brief Printed via a raw Serial.print when BSS has grown into the
/// overlay memory region.  This is a last-resort diagnostic that fires
/// before HAL/logger infrastructure can be trusted, so it deliberately
/// bypasses logError() - it must not be converted to a log macro call.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _bssOverflowErrorPrefix[] KEEP_IN_FLASH = "ERROR!!! 0x";

/// @var _greaterThanPrefix
///
/// @brief Separator printed between the two addresses in the BSS overflow
/// diagnostic.  See _bssOverflowErrorPrefix for why this stays a raw print.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _greaterThanPrefix[] KEEP_IN_FLASH = " > 0x";

/// @var _newline
///
/// @brief A single newline character.  See _bssOverflowErrorPrefix for why
/// this stays a raw print.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _newline[] KEEP_IN_FLASH = "\n";

/// @var _stackPositionPrefix
///
/// @brief Printed before the current stack pointer in the BSS overflow
/// diagnostic.  See _bssOverflowErrorPrefix for why this stays a raw print.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _stackPositionPrefix[] KEEP_IN_FLASH = "Stack position = 0x";

/// @var _bannerLine
///
/// @brief Border line printed above and below the BSS overflow warning.
/// See _bssOverflowErrorPrefix for why this stays a raw print.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _bannerLine[] KEEP_IN_FLASH
  = "*******************************************************\n";

/// @var _corruptionWarning
///
/// @brief Warning printed when BSS has grown into the overlay memory
/// region.  See _bssOverflowErrorPrefix for why this stays a raw print.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets.
static const char _corruptionWarning[] KEEP_IN_FLASH
  = "* Running user programs will corrupt system memory!!! *\n";

#ifdef __cplusplus
extern "C"
{
#endif

// We want to link in the built-in filesystem and FAT32 implementation, so
// provide those declarations here.
extern FilesystemState* filesystemInitDriver(FilesystemState
 *filesystemState);
extern const FilesystemCommandHandler
  fat32CommandHandlers[NUM_FILESYSTEM_COMMANDS];

#ifdef __cplusplus
} // extern "C"
#endif

int halArduinoSamD21x18AInit(HalArduinoSamD21x18AInitArgs *args) {
  // Wire up per-subsystem function arrays.
  halFunctions[HAL_PLATFORM]     = arduinoSamD21x18APlatformFunctions;
  halFunctions[HAL_MEMORY]       = arduinoSamD21x18AMemoryFunctions;
  halFunctions[HAL_UART]         = arduinoSamD21x18AUartFunctions;
  halFunctions[HAL_DIO]          = arduinoSamD21x18ADioFunctions;
  halFunctions[HAL_SPI]          = arduinoSamD21x18ASpiFunctions;
  halFunctions[HAL_CLOCK]        = arduinoSamD21x18AClockFunctions;
  halFunctions[HAL_POWER]        = arduinoSamD21x18APowerFunctions;
  halFunctions[HAL_TIMER]        = arduinoSamD21x18ATimerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = arduinoSamD21x18ABlockDeviceFunctions;

  // Set per-platform data members from the init args.
  _spiCopiDio          = args->spiCopiDio;
  _spiCipoDio          = args->spiCipoDio;
  _spiSckDio           = args->spiSckDio;
  _sdCardPinChipSelect = args->sdCardPinChipSelect;

  halArduinoSamD21x18AUartsOnline = args->uartsOnline;
  halArduinoSamD21x18ADiosOnline  = args->diosOnline;

  halImpl.memory->overlaySize = OVERLAY_SIZE;

  halImpl.memory->logBufferSize  = sizeof(_logBuffer);
  halImpl.memory->numLogEntries  = NUM_LOG_ENTRIES;
  memset(&_logEntries, 0, sizeof(_logEntries));
  memset(&_logMessages, 0, sizeof(_logMessages));
#ifdef NANO_OS_STRINGS_STRIPPED
  halImpl.memory->stringsPresent = false;
#else
  halImpl.memory->stringsPresent = true;
#endif // NANO_OS_STRINGS_STRIPPED

  memset(_namedProcesses, 0, sizeof(_namedProcesses));
  namedProcessTable = _namedProcesses;
  namedProcessTableCapacity
    = sizeof(_namedProcesses) / sizeof(_namedProcesses[0]);

  filesystemIpcCapabilities = _filesystemIpcCapabilities;
  numFilesystemIpcCapabilities
    = sizeof(_filesystemIpcCapabilities) / sizeof(_filesystemIpcCapabilities[0]);
  loggerIpcCapabilities = _loggerIpcCapabilities;
  numLoggerIpcCapabilities
    = sizeof(_loggerIpcCapabilities) / sizeof(_loggerIpcCapabilities[0]);

  sdCardHalCapabilities = _sdCardHalCapabilities;
  numSdCardHalCapabilities
    = sizeof(_sdCardHalCapabilities) / sizeof(_sdCardHalCapabilities[0]);

  memset(_allProcesses, 0, sizeof(_allProcesses));
  halImpl.memory->numProcesses   = NUM_PROCESSES;
  for (int ii = 0; ii < NUM_READY_QUEUES; ii++) {
    memset(_readyQueues[ii], 0, sizeof(HalProcessQueue));
  }
  memset(&_waitingQueue, 0, sizeof(HalProcessQueue));
  memset(&_timedWaitingQueue, 0, sizeof(HalProcessQueue));
  memset(&_freeQueue, 0, sizeof(HalProcessQueue));
  memset(&_processStorageBase, 0,
    NUM_PROCESSES * NUM_PROCESS_STORAGE_KEYS * sizeof(void*));
  for (int ii = 0; ii < NUM_PROCESSES; ii++) {
    _processStorage[ii] = _processStorageBase[ii];
  }

  halImpl.uart->numSupported = args->numUartsSupported;
  halImpl.uart->online       = args->uartsOnline;

  halImpl.dio->numSupported = args->numDiosSupported;
  halImpl.dio->online       = args->diosOnline;

  halImpl.spi->numSupported = MAX_SPI_DEVICES;
  halImpl.spi->online       = halArduinoSamD21x18ASpisOnline;

  halImpl.timer->numSupported = _numTimers;
  halImpl.timer->online       = halArduinoSamD21x18ATimersOnline;

  halImpl.blockDevice->numSupported = _numBlockDevices;
  halImpl.blockDevice->online       = halArduinoSamD21x18ABlockDevicesOnline;

  extern char __bss_end__;
  if (((uintptr_t) &__bss_end__)
    > ((uintptr_t) OVERLAY_ADDRESS)
  ) {
    int stackPosition = 0;
    Serial.begin(1000000);
    while (!Serial);
    Serial.print(_bssOverflowErrorPrefix);
    Serial.print((uintptr_t) &__bss_end__, HEX);
    Serial.print(_greaterThanPrefix);
    Serial.print((uintptr_t) OVERLAY_ADDRESS, HEX);
    Serial.print(_newline);
    Serial.print(_stackPositionPrefix);
    Serial.print((uintptr_t) &stackPosition, HEX);
    Serial.print(_newline);
    Serial.print(_bannerLine);
    Serial.print(_corruptionWarning);
    Serial.print(_bannerLine);
  }

  __enable_irq();  // Ensure global interrupts are enabled

  NANO_OS_API = &nanoOsApi;

  return halCommonInit(
    /* builtinFilesystemInitDriver= */ filesystemInitDriver,
    /* builtinFilesystemCommandHandlers= */ fat32CommandHandlers
  );
}

/// @var _diagHardFaultPrefix
///
/// @brief Strings for the HardFault handler below.
///
/// @note KEEP_IN_FLASH is required here because .rodata is removed from the
/// final binary on some targets -- and doubly so here, since this is the one
/// place we absolutely cannot afford a second fault while reporting the
/// first.
static const char _diagHardFaultPrefix[] KEEP_IN_FLASH
  = "\nDIAG HARDFAULT pc=";
static const char _diagHardFaultLr[] KEEP_IN_FLASH = " lr=";
static const char _diagHardFaultR0[] KEEP_IN_FLASH = " r0=";
static const char _diagHardFaultR1[] KEEP_IN_FLASH = " r1=";
static const char _diagHardFaultR2[] KEEP_IN_FLASH = " r2=";
static const char _diagHardFaultR3[] KEEP_IN_FLASH = " r3=";
static const char _diagHardFaultR4[] KEEP_IN_FLASH = " r4=";
static const char _diagHardFaultStackDump[] KEEP_IN_FLASH = "DIAG stack:";
static const char _diagHardFaultRunning[] KEEP_IN_FLASH
  = "DIAG runningPid=";
static const char _diagHardFaultRunCoro[] KEEP_IN_FLASH = " runningCoroutine=";
static const char _diagHardFaultThread[] KEEP_IN_FLASH = ":t";
static const char _diagHardFaultHalImpl[] KEEP_IN_FLASH = "DIAG halImpl@";
static const char _diagHardFaultHalFns[] KEEP_IN_FLASH = "DIAG halFunctions:";
static const char _diagHardFaultUartFns[] KEEP_IN_FLASH = "DIAG uartFns:";
static const char _diagHardFaultColon[] KEEP_IN_FLASH = ":";
static const char _diagHardFaultSpace[] KEEP_IN_FLASH = " ";
static const char _diagHardFaultFrame[] KEEP_IN_FLASH = " frame=";
static const char _diagHardFaultXpsr[] KEEP_IN_FLASH = " xpsr=";
static const char _diagHardFaultMsp[] KEEP_IN_FLASH = " msp=";
static const char _diagHardFaultPsp[] KEEP_IN_FLASH = " psp=";
static const char _diagHardFaultNewline[] KEEP_IN_FLASH = "\n";
static const char _diagHardFaultStacks[] KEEP_IN_FLASH
  = "DIAG stacks (pid:stackEnd:canary)";

/// @var _diagOverlayLoaded
///
/// @brief Strings describing the state of the overlay window at fault time.
/// The faulting PC lands inside that window, so what is loaded there -- and
/// whether it matches what the running process expects -- is the question.
static const char _diagOverlayLoaded[] KEEP_IN_FLASH
  = "DIAG overlay loaded magic=";
static const char _diagOverlayColon[] KEEP_IN_FLASH = ":";
static const char _diagOverlayDev[] KEEP_IN_FLASH = " dev=";
static const char _diagOverlayStart[] KEEP_IN_FLASH = " start=";
static const char _diagOverlayNum[] KEEP_IN_FLASH = " num=";
static const char _diagOverlayOsApi[] KEEP_IN_FLASH = " osApi=";
static const char _diagOverlayWanted[] KEEP_IN_FLASH
  = "DIAG overlay wanted dev=";
static const char _diagOverlayCode[] KEEP_IN_FLASH = "DIAG overlay code@";
static const char _diagOverlayBreak[] KEEP_IN_FLASH = "DIAG heap break=";
static const char _diagOverlayWindowBase[] KEEP_IN_FLASH = " windowBase=";
static const char _diagOverlayExpectApi[] KEEP_IN_FLASH = " expectedOsApi=";
static const char _diagOverlayBoundary[] KEEP_IN_FLASH
  = "DIAG boundary[base-16..base+16):";

/// @fn char* _sbrk(int increment)
///
/// @brief Override of newlib's heap break function, which otherwise comes
/// from libnosys.a(sbrk.o) and grows the break with no upper bound.
///
/// @details The newlib heap starts at .bss end and grows up toward the
/// overlay window.  Nothing used to stop it: USBCore/SPI/operator new took
/// the break past OVERLAY_ADDRESS, and since schedulerLoadOverlay memcpy()s
/// overlay images into that window and the overlay executes out of it, the
/// two corrupted each other silently -- a USB receive buffer straddling the
/// boundary overwrote the overlay header's osApi and the overlay then
/// branched through the garbage.  That took a long time to find precisely
/// because nothing failed at the point of the mistake.
///
/// Refusing to hand out memory at or past OVERLAY_ADDRESS turns that into an
/// ordinary allocation failure at the moment it happens.  Callers get NULL
/// from malloc instead of memory that another subsystem also believes it
/// owns.  Bounding against OVERLAY_ADDRESS directly (rather than a separate
/// constant) means there is no second value to keep in sync.
///
/// @param increment The number of bytes to move the break by.  May be zero
///   (to read the current break) or negative (to give memory back).
///
/// @return Returns the previous break on success, (char*) -1 on failure.
extern "C" char* _sbrk(int increment) {
  // Supplied by the linker: the first address past .bss, where the heap
  // begins.
  extern char end;

  // Zero-initialized at load, so this is safe no matter how early malloc is
  // first called.
  static char *heapEnd = NULL;
  if (heapEnd == NULL) {
    heapEnd = &end;
  }

  char *newHeapEnd = heapEnd + increment;
  if (newHeapEnd > ((char*) OVERLAY_ADDRESS)) {
    // Growing here would put heap blocks inside the overlay window.
    return (char*) -1;
  }

  char *previousHeapEnd = heapEnd;
  heapEnd = newHeapEnd;
  return previousHeapEnd;
}
static const char _diagHardFaultStackPid[] KEEP_IN_FLASH = " p";
static const char _diagHardFaultStackEnd[] KEEP_IN_FLASH = ":";
static const char _diagHardFaultCanaryOk[] KEEP_IN_FLASH = ":ok";
static const char _diagHardFaultCanaryBad[] KEEP_IN_FLASH = ":SMASHED";

extern "C" {

/// @fn void hardFaultReport(uint32_t *exceptionFrame)
///
/// @brief Report the stacked exception frame from a
/// HardFault.
///
/// @details The Arduino SAMD core's default HardFault_Handler is a weak
/// alias for Dummy_Handler, which is just "for (;;) { }" -- an infinite
/// loop with no output.  That is indistinguishable from a software hang:
/// dead console, nothing printed, nothing further scheduled.  Overriding it
/// with this lets us tell the two apart, and the stacked PC says exactly
/// which instruction faulted.
///
/// On exception entry the hardware stacks R0, R1, R2, R3, R12, LR, PC and
/// xPSR, so the frame indices below are fixed by the architecture.
///
/// @param exceptionFrame A pointer to the stacked exception frame, taken
///   from whichever stack pointer was active (see HardFault_Handler).
///
/// @return This function never returns.
void hardFaultReport(uint32_t *exceptionFrame, uint32_t r4Value) {
  printString(_diagHardFaultPrefix);
  printHex(exceptionFrame[6]); // Stacked PC: the faulting instruction.
  printString(_diagHardFaultLr);
  printHex(exceptionFrame[5]); // Stacked LR: who called it.
  printString(_diagHardFaultR0);
  printHex(exceptionFrame[0]);
  printString(_diagHardFaultR1);
  printHex(exceptionFrame[1]);
  printString(_diagHardFaultR2);
  printHex(exceptionFrame[2]);
  printString(_diagHardFaultR3);
  printHex(exceptionFrame[3]);

  // The frame address IS the stack pointer at the moment of the fault.  If
  // it lands at or near a coroutine's stack-end canary
  // (COROUTINE_STACK_END_VALUE, "STACKEND"), that is a stack overflow: the
  // hardware's automatic stacking ran off the end of the active stack, so
  // the register values above are whatever happened to already be in that
  // memory rather than a faithful snapshot.
  printString(_diagHardFaultFrame);
  printHex((uintptr_t) exceptionFrame);
  printString(_diagHardFaultXpsr);
  printHex(exceptionFrame[7]);

  uint32_t mspValue = 0;
  uint32_t pspValue = 0;
  __asm volatile ("mrs %0, msp" : "=r" (mspValue));
  __asm volatile ("mrs %0, psp" : "=r" (pspValue));
  printString(_diagHardFaultMsp);
  printHex(mspValue);
  printString(_diagHardFaultPsp);
  printHex(pspValue);
  // r4 is callee-saved so the hardware does not stack it, but it is still
  // live at fault time -- and it is the register holding the called
  // function pointer in the coroutine callback dispatch path.
  printString(_diagHardFaultR4);
  printHex(r4Value);
  printString(_diagHardFaultNewline);

  // Every coroutine stack is carved out of this one main stack by
  // coroutineAllocateStack, so the slabs are adjacent and separated only by
  // each coroutine's stack-end canary.  A process that overruns its budget
  // writes straight into its neighbour's stack, smashing saved return
  // addresses -- which is why faults land in function epilogues with an LR
  // that makes no sense for the call site.  Report each stack's boundary and
  // whether its canary survived, which names the process that overran.
  // Dump the words at and above the exception frame.  A single reported PC
  // has proven unreliable here -- an imprecise abort reports some later,
  // innocent instruction -- so dump raw stack words instead: the ones that
  // look like code addresses are the real return chain.  Bounded at
  // __StackTop so this cannot read off the end of RAM.
  extern uint32_t __StackTop;
  printString(_diagHardFaultStackDump);
  for (int ii = 0; ii < 32; ii++) {
    uint32_t *wordAddress = &exceptionFrame[ii];
    if (wordAddress >= &__StackTop) {
      break;
    }
    printString(_diagHardFaultSpace);
    printHex(*wordAddress);
  }
  printString(_diagHardFaultNewline);

  // Ownership of a stack address cannot be inferred from the gaps between
  // stackEnd values: the memory manager relocates its own stackEnd into its
  // init frame, and threadProvision carves slabs for dummy coroutines that
  // never appear in allProcesses[].  So ask the scheduler directly who is
  // running, and print each process's mainThread pointer to match against
  // the running coroutine.
  // r2 pointed into halImpl on the INVSTATE fault, and the fault itself was
  // a branch through a function pointer whose Thumb bit was clear.  Dump the
  // HAL dispatch struct's words so we can see directly whether its function
  // pointers have been corrupted -- a valid one is an odd flash address.
  {
    uint32_t *halWords = (uint32_t*) &halImpl;
    printString(_diagHardFaultHalImpl);
    printHex((uintptr_t) halWords);
    printString(_diagHardFaultColon);
    for (int ii = 0; ii < 24; ii++) {
      printString(_diagHardFaultSpace);
      printHex(halWords[ii]);
    }
    printString(_diagHardFaultNewline);
  }

  // callHal dispatches through halFunctions[subsystem][function], and only
  // rejects that entry if it is NULL -- a corrupted non-NULL value passes
  // the check and gets branched to, which is exactly an INVSTATE fault at a
  // non-Thumb address.  halImpl above is a different table; dump this one
  // too.  Every valid entry is an odd flash address.
  printString(_diagHardFaultHalFns);
  for (int ii = 0; ii < HAL_NUM_SUBSYSTEMS; ii++) {
    printString(_diagHardFaultSpace);
    printHex((uintptr_t) halFunctions[ii]);
  }
  printString(_diagHardFaultNewline);

  printString(_diagHardFaultUartFns);
  if (halFunctions[HAL_UART] != NULL) {
    for (int ii = 0; ii < 12; ii++) {
      printString(_diagHardFaultSpace);
      printHex((uintptr_t) halFunctions[HAL_UART][ii]);
    }
  }
  printString(_diagHardFaultNewline);

  printString(_diagHardFaultRunning);
  printInt(getRunningPid());
  printString(_diagHardFaultRunCoro);
  printHex((uintptr_t) getRunningCoroutine());
  printString(_diagHardFaultNewline);

  // The faulting PC has been identical across builds whose code moved, and it
  // lands inside the overlay window, so the question is whether the window
  // holds the overlay the running process expects.  Print what is loaded
  // (read out of the window itself, which is what schedulerLoadOverlay's
  // "already loaded" fast path trusts) next to what the running process's
  // descriptor asks for.  A mismatch means the process was resumed on top of
  // somebody else's overlay; a garbage magic means the window was overwritten.
  {
    NanoOsOverlayMap *faultOverlayMap = (NanoOsOverlayMap*) OVERLAY_ADDRESS;
    printString(_diagOverlayLoaded);
    printHex((uint32_t) (faultOverlayMap->header.magic >> 32));
    printString(_diagOverlayColon);
    printHex((uint32_t) faultOverlayMap->header.magic);
    printString(_diagOverlayDev);
    printHex((uintptr_t) faultOverlayMap->header.overlay.blockDevice);
    printString(_diagOverlayStart);
    printInt((int) faultOverlayMap->header.overlay.startBlock);
    printString(_diagOverlayNum);
    printInt((int) faultOverlayMap->header.overlay.numBlocks);
    printString(_diagOverlayOsApi);
    printHex((uintptr_t) faultOverlayMap->header.osApi);
    printString(_diagHardFaultNewline);

    // osApi is the first word of the window, so it is the first casualty if
    // anything growing up from .bss end runs into it.  NanoOs's own heap
    // starts above the window, but the Arduino core's malloc heap starts at
    // .bss end and grows toward it.  Print the current break next to the
    // window base: a break at or past the base proves that collision.  Also
    // print what osApi is supposed to be, and the words spanning the
    // boundary, so a near-miss is as visible as a hit.
    printString(_diagOverlayBreak);
    printHex((uintptr_t) _sbrk(0));
    printString(_diagOverlayWindowBase);
    printHex((uintptr_t) OVERLAY_ADDRESS);
    printString(_diagOverlayExpectApi);
    printHex((uintptr_t) NANO_OS_API);
    printString(_diagHardFaultNewline);

    printString(_diagOverlayBoundary);
    for (uint32_t address = ((uint32_t) OVERLAY_ADDRESS) - 16;
      address < ((uint32_t) OVERLAY_ADDRESS) + 16;
      address += 4
    ) {
      printString(_diagHardFaultSpace);
      printHex(*((uint32_t*) address));
    }
    printString(_diagHardFaultNewline);

    ProcessId faultPid = getRunningPid();
    if ((faultPid > 0) && (faultPid <= NUM_PROCESSES)) {
      ProcessDescriptor *faultProcess = &_allProcesses[faultPid - 1];
      printString(_diagOverlayWanted);
      printHex((uintptr_t) faultProcess->overlay.blockDevice);
      printString(_diagOverlayStart);
      printInt((int) faultProcess->overlay.startBlock);
      printString(_diagOverlayNum);
      printInt((int) faultProcess->overlay.numBlocks);
      printString(_diagHardFaultNewline);
    }

    // Dump the words around the faulting PC.  If the window really does hold
    // this process's code, these read as plausible Thumb; if the overlay was
    // swapped or clobbered, they will not.  Bounded to the window so this
    // cannot fault a second time.
    uint32_t faultPc = exceptionFrame[6] & ~((uint32_t) 3);
    uint32_t windowStart = (uint32_t) OVERLAY_ADDRESS;
    uint32_t windowEnd = windowStart + OVERLAY_SIZE;
    if ((faultPc >= windowStart + 16) && (faultPc < windowEnd)) {
      printString(_diagOverlayCode);
      printHex(faultPc - 16);
      printString(_diagOverlayColon);
      for (uint32_t address = faultPc - 16;
        (address < faultPc + 16) && (address < windowEnd);
        address += 4
      ) {
        printString(_diagHardFaultSpace);
        printHex(*((uint32_t*) address));
      }
      printString(_diagHardFaultNewline);
    }
  }

  printString(_diagHardFaultStacks);
  for (int ii = 0; ii < NUM_PROCESSES; ii++) {
    Coroutine *processThread = _allProcesses[ii].mainThread;
    if (processThread == NULL) {
      continue;
    }
    printString(_diagHardFaultStackPid);
    printInt(ii + 1);
    printString(_diagHardFaultThread);
    printHex((uintptr_t) processThread);
    printString(_diagHardFaultStackEnd);
    printHex((uintptr_t) processThread->stackEnd);
    if ((processThread->stackEnd != NULL)
      && (*(processThread->stackEnd) == COROUTINE_STACK_END_VALUE)
    ) {
      printString(_diagHardFaultCanaryOk);
    } else {
      printString(_diagHardFaultCanaryBad);
    }
  }
  printString(_diagHardFaultNewline);

  while (1) {
    // Same terminal state as the default handler, just no longer silent.
  }
}

/// @fn void HardFault_Handler(void)
///
/// @brief Strong override of the Arduino SAMD core's
/// weak HardFault_Handler.  Recovers the stacked exception frame from
/// whichever stack was in use (bit 2 of EXC_RETURN selects MSP vs PSP) and
/// hands it to hardFaultReport above.  Declared naked so the compiler can't
/// emit a prologue that would disturb the frame before we read it.
///
/// @return This function never returns.
///
/// @note Without this override the core's weak HardFault_Handler aliases
/// Dummy_Handler, which is a bare "for (;;) { }" -- a silent lockup that is
/// indistinguishable from a software hang.
__attribute__((naked)) void HardFault_Handler(void) {
  __asm volatile (
    "movs r0, #4                  \n"
    "mov  r1, lr                  \n"
    "tst  r0, r1                  \n"
    "beq  1f                      \n"
    "mrs  r0, psp                 \n"
    "b    2f                      \n"
    "1:                           \n"
    "mrs  r0, msp                 \n"
    "2:                           \n"
    "mov  r1, r4                  \n" /* r4 is not hardware-stacked */
    "ldr  r2, =hardFaultReport    \n"
    "bx   r2                      \n"
  );
}

} // extern "C"

#endif // defined(__SAMD21G18A__) || defined(__SAMD21E18A__)
