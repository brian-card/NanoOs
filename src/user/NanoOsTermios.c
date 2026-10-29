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
  
  int modes = getConsoleModes();
  if (modes < 0) {
    errno = ENOTTY;
    return -1;
  }

  memset(termios, 0, sizeof(struct termios));
  if (modes & CONSOLE_MODE_ECHO) {
    termios->c_lflag |= ECHO;
  }
  if (modes & CONSOLE_MODE_CANONICAL) {
    termios->c_lflag |= ICANON;
  }
  if (modes & CONSOLE_MODE_SIGNALS) {
    termios->c_lflag |= ISIG;
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
  
  uint8_t modes = 0;
  if (termios->c_lflag & ECHO) {
    modes |= CONSOLE_MODE_ECHO;
  }
  if (termios->c_lflag & ICANON) {
    modes |= CONSOLE_MODE_CANONICAL;
  }
  if (termios->c_lflag & ISIG) {
    modes |= CONSOLE_MODE_SIGNALS;
  }
  if (setConsoleModes(modes) != 0) {
    errno = ENOTTY;
    return -1;
  }
  
  return 0;
}

