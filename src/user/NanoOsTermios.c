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

#include "string.h"
#include "NanoOsLibC.h"
#include "NanoOsSysUtsname.h"
#include "../kernel/Console.h"
#include "../kernel/NanoOs.h"
#include "NanoOsTermios.h"
#include "NanoOsUnistd.h"

int nanoOsTcgetattr(int fd, struct termios *termios) {
  if ((termios == NULL) || (fd != STDIN_FILENO)) {
    // Unsupported right now.
    errno = EINVAL;
    return -1;
  }
  
  memset(termios, 0, sizeof(struct termios));
  if (getConsoleEcho()) {
    termios->c_lflag |= ECHO;
  }
  
  return 0;
}

int nanoOsTcsetattr(int fd, int optionalActions,
  const struct termios *termios
) {
  if ((termios == NULL) || (fd != STDIN_FILENO)) {
    // Unsupported right now.
    errno = EINVAL;
    return -1;
  }
  
  (void) optionalActions; // Ignored for now.
  
  bool echo = ((termios->c_lflag & ECHO));
  setConsoleEcho(echo);
  
  return 0;
}

