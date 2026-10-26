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

/// @file HalArduinoNanoEvery.c
///
/// @brief HAL implementation for an Arduino Nano Every.

#if defined(ARDUINO_AVR_NANO_EVERY)

#include "HalArduinoAvr.h"
#include "HalCommon.h"

/// @def NUM_UARTS
///
/// @brief The maximum number of serial ports we can support on the board.
#define NUM_UARTS 2

/// @def DIO_START
///
/// @brief On the Arduino Nano Every, D0 is used for Serial1's RX and D1 is
/// used for Serial1's TX.  We expect to use Serial1, so our first usable
/// DIO is 2.
#define DIO_START 2

/// @def NUM_DIO_PINS
///
/// @brief The number of digital IO pins on the board.  14 on an Arduino Nano.
#define NUM_DIO_PINS 14

// This board has no SPI bus wired to anything and no block device of any
// kind: arduinoAvrInitBlockDevice/etc below always return -ENODEV, and
// halArduinoInit() never sets HAL->spi or HAL->blockDevice.  So, unlike
// HalArduinoMega2560.cpp, there are no SPI pin or SD-card chip-select
// defines here -- there's nothing for them to configure.

/// @var halArduinoAvrImplUartsOnline
///
/// @brief Bitmask array of online UARTs.
static uint32_t halArduinoAvrImplUartsOnline[] = {
  0x00000003,
};

/// @var halArduinoAvrImplUartsPollOnly
///
/// @brief Bitmask array of online UARTs.
static uint32_t halArduinoAvrImplUartsPollOnly[] = {
  0x00000003,
};

/// @var halArduinoAvrImplDiosOnline
///
/// @brief Bitmask array of online DIOs.
static uint32_t halArduinoAvrImplDiosOnline[] = {
  0x00003fff,
};

int arduinoAvrInitBlockDevice(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENODEV;
}

int arduinoAvrGetBlockDevice(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) va_arg(args, int32_t);
  return -ENODEV;
}

/// @fn int arduinoAvrReadBlocks(int32_t deviceId, va_list args)
///
/// @brief HAL_BLOCK_DEVICE_READ_BLOCKS for a board with no block devices.
///
/// @param deviceId Ignored.
/// @param args Ignored.
///
/// @return Returns -ENODEV always.
int arduinoAvrReadBlocks(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENODEV;
}

/// @fn int arduinoAvrWriteBlocks(int32_t deviceId, va_list args)
///
/// @brief HAL_BLOCK_DEVICE_WRITE_BLOCKS for a board with no block devices.
///
/// @param deviceId Ignored.
/// @param args Ignored.
///
/// @return Returns -ENODEV always.
int arduinoAvrWriteBlocks(int32_t deviceId, va_list args) {
  (void) deviceId;
  (void) args;
  return -ENODEV;
}

int halArduinoInit(void) {
  HalArduinoAvrInitArgs args = {
    .numUartsSupported = NUM_UARTS,
    .uartsOnline       = halArduinoAvrImplUartsOnline,
    .uartsPollOnly     = halArduinoAvrImplUartsPollOnly,
    .dioStart          = DIO_START,
    .numDiosSupported  = NUM_DIO_PINS,
    .diosOnline        = halArduinoAvrImplDiosOnline,
  };

  return halArduinoAvrInit(&args);
}

#endif // ARDUINO_AVR_NANO_EVERY
