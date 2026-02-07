///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              09.01.2025
///
/// @file              NanoOsOverlay.h
///
/// @brief             Definitions used for exporting functionality of overlays
///                    so that they're accessible from the kernel.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
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
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef NANO_OS_OVERLAY_FUNCTIONS_H
#define NANO_OS_OVERLAY_FUNCTIONS_H

// Headers from kernel space.
#include "NanoOsOverlay.h"

#ifdef __cplusplus
extern "C"
{
#endif

int loadOverlay(const char *overlayDir, const char *overlay, char **envp);
OverlayFunction findOverlayFunction(const char *overlayFunctionName);
void* callOverlayFunction(const char *overlayDir, const char *overlay,
  const char *function, void *args);
int runOverlayCommand(const char *commandPath,
  int argc, char **argv, char **envp);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_OVERLAY_FUNCTIONS_H

