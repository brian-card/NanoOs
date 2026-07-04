///////////////////////////////////////////////////////////////////////////////
///
/// @file              HalArduinoAvr.h
///
/// @brief             Header for HALs based on AVR-based Arduinos.
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

#ifndef HAL_ARDUINO_AVR_H
#define HAL_ARDUINO_AVR_H

#include "../kernel/Hal.h"


#ifdef __cplusplus
extern "C"
{
#endif

typedef struct HalArduinoAvrInitArgs {
  uint32_t  numUartsSupported;
  uint32_t *uartsOnline;
  uint8_t   dioStart;
  uint32_t  numDiosSupported;
  uint32_t *diosOnline;
  uint8_t   spiCopiDio;
  uint8_t   spiCipoDio;
  uint8_t   spiSckDio;
} HalArduinoAvrInitArgs;

int32_t halArduinoAvrInit(HalArduinoAvrInitArgs *args);
int32_t halArduinoInit(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_ARDUINO_AVR_H

