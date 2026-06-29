///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              06.29.2026
///
/// @file              HalAgonLight2.h
///
/// @brief             Header for the Agon Light 2 (eZ80F92) HAL implementation.
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

#ifndef HAL_AGON_LIGHT_2_H
#define HAL_AGON_LIGHT_2_H

#include "../kernel/Hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @fn int32_t halAgonLight2Init(void)
///
/// @brief Initialise the HAL for the Agon Light 2.
///
/// Wires all HAL subsystem function tables, sets memory-layout parameters,
/// then calls halCommonInit() to finish the common HAL setup.
///
/// @return Returns 0 on success, -errno on failure.
int32_t halAgonLight2Init(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // HAL_AGON_LIGHT_2_H
