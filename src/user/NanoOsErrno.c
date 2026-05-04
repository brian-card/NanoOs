////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                     Copyright (c) 2012-2025 James Card                     //
//                                                                            //
// Permission is hereby granted, free of charge, to any person obtaining a    //
// copy of this software and associated documentation files (the "Software"), //
// to deal in the Software without restriction, including without limitation  //
// the rights to use, copy, modify, merge, publish, distribute, sublicense,   //
// and/or sell copies of the Software, and to permit persons to whom the      //
// Software is furnished to do so, subject to the following conditions:       //
//                                                                            //
// The above copyright notice and this permission notice shall be included    //
// in all copies or substantial portions of the Software.                     //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR //
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   //
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    //
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER //
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    //
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        //
// DEALINGS IN THE SOFTWARE.                                                  //
//                                                                            //
//                                 James Card                                 //
//                          http://www.jamescard.org                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// Doxygen marker
/// @file

#include "NanoOsErrno.h"
#include "../kernel/NanoOsTypes.h"
#include "../kernel/Processes.h"

/// @var processErrorNumbers
///
/// @brief Process-specific storage for each process's errno value.
static int processErrorNumbers[NANO_OS_NUM_PROCESSES + 1];

/// @fn in* errno_(void)
///
/// @brief Get a pointer to the element of the internal processErrorNumbers
/// array that corresponds to the current process.
///
/// @return This function always succeeds and always returns a valid pointer.
/// HOWEVER, if there is a system problem that prevents accurate retrieval of
/// the current process ID, a pointer to a default "scratch" storage space will
/// be returned instead of the pointer to the current process's storage.
int* errno_(void) {
  ProcessId currentProcessId = getRunningProcessId();
  if (currentProcessId > NANO_OS_NUM_PROCESSES) {
    // This isn't valid.  This shouldn't happen but that doesn't mean it won't.
    // Use the last index of the array as scratch storage.  This will prevent
    // a segfault as would happen if we returned NULL.
    currentProcessId = NANO_OS_NUM_PROCESSES;
  }
  
  return &processErrorNumbers[currentProcessId];
}

