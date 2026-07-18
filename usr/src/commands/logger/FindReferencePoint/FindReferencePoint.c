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

/// @file FindReferencePoint.c
///
/// @brief Userspace logger overlay for finding the reference point string
/// within a binary.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "UserspaceLogger.h"
#include "NanoOsExecutive.h"

/// @fn void* findReferencePoint(void *args)
///
/// @brief Find the location of the _referencePoint variable in the kernel's
/// logger.
///
/// @param args The full filesystem path to the binary to search.
///
/// @return Returns the offset reference point within the provided binary on
/// success, -1 cast to a void* on failure.
void* findReferencePoint(void *args) {
  char *binaryPath = (char*) args;
  void *returnValue = (void*) ((intptr_t) -1); // Bad status until success
  const char *referencePattern = REFERENCE_PATTERN;
  
  FILE *binaryFile = fopen(binaryPath, "r");
  if (binaryFile == NULL) {
    goto exit; // return bad status
  }
  
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
    goto closeBinaryFile; // return bad status
  }
  
  bool patternFound = false;
  while (patternFound == false) {
    size_t bytesRead = fread(fileBuffer, 1, fileBufferSize, binaryFile);
    for (size_t ii = 0; ii < bytesRead; ii++) {
      if (fileBuffer[ii] != referencePattern[0]) {
        continue;
      }
      
      size_t jj = 1;
      for (; jj < REFERENCE_PATTERN_LENGTH; jj++) {
        if (fileBuffer[ii + jj] != referencePattern[jj]) {
          break;
        }
      }
      if (jj != REFERENCE_PATTERN_LENGTH) {
        continue;
      }
      
      if (strcmp((char*) &fileBuffer[ii], REFERENCE_POINT_STRING) == 0) {
        // Reference point found!!  We're done!
        patternFound = true;
        break;
      }
    }
  }
  
//// freeFileBuffer:
  free(fileBuffer);
  
closeBinaryFile:
  fclose(binaryFile);
  
exit:
  return returnValue;
}

