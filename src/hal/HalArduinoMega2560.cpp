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

/// @file HalArduinoMega2560.cpp
///
/// @brief HAL implementation for an Arduino Mega 2560.
///
/// @note This is the only AVR board file that needs to be a .cpp: it's the
/// only one with a real SPI bus wired to anything (the Nano Every has none
/// -- see HalArduinoNanoEvery.c), so it's also the only one that needs the
/// Arduino SPI library.  Keeping the SPI implementation here instead of in
/// the shared HalArduinoAvr.cpp means the Nano Every's build never compiles
/// or links any of this code.

#if defined(ARDUINO_AVR_MEGA2560)

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

#include "HalArduinoAvr.h"
#include "HalCommon.h"
#include "../user/NanoOsStdio.h"

// See the identical guard in HalArduinoAvr.cpp: Arduino.h pulls in malloc
// et al, and the HAL must never use dynamic memory.
#undef malloc
#define malloc  MEMORY_ERROR
#undef calloc
#define calloc  MEMORY_ERROR
#undef realloc
#define realloc MEMORY_ERROR
#undef free
#define free   MEMORY_ERROR

/// @def NUM_UARTS
///
/// @brief The maximum number of serial ports we can support on the board.
#define NUM_UARTS 4

/// @def DIO_START
///
/// @brief On the Arduino Mega 2560, D0 is used for Serial's RX and D1 is
/// used for Serial's TX.  We expect to use Serial, so our first usable
/// DIO is 2.
#define DIO_START 2

/// @def NUM_DIO_PINS
///
/// @brief The number of digital IO pins on the board.  54 on an Arduino
/// Mega 2560 (D0-D53).  This does not include the analog-only pins, which
/// are also addressable as digital pins 54-69.
#define NUM_DIO_PINS 54

/// @def SPI_COPI_DIO
///
/// @brief DIO pin used for SPI COPI (MOSI) on the Arduino Mega 2560.
#define SPI_COPI_DIO 51

/// @def SPI_CIPO_DIO
///
/// @brief DIO pin used for SPI CIPO (MISO) on the Arduino Mega 2560.
#define SPI_CIPO_DIO 50

/// @def SPI_SCK_DIO
///
/// @brief DIO pin used for SPI serial clock on the Arduino Mega 2560.
#define SPI_SCK_DIO 52

/// @def SD_CARD_PIN_CHIP_SELECT
///
/// @brief Pin to use for the MicroSD card reader's SPI chip select line.
#define SD_CARD_PIN_CHIP_SELECT 4

/// @def MAX_SPI_DEVICES
///
/// @brief The maximum number of SPI devices the system can support.
#define MAX_SPI_DEVICES 2

/// @var halArduinoAvrImplUartsOnline
///
/// @brief Bitmask array of online UARTs.
static uint32_t halArduinoAvrImplUartsOnline[] = {
  0x0000000f,
};

/// @var halArduinoAvrImplDiosOnline
///
/// @brief Bitmask array of online DIOs.
static uint32_t halArduinoAvrImplDiosOnline[] = {
  0xffffffff,
  0x003fffff,
};

/// @var globalSpiConfigured
///
/// @brief Whether or not the Arduino's SPI interface has already been
/// configured.
static bool globalSpiConfigured = false;

/// @var globalSpiInUse
///
/// @brief Whether or not the Arduino's SPI interface is currently in use.
static bool globalSpiInUse = false;

/// @var arduinoAvrSpiDevices
///
/// @brief Array of structures that will hold the information about SPI
/// connections.
static struct ArduinoAvrSpi {
  bool     configured;         // Will default to false
  uint8_t  chipSelect;
  bool     transferInProgress; // Will default to false
  uint32_t baud;
} arduinoAvrSpiDevices[MAX_SPI_DEVICES] = {};

/// @var numArduinoSpis
///
/// @brief The number of devices we support in the arduinoAvrSpiDevices array.
static const int numArduinoSpis
  = sizeof(arduinoAvrSpiDevices) / sizeof(arduinoAvrSpiDevices[0]);

/// @def SPI_POWER_UP_CLOCK_BYTES
///
/// @brief 0xFF bytes clocked out with chip select deasserted right after a
/// device is configured.  The SD physical spec wants >= 74 clock cycles (>= 10
/// bytes) with CS and DI high before the first command; harmless for anything
/// else on the bus.
#define SPI_POWER_UP_CLOCK_BYTES 10

static int arduinoAvrInitSpiImpl(void) {
  if (globalSpiConfigured == false) {
    globalSpiConfigured = true;
    SPI.begin();
  }
  return 0;
}

int arduinoAvrInitSpi(va_list args) {
  (void) args;
  return arduinoAvrInitSpiImpl();
}

int arduinoAvrConfigureSpiDevice(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t cs   = (uint8_t) va_arg(args, int);
  uint8_t sck  = (uint8_t) va_arg(args, int);
  uint8_t copi = (uint8_t) va_arg(args, int);
  uint8_t cipo = (uint8_t) va_arg(args, int);
  uint32_t baud = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)) {
    return -ENODEV;
  } else if ((cs < DIO_START) || (cs >= NUM_DIO_PINS)) {
    return -ERANGE;
  } else if (
       (cs   == SPI_SCK_DIO)
    || (cs   == SPI_COPI_DIO)
    || (cs   == SPI_CIPO_DIO)
    || (sck  != SPI_SCK_DIO)
    || (copi != SPI_COPI_DIO)
    || (cipo != SPI_CIPO_DIO)
  ) {
    return -EINVAL;
  } else if (arduinoAvrSpiDevices[deviceId].configured == true) {
    return -EBUSY;
  }

  if (arduinoAvrInitSpiImpl() != 0) {
    return -ENODEV;
  }

  arduinoAvrConfigureDioImpl(cs, 1);
  arduinoAvrWriteDioImpl(cs, 1);

  // SD physical-spec power-up: >= 74 clocks with CS (and DI) high before the
  // device is ever selected.
  SPI.beginTransaction(SPISettings(baud, MSBFIRST, SPI_MODE0));
  for (uint8_t ii = 0; ii < SPI_POWER_UP_CLOCK_BYTES; ii++) {
    SPI.transfer(0xFF);
  }
  SPI.endTransaction();

  arduinoAvrSpiDevices[deviceId].chipSelect = cs;
  arduinoAvrSpiDevices[deviceId].baud = baud;
  arduinoAvrSpiDevices[deviceId].configured = true;

  return 0;
}

int arduinoAvrSetSpiSpeed(va_list args) {
  int32_t  deviceId = va_arg(args, int32_t);
  uint32_t baud     = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoAvrSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  }
  if (baud == 0) {
    return -EINVAL;
  }

  // Picked up by the SPISettings passed to the next SPI.beginTransaction() in
  // arduinoAvrStartSpiTransferImpl().
  arduinoAvrSpiDevices[deviceId].baud = baud;
  return 0;
}

static int arduinoAvrStartSpiTransferImpl(int32_t deviceId) {
  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoAvrSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  } else if (globalSpiInUse == true) {
    return -EBUSY;
  }

  globalSpiInUse = true;
  arduinoAvrWriteDioImpl(arduinoAvrSpiDevices[deviceId].chipSelect, 0);
  SPI.beginTransaction(SPISettings(arduinoAvrSpiDevices[deviceId].baud,
    MSBFIRST, SPI_MODE0));
  arduinoAvrSpiDevices[deviceId].transferInProgress = true;

  return 0;
}

int arduinoAvrStartSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  return arduinoAvrStartSpiTransferImpl(deviceId);
}

int arduinoAvrEndSpiTransfer(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoAvrSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  }

  arduinoAvrSpiDevices[deviceId].transferInProgress = false;
  SPI.endTransaction();
  arduinoAvrWriteDioImpl(arduinoAvrSpiDevices[deviceId].chipSelect, 1);
  for (int ii = 0; ii < 8; ii++) {
    SPI.transfer(0xFF);
  }
  globalSpiInUse = false;

  return 0;
}

int arduinoAvrSpiTransfer8(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t data = (uint8_t) va_arg(args, int);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoAvrSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  } else if (!arduinoAvrSpiDevices[deviceId].transferInProgress) {
    arduinoAvrStartSpiTransferImpl(deviceId);
  }

  return (int) SPI.transfer(data);
}

int arduinoAvrSpiTransferBytes(va_list args) {
  int32_t deviceId = va_arg(args, int32_t);
  uint8_t *data = va_arg(args, uint8_t*);
  uint32_t length = va_arg(args, uint32_t);

  if ((deviceId < 0) || (deviceId >= numArduinoSpis)
    || (arduinoAvrSpiDevices[deviceId].configured == false)
  ) {
    return -ENODEV;
  } else if (!arduinoAvrSpiDevices[deviceId].transferInProgress) {
    arduinoAvrStartSpiTransferImpl(deviceId);
  }

  SPI.transfer(data, length);

  return 0;
}

/// @var halArduinoAvrSpisOnline
///
/// @brief Bitmask array of online SPIs.
static uint32_t halArduinoAvrSpisOnline[] = {
  0x00000003,
};

/// @var arduinoAvrSpiFunctions
///
/// @brief Dispatch table for this board's SPI implementation.  Wired into
/// halFunctions[HAL_SPI] by halArduinoInit() below.  No other AVR board
/// links this table in: it lives here, rather than in the shared
/// HalArduinoAvr.cpp, specifically so the Nano Every's build never compiles
/// or links any of this SPI code.
static HalFunction arduinoAvrSpiFunctions[HAL_SPI_NUM_FNS] = {
  [HAL_SPI_INIT]           = arduinoAvrInitSpi,
  [HAL_SPI_CONFIGURE]      = arduinoAvrConfigureSpiDevice,
  [HAL_SPI_START_TRANSFER] = arduinoAvrStartSpiTransfer,
  [HAL_SPI_END_TRANSFER]   = arduinoAvrEndSpiTransfer,
  [HAL_SPI_TRANSFER8]      = arduinoAvrSpiTransfer8,
  [HAL_SPI_TRANSFER_BYTES] = arduinoAvrSpiTransferBytes,
  [HAL_SPI_SET_SPEED]      = arduinoAvrSetSpiSpeed,
};

/// @var _sdCardHalCapabilities
///
/// @brief This board's storage for the SD-over-SPI card process's HAL
/// capabilities.  See SD_CARD_HAL_CAPABILITIES_INITIALIZER.
static HalCapability _sdCardHalCapabilities[]
  = SD_CARD_HAL_CAPABILITIES_INITIALIZER;

extern BlockDevice *blockDevices[];

int arduinoAvrInitBlockDevice(va_list args) {
  (void) args;
  if (schedulerIsInitialized() == false) {
    return -EBUSY;
  }

  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio   = SD_CARD_PIN_CHIP_SELECT,
    .spiCopiDio = SPI_COPI_DIO,
    .spiCipoDio = SPI_CIPO_DIO,
    .spiSckDio  = SPI_SCK_DIO,
  };

  blockDevices[0] = halCommonInitRootSdSpiStorage(&sdCardSpiArgs);
  if (blockDevices[0] == NULL) {
    return -ENODEV;
  }
  setOnline(HAL->blockDevice, 0);

  return 0;
}

int arduinoAvrGetBlockDevice(va_list args) {
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

int arduinoAvrRestartBlockDevice(va_list args) {
  ProcessDescriptor *processDescriptor = va_arg(args, ProcessDescriptor*);
  int32_t deviceId = (int32_t) (intptr_t) processDescriptor->restartArgs;

  SdCardSpiArgs sdCardSpiArgs = {
    .spiCsDio   = SD_CARD_PIN_CHIP_SELECT,
    .spiCopiDio = SPI_COPI_DIO,
    .spiCipoDio = SPI_CIPO_DIO,
    .spiSckDio  = SPI_SCK_DIO,
  };

  if (processCreate(processDescriptor, runSdCardSpi, &sdCardSpiArgs)
    != processSuccess
  ) {
    logError("Could not restart SD card process\n");
    return -ENOMEM;
  }
  threadSetContext(processDescriptor->mainThread, processDescriptor);
  processDescriptor->name = "SD card";
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

int halArduinoInit(void) {
  HalArduinoAvrInitArgs args = {
    .numUartsSupported = NUM_UARTS,
    .uartsOnline       = halArduinoAvrImplUartsOnline,
    .dioStart          = DIO_START,
    .numDiosSupported  = NUM_DIO_PINS,
    .diosOnline        = halArduinoAvrImplDiosOnline,
  };

  arduinoAvrSetRootStorageFunctions(
    halCommonInitRootFilesystem, restartBuiltinFilesystem);

  sdCardHalCapabilities = _sdCardHalCapabilities;
  numSdCardHalCapabilities
    = sizeof(_sdCardHalCapabilities) / sizeof(_sdCardHalCapabilities[0]);

  halImpl.memory->stringsPresent = true;

  // This is the only AVR board with an SPI bus wired to anything, so it's
  // the only one that wires up HAL_SPI -- see HalArduinoAvr.cpp/
  // HalArduinoNanoEvery.c for the boards that leave halImpl.spi NULL
  // instead.  This must happen before halArduinoAvrInit() below, since that
  // call ends in halCommonInit(), which initializes HAL->spi as part of its
  // generic boot sequence.
  halFunctions[HAL_SPI]     = arduinoAvrSpiFunctions;
  halImpl.spi->numSupported = MAX_SPI_DEVICES;
  halImpl.spi->online       = halArduinoAvrSpisOnline;

  return halArduinoAvrInit(&args);
}

#endif // ARDUINO_AVR_MEGA2560
