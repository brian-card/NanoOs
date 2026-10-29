///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              12.25.2025
///
/// @file              CoroutineConfig.h
///
/// @brief             Definitions for configuring the Coroutines library for
///                    NanoOs.
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

#ifndef COROUTINE_CONFIG_H
#define COROUTINE_CONFIG_H

#ifdef __cplusplus
extern "C"
{
#endif

/// @def SINGLE_CORE_COROUTINES
///
/// @brief This define causes the Coroutines library to omit multi-thread
/// support.
#define SINGLE_CORE_COROUTINES

void msleep(int durationMs);

#ifdef __cplusplus
}
#endif

#endif // COROUTINE_CONFIG_H

