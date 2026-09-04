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

/// @file HalAgonLight2.c
///
/// @brief Stub HAL implementation for the Agon Light 2 (eZ80F92).
///
/// All subsystems return "good status" so that the kernel compiles and links
/// cleanly for a binary-size estimate.  Real implementations are to be filled
/// in as each subsystem is brought up on the hardware.

#ifdef NANO_OS_AGON_LIGHT_2

#include "HalAgonLight2.h"
#include "HalCommon.h"
#include "../kernel/Commands.h"
#include "../kernel/Logger.h"
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

// Assembly-level functions.
extern int agonLight2ReadPort(uint16_t port);
extern void agonLight2WritePort(uint16_t port, uint8_t c);
extern void agonLight2ConfigureSpiImpl(uint16_t divisor);
extern void agonLight2SetSpiBrgImpl(uint16_t divisor);
extern int agonLight2SpiTransfer8Impl(uint8_t c);
extern void agonLight2SystemReset(void);   // boot/AgonLight2/Boot.asm; no return
extern void agonLight2Halt(void);          // boot/AgonLight2/Boot.asm; no return
#ifdef __cplusplus
}
#endif


// ---------------------------------------------------------------------------
// Memory layout constants
// ---------------------------------------------------------------------------

/// @def SYSTEM_CLOCK_HZ
///
/// @brief The frequency the system clock runs at on the AgonLight 2.  This is
/// 18.432 MHz.
#define SYSTEM_CLOCK_HZ ((uint32_t) 18432000)

/// @def RAM_START_ADDRESS
///
/// @brief Address of the start of external RAM.  The AgonLight 2 uses the
/// eZ80F92 CPU, which begins external RAM at address 0x40000.  (0x0 through
/// 0x1ffff are 128 KB of flash.  0x20000 through 0x3ffff is missing on the
/// eZ80F92.  It's an additional 128 KB of flash on the eZ80F91.)
#define RAM_START_ADDRESS 0x40000

/// @def FILESYSTEM_DRIVER_ADDRESS
///
/// @brief Start of the contiguous filesystem image window.  NanoOs is primary
/// firmware executing in place from flash, so no external-RAM range is reserved
/// for the OS binary any more; the filesystem image sits at the very base of
/// external RAM.
#define FILESYSTEM_DRIVER_ADDRESS RAM_START_ADDRESS

/// @def FILESYSTEM_DRIVER_SIZE
///
/// @brief The size, in bytes, reserved for the filesystem driver in memory.  We
/// will reserve 32 KB for this.
#define FILESYSTEM_DRIVER_SIZE (32 * 1024)

/// @def DATA_BSS_REGION_ADDRESS
///
/// @brief Base of the external-RAM region holding NanoOs's own .bss and .data
/// (linked in that order), immediately above the filesystem window.
/// ld/AgonLight2.ld places both sections here; boot/AgonLight2/Boot.asm copies
/// .data in from its flash load image at startup and then zeroes .bss.
#define DATA_BSS_REGION_ADDRESS \
  (FILESYSTEM_DRIVER_ADDRESS + FILESYSTEM_DRIVER_SIZE)

/// @def DATA_BSS_REGION_SIZE
///
/// @brief Bytes reserved for .bss + .data + padding.  The current build uses
/// well under 2 KB; 4 KB leaves head-room as the HAL and kernel fill in.
#define DATA_BSS_REGION_SIZE (4 * 1024)

/// @def DATA_BSS_CANARY_ADDRESS
///
/// @brief The 36 KB mark of external RAM: the byte just past the .bss/.data
/// reservation.  Boot.asm stamps DATA_BSS_CANARY_VALUE here before it copies
/// .data; halAgonLight2Init() re-reads it as its last step.  A mismatch means
/// .bss or .data outgrew DATA_BSS_REGION_SIZE and corrupted memory past the
/// reservation.  Kept in sync with __data_bss_limit in ld/AgonLight2.ld.
#define DATA_BSS_CANARY_ADDRESS \
  (DATA_BSS_REGION_ADDRESS + DATA_BSS_REGION_SIZE)

/// @def DATA_BSS_CANARY_VALUE
///
/// @brief 64-bit known pattern written at DATA_BSS_CANARY_ADDRESS.
#define DATA_BSS_CANARY_VALUE ((uint64_t) 0x4abc4abc4abc4abcULL)

/// @def HEAP_START_ADDRESS
///
/// @brief Address of the start (bottom) of the heap.  The 8-byte integrity
/// canary occupies the first bytes here and is consumed once the heap is first
/// used, which happens after the boot-time integrity check has run.
#define HEAP_START_ADDRESS DATA_BSS_CANARY_ADDRESS

/// @def STATIC_LOGS_ADDRESS
///
/// @brief Static-log metadata/entry area: 4 KB above the bottom of the heap.
#define STATIC_LOGS_ADDRESS (HEAP_START_ADDRESS + (4 * 1024))

/// @def PROCESS_STACK_SIZE
///
/// @brief Default process stack size in bytes.
#define PROCESS_STACK_SIZE 1024

/// @def MEMORY_MANAGER_STACK_SIZE
///
/// @brief Stack for the memory-manager process.
#define MEMORY_MANAGER_STACK_SIZE 512

/// @def OVERLAY_ADDRESS
///
/// @brief Address in RAM where overlays are loaded.
/// Placed at end of the 24-bit address space, which is where the internal RAM
/// is mapped by the boot initialization code.
#define OVERLAY_ADDRESS 0xFFE000

/// @def OVERLAY_SIZE
///
/// @brief Bytes reserved for the overlay region.
#define OVERLAY_SIZE 8192 // 8 KB - the size of the internal RAM area

/// @def DIO_PIN_UNDEFINED
///
/// @brief Value to indicate that the value of a specific pin is undefined.
#define DIO_PIN_UNDEFINED 255

// -------------------------------------------------------------------------
// DIO pin numbering
// -------------------------------------------------------------------------
//
// A DIO number packs the eZ80F92 GPIO port and bit into one hex byte:
//   (port_nibble << 4) | bit    port_nibble = 0xB..0xD, bit = 0..7
// so 0xC2 *is* "Port C, bit 2" - the same as "GPIO_PC2" in the Agon Light 2
// documentation, the Olimex schematic net names, and the eZ80F92 datasheet.
// (Port A exists on the eZ80F92 package but is not bonded out on the Agon.)

#define PB0 0xB0
#define PB1 0xB1
#define PB2 0xB2
#define PB3 0xB3
#define PB4 0xB4
#define PB5 0xB5
#define PB6 0xB6
#define PB7 0xB7

#define PC0 0xC0
#define PC1 0xC1
#define PC2 0xC2
#define PC3 0xC3
#define PC4 0xC4
#define PC5 0xC5
#define PC6 0xC6
#define PC7 0xC7

#define PD0 0xD0
#define PD1 0xD1
#define PD2 0xD2
#define PD3 0xD3
#define PD4 0xD4
#define PD5 0xD5
#define PD6 0xD6
#define PD7 0xD7


/// @def MAX_SPI_DEVICES
///
/// @brief The maximum number of SPI devices the system can support.  All share
/// the one eZ80F92 hardware-SPI bus (PB3/PB6/PB7), each with its own chip
/// select: device 0 is the on-board microSD (CS = PB4), and up to three more
/// hang off the same bus with a CS on a free header GPIO (PC2-PC7 / PD4-PD7).
#define MAX_SPI_DEVICES 4

/// @def BASE_BAUD
///
/// @brief The baud rate that's used to compute the divisor that actually
/// initializes a UART periheral.
#define BASE_BAUD 1152000

/// @var _spiCopiDio
///
/// @brief DIO pin used for SPI COPI (eZ80F92 hardware SPI MOSI).
static uint8_t _spiCopiDio = PB7;

/// @var _spiCipoDio
///
/// @brief DIO pin used for SPI CIPO (eZ80F92 hardware SPI MISO).
static uint8_t _spiCipoDio = PB6;

/// @var _spiSckDio
///
/// @brief DIO pin used for the SPI serial clock (eZ80F92 hardware SPI SCK).
static uint8_t _spiSckDio = PB3;

/// @var _sdCardPinChipSelect
///
/// @brief Chip-select for the on-board microSD card reader.  Device 0's CS.
static uint8_t _sdCardPinChipSelect = PB4;

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

// ---------------------------------------------------------------------------
// Memory subsystem stubs
// ---------------------------------------------------------------------------

int32_t agonLight2ProcessStackSize(va_list args) {
  bool    debug       = (bool)   va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = PROCESS_STACK_SIZE;
  }
  return 0;
}

int32_t agonLight2MemoryManagerStackSize(va_list args) {
  bool    debug       = (bool)   va_arg(args, int);
  size_t *returnValue = va_arg(args, size_t*);
  if (returnValue != NULL) {
    *returnValue = debug ? MEMORY_MANAGER_DEBUG_STACK_SIZE
                         : MEMORY_MANAGER_STACK_SIZE;
  }
  return 0;
}

int32_t agonLight2BottomOfHeap(va_list args) {
  bool   debug       = (bool)  va_arg(args, int);
  void **returnValue = va_arg(args, void**);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = (void*) ((uintptr_t) HEAP_START_ADDRESS);
  }
  return 0;
}

int32_t agonLight2NumExtraSchedulerStacks(va_list args) {
  bool     debug       = (bool)    va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = 2;
  }
  return 0;
}

int32_t agonLight2NumExtraConsoleStacks(va_list args) {
  bool     debug       = (bool)    va_arg(args, int);
  uint8_t *returnValue = va_arg(args, uint8_t*);
  (void) debug;
  if (returnValue != NULL) {
    *returnValue = 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// UART subsystem stubs
// ---------------------------------------------------------------------------

int32_t agonLight2InitUart(va_list args) {
  (void) args;
  return 0;
}

extern void agonLight2ConfigureUart0Impl(uint16_t divisor);
extern void agonLight2ConfigureUart1Impl(uint16_t divisor);
void (*agonLight2ConfigureUartImpl[2])(uint16_t divisor) = {
  agonLight2ConfigureUart0Impl,
  agonLight2ConfigureUart1Impl,
};

int32_t agonLight2ConfigureUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint32_t baud = va_arg(args, uint32_t);
  int returnValue = -ERANGE;
  if (deviceId >= (sizeof(agonLight2ConfigureUartImpl)
    / sizeof(agonLight2ConfigureUartImpl[0]))
  ) {
    return returnValue; // -ERANGE
  }

  agonLight2ConfigureUartImpl[deviceId](BASE_BAUD / baud);
  returnValue = 0;

  return returnValue;
}

extern int agonLight2PollUart0Impl(void);
extern int agonLight2PollUart1Impl(void);
int (*agonLight2PollUartImpl[2])(void) = {
  agonLight2PollUart0Impl,
  agonLight2PollUart1Impl,
};

int32_t agonLight2PollUart(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  if (deviceId >= (sizeof(agonLight2ConfigureUartImpl)
    / sizeof(agonLight2ConfigureUartImpl[0]))
  ) {
    return -ERANGE;
  }

  return (int32_t) agonLight2PollUartImpl[deviceId]();
}

extern void agonLight2WriteUart0Impl(uint8_t c);
extern void agonLight2WriteUart1Impl(uint8_t c);
void (*agonLight2WriteUartImpl[2])(uint8_t c) = {
  agonLight2WriteUart0Impl,
  agonLight2WriteUart1Impl,
};

int32_t agonLight2WriteUart(va_list args) {
  int32_t  deviceId    = va_arg(args, int32_t);
  const uint8_t *data  = va_arg(args, const uint8_t*);
  ssize_t  length      = va_arg(args, ssize_t);
  ssize_t *returnValue = va_arg(args, ssize_t*);

  if (deviceId >= (sizeof(agonLight2ConfigureUartImpl)
    / sizeof(agonLight2ConfigureUartImpl[0]))
  ) {
    return -ERANGE;
  }
  ssize_t bytesWritten = 0;
  for (;bytesWritten < length; bytesWritten++) {
    agonLight2WriteUartImpl[deviceId](data[bytesWritten]);
  }
  if (returnValue != NULL) {
    *returnValue = bytesWritten;
  }

  return 0;
}

int32_t agonLight2IsUartConsole(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  bool *returnValue = va_arg(args, bool*);

  if (returnValue != NULL) {
    if (deviceId == 1) {
      *returnValue = true;
    } else {
      *returnValue = false;
    }
  }

  return 0;
}

// ---------------------------------------------------------------------------
// DIO subsystem stubs
// ---------------------------------------------------------------------------

/// @struct AgonLight2GpioPort
///
/// @brief The four eZ80F92 I/O-register addresses for one GPIO port.
///
/// @param dr   Data Register.
/// @param ddr  Data Direction Register (1 = input, 0 = output).
/// @param alt1 Alternate Function 1 Register.
/// @param alt2 Alternate Function 2 Register.
typedef struct AgonLight2GpioPort {
  uint8_t dr;
  uint8_t ddr;
  uint8_t alt1;
  uint8_t alt2;
} AgonLight2GpioPort;

/// @var _agonGpioPorts
///
/// @brief Register sets for Ports B, C and D, indexed by (port nibble - 0xB).
///
/// @note KEEP_IN_FLASH so it survives .rodata stripping on the shipped image.
static const AgonLight2GpioPort _agonGpioPorts[3] KEEP_IN_FLASH = {
  { 0x9A, 0x9B, 0x9C, 0x9D }, // Port B: DR, DDR, ALT1, ALT2
  { 0x9E, 0x9F, 0xA0, 0xA1 }, // Port C
  { 0xA2, 0xA3, 0xA4, 0xA5 }, // Port D
};

/// @fn const AgonLight2GpioPort* agonGpioDecode(int32_t dio, uint8_t *bit)
///
/// @brief Split a DIO number (0xB0..0xD7) into its port register set and bit.
///
/// @param dio The DIO number.
/// @param bit If non-NULL, receives the 0..7 bit number on success.
///
/// @return The port's register set, or NULL if dio is not a valid Agon
/// Port B/C/D pin.
static const AgonLight2GpioPort* agonGpioDecode(int32_t dio, uint8_t *bit) {
  uint8_t port = (uint8_t) (((uint32_t) dio >> 4) & 0x0F);
  uint8_t b    = (uint8_t) (((uint32_t) dio) & 0x0F);

  if ((port < 0x0B) || (port > 0x0D) || (b > 7)) {
    return NULL;
  }
  if (bit != NULL) {
    *bit = b;
  }
  return &_agonGpioPorts[port - 0x0B];
}

int32_t agonLight2InitDio(va_list args) {
  // This function is a no-op on this platform.
  (void) args;
  return 0;
}

int32_t agonLight2ConfigureDioImpl(int32_t dio, bool output) {
  uint8_t bit;
  const AgonLight2GpioPort *port = agonGpioDecode(dio, &bit);
  uint8_t c = 0x00;

  if (port == NULL) {
    return -ENODEV;
  }

  c = (uint8_t) agonLight2ReadPort(port->alt1);
  c &= (uint8_t) ~(1 << bit);            // plain GPIO: clear ALT1
  agonLight2WritePort(port->alt1, c);

  c = (uint8_t) agonLight2ReadPort(port->alt2);
  c &= (uint8_t) ~(1 << bit);            // plain GPIO: clear ALT2
  agonLight2WritePort(port->alt2, c);

  c = (uint8_t) agonLight2ReadPort(port->ddr);
  if (output == true) {
    c &= (uint8_t) ~(1 << bit);          // eZ80F92 DDR: 0 = output
  } else {
    c |= (uint8_t) (1 << bit);           //              1 = input
  }
  agonLight2WritePort(port->ddr, c);

  return 0;
}

int32_t agonLight2ConfigureDio(va_list args) {
  int32_t dio = va_arg(args, int32_t);
  bool output = (bool) va_arg(args, int);
  return agonLight2ConfigureDioImpl(dio, output);
}

int32_t agonLight2WriteDioImpl(int32_t dio, bool high) {
  uint8_t bit;
  const AgonLight2GpioPort *port = agonGpioDecode(dio, &bit);
  uint8_t c = 0x00;

  if (port == NULL) {
    return -ENODEV;
  }

  c = (uint8_t) agonLight2ReadPort(port->dr);
  if (high == true) {
    c |= (uint8_t) (1 << bit);
  } else {
    c &= (uint8_t) ~(1 << bit);
  }
  agonLight2WritePort(port->dr, c);

  return 0;
}

int32_t agonLight2WriteDio(va_list args) {
  int32_t dio = va_arg(args, int32_t);
  bool high = (bool) va_arg(args, int);
  return agonLight2WriteDioImpl(dio, high);
}

// ---------------------------------------------------------------------------
// SPI subsystem stubs (no SPI bus on the eZ80 side of the Agon)
// ---------------------------------------------------------------------------

/// @var globalSpiConfigured
///
/// @brief Whether or not the board's SPI interface has already been configured.
static bool globalSpiConfigured = false;

/// @var globalSpiInUse
///
/// @brief Set while any device holds a transfer.  The bus is shared, so a
/// second device cannot be selected until the first ends its transfer.
static bool globalSpiInUse = false;

/// @var spiDevices
///
/// @brief Array of HalSpiDevices that will hold the information about SPI
/// connections.
static HalSpiDevice spiDevices[MAX_SPI_DEVICES];

/// @def numSpis
///
/// @brief The number of devices we support in the spiDevices array.
///
/// @note This is a #define rather than a const int so that it doesn't need
/// its own KEEP_IN_FLASH treatment - it's folded into an immediate value at
/// each use site instead of occupying storage that could land in .rodata.
#define numSpis ((int) (sizeof(spiDevices) / sizeof(spiDevices[0])))

/// @def SPI_POWER_UP_CLOCK_BYTES
///
/// @brief Number of 0xFF bytes clocked out with every chip select deasserted
/// right after a device is configured.  The SD physical spec requires at least
/// 74 clock cycles (>= 10 bytes) with CS and DI high before the first command;
/// harmless for any other SPI peripheral.
#define SPI_POWER_UP_CLOCK_BYTES 10

/// @fn uint16_t agonLight2SpiDivisor(uint32_t baud)
///
/// @brief eZ80F92 SPI baud-rate divisor for a requested bit clock:
/// divisor = system clock / (2 * baud), clamped to the 16-bit register maximum
/// and to the master-mode minimum of 3.  Per the eZ80F92 Product Specification
/// (PS015317), "When the SPI is operating as a Master, the BRG divisor value
/// must be set to a value of 0003h or greater" - values 1 and 2 are only legal
/// for the RESET default / slave mode and produce an unreliable master clock.
/// A request at or above SYSTEM_CLOCK_HZ / 6 therefore saturates at the fastest
/// the master bus can run, ~3.07 MHz.
static uint16_t agonLight2SpiDivisor(uint32_t baud) {
  uint32_t divisor = (baud != 0)
    ? (SYSTEM_CLOCK_HZ / (baud << 1))
    : 0xFFFF;
  if (divisor < 3) {
    divisor = 3;
  } else if (divisor > 0xFFFF) {
    divisor = 0xFFFF;
  }
  return (uint16_t) divisor;
}

int32_t agonLight2InitSpi(va_list args) {
  (void) args;

  memset(&spiDevices, 0, numSpis * sizeof(HalSpiDevice));
  globalSpiConfigured = true;
  globalSpiInUse = false;

  return 0;
}

int32_t agonLight2ConfigureSpi(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t cs   = (uint8_t) va_arg(args, int);
  uint8_t sck  = (uint8_t) va_arg(args, int);
  uint8_t copi = (uint8_t) va_arg(args, int);
  uint8_t cipo = (uint8_t) va_arg(args, int);
  uint32_t baud = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numSpis)) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  }

  // The bus signals are fixed to the eZ80F92 hardware-SPI pins and every device
  // shares them.  The chip select must be either PB4 (the on-board microSD,
  // device 0) or one of the free header GPIOs the SPI bus is broken out
  // alongside: PD4..PD7 or PC2..PC7.  Nothing else is accepted.
  bool csValid = (cs == _sdCardPinChipSelect)
    || ((cs >= PD4) && (cs <= PD7))
    || ((cs >= PC2) && (cs <= PC7));
  if ((sck  != _spiSckDio)
    || (copi != _spiCopiDio)
    || (cipo != _spiCipoDio)
    || (csValid == false)
  ) {
    return -EINVAL;
  }

  if (spiDevices[deviceId].configured == true) {
    // Already set up; the caller must not reconfigure a slot in place.
    return -EBUSY;
  }

  // A chip-select line can only belong to one device.
  for (int ii = 0; ii < numSpis; ii++) {
    if ((spiDevices[ii].configured == true)
      && (spiDevices[ii].chipSelect == cs)
    ) {
      return -EBUSY;
    }
  }

  agonLight2ConfigureSpiImpl(agonLight2SpiDivisor(baud));

  agonLight2ConfigureDioImpl(cs, true); // Configure chip-select for output.
  agonLight2WriteDioImpl(cs, true); // Drive chip-select high (deselected).

  // SD physical-spec power-up sequence: >= 74 clock cycles with this device's
  // chip select (and DI) held high, before it is ever selected for a command.
  for (int ii = 0; ii < SPI_POWER_UP_CLOCK_BYTES; ii++) {
    agonLight2SpiTransfer8Impl(0xFF);
  }

  spiDevices[deviceId].chipSelect = cs;
  spiDevices[deviceId].baud = baud;
  spiDevices[deviceId].configured = true;
  // spiDevices[deviceId].transferInProgress is already false from the memset.

  return 0;
}

int32_t agonLight2SetSpiSpeed(va_list args) {
  int32_t  deviceId = va_arg(args, int32_t);
  uint32_t baud     = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numSpis)
    || (spiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  }
  if (baud == 0) {
    return -EINVAL;
  }

  spiDevices[deviceId].baud = baud;

  // If this device currently holds the bus, retune the BRG now; otherwise
  // agonLight2StartSpiTransferImpl re-applies it on the next transfer.  Only
  // the baud-rate generator is touched - the pin mux and SPI_CTL are already
  // live and rewriting them mid-session glitches the clock line.
  if (spiDevices[deviceId].transferInProgress == true) {
    agonLight2SetSpiBrgImpl(agonLight2SpiDivisor(baud));
  }

  return 0;
}

int32_t agonLight2StartSpiTransferImpl(int32_t deviceId) {
  if ((deviceId < 0) || (deviceId >= numSpis)
    || (spiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if ((spiDevices[deviceId].transferInProgress == true)
    || (globalSpiInUse == true)
  ) {
    // This device, or another one on the shared bus, already holds a transfer.
    return -EBUSY;
  }

  globalSpiInUse = true;

  // Re-apply this device's clock divider - devices on the shared bus may run at
  // different speeds, so whichever configure() / setSpeed() ran last does not
  // get to win.  This is a BRG-only reload: the one-time agonLight2ConfigureSpi
  // has already muxed the pins and programmed SPI_CTL, and re-running the full
  // configure here (pin re-mux + SPI_CTL rewrite) on the idle bus glitched the
  // clock and corrupted the first byte of the transfer.
  agonLight2SetSpiBrgImpl(agonLight2SpiDivisor(spiDevices[deviceId].baud));

  // Drive chip-select low.
  agonLight2WriteDioImpl(spiDevices[deviceId].chipSelect, false);
  spiDevices[deviceId].transferInProgress = true;

  return 0;
}

int32_t agonLight2StartSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  return agonLight2StartSpiTransferImpl(deviceId);
}

int32_t agonLight2EndSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  if ((deviceId < 0) || (deviceId >= numSpis)
    || (spiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  }

  // Drive chip-select high.
  agonLight2WriteDioImpl(spiDevices[deviceId].chipSelect, true);
  for (int ii = 0; ii < 8; ii++) {
    agonLight2SpiTransfer8Impl(0xFF); // 8 clock pulses
  }
  spiDevices[deviceId].transferInProgress = false;
  globalSpiInUse = false; // release the shared bus

  return 0;
}

int32_t agonLight2SpiTransfer8(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t data = (uint8_t) va_arg(args, int);

  if ((deviceId < 0) || (deviceId >= numSpis)
    || (spiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!spiDevices[deviceId].transferInProgress) {
    agonLight2StartSpiTransferImpl(deviceId);
  }

  return (int32_t) agonLight2SpiTransfer8Impl(data);
}

int32_t agonLight2SpiTransferBytes(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t *data = va_arg(args, uint8_t*);
  uint32_t length = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numSpis)
    || (spiDevices[deviceId].configured == false)
  ) {
    // Outside the limit of the devices we support.
    return -ENODEV;
  } else if (!spiDevices[deviceId].transferInProgress) {
    agonLight2StartSpiTransferImpl(deviceId);
  }

  for (uint32_t ii = 0; ii < length; ii++) {
    data[ii] = (uint8_t) agonLight2SpiTransfer8Impl(data[ii]);
  }

  return 0;
}

// ---------------------------------------------------------------------------
// Clock subsystem stubs
// ---------------------------------------------------------------------------

/// @var _baseSystemTimeNs
///
/// @brief Epoch offset set via setSystemTime(), in nanoseconds.
static int64_t _baseSystemTimeNs = 0;

// Assembly function prototypes.
extern void initClockTimer(void);
extern void readClock(int64_t *returnValue);

int32_t agonLight2InitClock(va_list args) {
  (void) args;
  initClockTimer();
  return 0;
}

int32_t agonLight2SetSystemTime(va_list args) {
  struct timespec *ts = va_arg(args, struct timespec*);
  if (ts == NULL) {
    return -EINVAL;
  }
  _baseSystemTimeNs
    = (((int64_t) ts->tv_sec) * ((int64_t) 1000000000))
    + ((int64_t) ts->tv_nsec);
  return 0;
}

int32_t agonLight2GetElapsedMilliseconds(va_list args) {
  int64_t  startTime   = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t  nowMs = 0;
  readClock(&nowMs);
  nowMs += _baseSystemTimeNs;
  nowMs /= (int64_t) 1000000;
  if (nowMs < startTime) {
    // Clock skew or a bogus start time: never report a negative interval.
    if (returnValue != NULL) {
      *returnValue = -1;
    }
    return -EIO;
  }
  if (returnValue != NULL) {
    *returnValue = nowMs - startTime;
  }
  return 0;
}

int32_t agonLight2GetElapsedMicroseconds(va_list args) {
  int64_t  startTime   = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t  nowUs = 0;
  readClock(&nowUs);
  nowUs += _baseSystemTimeNs;
  nowUs /= (int64_t) 1000;
  if (nowUs < startTime) {
    // Clock skew or a bogus start time: never report a negative interval.
    if (returnValue != NULL) {
      *returnValue = -1;
    }
    return -EIO;
  }
  if (returnValue != NULL) {
    *returnValue = nowUs - startTime;
  }
  return 0;
}

int32_t agonLight2GetElapsedNanoseconds(va_list args) {
  int64_t  startTime   = va_arg(args, int64_t);
  int64_t *returnValue = va_arg(args, int64_t*);
  int64_t  nowNs = 0;
  readClock(&nowNs);
  nowNs += _baseSystemTimeNs;
  if (nowNs < startTime) {
    // Clock skew or a bogus start time: never report a negative interval.
    if (returnValue != NULL) {
      *returnValue = -1;
    }
    return -EIO;
  }
  if (returnValue != NULL) {
    *returnValue = nowNs - startTime;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Power subsystem stub
// ---------------------------------------------------------------------------

int32_t agonLight2EnterMode(va_list args) {
  HalPowerMode powerMode = (HalPowerMode) va_arg(args, int);

  if (powerMode == HAL_POWER_MODE_RESET) {
    // agonLight2SystemReset (Boot.asm) arms the eZ80F92 watchdog for a real
    // full-chip reset on hardware, then falls through to re-entering the boot
    // vector - realStart re-runs the CS0 / flash / RAM bring-up, the .data
    // copy, the .bss clear, and main().  On the emulator (no WDT model) the
    // fall-through is all that happens.  Does not return.
    agonLight2SystemReset();
  }

  // HAL_POWER_MODE_OFF / HAL_POWER_MODE_SUSPEND, and the kernel's panic path
  // (MemoryManager.c calls enterMode(OFF) on unrecoverable errors): the Agon
  // Light 2 has no software power control - power is a physical switch - so the
  // best we can do is stop the CPU.  On fab-agon-emulator, writing I/O port
  // 0x00 ends the emulator with the low byte as its exit status (mirrors the
  // POSIX HAL's exit(0) for OFF); 0x00 is an unused I/O address on real
  // eZ80F92 silicon, so that write is a harmless no-op there and
  // agonLight2Halt (di; halt loop) is the actual behaviour.
  agonLight2WritePort(0x0000, 0x00);
  agonLight2Halt();

  return 0;   // not reached
}

// ---------------------------------------------------------------------------
// Timer subsystem
// ---------------------------------------------------------------------------
//
// HAL timer devices 0..4 map to eZ80F92 PRT1..PRT5 (PRT0 is the nanosecond
// system clock - see Clock.asm).  The IM2 vector for each PRT is a 2-byte
// slot that can only reach the first 64 KB, and the callback these devices
// carry is the scheduler's preemption callback, which context-switches.  So
// each vector runs through a trampoline in src/hal/AgonLight2/Interrupts.asm
// (prtNTramp): it clears that timer's PRT_IRQ, re-enables interrupts, and calls
// agonLight2TimerInterruptHandlerN() below.  See Interrupts.asm for the full
// rationale and its HalArduinoSamD21x18A.cpp analogue.
//
// Fully wired: the interrupt path (trampoline -> handler -> callback) plus
// initDevice / configOneShot / cancel / cancelAndGet / the configured &
// remaining queries against the PRT registers.  timer.numSupported = 5 and all
// five are online, so the scheduler arms PRT1 as its preemption timer and user
// tasks run preemptively.

/// @struct AgonLight2Timer
///
/// @brief Per-device bookkeeping for one PRT-backed HAL timer.  Mirrors the
/// non-SAMD-specific fields of HardwareTimer in HalArduinoSamD21x18A.cpp.
typedef struct AgonLight2Timer {
  bool    initialized;       ///< initDevice() has run for this timer
  void  (*callback)(void);   ///< fired once, when the timer expires
  bool    active;            ///< armed and counting
  int64_t startTimeNs;       ///< readClock() ns when configOneShot armed it
  int64_t deadlineNs;        ///< startTimeNs + the configured delay
} AgonLight2Timer;

/// @var _prtTimers
///
/// @brief State for HAL timer devices 0..4 (eZ80F92 PRT1..PRT5).
static AgonLight2Timer _prtTimers[5];

/// @def _numPrtTimers
///
/// @brief Number of PRT-backed HAL timers.  A #define, not a const, so it does
/// not need KEEP_IN_FLASH - it folds to an immediate at each use site.
#define _numPrtTimers ((int32_t) (sizeof(_prtTimers) / sizeof(_prtTimers[0])))

/// @var _prtCtlPort
///
/// @brief TMRn_CTL I/O address for HAL timer device 0..4 (eZ80F92 PRT1..PRT5).
/// Each timer's reload registers follow it contiguously: TMRn_RR_L = port + 1,
/// TMRn_RR_H = port + 2.  Written a byte at a time through agonLight2WritePort,
/// the same table-of-ports pattern agonLight2ConfigureDioImpl uses.
///
/// @note KEEP_IN_FLASH so it survives .rodata stripping on the shipped image.
static const uint8_t _prtCtlPort[5] KEEP_IN_FLASH = {
  0x83, 0x86, 0x89, 0x8C, 0x8F,
};

// Called from the PRT trampolines in src/hal/AgonLight2/Interrupts.asm.
void agonLight2TimerInterruptHandler(int32_t deviceId);
void agonLight2TimerInterruptHandler1(void);
void agonLight2TimerInterruptHandler2(void);
void agonLight2TimerInterruptHandler3(void);
void agonLight2TimerInterruptHandler4(void);
void agonLight2TimerInterruptHandler5(void);

/// @fn int32_t agonLight2CheckTimerCancelCapability(int32_t deviceId)
///
/// @brief Enforce the per-device HAL capability for tearing down a timer.
///
/// @details callHal() has already checked that the running process may invoke
/// this subsystem/function pair at all; this adds the device-bitmask
/// granularity that the POSIX and SAMD21 HALs apply on their cancel paths.  A
/// re-arm through configOneShot implicitly cancels whatever reload was already
/// loaded, so it is gated the same way.  Kernel-privileged callers - the
/// scheduler arming its own preemption timer, most notably - are always
/// allowed, as is any call made before the scheduler is up
/// (getRunningProcess() == NULL).
///
/// @param deviceId The zero-based ID of the timer being cancelled or re-armed.
///   Must already have been range-checked by the caller.
///
/// @return Returns 0 if the operation is permitted, -EACCES if not.
static int32_t agonLight2CheckTimerCancelCapability(int32_t deviceId) {
  ProcessDescriptor *processDescriptor = getRunningProcess();
  if ((processDescriptor != NULL)
    && (processDescriptor->privilegeLevel != PRIVILEGE_LEVEL_KERNEL)
    && (findHalCapabilityWithDevice(processDescriptor->halCapabilities,
      processDescriptor->numHalCapabilities, HAL_TIMER, HAL_TIMER_CANCEL,
      deviceId) == NULL)
  ) {
    return -EACCES;
  }
  return 0;
}

int32_t agonLight2InitTimer(va_list args) {
  // Nothing subsystem-wide to do: each eZ80F92 PRT (TMR0..TMR5) is fully
  // independent and clocked straight from the system clock - there is no
  // shared enable, clock gate, or reset the way the SAMD21's GCLK needs.  All
  // per-device work lives in agonLight2InitTimerDevice.
  (void) args;
  return 0;
}

int32_t agonLight2InitTimerDevice(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);

  if ((deviceId < 0) || (deviceId >= _numPrtTimers)) {
    return -ERANGE;
  }

  AgonLight2Timer *timer = &_prtTimers[deviceId];
  if (timer->initialized) {
    return 0;   // idempotent, like arduinoSamD21x18AInitTimerDevice
  }

  // Bring the PRT to a known, stopped state.  Writing TMRn_CTL = 0 drops EN and
  // IRQ_EN; the follow-up read clears any latched PRT_IRQ.  There is no reset
  // or mode-select step beyond this on the eZ80F92 - one-shot mode, the clock
  // divider, and the enable are all set by configOneShot's CTL write.  (The
  // PRTn vector slot is bound to prtNTramp at build time in Interrupts.asm, and
  // interrupts come online once, in enableInterrupts().)
  uint8_t ctlPort = _prtCtlPort[deviceId];
  agonLight2WritePort(ctlPort, 0x00);
  (void) agonLight2ReadPort(ctlPort);

  timer->callback    = NULL;
  timer->active      = false;
  timer->startTimeNs = 0;
  timer->deadlineNs  = 0;
  timer->initialized = true;

  return 0;
}

int32_t agonLight2ConfigOneShotTimer(va_list args) {
  int32_t   deviceId    = va_arg(args, int32_t);
  uint64_t  nanoseconds = va_arg(args, uint64_t);
  void    (*callback)(void) = va_arg(args, void (*)(void));

  if ((deviceId < 0) || (deviceId >= _numPrtTimers)) {
    return -ERANGE;
  }
  if (_prtTimers[deviceId].initialized == false) {
    return -EINVAL;
  }

  // Arming a timer overwrites whatever reload was already loaded, i.e. it
  // implicitly cancels any pending fire.  Require the same per-device cancel
  // capability an explicit cancel would need, matching the SAMD21 HAL (which
  // runs its cancel path from configOneShot for exactly this reason).
  int32_t capabilityStatus = agonLight2CheckTimerCancelCapability(deviceId);
  if (capabilityStatus != 0) {
    return capabilityStatus;
  }

  // Pick the smallest clock divider (4, 16, 64, 256) whose 16-bit reload
  // covers the requested delay.  reload = ns * SYSTEM_CLOCK_HZ / (clkDiv * 1e9)
  // - the same prescaler search arduinoSamD21x18AConfigOneShotTimer does.
  uint32_t clkDivSel = 0;
  uint64_t reload    = 0;
  for (clkDivSel = 0; clkDivSel < 4; clkDivSel++) {
    uint64_t clkDiv = ((uint64_t) 4) << (2 * clkDivSel);   // 4, 16, 64, 256
    reload = (nanoseconds * (uint64_t) SYSTEM_CLOCK_HZ)
      / (clkDiv * ((uint64_t) 1000000000));
    if (reload <= 0xFFFF) {
      break;
    }
  }
  if (clkDivSel >= 4) {
    // Longer than the PRT can express (~910 ms at /256); clamp to its maximum.
    clkDivSel = 3;
    reload    = 0xFFFF;
  }
  if (reload == 0) {
    reload = 1;   // never arm with a zero reload
  }

  // TMRn_CTL to arm: EN | RST_EN | IRQ_EN, single-pass (PRT_MODE = 0), with the
  // chosen divider in bits [3:2].  This is _initClockTimer's 0x57 without the
  // continuous-mode bit.
  uint8_t ctl     = (uint8_t) (0x43u | (clkDivSel << 2));
  uint8_t ctlPort = _prtCtlPort[deviceId];

  agonLight2WritePort(ctlPort, 0x00);                            // stop
  agonLight2WritePort(ctlPort + 1, (uint8_t) (reload & 0xFF));   // TMRn_RR_L
  agonLight2WritePort(ctlPort + 2, (uint8_t) (reload >> 8));     // TMRn_RR_H
  agonLight2WritePort(ctlPort, ctl);                             // arm

  int64_t nowNs = 0;
  readClock(&nowNs);

  _prtTimers[deviceId].callback    = callback;
  _prtTimers[deviceId].active      = true;
  _prtTimers[deviceId].startTimeNs = nowNs;
  _prtTimers[deviceId].deadlineNs  = nowNs + (int64_t) nanoseconds;

  return 0;
}

int32_t agonLight2ConfiguredTimerNanoseconds(va_list args) {
  int32_t   deviceId    = va_arg(args, int32_t);
  uint64_t *returnValue = va_arg(args, uint64_t*);

  if (returnValue != NULL) {
    *returnValue = 0;
  }
  if ((deviceId < 0) || (deviceId >= _numPrtTimers)) {
    return -ERANGE;
  }

  AgonLight2Timer *timer = &_prtTimers[deviceId];
  if ((timer->initialized == false) || (timer->active == false)) {
    return -EINVAL;
  }

  // The delay configOneShot was asked for, recovered from the recorded window.
  if (returnValue != NULL) {
    *returnValue = (uint64_t) (timer->deadlineNs - timer->startTimeNs);
  }
  return 0;
}

int32_t agonLight2RemainingTimerNanoseconds(va_list args) {
  int32_t   deviceId    = va_arg(args, int32_t);
  uint64_t *returnValue = va_arg(args, uint64_t*);

  if (returnValue != NULL) {
    *returnValue = 0;
  }
  if ((deviceId < 0) || (deviceId >= _numPrtTimers)) {
    return -ERANGE;
  }

  AgonLight2Timer *timer = &_prtTimers[deviceId];
  if ((timer->initialized == false) || (timer->active == false)) {
    return -EINVAL;
  }

  // Wall-clock estimate against the recorded deadline, as
  // arduinoSamD21x18ARemainingTimerNanoseconds does - no need to latch and
  // scale the live PRT counter.  Resolution is the system clock tick.
  int64_t nowNs = 0;
  readClock(&nowNs);
  if ((returnValue != NULL) && (nowNs < timer->deadlineNs)) {
    *returnValue = (uint64_t) (timer->deadlineNs - nowNs);
  }
  return 0;
}

int32_t agonLight2CancelTimer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);

  if ((deviceId < 0) || (deviceId >= _numPrtTimers)) {
    return -ERANGE;
  }

  int32_t capabilityStatus = agonLight2CheckTimerCancelCapability(deviceId);
  if (capabilityStatus != 0) {
    return capabilityStatus;
  }

  // Clearing TMRn_CTL drops EN and IRQ_EN, so the PRT stops and cannot raise
  // its interrupt.
  agonLight2WritePort(_prtCtlPort[deviceId], 0x00);

  _prtTimers[deviceId].callback    = NULL;
  _prtTimers[deviceId].active      = false;
  _prtTimers[deviceId].startTimeNs = 0;
  _prtTimers[deviceId].deadlineNs  = 0;

  return 0;
}

int32_t agonLight2CancelAndGetTimer(va_list args) {
  // Snapshot 'now' first: this is critical-path (stdatomic.c wraps every atomic
  // op in cancelAndGet + re-arm) and the remaining-time figure feeds straight
  // back into configOneShot.
  int64_t nowNs = 0;
  readClock(&nowNs);

  int32_t   deviceId              = va_arg(args, int32_t);
  uint64_t *configuredNanoseconds = va_arg(args, uint64_t*);
  uint64_t *remainingNanoseconds  = va_arg(args, uint64_t*);
  void   (**callback)(void)       = va_arg(args, void (**)(void));

  if ((deviceId < 0) || (deviceId >= _numPrtTimers)) {
    return -ERANGE;
  }

  AgonLight2Timer *timer = &_prtTimers[deviceId];
  if ((timer->initialized == false) || (timer->active == false)) {
    // Nothing to capture; the -EINVAL also tells the caller not to re-arm.
    return -EINVAL;
  }

  // Inline the stop - do NOT call agonLight2CancelTimer; this is the critical
  // path.  TMRn_CTL = 0 drops EN / IRQ_EN, and the follow-up read clears a
  // latched PRT_IRQ so a pending preemption cannot fire the instant we return.
  uint8_t ctlPort = _prtCtlPort[deviceId];
  agonLight2WritePort(ctlPort, 0x00);
  (void) agonLight2ReadPort(ctlPort);

  if (configuredNanoseconds != NULL) {
    *configuredNanoseconds = (timer->deadlineNs > timer->startTimeNs)
      ? (uint64_t) (timer->deadlineNs - timer->startTimeNs)
      : 0;
  }
  if (remainingNanoseconds != NULL) {
    *remainingNanoseconds = (nowNs < timer->deadlineNs)
      ? (uint64_t) (timer->deadlineNs - nowNs)
      : 0;
  }
  if (callback != NULL) {
    *callback = timer->callback;
  }

  timer->active      = false;
  timer->startTimeNs = 0;
  timer->deadlineNs  = 0;
  timer->callback    = NULL;

  return 0;
}

/// @fn void agonLight2TimerInterruptHandler(int32_t deviceId)
///
/// @brief Shared body for the PRT1..PRT5 one-shot timer interrupts.  Reached
/// via agonLight2TimerInterruptHandlerN() and the prtNTramp trampoline, so on
/// entry interrupts are already re-enabled and the timer's PRT_IRQ is already
/// cleared.  The PRT runs in single-pass mode and has disarmed itself in
/// hardware; this just clears the software bookkeeping and runs the callback.
/// Mirrors arduinoSamD21x18ATimerInterruptHandler().
///
/// @param deviceId 0..4, pre-selected by the calling wrapper (never out of
///   range).
///
/// @return This function returns no value.
void agonLight2TimerInterruptHandler(int32_t deviceId) {
  AgonLight2Timer *timer = &_prtTimers[deviceId];

  timer->active      = false;
  timer->startTimeNs = 0;
  timer->deadlineNs  = 0;

  if (timer->callback != NULL) {
    timer->callback();
  }
}

/// @fn void agonLight2TimerInterruptHandlerN(void)
///
/// @brief PRT<N> vector entry points.  Split per device, as
/// HalArduinoSamD21x18A.cpp splits ...Handler0 / ...Handler1, so the deviceId
/// is a compile-time constant and nothing has to be marshalled across the
/// eZ80 C ABI from the trampoline.
///
/// @return These functions return no value.
void agonLight2TimerInterruptHandler1(void) { agonLight2TimerInterruptHandler(0); }
void agonLight2TimerInterruptHandler2(void) { agonLight2TimerInterruptHandler(1); }
void agonLight2TimerInterruptHandler3(void) { agonLight2TimerInterruptHandler(2); }
void agonLight2TimerInterruptHandler4(void) { agonLight2TimerInterruptHandler(3); }
void agonLight2TimerInterruptHandler5(void) { agonLight2TimerInterruptHandler(4); }

// ---------------------------------------------------------------------------
// Block device subsystem stubs (no SD card driver for eZ80 side yet)
// ---------------------------------------------------------------------------

int32_t agonLight2InitBlockDevice(va_list args) {
  (void) args;
  if (SCHEDULER_STATE == NULL) {
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

int32_t agonLight2GetBlockDevice(va_list args) {
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

int32_t agonLight2RestartBlockDevice(va_list args) {
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

// ---------------------------------------------------------------------------
// Per-subsystem function tables
// ---------------------------------------------------------------------------

static HalFunction agonLight2MemoryFunctions[HAL_MEMORY_NUM_FNS] = {
  [HAL_MEMORY_PROCESS_STACK_SIZE]         = agonLight2ProcessStackSize,
  [HAL_MEMORY_MEMORY_MANAGER_STACK_SIZE]  = agonLight2MemoryManagerStackSize,
  [HAL_MEMORY_BOTTOM_OF_HEAP]             = agonLight2BottomOfHeap,
  [HAL_MEMORY_NUM_EXTRA_SCHEDULER_STACKS] = agonLight2NumExtraSchedulerStacks,
  [HAL_MEMORY_NUM_EXTRA_CONSOLE_STACKS]   = agonLight2NumExtraConsoleStacks,
};

static HalFunction agonLight2UartFunctions[HAL_UART_NUM_FNS] = {
  [HAL_UART_INIT]       = agonLight2InitUart,
  [HAL_UART_CONFIGURE]  = agonLight2ConfigureUart,
  [HAL_UART_POLL]       = agonLight2PollUart,
  [HAL_UART_WRITE]      = agonLight2WriteUart,
  [HAL_UART_IS_CONSOLE] = agonLight2IsUartConsole,
};

static HalFunction agonLight2DioFunctions[HAL_DIO_NUM_FNS] = {
  [HAL_DIO_INIT]      = agonLight2InitDio,
  [HAL_DIO_CONFIGURE] = agonLight2ConfigureDio,
  [HAL_DIO_WRITE]     = agonLight2WriteDio,
};

static HalFunction agonLight2SpiFunctions[HAL_SPI_NUM_FNS] = {
  [HAL_SPI_INIT]           = agonLight2InitSpi,
  [HAL_SPI_CONFIGURE]      = agonLight2ConfigureSpi,
  [HAL_SPI_START_TRANSFER] = agonLight2StartSpiTransfer,
  [HAL_SPI_END_TRANSFER]   = agonLight2EndSpiTransfer,
  [HAL_SPI_TRANSFER8]      = agonLight2SpiTransfer8,
  [HAL_SPI_TRANSFER_BYTES] = agonLight2SpiTransferBytes,
  [HAL_SPI_SET_SPEED]      = agonLight2SetSpiSpeed,
};

static HalFunction agonLight2ClockFunctions[HAL_CLOCK_NUM_FNS] = {
  [HAL_CLOCK_INIT]                     = agonLight2InitClock,
  [HAL_CLOCK_SET_SYSTEM_TIME]          = agonLight2SetSystemTime,
  [HAL_CLOCK_GET_ELAPSED_MILLISECONDS] = agonLight2GetElapsedMilliseconds,
  [HAL_CLOCK_GET_ELAPSED_MICROSECONDS] = agonLight2GetElapsedMicroseconds,
  [HAL_CLOCK_GET_ELAPSED_NANOSECONDS]  = agonLight2GetElapsedNanoseconds,
};

static HalFunction agonLight2PowerFunctions[HAL_POWER_NUM_FNS] = {
  [HAL_POWER_ENTER_MODE] = agonLight2EnterMode,
};

static HalFunction agonLight2TimerFunctions[HAL_TIMER_NUM_FNS] = {
  [HAL_TIMER_INIT]                   = agonLight2InitTimer,
  [HAL_TIMER_INIT_DEVICE]            = agonLight2InitTimerDevice,
  [HAL_TIMER_CONFIG_ONE_SHOT]        = agonLight2ConfigOneShotTimer,
  [HAL_TIMER_CONFIGURED_NANOSECONDS] = agonLight2ConfiguredTimerNanoseconds,
  [HAL_TIMER_REMAINING_NANOSECONDS]  = agonLight2RemainingTimerNanoseconds,
  [HAL_TIMER_CANCEL]                 = agonLight2CancelTimer,
  [HAL_TIMER_CANCEL_AND_GET]         = agonLight2CancelAndGetTimer,
};

static HalFunction agonLight2BlockDeviceFunctions[HAL_BLOCK_DEVICE_NUM_FNS] = {
  [HAL_BLOCK_DEVICE_INIT]    = agonLight2InitBlockDevice,
  [HAL_BLOCK_DEVICE_GET]     = agonLight2GetBlockDevice,
  [HAL_BLOCK_DEVICE_RESTART] = agonLight2RestartBlockDevice,
};

// ---------------------------------------------------------------------------
// Online-device bitmask arrays (all offline until hardware is brought up)
// ---------------------------------------------------------------------------

static uint32_t agonLight2UartsOnline[]        = { 0x00000003 };

/// @var agonLight2DiosOnline
///
/// @brief Online-DIO bitmask, indexed by the packed DIO id ((port << 4) | bit),
/// so it spans ids 0xB0..0xD7 and needs seven 32-bit words.  Only the pins the
/// HAL will actually accept as SPI chip selects are advertised: PB4 (the
/// on-board microSD CS) plus the free header GPIOs PC2..PC7 and PD4..PD7.  The
/// authoritative validity check for any DIO id is agonGpioDecode(), not this
/// mask.
static uint32_t agonLight2DiosOnline[] = {
  0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
  0x00100000, // word 5 (ids 0xA0..0xBF): bit 20 -> PB4 (0xB4)
  0x00F000FC, // word 6 (ids 0xC0..0xDF): PC2..PC7 (0xC2..0xC7), PD4..PD7 (0xD4..0xD7)
};
static uint32_t agonLight2SpisOnline[]         = { 0x0000000F }; // 4 SPI devices
static uint32_t agonLight2TimersOnline[]       = { 0x0000001F }; // PRT1..PRT5
static uint32_t agonLight2BlockDevicesOnline[] = { 0x00000000 };

// ---------------------------------------------------------------------------
// Platform init
// ---------------------------------------------------------------------------

/// @var _logBuffer
///
/// @brief Statically allocated buffer for formatting log messages.
static char _logBuffer[128];

/// @var _dataBssCanaryError
///
/// @brief Message printed when the external-RAM integrity canary that Boot.asm
/// stamps at DATA_BSS_CANARY_ADDRESS no longer reads back at the end of HAL
/// init -- meaning .bss/.data overran their reservation and trashed memory.
///
/// @note KEEP_IN_FLASH so it survives .rodata stripping in the shipped image.
static const char _dataBssCanaryError[] KEEP_IN_FLASH =
  "FATAL: NanoOs .bss/.data overran their reservation; "
  "external RAM past the canary is corrupted.\n";

extern void enableInterrupts(void);

int32_t halAgonLight2Init(void) {
  halFunctions[HAL_MEMORY]       = agonLight2MemoryFunctions;
  halFunctions[HAL_UART]         = agonLight2UartFunctions;
  halFunctions[HAL_DIO]          = agonLight2DioFunctions;
  halFunctions[HAL_SPI]          = agonLight2SpiFunctions;
  halFunctions[HAL_CLOCK]        = agonLight2ClockFunctions;
  halFunctions[HAL_POWER]        = agonLight2PowerFunctions;
  halFunctions[HAL_TIMER]        = agonLight2TimerFunctions;
  halFunctions[HAL_BLOCK_DEVICE] = agonLight2BlockDeviceFunctions;

  halImpl.platform.callFileOverlay = callOverlayFunctionFromFile;
  halImpl.platform.execCommand = execOverlayCommand;
  halImpl.platform.restartRootFilesystem = restartContiguousFilesystem;
  halImpl.platform.initRootStorage = halCommonInitRootFilesystem;
  halImpl.platform.restartShell = restartOverlayShell;

  halImpl.memory.contiguousFilesystem
    = (NanoOsOverlayMap*) FILESYSTEM_DRIVER_ADDRESS;
  halImpl.memory.contiguousFilesystemSize = FILESYSTEM_DRIVER_SIZE;

  halImpl.memory.overlayMap  = (NanoOsOverlayMap*) OVERLAY_ADDRESS;
  halImpl.memory.overlaySize = OVERLAY_SIZE;

  halImpl.memory.logBuffer      = _logBuffer;
  halImpl.memory.logBufferSize  = sizeof(_logBuffer);
#ifdef NANO_OS_STRINGS_STRIPPED
  halImpl.memory.stringsPresent = false;
#else
  halImpl.memory.stringsPresent = true;
#endif // NANO_OS_STRINGS_STRIPPED
  halImpl.memory.staticLogs     = (StaticLogs*) STATIC_LOGS_ADDRESS;
  memset(halImpl.memory.staticLogs, 0, sizeof(StaticLogs));

  halImpl.uart.numSupported        = 2;
  halImpl.uart.online              = agonLight2UartsOnline;

  // DIO ids are the packed value (port_nibble << 4) | bit, spanning 0xB0..0xD7,
  // so the online() bound is the largest valid id + 1 rather than a pin count.
  halImpl.dio.numSupported         = 0xD8;
  halImpl.dio.online               = agonLight2DiosOnline;

  halImpl.spi.numSupported         = MAX_SPI_DEVICES;
  halImpl.spi.online               = agonLight2SpisOnline;

  halImpl.timer.numSupported       = 5; // eZ80F92 PRT1..PRT5 (PRT0 is the clock)
  halImpl.timer.online             = agonLight2TimersOnline;

  halImpl.blockDevice.numSupported = _numBlockDevices;
  halImpl.blockDevice.online       = agonLight2BlockDevicesOnline;

  NANO_OS_API = &nanoOsApi;

  int returnValue = halCommonInit();

  enableInterrupts();

  // Final step: verify the external-RAM integrity canary that Boot.asm stamped
  // at DATA_BSS_CANARY_ADDRESS before it copied .data.  halCommonInit() has
  // brought up the console UART by now, so printString() reaches the user.
  if (*(volatile uint64_t *) (uintptr_t) DATA_BSS_CANARY_ADDRESS
    != DATA_BSS_CANARY_VALUE
  ) {
    printString(_dataBssCanaryError);
  }

  return returnValue;
}

#endif // NANO_OS_AGON_LIGHT_2
