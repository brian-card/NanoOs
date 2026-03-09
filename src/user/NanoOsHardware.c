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

/// @file NanoOsHardware.c
///
/// @brief Hardware abstraction for user processes.

#include "../kernel/Hal.h"
#include "NanoOsHardware.h"

// Must come last
#include "NanoOsStdio.h"

int nanoOsHardwareShutdown(NanoOsShutdownType shutdownType) {
  int returnValue = 0;
  
  switch (shutdownType) {
    case NANO_OS_SHUTDOWN_OFF:
      {
        HAL->shutdown(HAL_SHUTDOWN_OFF);
      }
      break;
    
    case NANO_OS_SHUTDOWN_HYBERNATE:
      {
        // Store RAM on disk and then power off.
        // TODO: Store RAM on disk.
        HAL->shutdown(HAL_SHUTDOWN_OFF);
      }
      break;
    
    case NANO_OS_SHUTDOWN_SUSPEND:
      {
        HAL->shutdown(HAL_SHUTDOWN_SUSPEND);
      }
      break;
    
    case NANO_OS_SHUTDOWN_RESET:
      {
        HAL->shutdown(HAL_SHUTDOWN_RESET);
      }
      break;
    
    default:
      {
        fprintf(stderr, "Error: Invalid shutdown type in nanoOsShutdown.\n");
        returnValue = -1;
      }
      break;
  }
  
  return returnValue;
}

