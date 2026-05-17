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

// Standard C includes
#include "string.h"

// NanoOs includes
#include "NanoOs.h"
#include "NanoOsTypes.h"
#include "Processes.h"
#include "Scheduler.h"

// Simulator includes
#include "HalPosix.h"

// undef all the things that NanoOs defines
#undef stdin
#undef stdout
#undef stderr
#undef fopen
#undef fclose
#undef remove
#undef fseek
#undef vfscanf
#undef fscanf
#undef scanf
#undef vfprintf
#undef fprintf
#undef printf
#undef fputs
#undef puts
#undef fgets
#undef fread
#undef fwrite
#undef strerror
#undef fileno
#undef FILE

// Standard C includes
#include <stdio.h>

//// #define printDebug(format, ...) fprintf(stderr, format, ##__VA_ARGS__)
#define printDebug(format, ...) {}

const Hal *HAL = NULL;

void usage(const char *argv0) {
  const char *programName = strrchr(argv0, '/');
  if (programName != NULL) {
    // The expected case.
    programName++;
  } else {
    programName = argv0;
  }
  
  fprintf(stderr, "Usage: %s <block device path>\n", programName);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    usage(argv[0]);
    return 1;
  }

  jmp_buf resetBuffer;
  setjmp(resetBuffer);

  HAL = halPosixInit(resetBuffer, argv[1]);
  if (HAL == NULL) {
    // Error message has already been printed.  Bail.
    return 1;
  }

  // On hardware, we need a "Booting..." message and a delay so that we give
  // ourselves enough time to start a firmware update in case we've loaded
  // something that's resulting in bricking the system.  Since the simulator is
  // just an application running in its own virtual memory sandbox, we don't
  // need that here, so skipping it.

  nanoOsStart();

  return 0;
}

