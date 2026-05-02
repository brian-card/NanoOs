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

/// @file HalAdafruitTrinketM0.cpp
///
/// @brief HAL implementation for a Seeed Xiao M0

#if defined(ADAFRUIT_TRINKET_M0)

#include "HalAdafruitTrinketM0.h"
#include "HalArduinoSamD21x18A.h"
#include "HalCommon.h"

/// @def NUM_DIO_PINS
///
/// @brief The number of digital IO pins on the board.
#define NUM_DIO_PINS 1

/// @def SPI_COPI_DIO
///
/// @brief DIO pin used for SPI COPI on the Adafruit Feather M0 WiFi.
#define SPI_COPI_DIO 4

/// @def SPI_CIPO_DIO
///
/// @brief DIO pin used for SPI CIPO on the Adafruit Feather M0 WiFi.
#define SPI_CIPO_DIO 2

/// @def SPI_SCK_DIO
///
/// @brief DIO pin used for SPI serial clock on the Adafruit Feather M0 WiFi.
#define SPI_SCK_DIO 3

/// @def SD_CARD_PIN_CHIP_SELECT
///
/// @brief Pin to use for the MicroSD card reader's SPI chip select line.
#define SD_CARD_PIN_CHIP_SELECT 0


const Hal* halAdafruitTrinketM0Init(void) {
  HalArduinoSamD21x18AInitArgs args = {
    .numDioPins          = NUM_DIO_PINS,
    .spiCopiDio          = SPI_COPI_DIO,
    .spiCipoDio          = SPI_CIPO_DIO,
    .spiSckDio           = SPI_SCK_DIO,
    .sdCardPinChipSelect = SD_CARD_PIN_CHIP_SELECT,
  };

  const Hal *hal = halArduinoSamD21x18AInit(&args);

  if (halCommonInit(hal) != 0) {
    return NULL;
  }

  return hal;
}

#endif // ADAFRUIT_TRINKET_M0
