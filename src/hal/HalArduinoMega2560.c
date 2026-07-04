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

/// @file HalArduinoMega2560.c
///
/// @brief HAL implementation for an Arduino Mega 2560.

#if defined(ARDUINO_AVR_MEGA2560)

#include "HalArduinoAvr.h"
#include "HalCommon.h"

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

int32_t halArduinoInit(void) {
  HalArduinoAvrInitArgs args = {
    .numUartsSupported = NUM_UARTS,
    .uartsOnline       = halArduinoAvrImplUartsOnline,
    .dioStart          = DIO_START,
    .numDiosSupported  = NUM_DIO_PINS,
    .diosOnline        = halArduinoAvrImplDiosOnline,
    .spiCopiDio        = SPI_COPI_DIO,
    .spiCipoDio        = SPI_CIPO_DIO,
    .spiSckDio         = SPI_SCK_DIO,
  };

  return halArduinoAvrInit(&args);
}

#endif // ARDUINO_AVR_MEGA2560
