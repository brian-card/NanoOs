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

/// @file main.c
///
/// @brief Entrypoint into userspace logger command.

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "UserspaceLogger.h"
#include "NanoOsExecutive.h"

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;
  
  char buffer[96];
  *buffer = '\0';
  
  // Allocate a buffer for reading from the file.  Sector size (512 bytes) is
  // ideal, but there may not be that much RAM available, so scale back if
  // necessary.
  size_t fileBufferSize = 512;
  uint8_t *fileBuffer = NULL;
  for (; (fileBuffer == NULL) && (fileBufferSize >= 64); fileBufferSize >>= 1) {
    fileBuffer = (uint8_t*) malloc(fileBufferSize);
  }
  if (fileBuffer == NULL) {
    fprintf(stderr, "ERROR: Could not allocate fileBuffer.  Halting logger.\n");
    while (1) sched_yield();
  }
  
  int returnValue = 0;
  do {
    // Attempt to process the command line as a built-in first before looking
    // on the filesystem.
    //
    // The variable 'input' is the same as the variable 'buffer', which is a
    // pointer to dynamic memory.  So, it's safe to pass as a parameter to
    // callOverlayFunction.
    printDebugString("Checking to see if command is a builtin\n");
    returnValue = (intptr_t) callOverlayFunction(
      OVERLAY_SAME_NAMESPACE, "Builtins", "processBuiltin", buffer);
  } while (returnValue != -1);
  
  printf("Gracefully exiting %s\n", argv[0]);
  return 0;
}

