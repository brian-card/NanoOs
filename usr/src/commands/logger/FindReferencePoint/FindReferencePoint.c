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
  FILE *binaryFile = (FILE*) args;
  intptr_t returnValue = -1; // Bad status until success
  const char *referencePattern = REFERENCE_PATTERN;
  
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
    goto exit; // return bad status
  }
  
  bool patternFound = false;
  while ((patternFound == false) && (!feof(binaryFile))) {
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
      
      for (jj = ii + REFERENCE_PATTERN_LENGTH;
        jj < bytesRead;
        jj += REFERENCE_PATTERN_LENGTH
      ) {
        size_t kk = 0;
        for (;
          (kk < REFERENCE_PATTERN_LENGTH) && ((jj + kk) < bytesRead);
          kk++
        ) {
          if (fileBuffer[jj + kk] != referencePattern[kk]) {
            break;
          }
        }
        if (kk != REFERENCE_PATTERN_LENGTH) {
          jj += kk;
          break;
        }
      }
      
      // The number of bytes we found of the reference point sting is now
      // (jj - ii).
      if ((jj - ii) == REFERENCE_POINT_STRING_LENGTH) {
        returnValue = ftell(binaryFile) - bytesRead + ii;
        goto freeFileBuffer;
      }
      // If we made it this far then, by definition, we read fewer than
      // REFERENCE_POINT_STRING_LENGTH bytes.  This is OK if we're near the
      // beginning or end of the buffer.
      
      // If we're near the beginning of the buffer, then ii has to be less than
      // REFERENCE_PATTERN_LENGTH and jj has to be greater than
      // (REFERENCE_PATTERN_LENGTH * (NUM_REFERENCE_PATTERNS - 1)).
      if ((ii < REFERENCE_PATTERN_LENGTH)
        && (jj > (REFERENCE_PATTERN_LENGTH * (NUM_REFERENCE_PATTERNS - 1)))
      ) {
        returnValue = ftell(binaryFile) - bytesRead
          - (REFERENCE_PATTERN_LENGTH - ii);
        goto freeFileBuffer;
      }
      
      // If we're near the end of the buffer then bytesRead has to be equal to
      // fileBufferSize (can't be a short read) and jj has to equal bytesRead.
      if ((bytesRead == fileBufferSize) && (jj == bytesRead)) {
        returnValue = ftell(binaryFile) - bytesRead + ii;
        goto freeFileBuffer;
      }
      
      // If we made it this far then we've discovered an instance of
      // REFERENCE_PATTERN in isolation that's not part of our full
      // REFERENCE_POINT_STRING.  We'll continue the search.
    }
  }
  
freeFileBuffer:
  free(fileBuffer);
  
exit:
  return (void*) returnValue;
}

