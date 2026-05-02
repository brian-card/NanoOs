///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              05.02.2026
///
/// @file              HalArduinoSamD21x18A.h
///
/// @brief             Header for HALs based on SAMD21x18A Arduinos.
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef HAL_ARDUINO_SAM_D21X18A_H
#define HAL_ARDUINO_SAM_D21X18A_H

#include "../kernel/Hal.h"


#ifdef __cplusplus
extern "C"
{
#endif

typedef struct HalArduinoSamD21x18AInitArgs {
  uint8_t numDioPins;
  uint8_t spiCopiDio;
  uint8_t spiCipoDio;
  uint8_t spiSckDio;
  uint8_t sdCardPinChipSelect;
} HalArduinoSamD21x18AInitArgs;

const Hal* halArduinoSamD21x18AInit(HalArduinoSamD21x18AInitArgs *args);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_ARDUINO_SAM_D21X18A_H

