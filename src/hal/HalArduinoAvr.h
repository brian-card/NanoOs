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

#include <stdarg.h>

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
} HalArduinoAvrInitArgs;

int halArduinoAvrInit(HalArduinoAvrInitArgs *args);
int halArduinoInit(void);

/// @fn int arduinoAvrConfigureDioImpl(int32_t deviceId, bool output)
///
/// @brief Fast-path DIO configure, shared with board-specific code that
/// needs to drive a DIO directly (e.g. an SPI chip-select line) without
/// going through the capability-gated HAL_DIO dispatch.  Defined in
/// HalArduinoAvr.cpp.
int arduinoAvrConfigureDioImpl(int32_t deviceId, bool output);

/// @fn int arduinoAvrWriteDioImpl(int32_t deviceId, bool high)
///
/// @brief Fast-path DIO write.  See arduinoAvrConfigureDioImpl above.
int arduinoAvrWriteDioImpl(int32_t deviceId, bool high);

/// @fn void arduinoAvrSetRootStorageFunctions(
///   HalInitRootStorageFn initRootStorage,
///   HalRestartRootFilesystemFn restartRootFilesystem)
///
/// @brief Set the board-specific root storage functions.  Boards with no root
/// storage (e.g. the Nano Every) simply never call this, leaving both NULL.
void arduinoAvrSetRootStorageFunctions(
  HalInitRootStorageFn initRootStorage,
  HalRestartRootFilesystemFn restartRootFilesystem);

// arduinoAvrInitBlockDevice, arduinoAvrGetBlockDevice, and
// arduinoAvrRestartBlockDevice are defined per-board (HalArduinoMega2560.cpp
// / HalArduinoNanoEvery.c), not in the shared HalArduinoAvr.cpp, but
// HalArduinoAvr.cpp's arduinoAvrBlockDeviceFunctions dispatch table
// references them.  Declaring them here, rather than privately in
// HalArduinoAvr.cpp, ensures a board file that's compiled as C++ (currently
// only HalArduinoMega2560.cpp) defines them with the C linkage that
// dispatch table (and HalArduinoNanoEvery.c's plain-C definitions) expect.
int arduinoAvrInitBlockDevice(va_list args);
int arduinoAvrGetBlockDevice(va_list args);
int arduinoAvrRestartBlockDevice(va_list args);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_ARDUINO_AVR_H

