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




/// @file Usage.c
///
/// @brief sysnook's usage summary, which is printed only for --help or a bad
/// option and so is kept out of the Start overlay.

#include "Wm.h"

/// @fn void* wmUsage(void *args)
///
/// @brief Print the usage summary for sysnook, as nook, the command that
/// runs it.
///
/// @param args The FILE* to print to.
///
/// @return Always returns the provided pointer.
void* wmUsage(void *args) {
  FILE *stream = (FILE*) args;
  fputs(
    "Usage: nook [options]\n"
    "\n"
    "Start Nook on the display, and wait until it ends.  Ctrl+Q quits it.\n"
    "\n"
    "Options:\n"
    "  --move=live|outline  How a moved window follows the pointer; outline by\n"
    "    default.\n"
    "  --resize=live|outline  How a resized window follows the pointer;\n"
    "    outline by default.\n"
    "  --timing  Print how long frames and echoes took when it ends.\n"
    "  --budget=BYTES  The most of the window server's memory each\n"
    "    application's windows may take, up to 65535; 16384 by default.\n"
    "  --finish  Wait until each frame is drawn before going on.\n"
    "  --help  Show this help and exit.\n",
    stream);

  return args;
}
