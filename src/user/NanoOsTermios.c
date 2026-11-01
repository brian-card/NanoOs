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
  if (modes & CONSOLE_MODE_RESIZE_REPORTS) {
    termios->c_lflag |= RESIZEREPORTS;
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
  if (termios->c_lflag & RESIZEREPORTS) {
    modes |= CONSOLE_MODE_RESIZE_REPORTS;
  }
  if (setConsoleModes(modes) != 0) {
    errno = ENOTTY;
    return -1;
  }
  
  return 0;
}

/// @fn int nanoOsTcgetwinsize(int fd, struct winsize *winsize)
///
/// @brief Get the size of the terminal stdin is connected to.
///
/// @param fd The file descriptor of the terminal, which must be stdin.
/// @param winsize Set to the terminal's size.  The pixel sizes are 0.
///
/// @return Returns 0 on success, -1 with errno set on failure.
int nanoOsTcgetwinsize(int fd, struct winsize *winsize) {
  if ((winsize == NULL) || (fd != STDIN_FILENO)) {
    errno = EINVAL;
    return -1;
  }

  uint16_t rows = 0;
  uint16_t columns = 0;
  if (getConsoleWinsize(&rows, &columns) != 0) {
    errno = ENOTTY;
    return -1;
  }

  winsize->ws_row = rows;
  winsize->ws_col = columns;
  winsize->ws_xpixel = 0;
  winsize->ws_ypixel = 0;
  return 0;
}

/// @fn int nanoOsTcsetwinsize(int fd, const struct winsize *winsize)
///
/// @brief Record the size of the terminal stdin is connected to.  The pixel
/// sizes are ignored.
///
/// @param fd The file descriptor of the terminal, which must be stdin.
/// @param winsize The terminal's size.
///
/// @return Returns 0 on success, -1 with errno set on failure.
int nanoOsTcsetwinsize(int fd, const struct winsize *winsize) {
  if ((winsize == NULL) || (fd != STDIN_FILENO)) {
    errno = EINVAL;
    return -1;
  }

  if (setConsoleWinsize(winsize->ws_row, winsize->ws_col) != 0) {
    errno = ENOTTY;
    return -1;
  }

  return 0;
}
