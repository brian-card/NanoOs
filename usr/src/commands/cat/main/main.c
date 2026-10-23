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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "NanoOsUtils.h"

/// @brief Print the usage summary for cat.
///
/// @param stream The stream to print to.
///
/// @return This function returns no value.
static void printUsage(FILE *stream) {
  fputs(
    "Usage: cat [file]\n"
    "\n"
    "Write the contents of a file to standard output.  With no file, copy\n"
    "standard input to standard output a line at a time until a line that is\n"
    "just \"EOF\" or the end of the input.\n"
    "\n"
    "Options:\n"
    "  --help  Show this help and exit.\n",
    stream);
}

int main(int argc, char **argv) {
  if ((argc > 1) && isHelpOption(argv[1])) {
    printUsage(stdout);
    return 0;
  }
  char buffer[96];
  if (argc > 1) {
    const char *filename = argv[1];
    FILE *inputFile = fopen(filename, "r");
    if (inputFile == NULL) {
      fprintf(stderr, "ERROR: Could not open file \"%s\": %s\n",
        filename, strerror(errno));
      return 1;
    }
    size_t bytesRead = fread(buffer, 1, sizeof(buffer), inputFile);
    while (bytesRead > 0) {
      fwrite(buffer, 1, bytesRead, stdout);
      bytesRead = fread(buffer, 1, sizeof(buffer), inputFile);
    }
    fclose(inputFile);
  } else {
    // Read from stdin and echo the input back to the user until "EOF\n" is
    // received.
    while (fgets(buffer, sizeof(buffer), stdin)) {
      if (strcmp(buffer, "EOF\n") != 0) {
        fputs(buffer, stdout);
      } else {
        break;
      }
    }
  }

  return 0;
}

