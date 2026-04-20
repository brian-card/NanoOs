///////////////////////////////////////////////////////////////////////////////
///
/// @author            James Card
/// @date              10.22.2025
///
/// @file              NanoOsTermios.h
///
/// @brief             Functionality from termios.h to be exported to user
///                    programs.
///
/// @copyright
///                   Copyright (c) 2012-2025 James Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                James Card
///                         http://www.jamescard.org
///
///////////////////////////////////////////////////////////////////////////////

#ifndef NANO_OS_USER_TERMIOS_H
#define NANO_OS_USER_TERMIOS_H

#ifdef __cplusplus
extern "C"
{
#endif

/* c_lflag bits - local modes specified by POSIX */

// Echo input characters to output
#define ECHO    0x0001
// Visually erase characters (backspace-space-backspace)
#define ECHOE   0x0002
// Echo newline after KILL character
#define ECHOK   0x0004
// Echo newline even if ECHO is off
#define ECHONL  0x0008
// Canonical input mode (line editing enabled)
#define ICANON  0x0010
// Enable extended input character processing
#define IEXTEN  0x0020
// Enable signal generation from special characters
#define ISIG    0x0040
// Don't flush after interrupt or quit signals
#define NOFLSH  0x0080
// Send SIGTTOU to background processes writing to terminal
#define TOSTOP  0x0100

/* c_lflag bits - XSI extensions in SUS */

// Echo control characters as ^X
#define ECHOCTL 0x0200
// Visual erase for KILL character
#define ECHOKE  0x0400
// Echo erased characters backward
#define ECHOPRT 0x0800

/* For tcsetattr */
#define TCSANOW   0x0001
#define TCSADRAIN 0x0002
#define TCSAFLUSH 0x0004

/* For tcflush */
#define TCIFLUSH  0x0001
#define TCOFLUSH  0x0002
#define TCIOFLUSH 0x0004

/* For tcflow */
#define TCOOFF 0x0001
#define TCOON  0x0002
#define TCIOFF 0x0004
#define TCION  0x0008

// Control characters
#define NCCS    20

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned long speed_t;

struct termios {
    tcflag_t c_iflag;    /* input modes */
    tcflag_t c_oflag;    /* output modes */
    tcflag_t c_cflag;    /* control modes */
    tcflag_t c_lflag;    /* local modes */
    cc_t     c_cc[NCCS]; /* control characters */
};

int nanoOsTcgetattr(int fd, struct termios *termios_p);
int nanoOsTcsetattr(int fd, int optional_actions,
  const struct termios *termios_p);

#ifdef __cplusplus
}
#endif

#endif // NANO_OS_USER_TERMIOS_H

